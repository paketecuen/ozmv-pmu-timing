"""M3 for every pass, with the paper's edge functions; the whole-reading shift (ACO trigger output) estimated.

usage: python3 analysis/s01_m3.py FLUKE_DIR PASS..."""
import json, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
from m2_fluke_phase import edge_positions, clean_edges, pair_edges  # noqa: E402

def load(path):
    d = np.loadtxt(path, delimiter=",", skiprows=1)
    meta = json.loads(Path(path).with_suffix(".json").read_text())
    return d[:, 1], d[:, 2], meta

root = Path(sys.argv[1])
for name in sys.argv[2:]:
    lags, dropped, shifts, per_burst = [], 0, set(), []
    for csv in sorted((root / name).glob("burst-*.csv")):
        mv, sv, meta = load(csv)
        ts, ta = meta["ts_s"], meta["ta_s"]
        pm, hm = edge_positions(mv, ts, ta); ps, hs = edge_positions(sv, ts, ta)
        pm, _ = clean_edges(pm, hm, ts); ps, _ = clean_edges(ps, hs, ts)
        # whole readings by which the slave sees the edges earlier: the median of nearest differences
        if pm.size and ps.size:
            near = np.array([ps[np.argmin(np.abs(ps - p))] for p in pm])
            shift = int(np.round(np.median(pm - near)))
        else:
            shift = 0
        shifts.add(shift)
        pairs = pair_edges(pm, ps + shift)
        d = (pairs[:, 0] - (pairs[:, 1] - shift)) * ts
        lags += list(d); dropped += pm.size + ps.size - 2 * pairs.shape[0]
        if d.size: per_burst.append(d.mean())
    lags = np.array(lags)
    print("%-22s %s range %s/%s trig %-4s: %4d edges, %3d dropped, shift %s, lag %9.1f ns +/- %5.1f, per edge %5.0f ns RMS, burst means spread %5.0f ns" % (
        name, "", meta.get("master_range"), meta.get("slave_range"), meta.get("trig_out", "ACO"), lags.size, dropped, sorted(shifts),
        lags.mean() * 1e9, lags.std(ddof=1) / np.sqrt(lags.size) * 1e9, lags.std(ddof=1) * 1e9, np.std(per_burst, ddof=1) * 1e9 if len(per_burst) > 1 else 0))
