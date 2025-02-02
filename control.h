//
//  control.h
//  SqueezeButtonPi
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

#ifndef control_h
#define control_h

#include "sbpd.h"
#include "GPIO.h"

//
//  Store command parameters for each button used
//
struct button_ctrl
{
    struct button * gpio_button;
    volatile bool waiting;
    char * shortfragment;
    char * longfragment;
    bool presstype;
    int cmdtype;
    int cmd_longtype;
	int key_code;
	int key_code_long;

};

//
//  Setup button control
//  Parameters:
//      cmd: Command type LMS. One of
//                  PLAY    - play/pause
//                  VOL+    - increment volume
//                  VOL-    - decrement volume
//                  PREV    - previous track
//                  NEXT    - next track
//          Command type SCRIPT.
//                  SCRIPT:/path/to/shell/script.sh
//      pin: the GPIO-Pin-Number
//      edge: one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//
int setup_button_ctrl( int pi, char * cmd, int pin, int resist, int pressed, char * cmd_long, int long_time);

//
//  Polling function: handle button commands
//  Parameters:
//      server: the server to send commands to
//
void handle_buttons(struct sbpd_server * server);

//
//  Store command parameters for each button used
//
struct encoder_ctrl
{
    struct encoder * gpio_encoder;
	int cmd_type;
    volatile long last_value;
    char * fragment;
	char * fragment_neg;
	int key_code_pos;
	int key_code_neg;
	int limit;
	volatile long long last_time;
	int min_time;
};
//
//  Setup encoder control
//  Parameters:
//      cmd: Command. Currently only
//                  VOLU    - volume
//          Can be NULL for volume or actually anything since it's ignored
//      pin1: the GPIO-Pin-Number for the first pin used
//      pin2: the GPIO-Pin-Number for the second pin used
//      mode: one of
//                  0 - ENCODER_MODE_DETENT
//                  1 - ENCODER_MODE_STEP  <default>
//
int setup_encoder_ctrl(int pi, char * cmd, int pin1, int pin2, int mode);

//
//  Polling function: handle encoders
//  Parameters:
//      server: the server to send commands to
//
void handle_encoders(struct sbpd_server * server);

//
// Set of commands to send to LMS server
//
#define MAXLEN 255
#define MAX_COMMANDS 80
struct lms_command {
  int code;
  char fragment[MAXLEN];
};

int add_lms_command_frament ( char * name, char * value );

//
// Keyboard controls
//
// global variable to initalaize uinput if a keyboard control is configured.
extern bool keyboard_inuse;
//  Keyboad key definitions
typedef struct{
	char name[32];
	int code;
}key_events_s;

#endif /* control_h */
