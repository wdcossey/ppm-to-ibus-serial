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

 At power-on the sketch watches the PPM signal for a couple of seconds (the
 "boot check"), works out where the gap between frames is and adjusts its
 timing limits to suit the radio. Power cycle to check again, e.g. after
 changing radios. The LED shows what it is doing:
	fast flash : boot check, or waiting for a PPM signal
	on         : receiving PPM
	slow flash : PPM signal lost

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
#define PPM_CHANNEL_MAX_US 2400            // Longest valid channel width (us), unless the boot check changes it
#define PPM_SYNC_MIN_US 2500               // Any gap at least this long is the frame separator (sync), unless the boot check changes it

// Boot check: how long to watch the PPM signal (from the first pulse) before
// deciding on the timing limits. 0 disables the boot check, the fixed values
// above are used straight away.
#ifndef PPM_BOOT_CHECK_MS
#define PPM_BOOT_CHECK_MS 2000
#endif
#define PPM_BOOT_CHECK_TIMEOUT_MS 5000     // Give up (and use the fixed values) if no frame layout is found by then
#define PPM_FULL_TRAVEL_US 2000            // Channels are assumed to reach at least this, even if the sticks weren't moved during the boot check

// Hysteresis filter: the reported value only moves once the measured value
// differs from it by more than this many microseconds, and then follows it
// with this much "slack" (big, quick moves are followed exactly). Holds
// sticks perfectly still without losing resolution. Set to 0 to disable.
// Increase if you still see jitter.
#define PPM_JITTER_THRESHOLD 4

#define PPM_SIGNAL_TIMEOUT_MS 100          // Stop sending iBus frames if no valid PPM frame was seen for this long

// Set to 1 to print human readable channel values (for the Arduino Serial
// Monitor) instead of binary iBus frames. Useful to check your wiring/receiver.
// Set to 2 to also print the raw time between PPM edges every 500ms, before
// any decoding or filtering. Useful to see what an unusual radio sends.
#ifndef DEBUG_OUTPUT
#define DEBUG_OUTPUT 0
#endif
#define DEBUG_PORT Serial                  // The debug output replaces the iBus output on the same port

// Status LED (see above). Comment out STATUS_LED_PIN to leave the LED alone.
// Pro Micro: it has no LED on pin 13, use its RX LED instead: pin 17 with STATUS_LED_ON LOW.
#if defined(LED_BUILTIN)
#define STATUS_LED_PIN LED_BUILTIN
#endif
#define STATUS_LED_ON HIGH                 // LOW for an LED that lights up when the pin is low

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

// ----- BEGIN SHARED: ppm-decoder -----
// PPM decoder, shared by ppm-to-ibus-serial and ppm-to-usb-joystick.
// Keep both copies identical (test/check_shared.py checks this).

#if PPM_MIN_CHANNELS < 1 || PPM_MIN_CHANNELS > PPM_MAX_CHANNELS
#error "PPM_MIN_CHANNELS must be between 1 and PPM_MAX_CHANNELS"
#endif

// Board specific input capture setup
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

// What PpmUpdate() reports
#define PPM_NO_CHANGE 0
#define PPM_NEW_FRAME 1                                // channel_us / channel_count hold a new frame
#define PPM_SIGNAL_LOST 2                              // channel_count has just become 0

// Shared between the capture ISR and the main code
volatile uint16_t ppm_frame_ticks[PPM_MAX_CHANNELS];   // Last complete, validated frame (timer ticks)
volatile uint8_t ppm_frame_channels = 0;               // Number of channels in ppm_frame_ticks
volatile bool ppm_frame_ready = false;                 // Set by the ISR when a new frame is available

// Timing limits used by the ISR, adjusted by the boot check
volatile uint16_t ppm_sync_min_ticks = US_TO_TICKS(PPM_SYNC_MIN_US);
volatile uint16_t ppm_channel_max_ticks = US_TO_TICKS(PPM_CHANNEL_MAX_US);
volatile bool ppm_decoding = (PPM_BOOT_CHECK_MS == 0); // false while the boot check runs

// The most recent edge-to-edge times, for the boot check and DEBUG_OUTPUT 2
#define EDGE_BUFFER_SIZE 128                           // Must be a power of 2
volatile uint16_t edge_ticks[EDGE_BUFFER_SIZE];
volatile uint16_t edge_count = 0;                      // Edges seen so far (wraps around)
volatile bool edge_buffer_full = false;                // Every entry of edge_ticks holds a real measurement

// The decoded channels, only used by the main code
uint16_t channel_us[PPM_MAX_CHANNELS];                 // Filtered channel values (microseconds)
uint8_t channel_count = 0;                             // 0 = no signal
unsigned long last_frame_ms = 0;

