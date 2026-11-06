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
//  Attribution: the uinput device setup and event emission code is derived
//  from the example in the Linux kernel documentation "uinput module"
//  (Documentation/input/uinput.rst),
//  https://www.kernel.org/doc/html/v4.19/input/uinput.html
//  The Linux kernel documentation is distributed under GPL-2.0.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <linux/uinput.h>
#include "uinput.h"
#include "sbpd.h"

static int usetup_fd;
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;

int init_uinput(void){
	int fd;
	struct uinput_setup usetup;
	int i;

	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if(fd < 0)
		return 1;

	//setup keyboard
	if(ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		return 1;
	//Add keys to device.
	for(i=0; i<255; i++){
		if(ioctl(fd, UI_SET_KEYBIT, i) < 0)
			return 1;
	}

	memset(&usetup, 0, sizeof(usetup));
	snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "jivelite-uinput");
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor  = 0x1234;
	usetup.id.product = 0x5678;

	if(ioctl(fd, UI_DEV_SETUP, &usetup) < 0)
		return 1;
	if(ioctl(fd, UI_DEV_CREATE) < 0)
		return 1;

	usetup_fd = fd;

	return 0;
}

int disconnect_uinput(void){
	if(ioctl(usetup_fd, UI_DEV_DESTROY) < 0)
		return 1;
	close(usetup_fd);
	return 0;
}

int emitKey(int key, int value){
	struct input_event ie;

	//not really needed right now, as we are polling inside the server rather than pure irq driven
	pthread_mutex_lock(&send_lock);

	ie.type = EV_KEY;
	ie.code = key;
	ie.value = value;
	/* timestamp values below are ignored */
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;
	if(write(usetup_fd, &ie, sizeof(ie)) < 0)
		return 1;
	// Send sync event
	ie.type = EV_SYN;
	ie.code = SYN_REPORT;
	ie.value = 0;
	if(write(usetup_fd, &ie, sizeof(ie)) < 0)
		return 1;

	pthread_mutex_unlock(&send_lock);
	return 0;
}
