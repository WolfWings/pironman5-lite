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

# monitor

Currently a proof-of-concept that displays a moving bar-graph of CPU usage (based on
'negative idle' time) and the current CPU temperature in C and F every second.

Future plans for this utility are:

* Full scripting based on a language TBD (either lua or PicoC) exposing any 'sensor'
  values to be processed once a second, with various graphical widgets plus multiple
  font sizes (based on Spleen) to render the screen buffer with.

* A full SystemD unit file for easy integration.

* Direct pin control based on the scripting, which will be able to be used to manage
  GPIO-based fans.

* Direct WS2812 support using any available SPI pins to support addressible RGB fans
  or any other accessories, such as to indicate temperature or network traffic based
  on LED colors or similar.
