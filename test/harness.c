// Feeds a PPM edge list into an ATmega328P firmware (PB0) and logs UART0 output.
// usage: harness firmware.elf stimulus.txt   (stimulus lines: "<time_us> <level>")
#include <stdio.h>
#include <stdlib.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_uart.h>

static avr_t *avr;
static void uart_out(struct avr_irq_t *irq, uint32_t value, void *param) {
	printf("U %.3f %u\n", avr->cycle / 16000.0, value & 0xff);   // ms, byte
}
int main(int argc, char **argv) {
	elf_firmware_t fw = {0};
	if (elf_read_firmware(argv[1], &fw)) return 1;
	avr = avr_make_mcu_by_name("atmega328p");
	avr_init(avr);
	avr->frequency = 16000000;
	avr_load_firmware(avr, &fw);
	avr->log = 0;
	uint32_t f = 0;
	avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &f);
	f &= ~AVR_UART_FLAG_STDIO;
	avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &f);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), uart_out, NULL);
	avr_irq_t *pin = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0);

	FILE *st = fopen(argv[2], "r");
	double t; int lvl; int have = fscanf(st, "%lf %d", &t, &lvl) == 2;
	double end_us = have ? t : 0;
	for (;;) {
		double now_us = avr->cycle / 16.0;
		while (have && now_us >= t) {
			avr_raise_irq(pin, lvl);
			end_us = t;
			have = fscanf(st, "%lf %d", &t, &lvl) == 2;
		}
		if (!have && now_us > end_us + 50000) break;   // run 50ms past last edge
		int s = avr_run(avr);
		if (s == cpu_Done || s == cpu_Crashed) { fprintf(stderr, "cpu state %d\n", s); break; }
	}
	return 0;
}
