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
#include <gpiod.h>
#include <glob.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
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
static struct gpiod_line_request *line_request;
static struct gpiod_edge_event_buffer *event_buffer;
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
    if (started || edge_fd >= 0 || pin < 0 || pin > 57 || line_index(pin) >= 0) {
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

/* Pi 5/CM5 header GPIOs belong to RP1, not the BCM2712 SoC controllers. */
static int chip_priority(const char *label)
{
    if (!label)
        return 0;
    if (!strcmp(label, "pinctrl-rp1"))
        return 2;
    if (!strcmp(label, "pinctrl-bcm2835") ||
        !strcmp(label, "pinctrl-bcm2711"))
        return 1;
    return 0;
}

/* Caller holds state_lock once the worker is running. */
static int sync_levels(bool keep_presses)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        logerr("Cannot timestamp GPIO levels: %s", strerror(errno));
        return -1;
    }
    snapshot_time = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    enum gpiod_line_value values[MAX_LINES];
    if (gpiod_line_request_get_values_subset(line_request, numberoflines, offsets, values) < 0) {
        logerr("GPIO values read failed: %s", strerror(errno));
        return -1;
    }
    for (int i = 0; i < numberoflines; i++)
        levels[i] = values[i] == GPIOD_LINE_VALUE_ACTIVE;
    for (int i = 0; i < numberofbuttons; i++) {
        struct button *button = &buttons[i];
        button->value = levels[line_index(button->pin)];
        if (!keep_presses || button->value != button->pressed)
            button->tracking_press = false;
        else if (!button->tracking_press) {
            button->tracking_press = true;
            button->timepressed = snapshot_time / 1000000;
        }
    }
    for (int i = 0; i < numberofencoders; i++)
        encoders[i].lastEncoded = (levels[line_index(encoders[i].pin_a)] << 1) |
                                  levels[line_index(encoders[i].pin_b)];
    return 0;
}

static int request_lines(void)
{
    const char *override = getenv("SBPD_GPIO_CHIP");
    glob_t paths = {0};
    struct gpiod_chip *chip = NULL;
    int priority = 0;
    bool ambiguous = false;
    if (!override) {
        int rc = glob("/dev/gpiochip*", 0, NULL, &paths);
        if (rc != 0) {
            globfree(&paths);
            logerr("Cannot discover GPIO chips (glob error %d)", rc);
            return -1;
        }
    }
    size_t count = override ? 1 : paths.gl_pathc;
    for (size_t i = 0; i < count; i++) {
        const char *path = override ? override : paths.gl_pathv[i];
        struct gpiod_chip *candidate = gpiod_chip_open(path);
        if (!candidate) {
            logerr("Cannot open GPIO chip %s: %s", path, strerror(errno));
            goto discovery_error;
        }
        struct gpiod_chip_info *info = gpiod_chip_get_info(candidate);
        if (!info) {
            logerr("Cannot read GPIO chip %s information: %s", path, strerror(errno));
            gpiod_chip_close(candidate);
            goto discovery_error;
        }
        int rank = chip_priority(gpiod_chip_info_get_label(info));
        size_t lines = gpiod_chip_info_get_num_lines(info);
        for (int j = 0; j < numberoflines; j++)
            if ((unsigned)pins[j] >= lines)
                rank = 0;
        gpiod_chip_info_free(info);
        if (rank > priority) {
            if (chip)
                gpiod_chip_close(chip);
            chip = candidate;
            priority = rank;
            ambiguous = false;
        } else {
            if (rank && rank == priority)
                ambiguous = true;
            gpiod_chip_close(candidate);
        }
    }
    globfree(&paths);
    if (!chip || ambiguous) {
        if (chip)
            gpiod_chip_close(chip);
        logerr("No unique Raspberry Pi GPIO chip for BCM pins; check SBPD_GPIO_CHIP and permissions");
        return -1;
    }
    struct gpiod_line_config *config = gpiod_line_config_new();
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_request_config *request_config = gpiod_request_config_new();
    int rc = -1;
    if (!config || !settings || !request_config)
        goto config_done;
    gpiod_request_config_set_consumer(request_config, "sbpd");
    gpiod_request_config_set_event_buffer_size(request_config, 1024);
    for (int i = 0; i < numberoflines; i++) {
        offsets[i] = (unsigned)pins[i];
        enum gpiod_line_bias bias = resistors[i] == PUD_UP ? GPIOD_LINE_BIAS_PULL_UP :
            resistors[i] == PUD_DOWN ? GPIOD_LINE_BIAS_PULL_DOWN : GPIOD_LINE_BIAS_DISABLED;
        if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT) < 0 ||
            gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH) < 0 ||
            gpiod_line_settings_set_event_clock(settings, GPIOD_LINE_CLOCK_MONOTONIC) < 0 ||
            gpiod_line_settings_set_bias(settings, bias) < 0 ||
            gpiod_line_config_add_line_settings(config, &offsets[i], 1, settings) < 0)
            goto config_done;
    }
    line_request = gpiod_chip_request_lines(chip, request_config, config);
    if (line_request)
        rc = 0;
