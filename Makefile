MAJOR_VERSION := 0
MINOR_VERSION := 0
PATCH_VERSION := 1

PACKAGE_VERSION := $(MAJOR_VERSION).$(MINOR_VERSION).$(PATCH_VERSION)

CC=gcc
CFLAGS=-Wall -pedantic -march=native -O3 -D__PACKAGE_VERSION__=\"$(PACKAGE_VERSION)\"
LDFLAGS=-s

FONTS_SRC := spleen_font/spleen-5x8.bdf
FONTS_SRC += spleen_font/spleen-8x16.bdf
FONTS_SRC += spleen_font/spleen-12x24.bdf
FONTS_SRC += spleen_font/spleen-16x32.bdf

.PHONY: all

all: leds monitor

leds: leds.c

monitor: monitor.c

monitor.c: ssd1306.h font.h masks.h

fonts.h: font_regenerate.py $(FONTS_SRC)
	./font_regenerate.py

clean:
	$(RM) leds monitor fonts.h
