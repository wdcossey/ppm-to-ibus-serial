# ppm-to-ibus-serial
This sketch enables you to use your PPM receiver with [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder).

Essentially it'll decode a PPM signal and re-encode it to IBus and transmit it over the Arduino Serial.

**Don't** use this sketch if your receiver already supports IBus (_pretty pointless_).

This sketch will:
* Automatically calculate the number of PPM channels (no fixed numbers).
* Pad unused IBus channels (with a default value of 1500).
* Generate a valid IBus checksum.

Hardware requirements:
* Arduino (Interrupt **PCINT0**, **D8** on the Uno).
* PPM receiver.

Software requirements:
* [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder) (see the page for additional requirements).


This project is built using Visual Studio 2017 Community and Visual Micro (Arduino extension).

If you only require the arduino sketch, download it from [here](/ppm-to-ibus-serial/ppm-to-ibus-serial.ino).
