/*
 Name:		ppm_to_ibus_serial.ino
 Created:	5/31/2018
 Author:	wdcossey

 Decodes a PPM (CPPM) signal and re-encodes it as FlySky iBus frames on the
 hardware serial port, e.g. for use with vJoySerialFeeder.

 The PPM edges are timestamped by the timer's Input Capture unit, so the
 measured channel widths are not affected by interrupt latency (from the
 millis() timer, the serial port, etc.). This removes most of the jitter; a
 small optional hysteresis filter (PPM_JITTER_THRESHOLD) removes the rest.

 PPM input pin (it must be the timer's input capture pin):
	Uno / Nano / Pro Mini (ATmega328P/168)  : D8  (ICP1)
	Leonardo / Micro / Pro Micro (32U4)     : D4  (ICP1)
	Mega 1280 / 2560                        : D49 (ICP4)
*/

#include <util/atomic.h>

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

#define SERIAL_BAUD_RATE 115200            // Must match the baud rate set in vJoySerialFeeder

#define PPM_MIN_CHANNELS 4                 // Frames with fewer channels are ignored
#define PPM_MAX_CHANNELS 14                // Frames with more channels are ignored (iBus carries at most 14)

#define PPM_CHANNEL_MIN_US 700             // Shortest valid channel width (us), anything shorter is a glitch
#define PPM_CHANNEL_MAX_US 2400            // Longest valid channel width (us)
#define PPM_SYNC_MIN_US 2500               // Any gap at least this long is the frame separator (sync)

// Hysteresis filter: the reported value only moves once the measured value
// differs from it by more than this many microseconds, and then follows it
// with this much "slack". Holds sticks perfectly still without losing
// resolution. Set to 0 to disable. Increase if you still see jitter.
#define PPM_JITTER_THRESHOLD 4

#define PPM_SIGNAL_TIMEOUT_MS 100          // Stop sending iBus frames if no valid PPM frame was seen for this long

// Set to 1 to print human readable channel values (for the Arduino Serial
// Monitor) instead of binary iBus frames. Useful to check your wiring/receiver.
#define DEBUG_OUTPUT 0

// ---------------------------------------------------------------------------
// iBus protocol
// ---------------------------------------------------------------------------

#define IBUS_FRAME_LENGTH 0x20             // iBus packet size (2 byte header, 14 channels x 2 bytes, 2 byte checksum)
#define IBUS_COMMAND40 0x40                // Command is always 0x40
#define IBUS_MAXCHANNELS 14                // iBus has a maximum of 14 channels
#define IBUS_DEFAULT_VALUE (uint16_t)1500  // Value sent for channels the PPM signal doesn't carry

#if PPM_MAX_CHANNELS > IBUS_MAXCHANNELS
#error "PPM_MAX_CHANNELS can't be greater than IBUS_MAXCHANNELS"
#endif

#if PPM_MIN_CHANNELS < 1 || PPM_MIN_CHANNELS > PPM_MAX_CHANNELS
#error "PPM_MIN_CHANNELS must be between 1 and PPM_MAX_CHANNELS"
#endif

// ---------------------------------------------------------------------------
// Board specific input capture setup
// ---------------------------------------------------------------------------

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega328__) || \
    defined(__AVR_ATmega168__) || defined(__AVR_ATmega168P__) || defined(__AVR_ATmega168PA__) || \
    defined(__AVR_ATmega88P__) || defined(__AVR_ATmega88PA__)
	#define PPM_INPUT_PIN 8
	#define PPM_TCCRA TCCR1A
	#define PPM_TCCRB TCCR1B
	#define PPM_ICR ICR1
	#define PPM_TIMSK TIMSK1
	#define PPM_TIFR TIFR1
	#define PPM_ICIE ICIE1
	#define PPM_ICF ICF1
	#define PPM_ICNC ICNC1
	#define PPM_ICES ICES1
	#define PPM_CS_DIV8 (1 << CS11)
	#define PPM_CAPT_vect TIMER1_CAPT_vect
#elif defined(__AVR_ATmega32U4__)
	#define PPM_INPUT_PIN 4
	#define PPM_TCCRA TCCR1A
	#define PPM_TCCRB TCCR1B
	#define PPM_ICR ICR1
	#define PPM_TIMSK TIMSK1
	#define PPM_TIFR TIFR1
	#define PPM_ICIE ICIE1
	#define PPM_ICF ICF1
	#define PPM_ICNC ICNC1
	#define PPM_ICES ICES1
	#define PPM_CS_DIV8 (1 << CS11)
	#define PPM_CAPT_vect TIMER1_CAPT_vect
