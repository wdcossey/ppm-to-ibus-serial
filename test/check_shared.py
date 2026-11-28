# Checks that the code blocks shared between sketches are identical.
# Usage: python3 check_shared.py  (run.sh runs it)
import os, re, sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
blocks = defaultdict(dict)
for folder in sorted(os.listdir(ROOT)):
    path = os.path.join(ROOT, folder, folder + ".ino")
    if not os.path.isfile(path):
        continue
    text = open(path).read()
    for m in re.finditer(r"// ----- BEGIN SHARED: (\S+) -----\n(.*?)// ----- END SHARED: \1 -----", text, re.S):
        blocks[m.group(1)][folder] = m.group(2)

ok = bool(blocks)
for name, copies in sorted(blocks.items()):
    texts = set(copies.values())
    same = len(copies) >= 2 and len(texts) == 1
    ok &= same
    print(("PASS " if same else "FAIL ") + f"shared block '{name}' identical in {', '.join(sorted(copies))}")
print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
