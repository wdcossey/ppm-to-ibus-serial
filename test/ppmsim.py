import random, subprocess, os, tempfile, sys

HARNESS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "harness")
PULSE = 300.0

def frames(seq, start=50_000.0, period=22_500.0, jitter=0.0, inverted=False, seed=1):
    """seq: list of channel lists (None = no signal for one period). Returns edge list [(t_us, level)]."""
    rnd = random.Random(seed)
    edges, t0 = [], start
    hi, lo = (0, 1) if inverted else (1, 0)
    if inverted:
        edges.append((t0 - 1000, 1))
    for ch in seq:
        if ch is None:
            t0 += period
            continue
        t = t0
        starts = [t]
        for w in ch:
            t += w
            starts.append(t)
        for s in starts:
            s += rnd.uniform(-jitter, jitter)
            edges.append((s, hi)); edges.append((s + PULSE, lo))
        t0 = max(t0 + period, starts[-1] + 4000)
    return sorted(edges)

def run(elf, edges):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        for t, l in edges:
            f.write(f"{t:.3f} {l}\n")
        name = f.name
    out = subprocess.run([HARNESS, elf, name], capture_output=True, text=True, check=True).stdout
    os.unlink(name)
    data = [(float(p[1]), int(p[2])) for p in (ln.split() for ln in out.splitlines()) if p and p[0] == "U"]
    return decode(data)

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
