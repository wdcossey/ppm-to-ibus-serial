/*
 Name:		ibus_to_usb_joystick.ino
 Created:	05/31/2018
 Author:	wdcossey

 Decodes FlySky iBus data from a receiver and presents it as a USB game
 controller. No PC software (vJoy etc.) is needed.

 Needs an ATmega32U4 board: Arduino Leonardo, Micro or Pro Micro.
 Connect the receiver's iBus servo output to the RX pin (D0).

 The LED shows what it is doing:
	fast flash : waiting for iBus data
	on         : receiving iBus
	slow flash : iBus signal lost (all axes centered, buttons released)

 Requires the Joystick library by Matthew Heironimus:
 https://github.com/MHeironimus/ArduinoJoystickLibrary
*/

#include <Joystick.h>

#if !defined(__AVR_ATmega32U4__)
#error "This sketch needs an ATmega32U4 board (Leonardo, Micro, Pro Micro) for USB game controller support"
#endif

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

// Which receiver channel (1-14) drives each axis, 0 = axis not used.
#define AXIS_X_CHANNEL 1
#define AXIS_Y_CHANNEL 2
#define AXIS_RX_CHANNEL 3
#define AXIS_RY_CHANNEL 4
#define AXIS_Z_CHANNEL 5
#define AXIS_RZ_CHANNEL 6

// Channels BUTTON_FIRST_CHANNEL onwards are reported as buttons, typically
// the switches on the transmitter. A button is pressed while its channel is
// above BUTTON_THRESHOLD. Set BUTTON_COUNT to 0 for no buttons.
#define BUTTON_FIRST_CHANNEL 7
#define BUTTON_COUNT 4
#define BUTTON_THRESHOLD 1750

#define CHANNEL_MIN 1000                   // Channel value mapped to the minimum axis position
#define CHANNEL_MAX 2000                   // Channel value mapped to the maximum axis position
#define CHANNEL_CENTER 1500                // Reported for all axes when there's no signal

#define IBUS_SIGNAL_TIMEOUT_MS 100         // Center the axes if no valid iBus frame arrives for this long

// Set to 1 to also print the values sent to the PC on DEBUG_PORT
// (open the Arduino Serial Monitor). Useful to check your wiring/receiver.
#ifndef DEBUG_OUTPUT
#define DEBUG_OUTPUT 0
#endif
#ifndef DEBUG_PORT
#define DEBUG_PORT Serial                  // Serial is the USB serial port on the 32U4
#endif
#ifndef DEBUG_INTERVAL_MS
#define DEBUG_INTERVAL_MS 100
#endif

// Status LED (see above). Comment out STATUS_LED_PIN to leave the LED alone.
// Pro Micro: it has no LED on pin 13, use its RX LED instead: pin 17 with STATUS_LED_ON LOW.
#if defined(LED_BUILTIN)
#define STATUS_LED_PIN LED_BUILTIN
#endif
#define STATUS_LED_ON HIGH                 // LOW for an LED that lights up when the pin is low

// ---------------------------------------------------------------------------
// iBus protocol
// ---------------------------------------------------------------------------

#define IBUS_BAUD_RATE 115200
#define IBUS_FRAME_LENGTH 0x20             // iBus packet size (2 byte header, 14 channels x 2 bytes, 2 byte checksum)
#define IBUS_COMMAND40 0x40                // Command is always 0x40
#define IBUS_MAXCHANNELS 14                // iBus has a maximum of 14 channels
#define IBUS_FRAME_GAP_US 2000             // Frames are sent every 7ms, a pause this long means a new frame starts

#define ibusSerial Serial1                 // Hardware UART, RX = D0

// The decoded channels
uint16_t channel_us[IBUS_MAXCHANNELS];
uint8_t channel_count = 0;                 // 0 = no signal
unsigned long last_frame_ms = 0;

static byte ibus_data[IBUS_FRAME_LENGTH];
static uint8_t ibus_index = 0;
static unsigned long last_byte_us = 0;

// ----- BEGIN SHARED: usb-joystick -----
// USB game controller output, shared by ppm-to-usb-joystick and ibus-to-usb-joystick.
// Keep both copies identical (test/check_shared.py checks this).
// Reads the channels from channel_us[] / channel_count (0 = no signal).

