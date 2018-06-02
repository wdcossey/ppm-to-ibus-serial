/*
 Name:		ppm_to_ibus_serial.ino
 Created:	5/31/2018
 Author:	wdcossey
*/

#define IBUS_FRAME_LENGTH 0x20                                  // iBus packet size (2 byte header, 14 channels x 2 bytes, 2 byte checksum)
#define IBUS_COMMAND40 0x40                                     // Command is always 0x40
#define IBUS_MAXCHANNELS 14                                     // iBus has a maximum of 14 channels

#define IBUS_DEFAULT_VALUE (uint16_t)1500

#define PPM_LOW_SEPARATOR (uint16_t)2100                        // Anything higher than this value is considered to be the pulse separator

#define PPM_MAX_CHANNELS 10                                     // Should be no more than 10
#define PPM_JITTER_THRESHOLD 8                                  // 

volatile uint16_t ppm_pulse_data[PPM_MAX_CHANNELS] = { 0 };		// 
volatile uint16_t ppm_channel_data[PPM_MAX_CHANNELS] = { 0 };	// 
volatile uint8_t ppm_pulse_index = 0;
volatile uint8_t ppm_channel_count = 0;

enum syncStates {
	CALC_CHANNELS, // Channel calculation
	PROCESS_PULSE, // Process pulse data
};

volatile enum syncStates syncState = CALC_CHANNELS;

byte serial_buffer[IBUS_FRAME_LENGTH] = { 0 };
int buffer_index = 0;

// the setup function runs once when you press reset or power the board
void setup() {

	PCICR |= (1 << PCIE0);
	PCMSK0 |= (1 << PCINT0);

	Serial.begin(115200);
}

void WriteSerial()
{

	uint16_t ibus_checksum = ((uint16_t)0xFFFF);

	switch (syncState) {
	case CALC_CHANNELS:
		//      if (channelCount <= 0 ) {
		//        Serial.println(F("Calculating Channels"));
		//      }
		//      else {
		//        Serial.println("Channel Count: " + ((String)channelCount));
		//      }
		break;
	case PROCESS_PULSE:

		//      Serial.println("Channel Count: " + ((String)currChannelCount));

		buffer_index = 0;

		// Write the IBus buffer length
		serial_buffer[buffer_index++] = (byte)IBUS_FRAME_LENGTH;
		// Write the IBus Command 0x40
		serial_buffer[buffer_index++] = (byte)IBUS_COMMAND40;

		// Write the IBus channel data to the buffer
		for (int i = 0; i < min(ppm_channel_count, IBUS_MAXCHANNELS); i++) {
			serial_buffer[buffer_index++] = (byte)(ppm_channel_data[i] & 0xFF);
			serial_buffer[buffer_index++] = (byte)((ppm_channel_data[i] >> 8) & 0xFF);
		}

		// Fill the remaining buffer channels with the default value
		if (ppm_channel_count < IBUS_MAXCHANNELS) {
			for (int i = 0; i < IBUS_MAXCHANNELS - ppm_channel_count; i++) {
				serial_buffer[buffer_index++] = (byte)(IBUS_DEFAULT_VALUE & 0xFF);
				serial_buffer[buffer_index++] = (byte)((IBUS_DEFAULT_VALUE >> 8) & 0xFF);
			}
		}

		// Calculate the IBus checksum
		for (int i = 0; i < buffer_index; i++) {
			ibus_checksum -= (uint16_t)serial_buffer[i];
		}

		// Write the IBus checksum to the buffer
		serial_buffer[buffer_index++] = (byte)(ibus_checksum & 0xFF);
		serial_buffer[buffer_index++] = (byte)((ibus_checksum >> 8) & 0xFF);

		// Write the buffer to the Serial pin
		Serial.write(serial_buffer, buffer_index);

		buffer_index = 0;

		break;
	default:
		break;
	}

}

// the loop function runs over and over again until power down or reset
void loop() {

	// Write the IBus data to the Serial Port
	WriteSerial();

	// Delay before next update
	delay(10);
}

ISR(PCINT0_vect) {
	if (PINB & B00000001) {

		uint16_t currentPulseWidth = micros();
		ppm_pulse_data[ppm_pulse_index] = currentPulseWidth;

		switch (syncState) {
		case CALC_CHANNELS:
			if (ppm_pulse_index > 0 &&
				(ppm_pulse_data[ppm_pulse_index] - ppm_pulse_data[ppm_pulse_index - 1] > PPM_LOW_SEPARATOR))
			{
				int prevChannelCount = ppm_channel_count;
				ppm_channel_count = ppm_pulse_index;

				// Clear the usless data
				ppm_pulse_data[PPM_MAX_CHANNELS] = { 0 };

				// Typically a PPM signal shouldn't carry more than 10 pulses
				// 9 channels and a separator
				if (ppm_channel_count > 0 &&
					ppm_channel_count <= PPM_MAX_CHANNELS &&
					ppm_channel_count == prevChannelCount) {

					// We should have a valid channel count.
					// Update pulse 0 with the current value (as it was cleared above).
					ppm_pulse_data[0] = currentPulseWidth;

					// Set the pulse index to 1.
					// We have the first pulse, no need to calculate it again
					ppm_pulse_index = 1;

					// Update the sync state to process pulse data
					syncState = PROCESS_PULSE;

					return;
				}

				// We are out of sync, start again.
				ppm_pulse_index = 0;

				return;
			}
			break;
		case PROCESS_PULSE:

			// Pulse index must be > 0 to run the calculation
			if (ppm_pulse_index > 0) {

				uint16_t next_width = ppm_pulse_data[ppm_pulse_index] - ppm_pulse_data[ppm_pulse_index - 1];
				//uint16_t prev_width = ppm_channel_data[ppm_pulse_index - 1];

				//Fix the jitter
				//if (next_width > (prev_width + PPM_JITTER_THRESHOLD) || next_width < (prev_width - PPM_JITTER_THRESHOLD)) {
				//	ppm_channel_data[ppm_pulse_index - 1] = next_width;
				//}

				ppm_channel_data[ppm_pulse_index - 1] = next_width;
				

				if (ppm_pulse_index == ppm_channel_count)
				{
					ppm_pulse_index = 0;
					return;
				}
			}
			break;

		default:
			break;
		}

		ppm_pulse_index++;

	}
}
