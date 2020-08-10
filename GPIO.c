//
//  GPIO.c
//  SqueezeButtonPi
//
//  Low-Level code to configure and read buttons and rotary encoders.
//  Calls callbacks on activity
//
//  Created by Jörg Schwieder on 02.02.17.
//
//
//  Copyright (c) 2017, Joerg Schwieder, PenguinLovesMusic.com
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

#include "GPIO.h"
#include "sbpd.h"

#include <pigpiod_if2.h>

//
//  Configured buttons
//
static int numberofbuttons = 0;
//
//  Pre-allocate encoder objects on the stack so we don't have to
//  worry about freeing them
//
static struct button buttons[max_buttons];

//
// GetTime function
//
uint32_t gettime_ms(void) {
	struct timespec ts;
	if (!clock_gettime(CLOCK_REALTIME, &ts)) {
		return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	}
	return 0;
}

//
// number of milliseconds to debounce the button
//
//#define NOPRESSTIME 50
//	No additional debouncing is needed. It is done by glitchfilter.
//
//
//
//  Button handler function
//  Called by the GPIO interrupt when a button is pressed or released
//  Depends on edge configuration.
//  Checks only the buttons pressed for status changes and
//  calls callback if state change detected.
//
static void  updateButtons( int pi, unsigned pin, unsigned  level, uint32_t tick, void *but){
	uint32_t now;
//	now = gettime_ms();
	struct button *button = but;

	bool bit = (level == 0)? 0 : 1;
	now = tick / 1000;
	logdebug("%lu - %lu= %i  Pin Value=%i   Stored Value=%i", (unsigned long)now, (unsigned long)button->timepressed, (signed int)(now - button->timepressed), bit, button->value);

	if ( (bit == button->pressed) && (button->timepressed == 0) && (level != 2) ){	
		button->timepressed = now;
		set_watchdog(pi, button->pin, button->long_press_time)
	} else if (button->timepressed != 0){	
		if (level == 2) {
			loginfo("Long PRESS: %i", (signed int)(now - button->timepressed));
			button->value = bit;
			button->callback(button, 1, LONGPRESS);
		} else {
			loginfo("Short PRESS: %i", (signed int)(now - button->timepressed));
			button->value = bit;
			button->callback(button, 1, SHORTPRESS);
		}
		button->timepressed = 0;
		set_watchdog(pi, button->pin, 0)
	}
}

//
//
//  Configuration function to define a button
//  Should be run for every button you want to control
//  For each button a button struct will be created
//
//  Parameters:
//      pin: GPIO-Pin used in BCM numbering scheme
//      callback: callback function to be called when button state changed
//      edge: edge to be used for trigger events,
//            one of INT_EDGE_RISING, INT_EDGE_FALLING or INT_EDGE_BOTH (the default)
//  Returns: pointer to the new button structure
//           The pointer will be NULL is the function failed for any reason
//
//
struct button *setupbutton(int pi, int pin, button_callback_t b_callback, int resist, bool pressed, int long_press_time)
{
    if (numberofbuttons > max_buttons)
    {
        logerr("Maximum number of buttons exceded: %i", max_buttons);
        return NULL;
    }

    int edge = EITHER_EDGE;  //Need to see both directions for button depressed time.

    struct button *newbutton = buttons + numberofbuttons++;
    newbutton->pi = pi;
    newbutton->pin = pin;
    newbutton->value = 0;
    newbutton->callback = b_callback;
    newbutton->timepressed = 0;
    newbutton->pressed = pressed;
    newbutton->long_press_time = long_press_time;
    set_mode( pi,  pin, PI_INPUT);
    set_pull_up_down(pi, pin, resist);
    set_glitch_filter(pi, pin, 50000);
    newbutton->cb_id = callback_ex(pi, (unsigned) pin, (unsigned)edge, (CBFuncEx_t)updateButtons, newbutton);

    return newbutton;
}

//
//
// Encoders
// Rotary Encoder taken from https://github.com/astine/rotaryencoder
// http://theatticlight.net/posts/Reading-a-Rotary-Encoder-from-a-Raspberry-Pi/
//
// Updated to encoder control based on 
// http://abyz.me.uk/rpi/pigpio/code/rotary_encoder_py.zip
//
//
//  Configured encoders
//
static int numberofencoders = 0;
//
//  Pre-allocate encoder objects on the stack so we don't have to
//  worry about freeing them
//
static struct encoder encoders[max_encoders];


