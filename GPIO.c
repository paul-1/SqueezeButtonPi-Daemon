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
//  Copyright (c) 2025, Joerg Schwieder, PenguinLovesMusic.com
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

#include <wiringPi.h>

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
#define NOPRESSTIME 50
//
//
//  Button handler function
//  Called by the GPIO interrupt when a button is pressed or released
//  Depends on edge configuration.
//  Checks all configred buttons for status changes and
//  calls callback if state change detected.
//
//
void updateButtons() {
	uint32_t now;
	now = gettime_ms();
	struct button *button = buttons;

	for (; button < buttons + numberofbuttons; button++) {
		bool bit = digitalRead(button->pin);
		bool presstype;
		logdebug("%lu - %lu= %i  Pin Value=%i   Stored Value=%i", (unsigned long)now, (unsigned long)button->timepressed, (signed int)(now - button->timepressed), bit, button->value);

		int increment = 0;
		if ( (bit == button->pressed) && (button->timepressed == 0) ){	
			button->timepressed = now;
			increment = 0;
		} else if (button->timepressed != 0){	
			if ((signed int)(now - button->timepressed) < (signed int)NOPRESSTIME ) {
				logdebug("No PRESS: %i", (signed int)(now - button->timepressed));
				increment = 0;
			} else if ((signed int)(now - button->timepressed) > (signed int)button->long_press_time ) {
				loginfo("Long PRESS: %i", (signed int)(now - button->timepressed));
				button->value = bit;
				presstype = LONGPRESS;
				increment = 1;
			} else {
				loginfo("Short PRESS: %i", (signed int)(now - button->timepressed));
				button->value = bit;
				presstype = SHORTPRESS;
				increment = 1;
			}
			button->timepressed = 0;
		}
		if (button->callback && increment)
			button->callback(button, increment, presstype);
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

    int edge = INT_EDGE_BOTH;  //Need to see both directions for button depressed time.

    struct button *newbutton = buttons + numberofbuttons++;
    newbutton->pi = pi;
    newbutton->pin = pin;
    newbutton->value = 0;
    newbutton->callback = b_callback;
    newbutton->timepressed = 0;
    newbutton->pressed = pressed;
    newbutton->long_press_time = long_press_time;
    pinMode( pin, INPUT);
    pullUpDnControl(pin, resist);
    newbutton->cb_id = wiringPiISR((unsigned) pin, (unsigned)edge, &updateButtons);

    return newbutton;
}

//
//
// Encoders
// Rotary Encoder taken from https://github.com/astine/rotaryencoder
// http://theatticlight.net/posts/Reading-a-Rotary-Encoder-from-a-Raspberry-Pi/
//
//  Configured encoders
//
static int numberofencoders = 0;
//
//  Pre-allocate encoder objects on the stack so we don't have to
//  worry about freeing them
//
static struct encoder encoders[max_encoders];

void updateEncoders()
{
    struct encoder *encoder = encoders;
    for (; encoder < encoders + numberofencoders; encoder++)
    {
        int MSB = digitalRead(encoder->pin_a);
        int LSB = digitalRead(encoder->pin_b);
        
        int encoded = (MSB << 1) | LSB;
        int sum = (encoder->lastEncoded << 2) | encoded;
        
        int increment = 0;
        
        if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) increment = 1;
        if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) increment = -1;
        
        encoder->value += increment;
        encoder->lastEncoded = encoded;
        encoder->detents = encoder->value / 4;

        if (encoder->callback)
            encoder->callback(encoder, increment);
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

    pinMode(pin_a, INPUT);
    pinMode(pin_b, INPUT);
    pullUpDnControl(pin_a, PUD_UP);
    pullUpDnControl(pin_b, PUD_UP);

    newencoder->lastEncoded = (digitalRead(pin_a) << 1) | digitalRead(pin_b);

    newencoder->cba_id = wiringPiISR((unsigned) pin_a, INT_EDGE_BOTH, &updateEncoders);
    newencoder->cbb_id = wiringPiISR((unsigned) pin_b, INT_EDGE_BOTH, &updateEncoders);

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
	int pi = wiringPiSetupGpio(); /* Connect to Pi. NULL means the local pigpiod*/
	return pi;
}

void shutdown_GPIO( int pi) {
    struct button *button = buttons;
    struct encoder *encoder = encoders;
    loginfo("Disconnecting from gpio");
    for (; button < buttons + numberofbuttons; button++) {
        if ( wiringPiISRStop(button->pin) == 0 ){
            loginfo("GPIO %d button callback cancelled.", button->pin);
        } else {
            loginfo("Error cancelling callback for GPIO %d.", button->pin);
        }
    }
    for (; encoder < encoders + numberofencoders; encoder++) {
        if ( wiringPiISRStop(encoder->pin_a) == 0 ) {
             loginfo("GPIO %d encoder callback cancelled.", encoder->pin_a);
        } else {
             loginfo("Error cancelling callback for GPIO %d.", encoder->pin_a);
        }
        if ( wiringPiISRStop(encoder->pin_b) == 0 ) {
             loginfo("GPIO %d encoder callback cancelled.", encoder->pin_b);
        } else {
             loginfo("Error cancelling callback for GPIO %d.", encoder->pin_b);
        }
    }
}

