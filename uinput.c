//
//  uinput.c
//  SqueezeButtonPi
//
//  Copyright (c) 2025-2026, paul-1 <6473457+paul-1@users.noreply.github.com>
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of ickStream nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
//  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
//  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
//  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include "uinput.h"
#include "sbpd.h"

#define KEYBOARD_NAME "jivelite-uinput"
#define KEYBOARD_KEY_COUNT 255

static int keyboard_fd = -1;
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;

static int create_keyboard(int fd)
{
	struct uinput_setup setup = {
		.id = { .bustype = BUS_VIRTUAL, .version = 1 },
	};

	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		return -1;
	for (int key = 0; key < KEYBOARD_KEY_COUNT; key++)
		if (ioctl(fd, UI_SET_KEYBIT, key) < 0)
			return -1;
	snprintf(setup.name, sizeof(setup.name), "%s", KEYBOARD_NAME);
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
		return -1;
	return ioctl(fd, UI_DEV_CREATE);
}

int init_uinput(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return 1;
	if (create_keyboard(fd) < 0) {
		close(fd);
		return 1;
	}
	keyboard_fd = fd;
	return 0;
}

int disconnect_uinput(void)
{
	int result = ioctl(keyboard_fd, UI_DEV_DESTROY) < 0;

	close(keyboard_fd);
	keyboard_fd = -1;
	return result;
}

/* The kernel stamps uinput events itself, so the time field stays zero. */
static int send_event(unsigned short type, unsigned short code, int value,
		      const char *what)
{
	const struct input_event event = { .type = type, .code = code, .value = value };
	ssize_t written;

	do {
		written = write(keyboard_fd, &event, sizeof(event));
	} while (written < 0 && errno == EINTR);
	if (written == sizeof(event))
		return 0;
	if (written < 0)
		logerr("Could not write keyboard %s event: %s", what, strerror(errno));
	else
		logerr("Incomplete keyboard %s write: %zd of %zu bytes",
		       what, written, sizeof(event));
	return 1;
}

int emitKey(int key, int value)
{
	int err = pthread_mutex_lock(&send_lock);
	int result;

	if (err != 0) {
		logerr("Could not lock keyboard output: %s", strerror(err));
		return 1;
	}
	result = send_event(EV_KEY, key, value, "key") ||
		 send_event(EV_SYN, SYN_REPORT, 0, "sync");
	err = pthread_mutex_unlock(&send_lock);
	if (err != 0) {
		logerr("Could not unlock keyboard output: %s", strerror(err));
		result = 1;
	}
	return result;
}
