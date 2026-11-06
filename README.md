
# SqeezeButtonPi Daemon

SqueezeButtonPi Daemon or sbpd is a controller tool to use buttons and rotary encoders to control an instance of SqueezeLite or SqueezePlay running on the same Raspberry Pi device in connection with a Logitech Media Server/Squeezebox Server instance.

Rotary encoders or rotary-push-encoders can be used for volume, buttons (and the push-function of a rotary encoder) can be used for play/pause, skip forward, skip back or toggle the power state.

## COPYRIGHT / LICENSE

Copyright (c) 2017, Joerg Schwieder, PenguinLovesMusic.com
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
   * Neither the name of ickStream nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Dependencies

SqueezeButtonPi uses libCurl, POSIX threads and **libgpiod 2.0.2 or newer**
(including 2.3.2). The Linux GPIO character-device v2 API requires kernel
**5.10 or newer**, with GPIO input edge and bias support enabled. Build with
`make`; a C compiler, make, pkg-config and development files for libCurl and
libgpiod are required. libgpiod must also be installed on the target system.
No WiringPi, pigpio or pigpiod is required.

### Event-driven GPIO

BCM pin numbers and command-line resistor values remain unchanged. At startup,
sbpd discovers `/dev/gpiochip*` by controller identity, not a fixed chip number
or line names (which may be changed by device-tree overlays). Supported labels
are `pinctrl-bcm2835` (Pi 1/2/3, Zero/Zero W/Zero 2 W and CM1/CM3),
`pinctrl-bcm2711` (Pi 4/400 and CM4), and `pinctrl-rp1` (Pi 5/500 and CM5).
For these controllers, the line offset is the BCM GPIO number. RP1 is preferred
when present; BCM2712's internal/AON controllers are deliberately not selected
for Pi 5 header GPIOs. This follows the controller distinction used by
[Raspberry Pi pinctrl](https://github.com/raspberrypi/utils/tree/master/pinctrl),
without importing its MMIO implementation.

Set `SBPD_GPIO_CHIP=/dev/gpiochipN` to restrict discovery to a supported chip.
Ambiguous chips require this explicit selection. All configured lines must fit
the selected controller. On Compute Modules the carrier determines which lines
are exposed; GPIO numbers are controller-local, not physical connector numbers.
Older 26-pin Pi headers also retain BCM numbering (including GPIO21 rather than
GPIO27 on revision 1 boards). The process needs read access to the discovered
chips and exclusive ownership of the configured lines. Unsupported controllers
are rejected rather than assuming that chip zero is the header controller.
Invalid/duplicate pins, unsupported bias, busy lines, unavailable chips and
worker startup failures are explicit errors; there is no polling fallback.

All lines are requested together with both edges and their configured bias.
Initial levels establish the startup baseline; activity during initialization
is not guaranteed to count. One explicitly normal-priority (`SCHED_OTHER`, 0)
thread blocks indefinitely in `poll()` on the ordered GPIO event fd and a
shutdown pipe. It processes only the affected button/encoder using event levels
and monotonic timestamps, not scans of MMIO pins. Unrelated edges cannot finish
a held button press. Button release classifies short/long presses after the
existing 50 ms chatter filter; encoder quadrature and four-step detents remain.
State is synchronized; no lock is held across main-thread command I/O.

There are no WiringPi ISR threads, real-time priority requests, or periodic
0.5 ms GPIO waits. The main loop retains its 100 ms command aggregation and
synchronous server requests. Pending button actions still coalesce between
main-loop passes. GPIO queue overflow/read failures stop the daemon explicitly
instead of silently miscounting. Shutdown wakes and joins the GPIO worker
before curl/uinput cleanup.

This is an application-side replacement for WiringPi's timed ISR waits, **not
a kernel fix**: it removes their idle wakeups (about 8,000/s for four ISR lines)
without changing kernel scheduling or masking kernel warnings. Real Raspberry
Pi testing is still needed for controller discovery, bias support and rapid encoder
bursts; the requested kernel event queue is 1,024 events.

## Configuration

Usage: 
`sbpd [OPTION...] [e,pin1,pin2,CMD,mode] [b,pin,CMD,edge,...]`

Options arguments:
  
    -A, --address=Server-Address   Set server address. Default: autodetect
    -f, --conf_file=</path/config-file>
                               Full path to command configuration file
    -M, --mac=MAC-Address      Set MAC address of player. Deafult: autodetect
    -p, --password=password    Set password for server. Default: none
    -P, --port=xxxx            Set server control port. Default: autodetect
    -u, --username=user name   Set user name for server. Default: none
    -d, --daemonize            Daemonize
    -s, --silent               Don't produce output
    -v, --verbose              Produce verbose output
    -z, --debug                Produce debug output
    -?, --help                 Give this help list
        --usage                Give a short usage message
    -V, --version              Print program version

Non-Option arguments.
At least one needs to be specified for the daemon to do anything useful
Arguments are a comma-separated list of configuration parameters:
  
    For rotary encoders (one, volume only):
        e,pin1,pin2,CMD[,edge]
            "e" for "Encoder"
            p1, p2: GPIO PIN numbers in BCM-notation
            CMD: Command. one of. \n\
                VOLU for Volume\n\
                TRAC for Prev/Next track\n\
                KEY:<Positive key_name>-<Negative key_name>
            mode: Optional. one of\n\
                1   - Step mode (default)\n\
                2-9 - Detent mode - Assumes 1 dial click is x steps.

    For buttons: 
        b,pin,CMD[,resist,pressed,CMD_LONG,long_time]
            "b" for "Button"
            pin: GPIO PIN numbers in BCM-notation
            CMD: Command. One of:
                   PLAY:   Play/pause
                   PREV:   Jump to previous track
                   NEXT:   Jump to next track
                   VOL+:   Increase volume
                   VOL-:   Decrease volume
                   POWR:   Toggle power state
                 Commands can be defined in config file
                     use -f option, ref:sbpd_commands.cfg 
                 Command type SCRIPT.
                   SCRIPT:/path/to/shell/script.sh
                 Command type KEY.
                      KEY:<linux key_name>.
            resist: Optional. one of
                0 - Internal resistor off
                1 - pull down         - input puts 3v on GPIO pin
                2 - pull up (default) - input pulls GPIO pin to ground
            pressed: Optional GPIO pinstate for button to read pressed
                0 - state is 0 (default)
                1 - state is 1
            CMD_LONG: Command to be used for a long button push, see above command list
            long_time: Number of milliseconds to define a long press

## Command configuration file

    #
    #  sbpd_commands.cfg  -  Custom commands definition
    #
    #   <CODE>=<JSON Formatted lms cli command>
    #
    #       CODE - MUST be a 4 character code, to be reference on command line when defining buttons
    #
    #       For commands reference the LMS cli documentation, commands are to be JSON formatted.
    #        
    # Default commands         
    PLAY=["pause"]
    VOL-=["button","voldown"]
    VOL+=["button","volup"]
    PREV=["button","rew"]
    NEXT=["button","fwd"]
    POWR=["button","power"]"
    MIX+=["mixer","volume","+5"]
    MIX-=["mixer","volume","-5"]

## Linux keycodes

    Uses the linux uinput kernel module.  Make sure to load it with sudo modprobe uinput.
    Keycode definitions can be found: https://github.com/raspberrypi/linux/blob/rpi-4.19.y/include/uapi/linux/input-event-codes.h

## Security

With appropriate `/dev/gpiochipN` permissions (and `/dev/uinput` permissions when
keyboard commands are used), sbpd does not need root permissions.

## Limitations

### IPv6
In the current state, this will probably not work in IPv6-only networks.

### Server Switching

The controller will follow the player if you switch the player to a new server.
This might not work with a remote server but should be reliable in an IPv4 network

### Encoder Speed

Server commands are fed to the server using a very simple scheduler on the main thread. Up to 10 commands per second can be sent but since all requests are being sent synchronously this depends on the reaction speed of the server.
The result of this is that very fast command sequences can result in jumping volume levels and delayed volume changes.

### Multiple Players

Probably not a limitation on a Pi. Only a single instance of SqueezeLite should be running if autodetection is being used since the code only looks for the first connection on port 3483.
With more than one player the server being found will be random. In such a setup, manual server configuration will be required.

### Multiple Network Interfaces

The MAC address detection is borrowed from SqueezeLite so when running automatically the MAC found should be the same used by SqueezeLite.
If that's not the case in setups with more than one MAC or SqueezeLite uses a manually configured MAC manual MAC configuration should be used.

### MySqueezebox.com

This tool will only work with Logitech Media Server, it doesn't authenticate against MySqueezebox.com. While authentication against MySqueezebox.com should generally be possible for a controller it should be unneeded in this case because DIY and 3rd party players are not supposed to directly connect to MySqueezebox.com anyway and the design of this tool is to control a player running on the same device as this controller.
