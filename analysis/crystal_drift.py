"""How fast the bench crystal's frequency changes, from the 10-hour GPS log (data/gps-sky-20261015/timeline.log).

The board measures its conversion period over each 10 s of GPS time; the logger kept it every 2 minutes. For
windows of 10, 30 and 60 minutes, this prints the largest rate of change of the fractional frequency found
between records of one continuous run (no gap over 5 minutes, same boot), and, as a worst case for the holdover
bound of Section V-A, the time error that the largest 30-minute rate would accumulate over one hour.
Run from the paper folder: python3 analysis/crystal_drift.py"""
import re
from pathlib import Path

LOG = Path(__file__).resolve().parents[1] / "data" / "gps-sky-20261015" / "timeline.log"
rows = []
for line in LOG.read_text().splitlines():
    m = re.search(r"periodNs ([\d.]+).*up ([\d.]+)", line)
    if m:
        rows.append((float(m.group(2)), float(m.group(1)), line[:8]))

runs, cur = [], [rows[0]]
for r in rows[1:]:
    if r[0] < cur[-1][0] or r[0] - cur[-1][0] > 300:
        runs.append(cur)
        cur = [r]
    else:
        cur.append(r)
runs.append(cur)

print(f"{len(rows)} records in {sum(1 for r in runs if len(r) > 1)} continuous runs")
for minutes in (10, 30, 60):
    best = (0.0, "")
    for run in runs:
        for i, (u0, p0, t0) in enumerate(run):
            for u1, p1, t1 in run[i + 1:]:
                if u1 - u0 >= 60 * minutes:
                    rate = abs(p1 - p0) / p0 / (u1 - u0) * 1e9
                    if rate > best[0]:
                        best = (rate, f"{t0}-{t1} UTC")
                    break
    print(f"largest |d(rho)/dt| over {minutes:2d} min: {best[0]:.2f} ppb/s ({best[1]})")

for rate_ppb_s in (0.5, 0.68):
    error_ms = 0.5 * rate_ppb_s * 1e-9 * 3600 ** 2 * 1e3
    print(f"an exact period drifting at {rate_ppb_s} ppb/s for one hour: {error_ms:.1f} ms off")
bound_ms = (0.5e-6 * 3600 + 0.5 * 0.5e-9 * 3600 ** 2) * 1e3
print(f"the bound (rho = 0.5 ppm, delta = 0.5 ppb/s, fresh period) after one hour: {bound_ms:.2f} ms")