/*

             +---------+         +---------+      0
             |         |         |         |
   A         |         |         |         |
             |         |         |         |
   +---------+         +---------+         +----- 1

       +---------+         +---------+            0
       |         |         |         |
   B   |         |         |         |
       |         |         |         |
   ----+         +---------+         +---------+  1

*/
static int transits[16]=
{
/* 0000 0001 0010 0011 0100 0101 0110 0111 */
      0,  -1,   1,   0,   1,   0,   0,  -1,
/* 1000 1001 1010 1011 1100 1101 1110 1111 */
     -1,   0,   0,   1,   0,   1,  -1,   0
};

static void updateEncoders(
   int pi, unsigned gpio, unsigned level, uint32_t tick, void *enc)
{
	struct encoder *encoder = enc;
	long newState, inc;

	if (level != PI_TIMEOUT)
	{
		if (gpio == encoder->pin_a)
			encoder->levA = level;
		else
			encoder->levB = level;

		newState = encoder->levA << 1 | encoder->levB;

		inc = transits[encoder->lastEncoded << 2 | newState];

/*	Since we are running in a polling mode, just update the encoder value and quit.
	no need to call the encoders callback.
		if (inc)
		{
			encoder->lastEncoded = newState;

			detent = encoder->value / 4;

			encoder->value += inc;

			if (encoder->callback)
			{
				if (encoder->mode == ENCODER_MODE_DETENT)
				{
					if (detent != (encoder->value / 4)) (encoder->callback)(encoder, encoder->value / 4);
				}
				else (encoder->callback)(encoder, encoder->value);
			}
		}*/
		if (inc){
			encoder->lastEncoded = newState;
			encoder->value += inc;
			encoder->detents = encoder->value / encoder->mode;
		}
	}
}

//
//
//  Configuration function to define a rotary encoder
//  Should be run for every rotary encoder you want to control
//  For each encoder a button struct will be created
//
//  Parameters:
//      pin_a, pin_b: GPIO-Pins used in BCM numbering scheme
//      callback: callback function to be called when encoder state changed
//      mode: operate in ENCODER_MODE_DETENT or , ENCODER_MODE_STEP
//
//  Returns: pointer to the new encoder structure
//           The pointer will be NULL is the function failed for any reason
//
//
struct encoder *setupencoder(int pi,
                             int pin_a,
                             int pin_b,
                             rotaryencoder_callback_t e_callback,
                             int mode)
{
    if (numberofencoders > max_encoders)
    {
        logerr("Maximum number of encodered exceded: %i", max_encoders);
        return NULL;
    }

    struct encoder *newencoder = encoders + numberofencoders++;
    newencoder->pi = pi;
    newencoder->pin_a = pin_a;
    newencoder->pin_b = pin_b;
    newencoder->value = 0;
    newencoder->detents = 0;
    newencoder->lastEncoded = 0;
    newencoder->callback = e_callback;
    newencoder->mode = mode;
    newencoder->glitch = 1000;
    newencoder->levA = 0;
    newencoder->levB = 0;

    set_mode(pi, pin_a, PI_INPUT);
    set_mode(pi, pin_b, PI_INPUT);
    set_pull_up_down(pi, pin_a, PI_PUD_UP);
    set_pull_up_down(pi, pin_b, PI_PUD_UP);
    set_glitch_filter(pi, pin_a, newencoder->glitch);
    set_glitch_filter(pi, pin_b, newencoder->glitch);

    newencoder->lastEncoded = (gpio_read(pi, pin_a) << 1) | gpio_read(pi, pin_b);

    newencoder->cba_id = callback_ex(pi, (unsigned) pin_a, EITHER_EDGE, (CBFuncEx_t)updateEncoders, newencoder);
    newencoder->cbb_id = callback_ex(pi, (unsigned) pin_b, EITHER_EDGE, (CBFuncEx_t)updateEncoders, newencoder);

    return newencoder;
}

//
//
//  Init GPIO functionality
//  Connect to the pigpiod interface.
//
//

int init_GPIO() {
	loginfo("Initializing GPIO");
	int pi = pigpio_start( NULL, NULL); /* Connect to Pi. NULL means the local pigpiod*/
	return pi;
}

void shutdown_GPIO( int pi) {
	loginfo("Disconnecting from pigpiod");
	pigpio_stop(pi);
}
