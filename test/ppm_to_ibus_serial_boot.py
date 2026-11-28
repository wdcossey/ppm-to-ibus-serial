# PPM to iBus serial: boot check and status LED. Usage: ./run.sh  (or: python3 ppm_to_ibus_serial_boot.py <firmware.elf> <debug firmware.elf>)
import sys
from ppmsim import frames, simulate, decode, START, LED_PIN

ELF, DEBUG_ELF = sys.argv[1], sys.argv[2]
ok = True
def check(name, cond, info=""):
    global ok
    ok &= bool(cond)
    print(("PASS " if cond else "FAIL ") + name + (f"  [{info}]" if info else ""))

def led_state(changes, t_ms):
    """LED level at time t_ms (ms)."""
    level = 0
    for t, l in changes:
        if t > t_ms:
            break
        level = l
    return level

def toggles(changes, a_ms, b_ms):
    return sum(1 for t, _ in changes if a_ms <= t < b_ms)

base = [1000, 1250, 1500, 1750, 2000, 1100, 1600, 1900]
pad = [1500] * 6

# 1. Normal radio: nothing sent during the boot check, then frames; LED flashes fast, then solid,
#    then flashes slowly after signal loss
e = frames([base] * 150, warmup=False)
e.append((e[-1][0] + 1_500_000, 0))   # no edge, keeps the simulation running after the signal stops
uart, pins = simulate(ELF, e, [LED_PIN])
out = decode(uart)
led = pins[LED_PIN]
first = out[0][0] if out else None
check("normal: first frame after the boot check", first and START / 1000 + 2000 <= first <= START / 1000 + 2400, f"first frame at {first}ms")
check("normal: values", out and all(v == base + pad for _, v in out), out[0][1] if out else None)
check("led: fast flash during boot check", 15 <= toggles(led, 200, 2000) <= 20, toggles(led, 200, 2000))
check("led: solid while receiving", toggles(led, 2600, 3300) == 0 and led_state(led, 3000) == 1)
end_ms = START / 1000 + 150 * 22.5
check("led: off right after signal loss", led_state(led, end_ms + 150) == 0)
check("led: slow flash after signal loss", 2 <= toggles(led, end_ms + 200, end_ms + 1300) <= 3, toggles(led, end_ms + 200, end_ms + 1300))

# 2. Radio switched on late: waits (flashing), checks once pulses arrive, then works
e = frames([base] * 150, start=3_000_000, warmup=False)
uart, pins = simulate(ELF, e, [LED_PIN])
out = decode(uart)
check("late radio: waits for the signal", out and out[0][0] >= 5000, out[0][0] if out else None)
check("late radio: flashes while waiting", toggles(pins[LED_PIN], 500, 2900) >= 20)
check("late radio: values", out and all(v == base + pad for _, v in out))

# 3. Radio with a frame gap shorter than the default 2500us sync threshold:
#    the boot check adapts, so it decodes, even once a stick reaches full travel
short = [1500, 1500, 1000, 1500, 1200, 1800, 1500, 1500, 1500, 1500]
full = [2050] + short[1:]
e = frames([short] * 150 + [full] * 100, period=0, min_gap=2_200, warmup=False)
out = decode(simulate(ELF, e)[0])
check("short gap: decoded after boot check", len(out) > 100 and all(v[:10] in (short, full) for _, v in out), f"{len(out)} frames")
check("short gap: stick at full travel still decodes", sum(v[:10] == full for _, v in out) >= 95, sum(v[:10] == full for _, v in out))

# 4. Channels longer than the default 2400us limit decode too
wide = [2450, 1500, 1000, 2000, 1500, 1500]
e = frames([wide] * 150, warmup=False)
out = decode(simulate(ELF, e)[0])
check("wide channels: decoded after boot check", len(out) > 30 and all(v[:6] == wide for _, v in out), f"{len(out)} frames")

# 5. Garbage for a while before the real signal (receiver powering up): still finds the layout
import random
rnd = random.Random(2)
t, e = START, []
while t < 1_000_000:
    t += rnd.uniform(50, 900); e.append((t, 1)); t += 30; e.append((t, 0))
e += frames([base] * 150, start=1_000_000, warmup=False)
out = decode(simulate(ELF, sorted(e))[0])
check("garbage first: decodes", len(out) > 50 and all(v == base + pad for _, v in out), f"{len(out)} frames")

# 6. Debug build reports what it found
text = "".join(chr(b) for _, b in simulate(DEBUG_ELF, frames([short] * 300, period=0, min_gap=2_200, warmup=False))[0])
lines = [l for l in text.splitlines() if l.startswith("Boot check")]
check("debug: boot check result printed", lines and "10 ch" in lines[0] and "gaps from 2200us" in lines[0], lines[:1])
check("debug: checking message during boot check", "Checking PPM signal..." in text)

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
