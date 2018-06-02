# ppm-to-ibus-serial
This sketch enables you to use your PPM receiver with [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder).

Essentially it'll decode a PPM signal and re-encode it to IBus and transmit it over the Arduino Serial.

The Arduino sketch can be found from [here](/ppm-to-ibus-serial.ino), build and upload to your Arduino of choice.

**Don't** use this sketch if your receiver already supports IBus (_pretty pointless_).

This sketch will:
* Automatically detect the number of PPM channels (no fixed numbers).
* Pads any unused IBus channels (with a default value of 1500).
* Generate a valid IBus checksum.

Hardware requirements:
* Arduino (Interrupt **PCINT0**, **D8** on the Uno).
* PPM receiver.

Software requirements:
* [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder) (see the page for additional requirements).

