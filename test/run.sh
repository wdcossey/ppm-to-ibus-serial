#!/bin/sh
# Builds every sketch, runs them in the simavr simulator (Uno: ATmega328P,
# Leonardo: ATmega32U4), feeds them synthetic PPM / iBus signals and checks
# what they send, plus the boot check, the status LED and the debug output.
# Needs (Debian/Ubuntu): gcc-avr avr-libc arduino-core-avr arduino-mk simavr libsimavr-dev python3 git
set -e
cd "$(dirname "$0")"
mkdir -p build/libs
gcc -O2 -o build/harness harness.c -lsimavr
[ -d build/libs/ArduinoJoystickLibrary ] || git clone -q --depth 1 https://github.com/MHeironimus/ArduinoJoystickLibrary build/libs/ArduinoJoystickLibrary

# build <name> <sketch folder> <board[:cpu]> <extra CPPFLAGS> [libraries]
build() {
	mkdir -p build/$1
	rm -f build/$1/*.ino
	cp ../$2/$2.ino build/$1/$1.ino
	cat > build/$1/Makefile <<MK
BOARD_TAG = ${3%%:*}
BOARD_SUB = $(echo $3 | sed -n 's/.*://p')
ARDUINO_DIR = /usr/share/arduino
ARDUINO_VERSION = 10819
USER_LIB_PATH = $(pwd)/build/libs
ARDUINO_LIBS = $5
CXXFLAGS += -Wextra
CPPFLAGS += $4
include /usr/share/arduino/Arduino.mk
MK
	make -s -C build/$1 >build/$1.log 2>&1 || { cat build/$1.log; exit 1; }
	if grep -q "$1.ino.*warning" build/$1.log; then grep "$1.ino" build/$1.log; exit 1; fi
}

JOYSTICK_TEST_FLAGS="-DDEBUG_OUTPUT=1 -DDEBUG_PORT=Serial1 -DDEBUG_INTERVAL_MS=0"
build serial ppm-to-ibus-serial uno ""
build serialraw ppm-to-ibus-serial uno "-DDEBUG_OUTPUT=2"
build serialpromini8 ppm-to-ibus-serial pro:8MHzatmega328 ""     # compile only
build serialleonardo ppm-to-ibus-serial leonardo ""            # compile only
build serialmega ppm-to-ibus-serial mega:atmega2560 ""          # compile only
build ppmjoy ppm-to-usb-joystick leonardo "" ArduinoJoystickLibrary
build ppmjoytest ppm-to-usb-joystick leonardo "$JOYSTICK_TEST_FLAGS" ArduinoJoystickLibrary
build ibusjoy ibus-to-usb-joystick leonardo "" ArduinoJoystickLibrary
build ibusjoytest ibus-to-usb-joystick leonardo "$JOYSTICK_TEST_FLAGS" ArduinoJoystickLibrary

python3 check_shared.py
python3 ppm_to_ibus_serial.py build/serial/build-uno/serial_.elf
python3 ppm_to_ibus_serial_boot.py build/serial/build-uno/serial_.elf build/serialraw/build-uno/serialraw_.elf
python3 ppm_to_ibus_serial_raw.py build/serialraw/build-uno/serialraw_.elf
python3 ppm_to_usb_joystick.py build/ppmjoytest/build-leonardo/ppmjoytest_.elf
python3 ibus_to_usb_joystick.py build/ibusjoytest/build-leonardo/ibusjoytest_.elf