#elif defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
	#define PPM_INPUT_PIN 49
	#define PPM_TCCRA TCCR4A
	#define PPM_TCCRB TCCR4B
	#define PPM_ICR ICR4
	#define PPM_TIMSK TIMSK4
	#define PPM_TIFR TIFR4
	#define PPM_ICIE ICIE4
	#define PPM_ICF ICF4
	#define PPM_ICNC ICNC4
	#define PPM_ICES ICES4
	#define PPM_CS_DIV8 (1 << CS41)
	#define PPM_CAPT_vect TIMER4_CAPT_vect
#else
	#error "Unsupported board: needs an AVR with a 16-bit timer input capture pin (Uno, Nano, Pro Mini, Leonardo, Micro, Mega)"
#endif

// The capture timer runs at F_CPU / 8 (0.5us per tick at 16MHz, 1us at 8MHz)
#define TIMER_PRESCALER 8UL
#define CPU_MHZ (F_CPU / 1000000UL)
#define US_TO_TICKS(us) ((uint16_t)(((uint32_t)(us) * CPU_MHZ) / TIMER_PRESCALER))

#define PPM_CHANNEL_MIN_TICKS US_TO_TICKS(PPM_CHANNEL_MIN_US)
#define PPM_CHANNEL_MAX_TICKS US_TO_TICKS(PPM_CHANNEL_MAX_US)
#define PPM_SYNC_MIN_TICKS US_TO_TICKS(PPM_SYNC_MIN_US)

// ---------------------------------------------------------------------------
// State shared between the capture ISR and loop()
// ---------------------------------------------------------------------------

volatile uint16_t ppm_frame_ticks[PPM_MAX_CHANNELS];   // Last complete, validated frame (timer ticks)
volatile uint8_t ppm_frame_channels = 0;               // Number of channels in ppm_frame_ticks
volatile bool ppm_frame_ready = false;                 // Set by the ISR when a new frame is available

// ---------------------------------------------------------------------------
// State only used by loop()
// ---------------------------------------------------------------------------

uint16_t channel_us[PPM_MAX_CHANNELS];                 // Filtered channel values (microseconds)
uint8_t channel_count = 0;                             // 0 = no signal
unsigned long last_frame_ms = 0;

byte serial_buffer[IBUS_FRAME_LENGTH];

// the setup function runs once when you press reset or power the board
void setup() {

	Serial.begin(SERIAL_BAUD_RATE);

	// Keep the input from floating (and generating noise) when nothing is connected
	pinMode(PPM_INPUT_PIN, INPUT_PULLUP);

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		// Normal mode, no PWM outputs, prescaler 8, capture on the rising edge
		// with the noise canceler enabled.
		// Any edge works: the time between two rising (or two falling) edges
		// is the channel width for both normal and inverted PPM.
		PPM_TCCRA = 0;
		PPM_TCCRB = (1 << PPM_ICNC) | (1 << PPM_ICES) | PPM_CS_DIV8;
		PPM_TIFR = (1 << PPM_ICF);                     // Clear any pending capture
		PPM_TIMSK = (1 << PPM_ICIE);                   // Enable the capture interrupt
	}
}

// Input capture interrupt, runs on every PPM edge.
// Every gap of at least PPM_SYNC_MIN_US starts a new frame, so the decoder
// re-synchronises on every frame and recovers from glitches by itself.
ISR(PPM_CAPT_vect) {

	static uint16_t last_capture = 0;
	static uint16_t work_ticks[PPM_MAX_CHANNELS];
	static uint8_t index = 0;
	static uint8_t previous_count = 0;
	static bool in_frame = false;                      // false until a sync gap has been seen

	const uint16_t capture = PPM_ICR;
	const uint16_t width = capture - last_capture;    // Unsigned math handles the timer wrapping
	last_capture = capture;

	if (width >= PPM_SYNC_MIN_TICKS) {

		if (in_frame) {
			// Only publish a frame if it has the same channel count as the one
			// before it, this filters out frames damaged by a missed edge.
			if (index >= PPM_MIN_CHANNELS && index == previous_count) {
				for (uint8_t i = 0; i < index; i++) {
					ppm_frame_ticks[i] = work_ticks[i];
				}
				ppm_frame_channels = index;
				ppm_frame_ready = true;
			}
			previous_count = index;
		}

		index = 0;
		in_frame = true;
		return;
	}

	if (!in_frame) {
		return;                                        // Waiting for a sync gap
	}

	if (width < PPM_CHANNEL_MIN_TICKS || width > PPM_CHANNEL_MAX_TICKS || index >= PPM_MAX_CHANNELS) {
		in_frame = false;                              // Glitch or too many channels, drop this frame
		return;
	}

	work_ticks[index++] = width;
}

