# Usage: ./run.sh  (or: python3 tests.py <firmware.elf>)
import sys, random
from ppmsim import frames, run

ELF = sys.argv[1]
TH = 4  # PPM_JITTER_THRESHOLD
ok = True
def check(name, cond, info=""):
    global ok
    ok &= bool(cond)
    print(("PASS " if cond else "FAIL ") + name + (f"  [{info}]" if info else ""))

base = [1000, 1250, 1500, 1750, 2000, 1100, 1600, 1900]
pad = [1500] * 6

# 1. Clean signal: exact values, padding, one iBus frame per PPM frame
out = run(ELF, frames([base] * 20))
check("clean: frames produced", len(out) >= 17, f"{len(out)} frames for 20 PPM frames")
check("clean: exact values + padding", all(v == base + pad for _, v in out), out[0][1] if out else None)
gaps = [b[0] - a[0] for a, b in zip(out, out[1:])]
check("clean: one frame per PPM period", gaps and all(abs(g - 22.5) < 0.5 for g in gaps), f"{min(gaps):.2f}-{max(gaps):.2f} ms")

# 2. Inverted polarity
out = run(ELF, frames([base] * 10, inverted=True))
check("inverted: exact values", len(out) >= 7 and all(v == base + pad for _, v in out), len(out))

# 3. Receiver jitter. +/-1.5us per edge (+/-3us per width) must be held perfectly steady;
#    +/-3us per edge (+/-6us per width) should at least be much calmer than the raw signal.
out = run(ELF, frames([base] * 80, jitter=1.5))
vals = [v[:8] for _, v in out[20:]]  # allow a short settling period
err = max(abs(a - b) for a, b in zip(vals[0], base))
check("jitter 1.5us: output steady", all(v == vals[0] for v in vals), f"distinct={len(set(map(tuple, vals)))}")
check("jitter 1.5us: within threshold", err <= TH + 3, f"max err {err}us")
out = run(ELF, frames([base] * 100, jitter=3.0, seed=7))
vals = [v[:8] for _, v in out[5:]]
changes = sum(a[c] != b[c] for a, b in zip(vals, vals[1:]) for c in range(8))
err = max(abs(v[c] - base[c]) for v in vals for c in range(8))
check("jitter 3us: few value changes", changes < len(vals) * 8 * 0.1, f"{changes} changes in {len(vals)*8} channel-frames, max err {err}us")

# 4. Moving stick still tracks (ramp ch1 1000 -> 2000)
ramp = [[1000 + 20 * i] + base[1:] for i in range(51)]
out = run(ELF, frames(ramp + [ramp[-1]] * 5, jitter=2.0))
check("ramp: reaches end value", abs(out[-1][1][0] - 2000) <= TH + 3, out[-1][1][0])
mono = all(b[1][0] >= a[1][0] for a, b in zip(out, out[1:]))
check("ramp: monotonic", mono)

# 5. Wide channels (>2100us, extended travel) decode
wide = [2250, 800, 1500, 1500, 2200, 1500]
out = run(ELF, frames([wide] * 10))
check("wide: >2100us channels", out and all(v[:6] == wide for _, v in out), out[0][1][:6] if out else None)

# 6. Glitch (spurious extra pulse) mid-stream: no bad values, recovers
e = frames([base] * 30)
e += [(50_000 + 22_500 * 10 + 2_600, 1), (50_000 + 22_500 * 10 + 2_650, 0)]
out = run(ELF, sorted(e))
check("glitch: never outputs bad values", all(v == base + pad for _, v in out))
check("glitch: recovers", len(out) >= 25 and out[-1][0] > 50 + 22.5 * 29, len(out))

# 7. Missed edge mid-stream
e = frames([base] * 30)
drop = 50_000 + 22_500 * 12 + sum(base[:3])
e = [x for x in e if abs(x[0] - drop) > 1 and abs(x[0] - drop - 300) > 1]
out = run(ELF, e)
check("missed edge: never outputs bad values", all(v == base + pad for _, v in out))
check("missed edge: recovers", out[-1][0] > 50 + 22.5 * 29)

# 8. Signal loss: output stops after timeout, resumes afterwards
out = run(ELF, frames([base] * 10 + [None] * 20 + [base] * 10))
ts = [t for t, _ in out]
biggest = max(b - a for a, b in zip(ts, ts[1:]))
check("loss: transmission paused", biggest > 400, f"gap {biggest:.0f}ms")
check("loss: resumes with right values", ts[-1] > 50 + 22.5 * 39 - 1 and out[-1][1] == base + pad)

# 9. Channel count change 8 -> 6
six = base[:6]
out = run(ELF, frames([base] * 10 + [six] * 10))
check("count change: 6 ch padded", out[-1][1] == six + [1500] * 8, out[-1][1])

# 10. Too many channels (16) -> ignored, 14 channels -> works
out = run(ELF, frames([[1500] * 16] * 10, period=40_000))
check("16 ch: ignored", len(out) == 0, len(out))
fourteen = [1000 + 50 * i for i in range(14)]
out = run(ELF, frames([fourteen] * 10, period=30_000))
check("14 ch: decoded", out and out[-1][1] == fourteen, out[-1][1] if out else None)

# 11. Noise / garbage only -> nothing sent
rnd = random.Random(5)
t, e = 50_000.0, []
for _ in range(400):
    t += rnd.uniform(5, 2000); e.append((t, 1)); t += rnd.uniform(5, 300); e.append((t, 0))
out = run(ELF, e)
check("noise: nothing sent", len(out) == 0, len(out))

print("ALL PASS" if ok else "SOME FAILED")
