"""Compare Linux stage 1 verifier outputs with the Windows reference (Linux/PLAN.md 4.4).

Usage: python3 -I compare-stage1.py <windows-reference-dir> <linux-evidence-dir>

- Vectors (.f32le) and audio (.wav) must be byte-identical. If not, the largest
  absolute and relative float error is reported so the owner can decide.
- CSV results are compared as text.
- Text reports differ in paths and timings, so only their PASS/FAIL lines are compared.
Exit code: 0 identical, 2 numerical differences, 1 missing output or a FAIL.
"""
import array
import pathlib
import sys

ref_root, new_root = (pathlib.Path(p) for p in sys.argv[1:3])
failures, differences = [], []


def floats(path):
    values = array.array("f")
    values.frombytes(path.read_bytes())
    if sys.byteorder != "little":
        values.byteswap()
    return values


def verdicts(path):
    return [line.strip() for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
            if line.strip().startswith("RESULT=") or line.strip().endswith(("=PASS", "=FAIL"))]


for ref in sorted(p for p in ref_root.rglob("*") if p.is_file()):
    rel = ref.relative_to(ref_root)
    new = new_root / rel
    if not new.is_file():
        failures.append(f"missing {rel}")
        continue
    if ref.suffix in (".f32le", ".wav"):
        if ref.read_bytes() == new.read_bytes():
            continue
        if ref.suffix == ".f32le" and ref.stat().st_size == new.stat().st_size:
            a, b = floats(ref), floats(new)
            worst = max(abs(x - y) for x, y in zip(a, b))
            peak = max(max(abs(x) for x in a), 1e-12)
            differences.append(f"{rel}: max abs {worst:.3e}, relative to peak {worst / peak:.3e}")
        else:
            differences.append(f"{rel}: bytes differ")
    elif ref.suffix == ".csv":
        if ref.read_text(encoding="utf-8") != new.read_text(encoding="utf-8"):
            differences.append(f"{rel}: values differ")
    else:
        old_v, new_v = verdicts(ref), verdicts(new)
        if any(v.endswith("=FAIL") for v in new_v):
            failures.append(f"{rel}: " + ", ".join(v for v in new_v if v.endswith("=FAIL")))
        if old_v != new_v:
            differences.append(f"{rel}: PASS/FAIL lines differ from Windows")

for line in failures:
    print("FAIL", line)
for line in differences:
    print("DIFF", line)
if failures:
    print("RESULT=FAIL")
    sys.exit(1)
if differences:
    print("RESULT=DIFFERENT (owner decision, Linux/PLAN.md 4.4)")
    sys.exit(2)
print("RESULT=IDENTICAL to Windows")
