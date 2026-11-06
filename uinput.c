//
//  uinput.c
//  SqueezeButtonPi
//
//  Code derived from  https://www.kernel.org/doc/html/v4.19/input/uinput.html

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
	int result = 0;
	int err = pthread_mutex_lock(&send_lock);

	if (err != 0) {
		logerr("Could not lock keyboard output: %s", strerror(err));
		return 1;
	}

	ie.type = EV_KEY;
	ie.code = key;
	ie.value = value;
	/* timestamp values below are ignored */
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;
	ssize_t written;
	do {
		written = write(usetup_fd, &ie, sizeof(ie));
	} while (written < 0 && errno == EINTR);
	if (written != sizeof(ie)) {
		if (written < 0)
			logerr("Could not write keyboard event: %s", strerror(errno));
		else
			logerr("Incomplete keyboard event write: %zd of %zu bytes", written, sizeof(ie));
		result = 1;
		goto unlock;
	}
	// Send sync event
	ie.type = EV_SYN;
	ie.code = SYN_REPORT;
	ie.value = 0;
	do {
		written = write(usetup_fd, &ie, sizeof(ie));
	} while (written < 0 && errno == EINTR);
	if (written != sizeof(ie)) {
		if (written < 0)
			logerr("Could not write keyboard sync event: %s", strerror(errno));
		else
			logerr("Incomplete keyboard sync write: %zd of %zu bytes", written, sizeof(ie));
		result = 1;
	}

unlock:
	err = pthread_mutex_unlock(&send_lock);
	if (err != 0) {
		logerr("Could not unlock keyboard output: %s", strerror(err));
		result = 1;
	}
	return result;
}
