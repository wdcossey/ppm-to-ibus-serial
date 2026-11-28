# PPM to iBus serial: DEBUG_OUTPUT 2 (raw timing). Usage: ./run.sh  (or: python3 ppm_to_ibus_serial_raw.py <debug firmware.elf>)
import sys
from ppmsim import frames, run_text

ELF = sys.argv[1]
ok = True
def check(name, cond, info=""):
    global ok
    ok &= bool(cond)
    print(("PASS " if cond else "FAIL ") + name + (f"  [{info}]" if info else ""))

def raw_lines(text):
    return [l.split()[1:] for l in text.splitlines() if l.startswith("raw:")]

# 1. Normal radio: raw widths match the channels, the frame gap is bracketed
base = [1000, 1250, 1500, 1750, 2000, 1100, 1600, 1900]
text = run_text(ELF, frames([base] * 60))
raws = [r for r in raw_lines(text) if r != ["no", "edges"]]
last = raws[-1] if raws else []
gaps = [i for i, v in enumerate(last) if v.startswith("[")]
check("normal: frame gaps marked", len(gaps) >= 2, last)
if len(gaps) >= 2:
    widths = [int(v) for v in last[gaps[-2] + 1:gaps[-1]]]
    check("normal: raw widths", len(widths) == 8 and all(abs(a - b) <= 1 for a, b in zip(widths, base)), widths)
check("normal: values still decoded", "8 ch: 1000 1250 1500 1750 2000 1100 1600 1900" in text)

# 2. Radio with a short frame gap: the boot check adapts and the raw output brackets the gap
ten = [2000] * 10
text = run_text(ELF, frames([ten] * 200, period=0, min_gap=2_200))
raws = [r for r in raw_lines(text) if r != ["no", "edges"]]
check("short gap: decoded", "10 ch: 2000" in text)
check("short gap: gap marked in raw output", raws and "[2200]" in raws[-1], " ".join(raws[-1][:12]) if raws else None)

# 3. A signal the boot check can't make sense of (a 600us "channel"): the fixed limits are used
#    after the timeout and the raw output marks the invalid width with "?"
odd = [1500, 1500, 600, 1500, 1500, 1500]
text = run_text(ELF, frames([odd] * 300, warmup=False))
raws = [r for r in raw_lines(text) if r != ["no", "edges"]]
check("odd signal: boot check gives up", "Boot check: no PPM frames found" in text)
check("odd signal: invalid width marked", raws and "600?" in raws[-1], " ".join(raws[-1][:8]) if raws else None)

# 4. No signal at all
text = run_text(ELF, [(1_500_000, 0)])
check("no signal: reported", raw_lines(text) and all(r == ["no", "edges"] for r in raw_lines(text)), raw_lines(text)[:2])

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
