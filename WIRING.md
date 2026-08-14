# Buzzer Wiring (D8 / GPIO15)

The clock's sound output is a KY-006 style active buzzer module on **D8**. The
module is active LOW and D8 is a boot-strap pin, so the module cannot connect to
D8 directly. D8 drives an NPN transistor, and that transistor pulls the module's
S pin low.

Every other usable GPIO on this board is already spoken for by the three TM1637
panels, the DS3231, and the button - see the pin map in
[src/hardware.h](src/hardware.h). D8 is the only pin left, which is why the
buffer is necessary rather than optional.

## Two transistors, do not confuse them

- The **PNP** is already inside the buzzer module. You do not wire it or buy it.
  It is the reason the module is active LOW.
- The **NPN** is the part you add, between D8 and the module's S pin. It inverts
  D8 so that a HIGH on D8 pulls the module's S pin low.

## Parts

- 1k base resistor
- 10k base pulldown resistor
- 2N2222, PN2222, BC547, or S8050 NPN transistor
- optional 1k - 10k linear potentiometer for hardware volume (see below)

A 2N7000 MOSFET works too: gate to D8, drain to module S, source to GND, 10k
from gate to GND, and no 1k needed.

## Exact wiring

```text
Wemos D1 mini / ESP8266

Module VCC ------------------- 3.3V
Module GND ------------------- GND
Module S   ------------------- collector of NPN

D8 (GPIO15) ---- 1k resistor ---- base of NPN transistor
                               |
                            10k resistor
                               |
                              GND

emitter of NPN --------------- GND
ESP8266 GND ------------------ same GND
```

## ASCII schematic

```text
                     3.3V ---- Module VCC
                      GND ---- Module GND
                                Module S
                                    |
                                    |
D8(GPIO15) --[1k]--+--------- B     C   NPN transistor
                   |               ...
                 [10k]              E
                   |                |
                  GND              GND
```

D8 HIGH -> NPN on -> module S pulled LOW -> buzzer sounds.
D8 LOW -> NPN off -> module idle and silent.

## Why the 10k is mandatory

- D8 is GPIO15, a boot-strap pin. It must be LOW at reset or the chip boots to
  the wrong mode and never runs. This is the same constraint that kept D8
  unconnected before the buzzer existed.
- Connecting the module's S pin to D8 directly holds GPIO15 at 2.7V through the
  forward-biased base-emitter junction inside the module, so the board does not
  boot.
- A pulldown straight on D8 does not fix that. Beating the module's ~1k source
  impedance needs a resistor around 330 ohms, which lands near the 0.8V logic
  threshold, wastes 8mA continuously, and fights the pin whenever it drives
  high.
- With the NPN buffer, D8 connects only to a 1k base resistor and a 10k
  pulldown. There is no path from D8 to VCC, so the strap requirement is
  satisfied. At reset D8 is low, the NPN is off, and the module's S pin floats
  back to 2.7V on its own; a steady DC level makes no sound.

## Inverted logic is fine

D8 HIGH turns the NPN on, pulls S low, and sounds the buzzer. The firmware
drives a square wave, and inverting a 50% duty square wave gives the same
waveform at the same frequency. It sounds identical.

Below 50% duty - which is how [src/sound_player.cpp](src/sound_player.cpp)
implements volume - inversion swaps the mark and space ratio. A 20% duty wave
reaches the buzzer as 80%. Loudness is symmetric about 50%, so the effect on
volume is the same in either direction; only the timbre differs slightly.

## Current

- base drive: (3.3V - 0.7V) / 1k = about 2.6mA
- pulling S low: about 2.4mA through the module's own base resistor

Both are small enough that the buzzer does not contribute to the USB supply
droop described in CLAUDE.md's WiFi performance note.

## Volume: potentiometer on VCC (hardware volume control)

The buzzer's loudness follows the module's supply voltage, so a potentiometer
wired as a divider into the module's VCC makes it adjustable in hardware. This
replaces the `Module VCC -> 3.3V` connection above.

```text
        3.3V
         |
        .-.
        | |
        | |  potentiometer (1k - 10k, linear)
        |W|>------- Module VCC      (W = wiper, the middle pin)
        | |
        | |
        '-'
         |
        GND
```

- Outer pins go to 3.3V and GND; the middle pin (wiper) goes to Module VCC.
- Wiper toward 3.3V = louder, toward GND = quieter.
- Keep the top of the pot on **3.3V, not 5V** - do not overdrive the module.
- The D8 NPN buffer is unchanged. The pot only touches the module's VCC rail.

If you use this, set `USE_SOFTWARE_VOLUME` to `0` in
[src/sound_player.cpp](src/sound_player.cpp) so the firmware plays at full duty.
Otherwise the software attenuation and the pot stack on top of each other, and
the volume slider on `/format` stops meaning anything.

## Verifying before you trust it

1. Wire it with the ESP powered off, then power on. **If the board still boots
   and flashes normally, the strap is satisfied.** A board that no longer boots
   means the 10k is missing, open, or on the wrong side of the 1k.
2. Flash the firmware and open `/format`. The preview button next to any sound
   dropdown plays that sound immediately.
3. If the buzzer sounds continuously at boot before the firmware runs, the NPN's
   collector and emitter are swapped.