#if BUTTON_COUNT > 0 && (BUTTON_FIRST_CHANNEL < 1 || BUTTON_FIRST_CHANNEL + BUTTON_COUNT - 1 > 14)
#error "Button channels must be within 1-14"
#endif

Joystick_ Joystick(
	JOYSTICK_DEFAULT_REPORT_ID,                        // hidReportId
	JOYSTICK_TYPE_MULTI_AXIS,                          // joystickType
	BUTTON_COUNT,                                      // buttonCount
	0,                                                 // hatSwitchCount
	AXIS_X_CHANNEL > 0,                                // includeXAxis
	AXIS_Y_CHANNEL > 0,                                // includeYAxis
	AXIS_Z_CHANNEL > 0,                                // includeZAxis
	AXIS_RX_CHANNEL > 0,                               // includeRxAxis
	AXIS_RY_CHANNEL > 0,                               // includeRyAxis
	AXIS_RZ_CHANNEL > 0,                               // includeRzAxis
	false,                                             // includeRudder
	false,                                             // includeThrottle
	false,                                             // includeAccelerator
	false,                                             // includeBrake
	false                                              // includeSteering
);

// Value of a channel (1-based), or the center value if there's no signal
// or the signal doesn't carry that channel
static uint16_t ChannelValue(uint8_t channel)
{
	if (channel < 1 || channel > channel_count) {
		return CHANNEL_CENTER;
	}
	return channel_us[channel - 1];
}

// Prints the values sent to the PC: number of channels, the axes in the
// order X Y Z Rx Ry Rz (0 = axis not used), then the buttons
static void PrintJoystickDebug()
{
#if DEBUG_INTERVAL_MS > 0
	static unsigned long last_print_ms = 0;
	const unsigned long now = millis();

	if (now - last_print_ms < DEBUG_INTERVAL_MS) {
		return;
	}
	last_print_ms = now;
#endif

	if (channel_count == 0) {
		DEBUG_PORT.println(F("No signal"));
		return;
	}

	const uint8_t axes[] = { AXIS_X_CHANNEL, AXIS_Y_CHANNEL, AXIS_Z_CHANNEL, AXIS_RX_CHANNEL, AXIS_RY_CHANNEL, AXIS_RZ_CHANNEL };
	DEBUG_PORT.print(channel_count);
	DEBUG_PORT.print(F("ch "));
	for (uint8_t i = 0; i < sizeof(axes); i++) {
		DEBUG_PORT.print(axes[i] ? ChannelValue(axes[i]) : 0);
		DEBUG_PORT.print(' ');
	}
	DEBUG_PORT.print('b');
	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		DEBUG_PORT.print(ChannelValue(BUTTON_FIRST_CHANNEL + i) > BUTTON_THRESHOLD ? '1' : '0');
	}
	DEBUG_PORT.println();
}

// Sends the current channel values to the PC
static void SendJoystickState()
{
	if (AXIS_X_CHANNEL) Joystick.setXAxis(ChannelValue(AXIS_X_CHANNEL));
	if (AXIS_Y_CHANNEL) Joystick.setYAxis(ChannelValue(AXIS_Y_CHANNEL));
	if (AXIS_Z_CHANNEL) Joystick.setZAxis(ChannelValue(AXIS_Z_CHANNEL));
	if (AXIS_RX_CHANNEL) Joystick.setRxAxis(ChannelValue(AXIS_RX_CHANNEL));
	if (AXIS_RY_CHANNEL) Joystick.setRyAxis(ChannelValue(AXIS_RY_CHANNEL));
	if (AXIS_RZ_CHANNEL) Joystick.setRzAxis(ChannelValue(AXIS_RZ_CHANNEL));

	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		Joystick.setButton(i, ChannelValue(BUTTON_FIRST_CHANNEL + i) > BUTTON_THRESHOLD ? 1 : 0);
	}

	Joystick.sendState();

	if (DEBUG_OUTPUT) {
		PrintJoystickDebug();
	}
}

