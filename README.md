This is a set of 'lite' utilities for managing the components included in the
[Pironman 5 cases by SunFounder](https://docs.sunfounder.com/projects/pironman5/)
which include a GPIO extended with WS2812 LEDs, a 128x64 OLED micro-display, and
additional fans behind the included 'tower' style cooler.

# leds

This utility manages a WS2812 LED string connected to any SPI-capable pin on the
RPi5, allowing either a global color to be set to all LEDs, or specifying individual
colors to be assigned to each LED.

It handles extended strings if more LEDs are attached to the header on the case
and the total count of LEDs is specified, and accepts short (0x000) or long (0x000000)
color specifications, as well as 'raw' (000), 'hash' (#000), or 'C constant' (0x000000)
formats.

It will repeat the specified colors to 'fill' the specified string length as needed.
