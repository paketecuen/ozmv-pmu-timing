"""M4 from simultaneous recordings: per UTC second, VA's phase on A and on B by their own labels.

delta = (phi_A - phi_B) / (2 pi f) is how much later B's labels see the same voltage than A's
(delay_B - delay_A in M2's sense). Only GPS-confirmed records (flags 267), whole seconds without holes.

usage: python3 analysis/s01_ab_wmu.py SESSION_DIR TIME_DIR OUT.csv [DATE]   (DATE 20261027 by default)   (TIME_DIR holds <board>-<stamp>-time.csv from batch_wmu.py)"""
import csv, subprocess, sys
from pathlib import Path
import numpy as np

session, tdir, out = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
date = sys.argv[4] if len(sys.argv) > 4 else "20261027"
FS = 1e9 / 31744.0

def labelled(board, ogg):
    """(UTC second of the first record, per-sample label in s relative to it, VA in volts) for GPS records."""
    stamp = ogg.stem[4:]
    rows = [r for r in csv.DictReader(open(tdir / f"{board}-{stamp}-time.csv")) if r["flags"] == "267"]
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(ogg), "-map", "0:a:0", "-f", "s32le", "-acodec", "pcm_s32le", "-"],
                         check=True, capture_output=True).stdout
    va = np.frombuffer(raw, dtype="<i4").reshape(-1, 7)[:, 0]
    base = int(rows[0]["first_utc_ns"]) // 10**9
    t, v = [], []
    for r in rows:
        a, n = int(r["first_sample"]), int(r["count"])
        if a + n > va.size:
            break
        u0, u1 = int(r["first_utc_ns"]), int(r["last_utc_ns"])
        i = np.arange(n)
        t.append(((u0 - base * 10**9) + i * (u1 - u0) / (n - 1)) * 1e-9)
        v.append(va[a:a + n] / 256 / 1e4)
    return base, np.concatenate(t), np.concatenate(v)

def span(board, ogg):
    rows = [r for r in csv.DictReader(open(tdir / f"{board}-{ogg.stem[4:]}-time.csv")) if r["flags"] == "267"]
    return (int(rows[0]["first_utc_ns"]) // 10**9, int(rows[-1]["last_utc_ns"]) // 10**9) if rows else None

files = {b: [(p, span(b, p)) for p in sorted((session / b).glob(f"wmu-{date}T*.ogg"))] for b in "AB"}
results = []
for fa, sa in files["A"]:
    if not sa:
        continue
    for fb, sb in files["B"]:
        if not sb or sb[1] < sa[0] or sb[0] > sa[1]:
            continue
        lo, hi = max(sa[0], sb[0]) + 1, min(sa[1], sb[1]) - 1
        if hi - lo < 10:
            continue
        ba, ta, va = labelled("A", fa)
        bb, tb, vb = labelled("B", fb)
        tb = tb + (bb - ba)                       # both relative to A's whole second
        for s in range(lo - ba, hi - ba):
            ia = np.searchsorted(ta, [s, s + 1]); ib = np.searchsorted(tb, [s, s + 1])
            if ia[1] - ia[0] < 0.999 * FS or ib[1] - ib[0] < 0.999 * FS:
                continue
            x, y = ta[ia[0]:ia[1]] - s, va[ia[0]:ia[1]]
            # frequency from A by a short Gauss-Newton on a 4-parameter sine
            f = 50.0
            for _ in range(6):
                M = np.column_stack([np.cos(2 * np.pi * f * x), np.sin(2 * np.pi * f * x), np.ones_like(x)])
                c, *_ = np.linalg.lstsq(M, y, rcond=None)
                A, ph = np.hypot(c[0], c[1]), np.arctan2(-c[1], c[0])
                r = y - M @ c
                d = -A * 2 * np.pi * x * np.sin(2 * np.pi * f * x + ph)
                f += np.dot(d, r - r.mean()) / np.dot(d, d)
            def phase(xx, yy):
                M = np.column_stack([np.cos(2 * np.pi * f * xx), np.sin(2 * np.pi * f * xx), np.ones_like(xx)])
                c, *_ = np.linalg.lstsq(M, yy, rcond=None)
                return np.hypot(c[0], c[1]), np.arctan2(-c[1], c[0])
            amp_a, pa = phase(x, y)
            amp_b, pb = phase(tb[ib[0]:ib[1]] - s, vb[ib[0]:ib[1]])
            delta = np.angle(np.exp(1j * (pa - pb))) / (2 * np.pi * f)
            results.append((ba + s, f, amp_a / np.sqrt(2), amp_b / np.sqrt(2), delta * 1e9))
        print(f"{fa.name} x {fb.name}: {sum(1 for r in results if lo <= r[0] < hi)} seconds", flush=True)
with open(out, "w") as fo:
    fo.write("utc_s,freq_hz,va_rms_A,va_rms_B,b_minus_a_ns\n")
    for r in sorted(results):
        fo.write("%d,%.6f,%.3f,%.3f,%.1f\n" % r)
d = np.array([r[4] for r in results])
print("seconds %d: B minus A %.1f ns mean, median %.1f, RMS about mean %.1f, p2.5..p97.5 %.1f..%.1f" % (
    d.size, d.mean(), np.median(d), d.std(), np.percentile(d, 2.5), np.percentile(d, 97.5)))
