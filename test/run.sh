#!/bin/sh
# Simulates the sketch on an ATmega328P (simavr), feeds it synthetic PPM
# signals and checks the IBus output, the boot check and the status LED.
# Needs (Debian/Ubuntu): gcc-avr avr-libc arduino-core-avr arduino-mk simavr libsimavr-dev python3
set -e
cd "$(dirname "$0")"
mkdir -p build
gcc -O2 -o build/harness harness.c -lsimavr

# build <name> <extra CPPFLAGS>: builds the sketch for the Uno into build/<name>
build() {
	mkdir -p build/$1
	rm -f build/$1/*.ino
	cp ../ppm-to-ibus-serial.ino build/$1/$1.ino
	printf 'BOARD_TAG = uno\nARDUINO_DIR = /usr/share/arduino\nCPPFLAGS += %s\ninclude /usr/share/arduino/Arduino.mk\n' "$2" > build/$1/Makefile
	make -s -C build/$1 >build/$1.log 2>&1 || { cat build/$1.log; exit 1; }
}

build sketch ""
build raw "-DDEBUG_OUTPUT=2"
python3 tests.py build/sketch/build-uno/sketch_.elf
python3 tests_boot.py build/sketch/build-uno/sketch_.elf build/raw/build-uno/raw_.elf
python3 tests_raw.py build/raw/build-uno/raw_.elf