static uint16_t TicksToMicroseconds(uint16_t ticks)
{
	return (uint16_t)(((uint32_t)ticks * TIMER_PRESCALER + CPU_MHZ / 2) / CPU_MHZ);
}

static uint16_t FilterJitter(uint16_t current, uint16_t sample)
{
#if PPM_JITTER_THRESHOLD > 0
	if (sample > current + PPM_JITTER_THRESHOLD) {
		return sample - PPM_JITTER_THRESHOLD;
	}
	if (sample + PPM_JITTER_THRESHOLD < current) {
		return sample + PPM_JITTER_THRESHOLD;
	}
	return current;
#else
	(void)current;
	return sample;
#endif
}

static void WriteIbus()
{
	uint8_t buffer_index = 0;

	// Write the IBus buffer length
	serial_buffer[buffer_index++] = (byte)IBUS_FRAME_LENGTH;
	// Write the IBus Command 0x40
	serial_buffer[buffer_index++] = (byte)IBUS_COMMAND40;

	// Write the IBus channel data to the buffer,
	// padding the unused channels with the default value
	for (uint8_t i = 0; i < IBUS_MAXCHANNELS; i++) {
		const uint16_t value = (i < channel_count) ? channel_us[i] : IBUS_DEFAULT_VALUE;
		serial_buffer[buffer_index++] = (byte)(value & 0xFF);
		serial_buffer[buffer_index++] = (byte)((value >> 8) & 0xFF);
	}

	// Calculate the IBus checksum
	uint16_t ibus_checksum = 0xFFFF;
	for (uint8_t i = 0; i < buffer_index; i++) {
		ibus_checksum -= (uint16_t)serial_buffer[i];
	}

	// Write the IBus checksum to the buffer
	serial_buffer[buffer_index++] = (byte)(ibus_checksum & 0xFF);
	serial_buffer[buffer_index++] = (byte)((ibus_checksum >> 8) & 0xFF);

	// Write the buffer to the Serial pin
	Serial.write(serial_buffer, buffer_index);
}

static void WriteDebug()
{
	static unsigned long last_print_ms = 0;
	const unsigned long now = millis();

	if (now - last_print_ms < 100) {
		return;
	}
	last_print_ms = now;

	if (channel_count == 0) {
		Serial.println(F("No PPM signal"));
		return;
	}

	Serial.print(channel_count);
	Serial.print(F(" ch:"));
	for (uint8_t i = 0; i < channel_count; i++) {
		Serial.print(' ');
		Serial.print(channel_us[i]);
	}
	Serial.println();
}

// the loop function runs over and over again until power down or reset
void loop() {

	uint16_t frame_ticks[PPM_MAX_CHANNELS];
	uint8_t frame_channels = 0;

	// Copy the latest frame with interrupts disabled, so the ISR can't change
	// it half way through (16-bit reads aren't atomic on AVR).
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		if (ppm_frame_ready) {
			frame_channels = ppm_frame_channels;
			for (uint8_t i = 0; i < frame_channels; i++) {
				frame_ticks[i] = ppm_frame_ticks[i];
			}
			ppm_frame_ready = false;
		}
	}

	const unsigned long now = millis();

	if (frame_channels > 0) {
		// Don't filter against stale values after signal loss or a channel count change
		const bool filter = (frame_channels == channel_count);

		for (uint8_t i = 0; i < frame_channels; i++) {
			const uint16_t us = TicksToMicroseconds(frame_ticks[i]);
			channel_us[i] = filter ? FilterJitter(channel_us[i], us) : us;
		}

		channel_count = frame_channels;
		last_frame_ms = now;

		// One iBus frame per PPM frame
		if (!DEBUG_OUTPUT) {
			WriteIbus();
		}
	}
	else if (channel_count > 0 && (now - last_frame_ms) > PPM_SIGNAL_TIMEOUT_MS) {
		// Signal lost: stop sending, so the PC side sees the loss instead of frozen sticks
		channel_count = 0;
	}

	if (DEBUG_OUTPUT) {
		WriteDebug();
	}
}