config_done:
    ;
    int saved_errno = errno;
    if (config) gpiod_line_config_free(config);
    if (settings) gpiod_line_settings_free(settings);
    if (request_config) gpiod_request_config_free(request_config);
    gpiod_chip_close(chip);
    if (rc < 0) {
        logerr("GPIO edge/bias request failed: %s", strerror(saved_errno));
        return -1;
    }
    edge_fd = gpiod_line_request_get_fd(line_request);
    event_buffer = gpiod_edge_event_buffer_new(64);
    if (!event_buffer) {
        logerr("Cannot allocate GPIO event buffer: %s", strerror(errno));
        return -1;
    }
    return sync_levels(false);
    return 0;
discovery_error:
    if (chip) gpiod_chip_close(chip);
    globfree(&paths);
    return -1;
}

static void dispatch_event(struct gpiod_edge_event *event, int index)
{
    int bit = gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE;
    levels[index] = bit;
    uint64_t now = gpiod_edge_event_get_timestamp_ns(event) / 1000000;
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
    unsigned long line_sequence[MAX_LINES] = {0};
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
        int count;
        do {
            count = gpiod_line_request_read_edge_events(line_request, event_buffer, 64);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) {
            logerr("GPIO event read failed: %s", count < 0 ? strerror(errno) : "no events");
            break;
        }
        for (int j = 0; j < count; j++) {
            struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(event_buffer, j);
            int index = line_index((int)gpiod_edge_event_get_line_offset(event));
            if (index < 0) {
                logerr("GPIO event for an unrequested line");
                goto worker_failed;
            }
            /*
             * The request-wide seqno can skip when interrupts coalesce.
             * Per-line seqnos are contiguous unless the kernel FIFO dropped events.
             */
            unsigned long seqno = gpiod_edge_event_get_line_seqno(event);
            bool lost = line_sequence[index] && seqno != line_sequence[index] + 1;
            line_sequence[index] = seqno;
            if (lost) {
                logwarn("GPIO events lost on BCM %d; resynchronizing levels", pins[index]);
                pthread_mutex_lock(&state_lock);
                rc = sync_levels(true);
                pthread_mutex_unlock(&state_lock);
                if (rc < 0)
                    goto worker_failed;
                continue;
            }
            if (gpiod_edge_event_get_timestamp_ns(event) <= snapshot_time)
                continue;
            pthread_mutex_lock(&state_lock);
            dispatch_event(event, index);
            pthread_mutex_unlock(&state_lock);
        }
    }
worker_failed:
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
    if (event_buffer) gpiod_edge_event_buffer_free(event_buffer);
    if (line_request) gpiod_line_request_release(line_request);
    event_buffer = NULL;
    line_request = NULL;
    if (wake_pipe[0] >= 0) close(wake_pipe[0]);
    if (wake_pipe[1] >= 0) close(wake_pipe[1]);
    edge_fd = wake_pipe[0] = wake_pipe[1] = -1;
}
