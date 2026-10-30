"""PPS edge times on the bare sample grid (index x constant period) vs on the WMU labels.

The ADC's sample period is fixed by its crystal, which wanders slowly; the second difference of the edge
times then isolates the per-second noise: of the measurement plus the receiver's pulse (grid), or of the
measurement, the pulse and the labels (labels).

usage: python3 analysis/s01_grid_vs_labels.py STREAM.s32 TIME_GPS.csv RC_US"""
import csv, sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares
sys.path.insert(0, str(Path(__file__).resolve().parent))
from m1_pps_step_sim import T_MOD_NOM, response  # noqa: E402

x = np.memmap(sys.argv[1], dtype="<i4", mode="r").reshape(-1, 7)
rows = list(csv.DictReader(open(sys.argv[2])))
rc = float(sys.argv[3]) * 1e-6
first = np.array([int(r["first_sample"]) for r in rows]); count = np.array([int(r["count"]) for r in rows])
u0 = np.array([int(r["first_utc_ns"]) for r in rows]); u1 = np.array([int(r["last_utc_ns"]) for r in rows])
# the longest stretch of consecutive GPS records
cont = np.r_[False, first[1:] == first[:-1] + count[:-1]]
starts = np.where(~cont)[0]; ends = np.r_[starts[1:], len(rows)]
a, b = max(zip(starts, ends), key=lambda p: p[1] - p[0])
print("stretch: records %d..%d, %.0f s" % (a, b, (u1[b - 1] - u0[a]) / 1e9))
idx0 = first[a]
# straight line labels vs index over the stretch (records' first samples)
ii = (first[a:b] - idx0).astype(float); ll = (u0[a:b] - u0[a]).astype(float)
P, T0 = np.polyfit(ii, ll, 1)
print("grid period %.4f ns" % P)
base = u0[a]
def fit_edge(tau, v):
    lo = v[(tau > -1.2e-3) & (tau < -0.25e-3)]; hi = v[(tau > 0.35e-3) & (tau < 1.2e-3)]
    step = hi.mean() - lo.mean()
    if lo.size < 10 or hi.size < 10 or step <= 20 * max(lo.std(), hi.std()):
        return np.nan
    win = (tau > -0.12e-3) & (tau < 0.20e-3)
    y = (v[win] - lo.mean()) / step
    return least_squares(lambda p: response(tau[win] - p[0], T_MOD_NOM, rc) - y, [11.9e-6], x_scale=[1e-6]).x[0]
S, eg, el = [], [], []
for s in range(int(u0[a] // 10**9) + 1, int(u1[b - 1] // 10**9)):
    # grid: sample i at base + T0 + (i - idx0) * P
    ic = idx0 + int(round((s * 10**9 - base - T0) / P))
    i = np.arange(ic - 40, ic + 40)
    tg = ((base - s * 10**9) + T0 + (i - idx0) * P) * 1e-9
    k = np.searchsorted(u0, s * 10**9) - 1
    # labels of the same samples, from their records
    rec = np.searchsorted(first, i, side="right") - 1
    if np.any(rec < a) or np.any(rec >= b):
        continue
    tl = ((u0[rec] - s * 10**9) + (i - first[rec]) * (u1[rec] - u0[rec]) / (count[rec] - 1)) * 1e-9
    v = x[i, 6].astype(float)
    S.append(s); eg.append(fit_edge(tg, v)); el.append(fit_edge(tl, v))
S = np.array(S); eg = np.array(eg) * 1e9; el = np.array(el) * 1e9
ok = np.isfinite(eg) & np.isfinite(el) & np.r_[True, np.diff(S) == 1]
for name, e in (("grid", eg), ("labels", el)):
    d2 = e[2:] - 2 * e[1:-1] + e[:-2]
    good = ok[2:] & ok[1:-1] & ok[:-2] & (np.diff(S)[1:] == 1) & (np.diff(S)[:-1] == 1)
    print("%-6s edges %d: second difference RMS/sqrt(6) = %.1f ns (robust %.1f ns)" % (
        name, np.isfinite(e).sum(), d2[good].std() / np.sqrt(6), 1.4826 * np.median(np.abs(d2[good] - np.median(d2[good]))) / np.sqrt(6)))
dl = el - eg
print("labels minus grid: RMS %.1f ns about its mean; second difference RMS/sqrt(6) %.1f ns" % (
    np.nanstd(dl), np.nanstd(dl[2:] - 2 * dl[1:-1] + dl[:-2]) / np.sqrt(6)))
np.savetxt(sys.argv[2].replace(".csv", "-grid-vs-labels.csv"), np.column_stack([S, eg, el]), fmt="%.1f", delimiter=",",
           header="utc_s,edge_on_grid_ns,edge_on_labels_ns", comments="")