// Call from setup()
static void JoystickSetup()
{
	if (DEBUG_OUTPUT) {
		DEBUG_PORT.begin(115200);
	}

	Joystick.setXAxisRange(CHANNEL_MIN, CHANNEL_MAX);
	Joystick.setYAxisRange(CHANNEL_MIN, CHANNEL_MAX);
	Joystick.setZAxisRange(CHANNEL_MIN, CHANNEL_MAX);
	Joystick.setRxAxisRange(CHANNEL_MIN, CHANNEL_MAX);
	Joystick.setRyAxisRange(CHANNEL_MIN, CHANNEL_MAX);
	Joystick.setRzAxisRange(CHANNEL_MIN, CHANNEL_MAX);
	Joystick.begin(false);

	SendJoystickState();                               // Start centered
}
// ----- END SHARED: usb-joystick -----

// Feeds one received byte to the frame parser, returns true when a complete,
// valid frame has been decoded into channel_us
static bool ParseIbusByte(byte value)
{
	if (ibus_index == 0 && value != IBUS_FRAME_LENGTH) {
		return false;                                  // Waiting for the start of a frame
	}

	if (ibus_index == 1 && value != IBUS_COMMAND40) {
		// Not a frame header. The byte could be the start of the real one.
		ibus_index = (value == IBUS_FRAME_LENGTH) ? 1 : 0;
		return false;
	}

	ibus_data[ibus_index++] = value;

	if (ibus_index < IBUS_FRAME_LENGTH) {
		return false;
	}

	ibus_index = 0;

	// Checksum is 0xFFFF minus the sum of all bytes before it
	uint16_t checksum = 0xFFFF;
	for (uint8_t i = 0; i < IBUS_FRAME_LENGTH - 2; i++) {
		checksum -= ibus_data[i];
	}
	if (checksum != (uint16_t)(ibus_data[30] | (ibus_data[31] << 8))) {
		// We probably locked onto bytes that only looked like a header,
		// the real frame may start later in the buffer.
		for (uint8_t start = 1; start < IBUS_FRAME_LENGTH; start++) {
			if (ibus_data[start] == IBUS_FRAME_LENGTH &&
			    (start == IBUS_FRAME_LENGTH - 1 || ibus_data[start + 1] == IBUS_COMMAND40)) {
				ibus_index = IBUS_FRAME_LENGTH - start;
				memmove(ibus_data, ibus_data + start, ibus_index);
				break;
			}
		}
		return false;
	}

	for (uint8_t i = 0; i < IBUS_MAXCHANNELS; i++) {
		// Newer receivers use the top 4 bits for extra channels, so mask them off
		channel_us[i] = (uint16_t)(ibus_data[2 + i * 2] | (ibus_data[3 + i * 2] << 8)) & 0x0FFF;
	}
	return true;
}

static void UpdateStatusLed(unsigned long now)
{
#ifdef STATUS_LED_PIN
	bool on;
	if (last_frame_ms == 0) {
		on = (now / 100) & 1;                          // Fast flash: waiting for iBus data
	}
	else if (channel_count > 0) {
		on = true;                                     // On: receiving
	}
	else {
		on = ((now - last_frame_ms) / 500) & 1;        // Slow flash: signal lost (starts off)
	}
	digitalWrite(STATUS_LED_PIN, on ? STATUS_LED_ON : !STATUS_LED_ON);
#else
	(void)now;
#endif
}

void setup() {

#ifdef STATUS_LED_PIN
	pinMode(STATUS_LED_PIN, OUTPUT);
#endif

	JoystickSetup();
	ibusSerial.begin(IBUS_BAUD_RATE);
}

void loop() {

	bool new_frame = false;

	while (ibusSerial.available()) {
		const unsigned long now_us = micros();

		// A pause in the data means the next byte starts a new frame
		if (now_us - last_byte_us > IBUS_FRAME_GAP_US) {
			ibus_index = 0;
		}
		last_byte_us = now_us;

		if (ParseIbusByte(ibusSerial.read())) {
			new_frame = true;
		}
	}

	const unsigned long now = millis();

	if (new_frame) {
		channel_count = IBUS_MAXCHANNELS;
		last_frame_ms = now ? now : 1;                 // 0 means "never received"
		SendJoystickState();
	}
	else if (channel_count > 0 && now - last_frame_ms > IBUS_SIGNAL_TIMEOUT_MS) {
		// Lost the receiver: center everything rather than leave the sticks frozen
		channel_count = 0;
		SendJoystickState();
	}

	UpdateStatusLed(now);
}
