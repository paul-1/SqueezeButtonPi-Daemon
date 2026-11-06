//
//  GPIO.h
//  SqueezeButtonPi
//
//  Created by Jörg Schwieder on 02.02.17.
//
//
//  Copyright (c) 2025, Joerg Schwieder, PenguinLovesMusic.com
//  Copyright (c) 2026, paul-1 <6473457+paul-1@users.noreply.github.com>
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

#ifndef GPIO_h
#define GPIO_h

#include "sbpd.h"
#include "time.h"


//
//
//  Init GPIO functionality
//  Configure Linux GPIO character-device input lines.
//
//

int init_GPIO();
int start_GPIO(void);
bool GPIO_failed(void);
void shutdown_GPIO( int pi );
struct encoder;
long encoder_value(const struct encoder *encoder, bool detents);

/* Keep command-line resistor values compatible with WiringPi. */
#define PUD_OFF 0
#define PUD_DOWN 1
#define PUD_UP 2
//
// Buttons and Rotary Encoders
// Rotary Encoder taken from https://github.com/astine/rotaryencoder
// http://theatticlight.net/posts/Reading-a-Rotary-Encoder-from-a-Raspberry-Pi/
//

//17 pins / 2 pins per encoder = 8 maximum encoders
#define max_encoders 8
//17 pins / 1 pins per button = 17 maximum buttons
#define max_buttons 17

struct button;

#define SHORTPRESS 0
#define LONGPRESS 1

//
//  A callback executed when a button gets triggered. Button struct and change returned.
//  Note: change might be "0" indicating no change, this happens when buttons chatter
//  Value in struct already updated.
//
typedef void (*button_callback_t)(const struct button * button, int change, bool presstype);

struct button {
    int pi;
    int pin;
    bool value;
    button_callback_t callback;
    uint64_t timepressed;
    bool tracking_press;
    bool pressed;
    int long_press_time;
};

//
//
//  Configuration function to define a button
//  Should be run for every button you want to control
//  For each button a button struct will be created
//
//  Parameters:
//      pin: GPIO-Pin used in BCM numbering scheme
//      callback: callback function to be called when button state changed
//      resist: PUD_OFF, PUD_DOWN or PUD_UP
//      pressed: input level indicating a press
//      long_press_time: threshold in milliseconds
//  Returns: pointer to the new button structure
//           The pointer will be NULL is the function failed for any reason
//
//
struct button *setupbutton(int pi,
                           int pin,
                           button_callback_t b_callback,
                           int resist,
                           bool pressed,
                           int long_press_time);

struct encoder;

//
//  A callback executed when a rotary encoder changes it's value.
//  Encoder struct and change returned.
//  Value in struct already updated.
//
typedef void (*rotaryencoder_callback_t)(const struct encoder * encoder, long change);

struct encoder
{
    int pi;
    int pin_a;
    int pin_b;
    long value;
    long detents;
    int lastEncoded;
    rotaryencoder_callback_t callback;
    int mode;
};

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
                             rotaryencoder_callback_t callback,
                             int mode);

#define ENCODER_MODE_DETENT 0
#define ENCODER_MODE_STEP   1

#endif /* GPIO_h */
