# Arduino Framework

- Keep `setup()` responsible for initialization and `loop()` responsive.
- Prefer elapsed-time scheduling (`millis()` or a project-provided clock) over blocking `delay()` in normal runtime paths.
- Handle `millis()` wraparound using unsigned elapsed-time arithmetic.
- Use bounded timeouts and retries for hardware/network operations; keep watchdog requirements in mind.
- Avoid slow I/O, heap allocation, and logging inside ISRs; communicate via minimal synchronized state.
- Check actual target APIs and framework versions rather than assuming ESP8266/ESP32 parity.
- Treat pins, voltage levels, bootstrapping, interrupts, and power as hardware-specific facts.
- Never flash firmware or upload filesystems without explicit authorization.
