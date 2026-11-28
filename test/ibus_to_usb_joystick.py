# iBus to USB joystick (Leonardo): feeds iBus data into Serial1 and checks the values sent to
# the PC, taken from the debug output (redirected to Serial1 TX for the test build).
# Usage: ./run.sh  (or: python3 ibus_to_usb_joystick.py <debug firmware.elf>)
import random, sys
import ppmsim
from ppmsim import simulate, text_lines

ppmsim.configure("atmega32u4", ppm_pin="D4", led_pin="C7", uart="1")
ELF = sys.argv[1]
BYTE_US = 1e6 / 11520          # 10 bits per byte at 115200 baud

def frame(vals):
    v = vals + [1500] * (14 - len(vals))
    b = [0x20, 0x40] + [x for c in v for x in (c & 0xFF, c >> 8)]
    c = (0xFFFF - sum(b)) & 0xFFFF
    return b + [c & 0xFF, c >> 8]

def stream(chunks, start=100_000.0, period=7000.0):
    """chunks: list of byte lists (or None = silence for one period), one per period."""
    out, t = [], start
    for c in chunks:
        if c:
            out += [(t + i * BYTE_US, b) for i, b in enumerate(c)]
        t += period
    return out

def run(uart_in, watch=()):
    uart, pins = simulate(ELF, [], watch, uart_in)
    return [line for _, line in text_lines(uart)], pins

ok = True
def check(name, cond, info=""):
    global ok
    ok &= bool(cond)
    print(("PASS " if cond else "FAIL ") + name + (f"  [{info}]" if info else ""))

ch = [1000, 1100, 1200, 1300, 1400, 1600, 2000, 1000, 2000, 1500]
# X=ch1 Y=ch2 Z=ch5 Rx=ch3 Ry=ch4 Rz=ch6, buttons = ch7..ch10 > 1750
EXPECT = "14ch 1000 1100 1400 1200 1300 1600 b1010"

def data(out):
    return [l for l in out if l != "No signal"]

# 1. Clean stream
out, _ = run(stream([frame(ch)] * 20))
check("clean: mapping and buttons", data(out) and all(l == EXPECT for l in data(out)), data(out)[:1])
check("clean: one update per frame", len(data(out)) == 20, len(data(out)))
check("clean: no signal before and after", out[0] == "No signal" and out[-1] == "No signal")

# 2. Noise between frames, including fake headers
rnd = random.Random(3)
chunks = []
for i in range(20):
    junk = [rnd.randrange(256) for _ in range(rnd.randrange(0, 20))] + ([0x20, 0x40] if i % 3 == 0 else [])
    chunks.append(junk + frame(ch))
out, _ = run(stream(chunks, period=9000))
check("noise: never wrong values", all(l == EXPECT for l in data(out)))
check("noise: most frames decoded", len(data(out)) >= 18, f"{len(data(out))}/20")

# 3. Corrupted / truncated frames are dropped, decoding recovers
chunks = [frame(ch)] * 5
bad = frame([1999] * 10); bad[10] ^= 0x01
chunks += [bad, frame([1999] * 10)[:17], frame(ch)[:5]] + [frame(ch)] * 5
out, _ = run(stream(chunks))
check("corrupt: never wrong values", all(l == EXPECT for l in data(out)))
check("corrupt: all good frames decoded", len(data(out)) == 10, len(data(out)))

# 4. Back-to-back frames with no gap. (Only 5: the test build prints every update on the same
#    115200 baud UART, which can't keep up with a long burst. Real builds print over USB, throttled.)
out, _ = run(stream([frame(ch) * 5]))
check("no gap: all frames decoded", len(data(out)) == 5 and all(l == EXPECT for l in data(out)), len(data(out)))

# 5. Signal loss centers everything, then recovers
out, _ = run(stream([frame(ch)] * 5 + [None] * 30 + [frame(ch)] * 5))
idx = [i for i, l in enumerate(out) if l == "No signal"]
check("loss: reports no signal mid-stream", any(0 < i < len(out) - 1 for i in idx))
check("loss: recovers", len(out) > 1 and out[-2] == EXPECT)

# 6. Upper 4 bits (used by 18 channel receivers) are masked off
out, _ = run(stream([frame([v | 0x5000 for v in ch])] * 3))
check("high bits: masked", data(out) and all(l == EXPECT for l in data(out)), data(out)[:1])

# 7. Garbage only
g = random.Random(9)
out, _ = run([(100_000 + i * BYTE_US, g.randrange(256)) for i in range(2000)])
check("garbage: nothing decoded", not data(out), out[:3])

# 8. LED: fast flash while waiting, on while receiving, slow flash after loss (Leonardo LED = PC7)
data_in = stream([frame(ch)] * 100, start=1_000_000)                  # 1.0s - 1.7s
data_in.append((data_in[-1][0] + 1_500_000, 0x00))                   # a stray byte later keeps the simulation running
_, pins = run(data_in, [ppmsim.LED_PIN])
led = pins[ppmsim.LED_PIN]
toggles = lambda a, b: sum(1 for t, _ in led if a <= t < b)
level = lambda t_ms: ([l for t, l in led if t <= t_ms] or [0])[-1]
check("led: fast flash while waiting", toggles(100, 1000) >= 8, toggles(100, 1000))
check("led: on while receiving", toggles(1010, 1690) == 0 and level(1500) == 1)
check("led: slow flash after loss", level(1850) == 0 and 2 <= toggles(1850, 3000) <= 3, toggles(1850, 3000))

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
