import os, random, subprocess, tempfile

HARNESS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "harness")
# Board being simulated, see configure()
MCU = "atmega328p"
PPM_PIN = "B0"       # D8 on the Uno
LED_PIN = "B5"       # D13 on the Uno
UART = "0"           # Serial on the Uno


def configure(mcu, ppm_pin, led_pin, uart):
    """Selects the simulated board, e.g. configure("atmega32u4", "D4", "C7", "1") for a Leonardo."""
    global MCU, PPM_PIN, LED_PIN, UART
    MCU, PPM_PIN, LED_PIN, UART = mcu, ppm_pin, led_pin, uart
PULSE = 300.0

START = 50_000.0     # first PPM pulse (us after power-on)
BOOT = 2_500_000.0   # signal fed before the test sequence, so the boot check has finished
T0 = START + BOOT    # the test sequence starts here (us)

def frames(seq, start=START, period=22_500.0, jitter=0.0, inverted=False, seed=1, min_gap=4000.0, warmup=True):
    """seq: list of channel lists (None = no signal for one period). Returns [(t_us, level)].
    With warmup, the first frame of seq is repeated from `start` until T0, then seq starts at T0."""
    rnd = random.Random(seed)
    edges, t0 = [], start
    hi, lo = (0, 1) if inverted else (1, 0)
    if inverted:
        edges.append((t0 - 1000, 1))

    def frame(ch):
        nonlocal t0
        starts, t = [t0], t0
        for w in ch:
            t += w
            starts.append(t)
        for s in starts:
            s += rnd.uniform(-jitter, jitter)
            edges.append((s, hi)); edges.append((s + PULSE, lo))
        t0 = max(t0 + period, starts[-1] + min_gap)

    if warmup:
        first = next(c for c in seq if c is not None)
        while t0 < T0 - 25_000:
            frame(first)
        t0 = max(t0, T0)
    for ch in seq:
        if ch is None:
            t0 += max(period, 20_000)
        else:
            frame(ch)
    return sorted(edges)

def simulate(elf, edges, watch=(), uart_in=()):
    """Runs the firmware with PPM `edges` [(t_us, level)] and bytes `uart_in` [(t_us, byte)] sent to its UART.
    Returns (uart bytes [(t_ms, byte)], pin changes {pin: [(t_ms, level)]})."""
    events = [(t, f"P {PPM_PIN} {l}") for t, l in edges] + [(t, f"U {UART} {b}") for t, b in uart_in]
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        for t, e in sorted(events, key=lambda x: x[0]):
            f.write(f"{t:.3f} {e}\n")
        name = f.name
    out = subprocess.run([HARNESS, MCU, elf, name, *watch], capture_output=True, text=True, check=True).stdout
    os.unlink(name)
    uart, pins = [], {p: [] for p in watch}
    for p in (ln.split() for ln in out.splitlines()):
        if p and p[0] == "U" + UART:
            uart.append((float(p[1]), int(p[2])))
        elif p and p[0][0] == "P" and p[0][1:] in pins:
            pins[p[0][1:]].append((float(p[1]), int(p[2])))
    return uart, pins

def run(elf, edges, since=T0):
    """Decoded iBus frames [(t_ms, [14 values])] sent after `since` (us)."""
    return [f for f in decode(simulate(elf, edges)[0]) if f[0] >= since / 1000]

def run_text(elf, edges):
    """Serial output as text (for the DEBUG_OUTPUT builds)."""
    return "".join(chr(b) for _, b in simulate(elf, edges)[0])


def text_lines(uart):
    """Splits serial output into [(t_ms at the end of the line, line)]."""
    res, cur = [], ""
    for t, b in uart:
        if b == 10:
            if cur.strip():
                res.append((t, cur.strip()))
            cur = ""
        else:
            cur += chr(b)
    return res

def decode(data):
    """Parse iBus frames; returns list of (t_ms, [14 values]); raises on bad checksum/garbage."""
    res, i, bad = [], 0, 0
    while i + 32 <= len(data):
        b = [x[1] for x in data[i:i+32]]
        if b[0] == 0x20 and b[1] == 0x40:
            chk = (0xFFFF - sum(b[:30])) & 0xFFFF
            if chk == b[30] | (b[31] << 8):
                res.append((data[i][0], [b[2+2*k] | (b[3+2*k] << 8) for k in range(14)]))
                i += 32
                continue
        bad += 1; i += 1
    if bad or i != len(data):
        raise AssertionError(f"{bad} garbage bytes, {len(data)-i} trailing")
    return res
