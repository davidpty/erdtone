# Rotary Dial Pulse-to-DTMF Converter

This build was designed for the Siemens W48 rotary phone with support for `Earth key` (German: Erdtaste). It turns pulse dialing into DTMF, stores the last number for redial, supports speed dial, `*` and `#`, and can auto-dial a hotline on power-up.

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

## Operation

### Normal dialing

Dial a number and release. The firmware sends the matching DTMF tone.

### Earth key

`Earth key` is a quick redial shortcut.

- Brief press and release: redial the last number
- If pressed during startup, hotline calling is canceled for that cycle.

### Rotary hold menu

Use the rotary dial for special functions:

- Dial a digit and hold until the first beep, then release:
  - `1` = send `*`
  - `2` = send `#`
  - `3` = redial last number
  - `0`, `4` to `9` = dial the stored speed-dial number
- Keep holding until the second beep to enter programming:
  - `0`, `4` to `9` = select the speed-dial slot to modify
  - `3` = enters hotline setup
        
### Speed dial programming

1. Dial the slot digit.
2. Keep holding until the second beep, then release.
3. Enter the number one digit at a time.
4. Release after each digit.
5. Hang up when done.

Notes:

- Each digit is written to EEPROM immediately.
- `*` and `#` can be stored by dialing `1` or `2`, holding until the first beep, then releasing.
- Hold `0` until the first beep to clear the current slot. A normal `0` release still stores `0`.
- First beep = play mode, second beep = program mode.

### Hotline setup

Hotline is stored in one of the speed-dial slots and auto-dials after power-up if enabled.

Set it:

1. Dial `3`.
2. Hold until the second beep, then release.
3. Dial the speed-dial slot.
4. The hotline is now saved with the default `5` second delay.
5. Optional: dial a delay digit from `0` to `9` to override it.

Clear it:

1. Dial `3`.
2. Keep holding until the second beep, then release.
3. Hold `0` until the first beep, then release to clear it.

Default hotline delay: `5` seconds

Pressing `Earth key` or starting to dial during startup cancels the hotline call for that cycle.
