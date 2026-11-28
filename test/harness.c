// Runs an AVR firmware in simavr, drives its inputs from a stimulus file and
// logs everything it transmits on its UARTs.
//
// usage: harness <mcu> <firmware.elf> <stimulus.txt> [pin to watch, e.g. B5 ...]
// stimulus lines (sorted by time):
//   <time_us> P <port><bit> <level>   e.g. "50000 P D4 1"  drive an input pin
//   <time_us> U <uart> <byte>         e.g. "50000 U 1 32"  send a byte to a UART RX
// output lines: "U<uart> <time_ms> <byte>" and "P<port><bit> <time_ms> <level>"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_uart.h>

static avr_t *avr;

static void uart_out(struct avr_irq_t *irq, uint32_t value, void *param) {
	printf("U%c %.4f %u\n", (char)(intptr_t)param, avr->cycle * 1000.0 / avr->frequency, value & 0xff);
}

static void pin_out(struct avr_irq_t *irq, uint32_t value, void *param) {
	const char *name = param;
	printf("P%s %.4f %u\n", name, avr->cycle * 1000.0 / avr->frequency, value & 1);
}

int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage: %s mcu firmware.elf stimulus.txt [watch pins]\n", argv[0]); return 2; }
	elf_firmware_t fw = {0};
	if (elf_read_firmware(argv[2], &fw)) return 1;
	avr = avr_make_mcu_by_name(argv[1]);
	if (!avr) { fprintf(stderr, "unknown mcu %s\n", argv[1]); return 1; }
	avr_init(avr);
	avr->frequency = 16000000;
	avr_load_firmware(avr, &fw);

	for (char u = '0'; u <= '3'; u++) {
		uint32_t f = 0;
		if (avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS(u), &f) < 0) continue;   // UART not present
		f &= ~AVR_UART_FLAG_STDIO;
		avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS(u), &f);
		avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ(u), UART_IRQ_OUTPUT), uart_out, (void *)(intptr_t)u);
	}

	for (int i = 4; i < argc; i++)
		avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(argv[i][0]), argv[i][1] - '0'), pin_out, argv[i]);

	FILE *st = fopen(argv[3], "r");
	if (!st) { perror(argv[3]); return 1; }
	avr_ioport_external_t external[12] = {{0}};
	double t = 0, end_us = 0;
	char kind[4], target[8];
	int value;
	int have = fscanf(st, "%lf %3s %7s %d", &t, kind, target, &value) == 4;
	for (;;) {
		double now_us = avr->cycle * 1e6 / avr->frequency;
		while (have && now_us >= t) {
			if (kind[0] == 'P') {
				// Tell simavr the pin is driven from outside, otherwise any write to
				// the port register re-applies the internal pull-up to the pin.
				const int port = target[0] - 'A', bit = target[1] - '0';
				external[port].name = target[0];
				external[port].mask |= 1 << bit;
				external[port].value = (external[port].value & ~(1 << bit)) | ((value & 1) << bit);
				avr_ioctl(avr, AVR_IOCTL_IOPORT_SET_EXTERNAL(target[0]), &external[port]);
				avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(target[0]), bit), value);
			}
			else
				avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ(target[0]), UART_IRQ_INPUT), value);
			end_us = t;
			have = fscanf(st, "%lf %3s %7s %d", &t, kind, target, &value) == 4;
		}
		if (!have && now_us > end_us + 300000) break;   // run 300ms past the last input
		int s = avr_run(avr);
		if (s == cpu_Done || s == cpu_Crashed) { fprintf(stderr, "cpu state %d\n", s); return 1; }
	}
	return 0;
}
