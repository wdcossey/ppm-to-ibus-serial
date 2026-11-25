#!/bin/sh
# Simulates the sketch on an ATmega328P (simavr), feeds it synthetic PPM
# signals and checks the IBus output.
# Needs (Debian/Ubuntu): gcc-avr avr-libc arduino-core-avr arduino-mk simavr libsimavr-dev python3
set -e
cd "$(dirname "$0")"
mkdir -p build/sketch
cp ../ppm-to-ibus-serial.ino build/sketch/
printf 'BOARD_TAG = uno\nARDUINO_DIR = /usr/share/arduino\ninclude /usr/share/arduino/Arduino.mk\n' > build/sketch/Makefile
make -s -C build/sketch >build/make.log 2>&1 || { cat build/make.log; exit 1; }
gcc -O2 -o build/harness harness.c -lsimavr
python3 tests.py build/sketch/build-uno/sketch_.elf
