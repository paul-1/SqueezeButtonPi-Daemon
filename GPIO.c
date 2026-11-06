// Copyright (c) 2025, Joerg Schwieder, PenguinLovesMusic.com
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of ickStream nor the names of its contributors may be used
//   to endorse or promote products derived from this software without specific
//   prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#define _GNU_SOURCE
#include "GPIO.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_LINES (max_buttons + 2 * max_encoders)
#define NOPRESSTIME 50

static struct button buttons[max_buttons];
static struct encoder encoders[max_encoders];
static int numberofbuttons, numberofencoders, numberoflines;
static int pins[MAX_LINES], resistors[MAX_LINES], levels[MAX_LINES];
static unsigned offsets[MAX_LINES];
static uint64_t snapshot_time;
static int edge_fd = -1, wake_pipe[2] = {-1, -1};
static pthread_t worker;
static bool started, failed;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static int line_index(int pin)
{
    for (int i = 0; i < numberoflines; i++)
        if (pins[i] == pin)
            return i;
    return -1;
}

static bool valid_pin(int pin)
{
    if (started || edge_fd >= 0 || pin < 0 || pin > 53 || line_index(pin) >= 0) {
        logerr("Invalid, duplicate or late GPIO configuration: BCM %d", pin);
        return false;
    }
    return true;
}

static void add_line(int pin, int resist)
{
    pins[numberoflines] = pin;
    resistors[numberoflines++] = resist;
}

struct button *setupbutton(int pi, int pin, button_callback_t callback,
                          int resist, bool pressed, int long_press_time)
{
    if (numberofbuttons >= max_buttons || !valid_pin(pin) ||
        resist < PUD_OFF || resist > PUD_UP || long_press_time < 0) {
        logerr("Cannot configure button on BCM %d", pin);
        return NULL;
    }
    struct button *button = &buttons[numberofbuttons++];
    *button = (struct button){.pi = pi, .pin = pin, .callback = callback,
                             .pressed = pressed, .long_press_time = long_press_time};
    add_line(pin, resist);
    return button;
}

struct encoder *setupencoder(int pi, int pin_a, int pin_b,
                            rotaryencoder_callback_t callback, int mode)
{
    if (numberofencoders >= max_encoders || pin_a == pin_b ||
        !valid_pin(pin_a) || !valid_pin(pin_b)) {
        logerr("Cannot configure encoder on BCM %d, %d", pin_a, pin_b);
        return NULL;
    }
    struct encoder *encoder = &encoders[numberofencoders++];
    *encoder = (struct encoder){.pi = pi, .pin_a = pin_a, .pin_b = pin_b,
                               .callback = callback, .mode = mode};
    add_line(pin_a, PUD_UP);
    add_line(pin_b, PUD_UP);
    return encoder;
}

/* The bcm2835 pinctrl driver's line offsets are the BCM GPIO numbers. */
static bool bcm2835_offsets(int fd, struct gpio_v2_line_request *request)
{
    struct gpiochip_info chip;
    if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip) < 0)
        return false;
    if (strcmp(chip.label, "pinctrl-bcm2835") != 0)
        return false;
    for (int i = 0; i < numberoflines; i++) {
        if ((unsigned)pins[i] >= chip.lines)
            return false;
        request->offsets[i] = (unsigned)pins[i];
    }
    return true;
}

