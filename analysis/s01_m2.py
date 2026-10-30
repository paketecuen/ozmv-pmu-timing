"""M2 for one pass: per-window delay of the board's VA labels against the PPS-referenced 8588A.

The paper's functions (burst_time_base, board_delay), the board's samples restricted to GPS-confirmed records,
one or more board files (a pass may straddle two recordings). Per-window results go to a CSV with their UTC.

usage: python3 analysis/s01_m2.py PASS_DIR LAG_NS OUT.csv BOARD.ogg GPS_TIME.csv [BOARD2.ogg GPS_TIME2.csv]"""
import json, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
from m2_fluke_phase import burst_time_base, board_delay  # noqa: E402
from m1_pps_delay import load  # noqa: E402

pass_dir, lag, out = Path(sys.argv[1]), float(sys.argv[2]) * 1e-9, sys.argv[3]
boards = [load(sys.argv[i], sys.argv[i + 1], 0) for i in range(4, len(sys.argv), 2)]
epoch = boards[0][2] // 1_000_000_000
# all board samples on one time axis, relative to the first file's whole second
tb = np.concatenate([t + (b // 1_000_000_000 - epoch) for t, _, b in boards])
vb = np.concatenate([v for _, v, _ in boards])
order = np.argsort(tb); tb, vb = tb[order], vb[order]
rows, resid_all, gaps = [], [], 0
for csv in sorted(pass_dir.glob("burst-*.csv")):
    d = np.loadtxt(csv, delimiter=",", skiprows=1)
    meta = json.loads(csv.with_suffix(".json").read_text())
    k, mv, sv = d[:, 0], d[:, 1], d[:, 2]
    ts, ta = meta["ts_s"], meta["ta_s"]
    a, b, resid, pos, n_gap = burst_time_base(sv, ts, ta, meta["host_utc_start_ns"], epoch)
    gaps += n_gap; resid_all += list(resid)
    host_off = (meta["host_utc_start_ns"] - epoch * 1_000_000_000) * 1e-9 - a   # PC clock minus the PPS-derived start
    t_master = a + b * k - lag + ta / 2
    # restrict the board to this burst's span (faster), then the paper's 0.2 s windows
    lo, hi = np.searchsorted(tb, [t_master[0] - 1, t_master[-1] + 1])
    dl = board_delay(t_master, mv, tb[lo:hi], vb[lo:hi])
    w0 = np.arange(t_master[0], t_master[-1] - 0.2, 0.2)
    rows.append((csv.stem, a, b, len(pos), float(np.sqrt(np.mean(resid ** 2))) if len(resid) else np.nan, host_off, dl))
alld = np.concatenate([r[6] for r in rows if r[6].size])
with open(out, "w") as f:
    f.write("burst,utc_start_s_rel,windows,mean_delay_ns,std_ns,pps_edges,timebase_resid_ns,pc_clock_minus_pps_ms,fluke_rate_ppm\n")
    for name, a, b, ne, rr, ho, dl in rows:
        f.write("%s,%.6f,%d,%s,%s,%d,%.1f,%.1f,%.3f\n" % (name, a, dl.size, "%.1f" % (dl.mean() * 1e9) if dl.size else "",
                "%.1f" % (dl.std() * 1e9) if dl.size > 1 else "", ne, rr * 1e9, ho * 1e3, (b / 5e-5 - 1) * 1e6))
used = [r for r in rows if r[6].size]
bm = np.array([r[6].mean() for r in used])
print(json.dumps({"pass": pass_dir.name, "lag_ns": lag * 1e9, "bursts": len(rows), "bursts_with_board": len(used),
                  "windows": int(alld.size), "board_delay_ns": alld.mean() * 1e9,
                  "board_delay_u_ns_windows": alld.std(ddof=1) / np.sqrt(alld.size) * 1e9,
                  "board_delay_u_ns_bursts": bm.std(ddof=1) / np.sqrt(bm.size) * 1e9 if bm.size > 1 else None,
                  "median_ns": float(np.median(alld)) * 1e9, "per_window_rms_ns": alld.std() * 1e9,
                  "burst_mean_spread_ns": bm.std(ddof=1) * 1e9 if bm.size > 1 else None,
                  "edges_in_gap_dropped": gaps, "timebase_resid_rms_ns": float(np.sqrt(np.mean(np.array(resid_all) ** 2))) * 1e9,
                  "pc_clock_minus_pps_ms_range": [min(r[5] for r in rows) * 1e3, max(r[5] for r in rows) * 1e3],
                  "epoch": int(epoch)}, indent=1))