// Call from setup()
static void PpmSetup()
{
#ifdef STATUS_LED_PIN
	pinMode(STATUS_LED_PIN, OUTPUT);
#endif

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
// Every gap of at least ppm_sync_min_ticks starts a new frame, so the decoder
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

	edge_ticks[edge_count % EDGE_BUFFER_SIZE] = width;
	if (++edge_count > EDGE_BUFFER_SIZE) {
		edge_buffer_full = true;                       // The very first (meaningless) measurement has been overwritten
	}

	if (!ppm_decoding) {
		return;                                        // Boot check still running
	}

	if (width >= ppm_sync_min_ticks) {

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

	if (width < PPM_CHANNEL_MIN_TICKS || width > ppm_channel_max_ticks || index >= PPM_MAX_CHANNELS) {
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
	if (sample > current + 4 * PPM_JITTER_THRESHOLD || sample + 4 * PPM_JITTER_THRESHOLD < current) {
		return sample;                                 // A big, quick move isn't jitter, follow it exactly
	}
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

// Copies the edge buffer, oldest first, converted to microseconds.
// Returns the number of valid entries, *edges is set to edge_count.
static uint8_t CopyEdges(uint16_t *us, uint16_t *edges)
{
	uint16_t count;
	uint8_t valid;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		count = edge_count;
		// Skip the first measurement after power-on, it isn't a real edge-to-edge time
		valid = edge_buffer_full ? EDGE_BUFFER_SIZE : (count > 1 ? count - 1 : 0);
		for (uint8_t i = 0; i < valid; i++) {
			us[i] = edge_ticks[(uint16_t)(count - valid + i) % EDGE_BUFFER_SIZE];
		}
	}
	*edges = count;

	for (uint8_t i = 0; i < valid; i++) {
		us[i] = TicksToMicroseconds(us[i]);
	}
	return valid;
}

// Looks for a repeating PPM frame layout in a list of edge-to-edge times:
// a fixed number of channels followed by a gap that's longer than all of them.
// Picks the layout with the clearest difference between gaps and channels.
static bool FindFrameLayout(const uint16_t *us, uint8_t count, uint8_t *layout_channels, uint16_t *layout_gap_min_us, uint16_t *layout_channel_max_us)
{
	int32_t best_margin = 0;

	for (uint8_t channels = PPM_MIN_CHANNELS; channels <= PPM_MAX_CHANNELS; channels++) {
		for (uint8_t phase = 0; phase <= channels; phase++) {
			uint16_t gap_min = 0xFFFF, channel_min = 0xFFFF, channel_max = 0;
			uint8_t frames = 0;

			// us[gap] is a frame gap, the channels follow it
			for (uint8_t gap = phase; gap + channels < count; gap += channels + 1) {
				frames++;
				gap_min = min(gap_min, us[gap]);
				for (uint8_t c = 1; c <= channels; c++) {
					channel_min = min(channel_min, us[gap + c]);
					channel_max = max(channel_max, us[gap + c]);
				}
			}

			const int32_t margin = (int32_t)gap_min - (int32_t)channel_max;
			if (frames >= 3 && channel_min >= PPM_CHANNEL_MIN_US && margin > best_margin) {
				best_margin = margin;
				*layout_channels = channels;
				*layout_gap_min_us = gap_min;
				*layout_channel_max_us = channel_max;
			}
		}
	}
	return best_margin > 0;
}

// Runs until the boot check has finished
static void RunBootCheck(unsigned long now)
{
	static unsigned long first_edge_ms = 0;
	static bool seen_edges = false;
	static unsigned long last_try_ms = 0;

	uint16_t us[EDGE_BUFFER_SIZE];
	uint16_t edges;
	const uint8_t count = CopyEdges(us, &edges);

	if (edges == 0) {
		return;                                        // No PPM signal yet, keep waiting
	}
	if (!seen_edges) {
		seen_edges = true;
		first_edge_ms = now;
	}

	const unsigned long elapsed = now - first_edge_ms;
	if (elapsed < PPM_BOOT_CHECK_MS || now - last_try_ms < 250) {
		return;
	}
	last_try_ms = now;

	uint16_t sync_us = PPM_SYNC_MIN_US;
	uint16_t channel_max_us = PPM_CHANNEL_MAX_US;
	uint8_t found_channels = 0;
	uint16_t found_gap_min_us = 0, found_channel_max_us = 0;
	const bool found = (count == EDGE_BUFFER_SIZE) &&
	                   FindFrameLayout(us, count, &found_channels, &found_gap_min_us, &found_channel_max_us);

	if (!found && elapsed < PPM_BOOT_CHECK_TIMEOUT_MS) {
		return;                                        // Try again in a moment
	}

	if (found && (found_gap_min_us < PPM_SYNC_MIN_US || found_channel_max_us > PPM_CHANNEL_MAX_US)) {
		// The default limits don't fit this radio. Put the sync threshold
		// between its longest channel and its shortest gap. Leave room for
		// the sticks reaching full travel, and don't go much higher than
		// needed: on fixed length frames the gap gets shorter as the sticks move.
		uint16_t longest = max(found_channel_max_us, (uint16_t)PPM_FULL_TRAVEL_US);
		if (longest >= found_gap_min_us) {
			longest = found_channel_max_us;
		}
		sync_us = min((uint16_t)((found_gap_min_us + longest) / 2),
		              max((uint16_t)PPM_SYNC_MIN_US, (uint16_t)(found_channel_max_us + 200)));
		channel_max_us = sync_us - 1;
	}

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		ppm_sync_min_ticks = US_TO_TICKS(sync_us);
		ppm_channel_max_ticks = US_TO_TICKS(channel_max_us);
		ppm_decoding = true;
	}

	if (DEBUG_OUTPUT) {
		DEBUG_PORT.print(F("Boot check: "));
		if (found) {
			DEBUG_PORT.print(found_channels);
			DEBUG_PORT.print(F(" ch, channels up to "));
			DEBUG_PORT.print(found_channel_max_us);
			DEBUG_PORT.print(F("us, frame gaps from "));
			DEBUG_PORT.print(found_gap_min_us);
			DEBUG_PORT.print(F("us"));
		}
		else {
			DEBUG_PORT.print(F("no PPM frames found"));
		}
		DEBUG_PORT.print(F(". Using sync >= "));
		DEBUG_PORT.print(sync_us);
		DEBUG_PORT.print(F("us, channels "));
		DEBUG_PORT.print(PPM_CHANNEL_MIN_US);
		DEBUG_PORT.print('-');
		DEBUG_PORT.print(channel_max_us);
		DEBUG_PORT.println(F("us"));
	}
}

static void UpdateStatusLed(unsigned long now)
{
#ifdef STATUS_LED_PIN
	bool on;
	if (!ppm_decoding) {
		on = (now / 100) & 1;                          // Fast flash: boot check / waiting for a signal
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

#if DEBUG_OUTPUT >= 2
// Prints the last 32 edge-to-edge times in microseconds, oldest first:
// [12000] = frame gap (sync), 2450? = neither a valid channel nor a sync gap
static void WriteRawTiming(unsigned long now)
{
	static unsigned long last_print_ms = 0;
	static uint16_t last_edges = 0;

	if (now - last_print_ms < 500) {
		return;
	}
	last_print_ms = now;

	uint16_t us[EDGE_BUFFER_SIZE];
	uint16_t edges;
	const uint8_t count = CopyEdges(us, &edges);

	DEBUG_PORT.print(F("raw:"));
	if (edges == last_edges || count == 0) {
		DEBUG_PORT.println(F(" no edges"));
		return;
	}
	last_edges = edges;

	const uint16_t sync_us = TicksToMicroseconds(ppm_sync_min_ticks);
	const uint16_t channel_max_us = TicksToMicroseconds(ppm_channel_max_ticks);

	for (uint8_t i = (count > 32) ? count - 32 : 0; i < count; i++) {
		DEBUG_PORT.print(' ');
		if (us[i] >= sync_us) {
			DEBUG_PORT.print('[');
			DEBUG_PORT.print(us[i]);
			DEBUG_PORT.print(']');
		}
		else {
			DEBUG_PORT.print(us[i]);
			if (us[i] < PPM_CHANNEL_MIN_US || us[i] > channel_max_us) {
				DEBUG_PORT.print('?');
			}
		}
	}
	DEBUG_PORT.println();
}
#endif

// Call from loop(). Runs the boot check, picks up new frames (filtered, in
// channel_us / channel_count), detects signal loss and updates the LED.
static uint8_t PpmUpdate()
{
	uint16_t frame_ticks[PPM_MAX_CHANNELS];
	uint8_t frame_channels = 0;
	uint8_t result = PPM_NO_CHANGE;

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

	if (!ppm_decoding) {
		RunBootCheck(now);
	}

	if (frame_channels > 0) {
		// Don't filter against stale values after signal loss or a channel count change
		const bool filter = (frame_channels == channel_count);

		for (uint8_t i = 0; i < frame_channels; i++) {
			const uint16_t us = TicksToMicroseconds(frame_ticks[i]);
			channel_us[i] = filter ? FilterJitter(channel_us[i], us) : us;
		}

		channel_count = frame_channels;
		last_frame_ms = now;
		result = PPM_NEW_FRAME;
	}
	else if (channel_count > 0 && (now - last_frame_ms) > PPM_SIGNAL_TIMEOUT_MS) {
		channel_count = 0;
		result = PPM_SIGNAL_LOST;
	}

	UpdateStatusLed(now);

#if DEBUG_OUTPUT >= 2
	WriteRawTiming(now);
#endif

	return result;
}

// ----- END SHARED: ppm-decoder -----

byte serial_buffer[IBUS_FRAME_LENGTH];

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

	if (!ppm_decoding) {
		Serial.println(F("Checking PPM signal..."));
		return;
	}

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

// the setup function runs once when you press reset or power the board
void setup() {

	Serial.begin(SERIAL_BAUD_RATE);
	PpmSetup();
}

// the loop function runs over and over again until power down or reset
void loop() {

	// One iBus frame per PPM frame. Nothing is sent without a signal, so the
	// PC side sees the loss instead of frozen sticks.
	if (PpmUpdate() == PPM_NEW_FRAME && !DEBUG_OUTPUT) {
		WriteIbus();
	}

	if (DEBUG_OUTPUT) {
		WriteDebug();
	}
}