static int request_lines(void)
{
    struct gpio_v2_line_request request = {0};
    const char *override = getenv("SBPD_GPIO_CHIP");
    int chip_fd = -1;
    for (int i = 0; i < 64; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
        int fd = open(override ? override : path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            if (bcm2835_offsets(fd, &request)) {
                if (chip_fd >= 0) {
                    close(fd);
                    close(chip_fd);
                    logerr("Ambiguous BCM GPIO chips; set SBPD_GPIO_CHIP");
                    return -1;
                }
                chip_fd = fd;
            } else {
                close(fd);
            }
        }
        if (override)
            break;
    }
    if (chip_fd < 0) {
        logerr("No accessible bcm2835 GPIO v2 chip for configured BCM pins");
        return -1;
    }
    snprintf(request.consumer, sizeof(request.consumer), "sbpd");
    request.num_lines = numberoflines;
    request.event_buffer_size = 1024;
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT |
                           GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
    for (int resist = PUD_OFF; resist <= PUD_UP; resist++) {
        uint64_t mask = 0;
        for (int i = 0; i < numberoflines; i++)
            if (resistors[i] == resist)
                mask |= 1ULL << i;
        if (!mask)
            continue;
        struct gpio_v2_line_config_attribute *attr =
            &request.config.attrs[request.config.num_attrs++];
        attr->mask = mask;
        attr->attr.id = GPIO_V2_LINE_ATTR_ID_FLAGS;
        attr->attr.flags = request.config.flags |
            (resist == PUD_UP ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP :
             resist == PUD_DOWN ? GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN :
                                 GPIO_V2_LINE_FLAG_BIAS_DISABLED);
    }
    int rc = ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
    int saved_errno = errno;
    close(chip_fd);
    if (rc < 0) {
        logerr("GPIO v2 edge/bias request failed: %s", strerror(saved_errno));
        return -1;
    }
    edge_fd = request.fd;
    for (int i = 0; i < numberoflines; i++)
        offsets[i] = request.offsets[i];
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    snapshot_time = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    struct gpio_v2_line_values values = {.mask = (1ULL << numberoflines) - 1};
    if (ioctl(edge_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
        logerr("GPIO initial values failed: %s", strerror(errno));
        return -1;
    }
    for (int i = 0; i < numberoflines; i++)
        levels[i] = !!(values.bits & (1ULL << i));
    for (int i = 0; i < numberofbuttons; i++)
        buttons[i].value = levels[line_index(buttons[i].pin)];
    for (int i = 0; i < numberofencoders; i++)
        encoders[i].lastEncoded = (levels[line_index(encoders[i].pin_a)] << 1) |
                                  levels[line_index(encoders[i].pin_b)];
    return 0;
}

static void dispatch_event(const struct gpio_v2_line_event *event, int index)
{
    int bit = event->id == GPIO_V2_LINE_EVENT_RISING_EDGE;
    levels[index] = bit;
    uint64_t now = event->timestamp_ns / 1000000;
    for (int i = 0; i < numberofbuttons; i++) {
        struct button *button = &buttons[i];
        if (button->pin != pins[index])
            continue;
        button->value = bit;
        if (bit == button->pressed) {
            if (!button->tracking_press) {
                button->tracking_press = true;
                button->timepressed = now;
            }
        } else if (button->tracking_press) {
            uint64_t duration = now - button->timepressed;
            button->tracking_press = false;
            if (duration >= NOPRESSTIME && button->callback)
                button->callback(button, 1, duration > (uint64_t)button->long_press_time);
        }
    }
    for (int i = 0; i < numberofencoders; i++) {
        struct encoder *encoder = &encoders[i];
        if (encoder->pin_a != pins[index] && encoder->pin_b != pins[index])
            continue;
        int encoded = (levels[line_index(encoder->pin_a)] << 1) |
                       levels[line_index(encoder->pin_b)];
        int sum = (encoder->lastEncoded << 2) | encoded;
        int increment = 0;
        if (sum == 13 || sum == 4 || sum == 2 || sum == 11) increment = 1;
        if (sum == 14 || sum == 7 || sum == 1 || sum == 8) increment = -1;
        encoder->value += increment;
        encoder->lastEncoded = encoded;
        encoder->detents = encoder->value / 4;
        if (encoder->callback && increment)
            encoder->callback(encoder, increment);
    }
}

static void *wait_edges(void *unused)
{
    (void)unused;
    struct pollfd fds[] = {{wake_pipe[0], POLLIN, 0}, {edge_fd, POLLIN, 0}};
    uint32_t sequence = 0;
    while (true) {
        int rc = poll(fds, 2, -1);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0) {
            logerr("GPIO event poll failed: %s", strerror(errno));
            break;
        }
        if (fds[0].revents & POLLIN)
            return NULL;
        if ((fds[0].revents | fds[1].revents) & (POLLERR | POLLHUP | POLLNVAL)) {
            logerr("GPIO event descriptors failed: wake=%x events=%x",
                   fds[0].revents, fds[1].revents);
            break;
        }
        if (!(fds[1].revents & POLLIN))
            continue;
        struct gpio_v2_line_event event;
        ssize_t size;
        do {
            size = read(edge_fd, &event, sizeof(event));
        } while (size < 0 && errno == EINTR);
        if (size != sizeof(event) || event.seqno != sequence + 1 ||
            (event.id != GPIO_V2_LINE_EVENT_RISING_EDGE &&
             event.id != GPIO_V2_LINE_EVENT_FALLING_EDGE)) {
            logerr("GPIO event read/sequence error: size=%zd seq=%u expected=%u id=%u errno=%s",
                   size, event.seqno, sequence + 1, event.id,
                   size < 0 ? strerror(errno) : "none");
            break;
        }
        sequence = event.seqno;
        int index = -1;
        /* Offset is not necessarily BCM numbering. */
        for (int i = 0; i < numberoflines; i++)
            if (offsets[i] == event.offset)
                index = i;
        if (index < 0)
            break;
        if (event.timestamp_ns <= snapshot_time)
            continue;
        pthread_mutex_lock(&state_lock);
        dispatch_event(&event, index);
        pthread_mutex_unlock(&state_lock);
    }
    logerr("GPIO event worker stopped unexpectedly");
    pthread_mutex_lock(&state_lock);
    failed = true;
    pthread_mutex_unlock(&state_lock);
    return NULL;
}

