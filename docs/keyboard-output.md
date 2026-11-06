# Keyboard output notes

Developer notes for sbpd's uinput keyboard output (`uinput.c`, `control.c`).

- The virtual keyboard (`jivelite-uinput`, `BUS_VIRTUAL`) enables key codes
  0-254. Key names are generated at build time from the system
  `linux/input-event-codes.h` by `gen_key_event_codes.sh`; unknown names fall
  back to `KEY_RESERVED`.
- Writes to `/dev/uinput` are nonblocking and retried on `EINTR`. Each key event
  is followed by an `EV_SYN`/`SYN_REPORT` event.
- Failed or incomplete writes are logged and stop the current repeat sequence.
  The output mutex is always released; a previous version leaked it on write
  failure, which blocked all later keyboard commands.
- A release is attempted even when the press fails, since a failed sync write
  can follow a delivered press. A release failure is reported; delivery cannot
  be guaranteed when the uinput device is unavailable.
