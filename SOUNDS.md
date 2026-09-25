# Generated beeps

All audio is generated mathematically. No catalog, note sequence, or audio file
is loaded from LittleFS. The Format page contains display settings; the Sound
page owns automatic enable, volume, event beeps, and approach patterns.

## Approach patterns

| Pattern | Boundaries | Default tone | Duration | Initial rate |
| --- | --- | --- | --- | --- |
| Boundary 1 | Friday sunset / first Trading open | 880 Hz | 40 s | 2 beeps/s |
| Boundary 2 | Saturday sunset / last Trading close | 1320 Hz | 40 s | 2 beeps/s |

Each has four equal phases at rates R, 2R, 4R, and 8R. Each beep occupies
the end of its slot, lasting the smaller of 100 ms and half the slot.
The final beep ends exactly at the scheduled boundary. The pure envelope
lives in [beep_pattern.cpp](src/beep_pattern.cpp) and has host tests.

Tone accepts 100-5000 Hz, duration 4-1200 whole seconds, and starting rate
1-10 beeps/s. Settings retain their existing `sound.boundaryAlert` JSON shape.
The overall sound switch and approach-alert switch must both be enabled.

The controller supplies the current target and RTC second phase every accepted
SQW tick. [BeepPlayer](src/beep_player.h) calculates the current pulse on each
loop; late loops skip missed pulses. Arrival inside an alert window resumes at
the correct position. There are no blocking waits or per-note deadlines.

## Event beeps and previews

Optional 150 ms event beeps default off. Startup, countdown completion, Friday
sunset, and Trading open use 880 Hz; Trading close uses 1320 Hz. Startup sounds
after initialization. Boundary/completion beeps require a live crossing; boot,
config apply, and time correction never announce elapsed boundaries.

Preview has priority over an event beep, which has priority over an approach
pattern. Schedule updates do not cut previews off. An automatic pattern resumes
at its current position when an override ends. Stop silences all playback and
suppresses the current scheduled occurrence until its target changes.

Preview uses the saved volume and bypasses automatic-enable switches. The page
previews unsaved pattern edits, using `POST /api/sound/test` with `boundaryAlert`
fields `frequencyHz`, `totalDurationSeconds`, and `startingBeatsHz`. The response
includes `durationMs` for the button timer. `{ "stop": true }` stops playback.
Volume maps 0-100% to PWM duty 0-50%; GPIO15 remains LOW when silent.

## Existing installations

Existing enable, volume, and approach-pattern settings remain readable. Old
song-name fields are ignored and disappear on the next config save; event beeps
use new boolean fields ending in `Beep`. No catalog API or build step remains.
An old `/songs.bin` on an already-flashed device is unused and may be deleted
through the file manager. A filesystem upload is not needed for generated audio.
