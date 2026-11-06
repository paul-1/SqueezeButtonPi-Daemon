# GPIO design notes

Developer notes for sbpd's event-driven GPIO implementation (`GPIO.c`).

## Background

sbpd previously used WiringPi ISR threads. In WiringPi 3.20 each ISR thread
runs at real-time priority (`SCHED_RR` 55) and waits in `ppoll()` with a 0.5 ms
timeout, causing about 8,000 wakeups per second for four lines. On single-core
boards (Pi Zero W) with the `ondemand` governor this correlated with system
stalls. Replacing it is an application-side mitigation, not a kernel fix.

## Chip discovery

sbpd scans `/dev/gpiochip*` and selects a controller by label, not by chip
number or line names (which device-tree overlays may change):

| Label             | Boards                                       |
|-------------------|----------------------------------------------|
| `pinctrl-bcm2835` | Pi 1/2/3, Zero/Zero W/Zero 2 W, CM1/CM3       |
| `pinctrl-bcm2711` | Pi 4/400, CM4                                 |
| `pinctrl-rp1`     | Pi 5/500, CM5                                 |

For these controllers the line offset equals the BCM GPIO number. RP1 is
preferred when present; BCM2712 internal/AON controllers are never selected
for header GPIOs. This mirrors the controller distinction in
[Raspberry Pi pinctrl](https://github.com/raspberrypi/utils/tree/master/pinctrl)
without using its MMIO access.

All configured lines must fit the selected controller. Unsupported or
ambiguous chips are rejected; `SBPD_GPIO_CHIP` resolves ambiguity. Invalid or
duplicate pins, unsupported bias, busy lines, unavailable chips and worker
startup failures are fatal; there is no polling fallback.

## Event handling

- All lines are requested together: input, both edges, configured bias,
  monotonic event clock, kernel event buffer of 1,024 events.
- Initial levels are read after the request and form the baseline; events
  timestamped before that snapshot are ignored.
- One `SCHED_OTHER` worker thread blocks indefinitely in `poll()` on the line
  request fd and a shutdown pipe. There are no periodic timeouts.
- Events are read in batches of up to 64 and dispatched only to the affected
  button or encoder, using event edge type and timestamp.
- Buttons keep the 50 ms chatter filter and short/long press classification.
  Encoders keep quadrature decoding and four-step detents.
- Button/encoder state is protected by `state_lock`; no lock is held across
  main-thread command I/O. The main loop keeps its 100 ms command aggregation.
- Shutdown wakes and joins the worker before curl/uinput cleanup.

## Sequence numbers and dropped events

The request-wide (global) seqno is incremented in the hard IRQ handler, so
coalesced interrupts produce gaps without any lost events; it must not be used
to detect overflow. sbpd tracks per-line seqnos instead. A per-line gap means
the kernel FIFO dropped events: sbpd logs a warning, re-reads current levels,
and ignores older queued events. Read/poll failures stop the daemon.

## Testing status

Hardware-validated: buttons on BCM2835 (Pi Zero W). Still needing hardware
validation: encoder after the seqno fix, BCM2711 and RP1 discovery, bias
support and rapid encoder bursts.
