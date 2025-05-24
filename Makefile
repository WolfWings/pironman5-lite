MAJOR_VERSION := 0
MINOR_VERSION := 1
PATCH_VERSION := 0

PACKAGE_VERSION := $(MAJOR_VERSION).$(MINOR_VERSION).$(PATCH_VERSION)

CC=gcc
CFLAGS=-Wall -pedantic -march=native -O3 -D__PACKAGE_VERSION__=\"$(PACKAGE_VERSION)\"
LDFLAGS=-s
LDLIBS=-lluajit-5.1

FONTS_SRC := spleen_font/spleen-5x8.bdf
FONTS_SRC += spleen_font/spleen-8x16.bdf
FONTS_SRC += spleen_font/spleen-12x24.bdf
FONTS_SRC += spleen_font/spleen-16x32.bdf

.PHONY: all

all: leds monitor

leds: leds.o

monitor: monitor.o

monitor.o: monitor.c $(wildcard monitor_*.i) fonts.h masks.h default_script.h

fonts.h: generate_fonts.py $(FONTS_SRC)
	./generate_fonts.py

masks.h: generate_masks.py default_mask.txt
	./generate_masks.py default_mask.txt

default_script.h: default_script.lua generate_default_script.awk
	awk -f generate_default_script.awk default_script.lua > default_script.h

clean:
	$(RM) leds monitor fonts.h masks.h default_script.h *.o
