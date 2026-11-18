# ESP8266 Platform Guidelines

- Verify exact board variant, pin aliases, voltage, and peripheral assignments against the project's hardware source.
- ESP8266 GPIO0 and GPIO2 must be HIGH, and GPIO15 LOW, for normal flash boot; external circuitry can affect startup.
- GPIO16 has different interrupt/deep-sleep capabilities from most GPIOs; check intended use.
- Avoid assuming 5V tolerance on GPIOs.
- Respect watchdog and Wi-Fi scheduling needs; avoid long blocking loops.
- Treat limited heap, DRAM, stack, and flash placement as design constraints; measure resource changes.
- Use the actual core version's APIs for flash-resident strings and peripheral behavior.
- Check boot UART behavior and serial speed against the project; do not impose one baud rate globally.
