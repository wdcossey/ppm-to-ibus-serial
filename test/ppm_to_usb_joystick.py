# PPM to USB joystick (Leonardo): the values sent to the PC, taken from the debug output
# (redirected to Serial1 for the test build). Usage: ./run.sh  (or: python3 ppm_to_usb_joystick.py <debug firmware.elf>)
import random, sys
import ppmsim
from ppmsim import frames, simulate, text_lines, T0, PULSE

ppmsim.configure("atmega32u4", ppm_pin="D4", led_pin="C7", uart="1")
ELF = sys.argv[1]
TH = 4  # PPM_JITTER_THRESHOLD

def run(edges, since=T0):
    """Debug lines after `since` (us): 'No signal' or (channels, [X Y Z Rx Ry Rz], 'bXXXX')."""
    res = []
    for t, line in text_lines(simulate(ELF, edges)[0]):
        if t < since / 1000 or line.startswith("Boot check"):
            continue
        if line == "No signal":
            res.append(line)
        else:
            p = line.split()
            res.append((int(p[0][:-2]), [int(x) for x in p[1:7]], p[7]))
    return res

def axes(ch):
    """Expected X Y Z Rx Ry Rz for a channel list (X=1 Y=2 Z=5 Rx=3 Ry=4 Rz=6)."""
    g = lambda n: ch[n - 1] if n <= len(ch) else 1500
    return [g(1), g(2), g(5), g(3), g(4), g(6)]

def buttons(ch):
    return "b" + "".join("1" if n <= len(ch) and ch[n - 1] > 1750 else "0" for n in range(7, 11))

def data(out):
    return [o for o in out if o != "No signal"]

ok = True
def check(name, cond, info=""):
    global ok
    ok &= bool(cond)
    print(("PASS " if cond else "FAIL ") + name + (f"  [{info}]" if info else ""))

base = [1000, 1250, 1500, 1750, 2000, 1100, 2000, 1000]
EXP = (8, axes(base), buttons(base))

# 1. Clean signal
d = data(run(frames([base] * 20)))
check("clean: values, mapping, buttons", d and all(o == EXP for o in d), d[0] if d else None)
check("clean: one update per frame", len(d) >= 19, f"{len(d)} updates for 20 frames")

# 2. Inverted PPM
d = data(run(frames([base] * 10, inverted=True)))
check("inverted: values", len(d) >= 9 and all(o == EXP for o in d), len(d))

# 3. Jitter: (almost) steady
d = data(run(frames([base] * 150, jitter=1.5)))
changes = [i for i in range(1, len(d)) if d[i] != d[i - 1]]
err = max(abs(a - b) for a, b in zip(d[-1][1], EXP[1]))
check("jitter 1.5us: (almost) steady", len(changes) <= 3, f"{len(changes)} changes in {len(d)} frames")
check("jitter 1.5us: accurate", err <= TH + 3, f"max err {err}us")

# 4. Ramp tracks
ramp = [[1000 + 20 * i] + base[1:] for i in range(51)]
d = data(run(frames(ramp + [ramp[-1]] * 5, jitter=2.0)))
check("ramp: reaches end", abs(d[-1][1][0] - 2000) <= TH + 3, d[-1][1][0])
check("ramp: monotonic", all(b[1][0] >= a[1][0] for a, b in zip(d, d[1:])))

# 5. Extended travel (>2100us)
wide = [2250, 800, 1500, 1500, 2200, 1500]
d = data(run(frames([wide] * 10)))
check("wide: decoded", d and all(o == (6, axes(wide), buttons(wide)) for o in d), d[0] if d else None)

# 6. Glitch and missed edge
e = frames([base] * 30) + [(T0 + 22_500 * 10 + 2_600, 1), (T0 + 22_500 * 10 + 2_650, 0)]
d = data(run(sorted(e)))
check("glitch: never bad values", all(o == EXP for o in d))
check("glitch: recovers", len(d) >= 25, len(d))
drop = T0 + 22_500 * 12 + sum(base[:3])
e = [x for x in frames([base] * 30) if abs(x[0] - drop) > 1 and abs(x[0] - drop - PULSE) > 1]
d = data(run(e))
check("missed edge: never bad values", all(o == EXP for o in d))
check("missed edge: recovers", len(d) >= 25, len(d))

# 7. Signal loss centers, then recovers
out = run(frames([base] * 10 + [None] * 20 + [base] * 10))
# (every run ends with a real signal loss: the simulation continues after the last pulse)
mid = [i for i, o in enumerate(out[:-1]) if o == "No signal"]
check("loss: centers mid-stream", len(mid) == 1 and mid[0] > 0, mid)
check("loss: recovers", out[-2] == EXP and out[-1] == "No signal")

# 8. Channel count change: 8 -> 6, missing channels' buttons released
six = [1000, 1250, 1500, 1750, 2000, 1100]
d = data(run(frames([base] * 10 + [six] * 10)))
check("count change: 6 ch", d[-1] == (6, axes(six), "b0000"), d[-1])

# 9. 14 channels
fourteen = [1000 + 50 * i for i in range(14)]
d = data(run(frames([fourteen] * 10, period=30_000)))
check("14 ch: decoded", d and d[-1] == (14, axes(fourteen), buttons(fourteen)), d[-1] if d else None)

# 10. Short frame gap radio: decoded thanks to the boot check
short = [1500, 1500, 1000, 1500, 1200, 1800, 1500, 1500, 1500, 1500]
d = data(run(frames([short] * 150, period=0, min_gap=2_200, warmup=False), since=0))
check("short gap: decoded after boot check", len(d) > 20 and all(o == (10, axes(short), buttons(short)) for o in d), len(d))

# 11. Noise only
rnd = random.Random(5)
t, e = 100_000.0, []
for _ in range(400):
    t += rnd.uniform(5, 2000); e.append((t, 1)); t += rnd.uniform(5, 300); e.append((t, 0))
check("noise: nothing decoded", not data(run(e, since=0)))

# 12. LED: fast flash during the boot check, then on (Leonardo LED = PC7)
_, pins = simulate(ELF, frames([base] * 150, warmup=False), [ppmsim.LED_PIN])
led = pins[ppmsim.LED_PIN]
check("led: flashes during boot check", sum(1 for t, _ in led if 200 <= t < 2000) >= 15)
level = lambda t_ms: ([l for t, l in led if t <= t_ms] or [0])[-1]
check("led: on while receiving", level(3000) == 1 and not any(2300 <= t < 3400 for t, _ in led), led[-3:])

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
