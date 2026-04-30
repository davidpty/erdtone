# ErdTone - Rotary Dial Pulse-to-DTMF Converter

Bring a vintage rotary phone back to life on a modern telephone exchange. ErdTone converts pulse dialing to DTMF tones in real time, running on a single ATtiny85 hidden inside the phone with no visible modifications.

Built around the Siemens W48 with full support for the `Earth key` (German: Erdtaste), it goes beyond basic pulse conversion: last-number redial, speed dial with pause support, `*` and `#` via hold gestures, auto-dial hotline on pickup, and adjustable DTMF timing for compatibility with older exchanges. Everything is configured directly from the rotary dial - no computer needed after flashing.

This project is a fork of the rotarydial firmware, extended with hotline auto-dial, Earth key redial, Erdtaste speed dial, and configurable DTMF and menu timing.

## Hardware

Needed build parts:

- 1x ATTINY85V
- 1x 78L05 5V regulator
- 1x 4 MHz crystal
- 1x 680 ohm resistor
- 1x 27 V 1 W zener diode
- 2x 22 pF ceramic capacitors
- 4x 0.1 uF (100 nF) ceramic capacitors
- 1x 4 position terminal
- 1x 1N4007 diode

## Circuit

A Fritzing layout is included with a compact footprint designed to fit inside the W48 housing without modifications. See `ErdTone.fzz`.

## Wiring

### Rotary dial

- Disconnect the rotary dial from the phone circuitry (no parallel connection).
- Connect it to the converter:
  - Pulse contact (normally closed, opens for each pulse) → `PULSE`
  - Dial contact (closes while dial is moving) → `DIAL`

### Earth key

- Disconnect `Earth key` from the phone circuitry.
- Connect it to the same `DIAL` line used by the rotary dial:
  - One side → `DIAL`
  - Other side → GND  
- Pressing `Earth key` must pull `DIAL` to GND.

### Power

- Connect power **after the hookswitch**:
  - Line `+` → `VCC`
  - Line `-` → `GND`
- Device must be off when on-hook.
- Optional: add a diode (e.g. 1N4007) between line `+` (anode) and `VCC` (cathode) for protection.

## Build

Install tools:
```bash
sudo apt update && sudo apt install gcc-avr avr-libc binutils-avr avrdude make
```

Build, flash and set fuses:
```bash
make install              # default 4 MHz (external crystal)
make CLOCK_MODE=8 install # 8 MHz (internal oscillator)
```

Erase EEPROM (clears all stored numbers and settings):
```bash
make erase
```

## Operation

### Normal dialing

Dial a number and release. The firmware sends the matching DTMF tone. If you pause for more than ~3 seconds between digits, a pause is automatically inserted  into the redial memory at that point. If you wait more than ~6 seconds, two pauses are inserted. This allows redial to correctly replay numbers that require waiting for an automated phone menu or office switchboard to respond.

### Earth key

`Earth key` is a quick redial shortcut.

- Brief press and release: redial the last number
- Hold until first beep: dial the stored Earth key speed dial number
- Hold until second beep: program the Earth key speed dial number
- If pressed during startup, hotline calling is canceled for that cycle.

### Rotary hold menu

Use the rotary dial for special functions:

- Dial a digit and hold until the first beep, then release:
  - `1` = send `*`
  - `2` = send `#`
  - `3` = redial last number
  - `4` to `9`, `0` = dial the stored speed-dial number
- Keep holding until the second beep:
  - `4` to `9`, `0` = enter programming mode for that slot
  - `1` = cycle DTMF tone duration (80ms / 200ms).
  - `2` = cycle menu hold time (1s / 2s).
  - `3` = enter hotline setup
        
### Speed dial programming

1. Dial the slot digit.
2. Keep holding until the second beep, then release.
3. Enter the number one digit at a time.
4. Release after each digit.
5. Hang up when done.

Notes:

- Each digit is written to EEPROM immediately.
- `*` and `#` or `pause` can be stored by dialing `1`, `2` or `3`, holding until the first beep, then releasing.
- The Earth key has its own dedicated speed dial slot programmed via Earth key hold to second beep.
- During programming, you can copy into the current slot: press `Earth key` to load the last dialed number, or dial a source speed-dial slot (`4` to `9`, `0`) and hold until the first beep.
- Hold `0` until the second beep to clear the current slot (works for Earth key slot too).

### Hotline setup

Hotline is stored in one of the speed-dial slots and auto-dials after power-up if enabled.

Set it:

1. Dial `3`.
2. Hold until the second beep, then release.
3. Dial the speed-dial slot, or press `Earth key` to use the Earth key speed dial number as the hotline.
4. The hotline is now saved with the default `5` second delay.
5. Optional: dial a delay digit from `0` to `9` to override it.

Clear it:

1. Dial `3`.
2. Keep holding until the second beep, then release.
3. Hold `0` until the second beep, then release to clear it.

Pressing `Earth key` or starting to dial during startup cancels the hotline call for that cycle.

## Service Codes

Service codes are entered during a normal call using the rotary hold menu for `*` and `#`.

### Lock / Unlock

Dial lock codes with a 3-digit PIN `PPP` using digits only.

- If no PIN is set, `**PPP` sets the PIN and locks programming.
- If no PIN is set, `*#PPP` sets the PIN and locks programming plus dialing.
- If any lock is active, `**PPP` or `*#PPP` with the correct PIN permanently unlocks and clears the PIN.
- If any lock is active, `##PPP` with the correct PIN temporarily unlocks the phone for the current call only.

When programming-locked:
- Normal dialing still works
- Programming, settings changes and factory reset are blocked

When fully locked:
- Normal dialing is blocked
- Speed dial, redial and hotline still work
- Programming, settings changes and factory reset are blocked

### Factory reset

Dial `*#0#*` to erase all stored numbers, speed dial slots, hotline config, and reset DTMF duration and menu hold time to defaults. Blocked when locked.

## Credits

ErdTone builds on the work of [Boris Cherkasskiy](http://boris0.blogspot.ca/2013/09/rotary-dial-for-digital-age.html) who created the original firmware in 2011, [Arnie Weber](https://bitbucket.org/310weber/rotary_dial/) who reworked the hardware in 2015, and [Matthew Millman](http://tech.mattmillman.com/) who cleaned up the implementation in 2018.
