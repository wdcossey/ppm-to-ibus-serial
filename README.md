# ppm-to-ibus-serial
This sketch enables you to use your PPM receiver with [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder).

Essentially it'll decode a PPM signal and re-encode it to IBus and transmit it over the Arduino Serial.

This sketch will:

1. Automatically calculate the number of PPM channels (no fixed numbers).
2. Pad unused IBus channels (with a default value of 1500).
3. Generate valid IBus checksums.

Hardware requirements:
1. Arduino.
2. PPM receiver.

**Don't** use this sketch if your receiver already supports IBus (_pretty pointless_).
