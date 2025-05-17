MAJOR_VERSION := 0
MINOR_VERSION := 0
PATCH_VERSION := 1

PACKAGE_VERSION := $(MAJOR_VERSION).$(MINOR_VERSION).$(PATCH_VERSION)

CC=gcc
CFLAGS=-Wall -pedantic -march=native -O3 -D__PACKAGE_VERSION__=\"$(PACKAGE_VERSION)\"
LDFLAGS=-s

.PHONY: all

all: leds

leds: leds.c

clean:
	$(RM) leds
