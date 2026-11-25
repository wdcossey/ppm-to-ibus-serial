# ppm-to-ibus-serial
This sketch enables you to use your PPM receiver with [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder).

Essentially it'll decode a PPM signal and re-encode it to IBus and transmit it over the Arduino Serial.

The Arduino sketch can be found from [here](/ppm-to-ibus-serial.ino), build and upload to your Arduino of choice.

**Don't** use this sketch if your receiver already supports IBus (_pretty pointless_).

This sketch will:
* Automatically detect the number of PPM channels (4 to 14, no fixed numbers).
* Measure the PPM pulses with the timer's hardware input capture (0.5us resolution, unaffected by other interrupts), so there's very little jitter to begin with.
* Filter out the remaining jitter (configurable, see below).
* Re-synchronise on every PPM frame and drop damaged frames, so glitches don't scramble the channels.
* Pad any unused IBus channels (with a default value of 1500).
* Generate a valid IBus checksum.
* Send one IBus frame for every PPM frame, and stop sending if the PPM signal is lost.

Both normal and inverted PPM signals work.

## Hardware requirements
* Arduino, with the PPM signal connected to the timer input capture pin:

  | Board | PPM pin |
  |---|---|
  | Uno, Nano, Pro Mini (ATmega328P/168) | **D8** |
  | Leonardo, Micro, Pro Micro (ATmega32U4) | **D4** |
  | Mega 1280/2560 | **D49** |

* PPM receiver (or any other PPM source, e.g. a transmitter's trainer port).
* Connect the receiver's **GND to the Arduino's GND**. The PPM signal must reach about 3V for a 5V Arduino to read it reliably.

## Software requirements
* [VJoySerialFeeder](https://github.com/Cleric-K/vJoySerialFeeder) (see the page for additional requirements).
* In VJoySerialFeeder select the **IBUS** protocol and a baud rate of **115200** (must match `SERIAL_BAUD_RATE`).

## Configuration
The settings are at the top of the sketch:

| Setting | Default | Description |
|---|---|---|
| `SERIAL_BAUD_RATE` | 115200 | Serial baud rate. |
| `PPM_JITTER_THRESHOLD` | 4 | Jitter filter in microseconds. A channel only moves once its value changes by more than this. Raise it (e.g. 8) for a noisy receiver, `0` disables it. |
| `PPM_MIN_CHANNELS` / `PPM_MAX_CHANNELS` | 4 / 14 | Frames with fewer/more channels are ignored. |
| `PPM_CHANNEL_MIN_US` / `PPM_CHANNEL_MAX_US` | 700 / 2400 | Valid channel width range. Anything outside is treated as a glitch. |
| `PPM_SYNC_MIN_US` | 2500 | A gap at least this long marks the end of a PPM frame. |
| `PPM_SIGNAL_TIMEOUT_MS` | 100 | Stop sending IBus frames after this long without a valid PPM frame. |
| `DEBUG_OUTPUT` | 0 | Set to `1` to print readable channel values instead of IBus data. |

## Troubleshooting
If VJoySerialFeeder doesn't show any data:
1. Check the baud rate (115200) and that the IBUS protocol is selected.
2. Set `DEBUG_OUTPUT` to `1`, upload, and open the Arduino Serial Monitor at 115200 baud.
   * `No PPM signal`: the Arduino isn't seeing a valid PPM signal. Check the pin, the ground connection, that the receiver is bound and actually in PPM mode, and the signal voltage.
   * `8 ch: 1500 1500 ...`: decoding works, set `DEBUG_OUTPUT` back to `0`.

## Testing
`test/run.sh` runs the sketch in the [simavr](https://github.com/buserror/simavr) ATmega328P simulator, feeds it synthetic PPM signals (jitter, glitches, signal loss, channel count changes, inverted PPM, ...) and checks the IBus output. See the script for the packages it needs.