int init_GPIO(void)
{
    loginfo("Initializing event-driven GPIO");
    return 0;
}

int start_GPIO(void)
{
    if (started)
        return -1;
    if (!numberoflines)
        return 0;
    if (request_lines() < 0 || pipe2(wake_pipe, O_CLOEXEC) < 0) {
        logerr("GPIO startup failed: %s", strerror(errno));
        shutdown_GPIO(0);
        return -1;
    }
    pthread_attr_t attr;
    struct sched_param priority = {.sched_priority = 0};
    int rc = pthread_attr_init(&attr);
    if (rc == 0) {
        rc = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        if (!rc) rc = pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
        if (!rc) rc = pthread_attr_setschedparam(&attr, &priority);
        if (!rc) rc = pthread_create(&worker, &attr, wait_edges, NULL);
        pthread_attr_destroy(&attr);
    }
    if (rc) {
        logerr("Cannot start normal-priority GPIO worker: %s", strerror(rc));
        shutdown_GPIO(0);
        return -1;
    }
    started = true;
    return 0;
}

bool GPIO_failed(void)
{
    pthread_mutex_lock(&state_lock);
    bool result = failed;
    pthread_mutex_unlock(&state_lock);
    return result;
}

long encoder_value(const struct encoder *encoder, bool detents)
{
    pthread_mutex_lock(&state_lock);
    long value = detents ? encoder->detents : encoder->value;
    pthread_mutex_unlock(&state_lock);
    return value;
}

void shutdown_GPIO(int pi)
{
    (void)pi;
    if (started) {
        ssize_t rc;
        do {
            rc = write(wake_pipe[1], "x", 1);
        } while (rc < 0 && errno == EINTR);
        if (rc != 1) {
            logerr("GPIO shutdown wake failed: %s", strerror(errno));
            close(wake_pipe[1]);
            wake_pipe[1] = -1;
        }
        pthread_join(worker, NULL);
        started = false;
    }
    if (edge_fd >= 0) close(edge_fd);
    if (wake_pipe[0] >= 0) close(wake_pipe[0]);
    if (wake_pipe[1] >= 0) close(wake_pipe[1]);
    edge_fd = wake_pipe[0] = wake_pipe[1] = -1;
}
