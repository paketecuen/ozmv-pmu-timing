"""Holdover and provisional time against each board's own PPS on IN, from batch_wmu.py's per-second files.

For each run of seconds flagged holdover (32) or with a syncing/system source, the error is the offset minus
the median offset of the GPS seconds within 120 s around it; the declared U (k = 2 or bound) beside it.

usage: python3 analysis/s01_holdover.py BATCH_DIR"""
import csv, sys, datetime
from pathlib import Path
import numpy as np

def hhmmss(s):
    return datetime.datetime.fromtimestamp(s, datetime.timezone.utc).strftime("%H:%M:%S")

episodes = []
for f in sorted(Path(sys.argv[1]).glob("*-per-second.csv")):
    board = f.name[0]
    rows = list(csv.DictReader(open(f)))
    t = np.array([int(r["utc_s"]) for r in rows]); off = np.array([float(r["offset_ns"]) if r["offset_ns"] else np.nan for r in rows])
    fl = np.array([int(r["flags_or"]) for r in rows]); u = np.array([int(r["uncertainty_ns"]) for r in rows])
    gps = fl == 267
    kind = np.where(fl & 32, "holdover", np.where((fl >> 8) == 2, "syncing", np.where(gps, "gps", "other")))
    idx = np.where(kind != "gps")[0]
    for g in np.split(idx, np.where(np.diff(idx) > 1)[0] + 1):
        if g.size == 0:
            continue
        near = gps & (np.abs(t - t[g[0]]) < 120) & np.isfinite(off)
        if near.sum() < 20:
            continue
        ref = np.median(off[near])
        e = off[g] - ref
        k = kind[g]
        for kk in ("holdover", "syncing", "other"):
            m = (k == kk)
            if not m.any():
                continue
            ee, uu, tt = e[m], u[g][m], t[g][m]
            ok = np.isfinite(ee)
            episodes.append({"board": board, "kind": kk, "start": hhmmss(tt[0]), "seconds": int(m.sum()),
                             "with_edge": int(ok.sum()), "max_abs_err_ns": float(np.nanmax(np.abs(ee))) if ok.any() else np.nan,
                             "last_err_ns": float(ee[ok][-1]) if ok.any() else np.nan, "last_u_ns": int(uu[-1]),
                             "worst_ratio": float(np.nanmax(np.abs(ee[ok]) / np.maximum(uu[ok], 1))) if ok.any() and uu[ok].max() > 0 else np.nan,
                             "after_err_ns": float(off[min(g[-1] + 1, len(off) - 1)] - ref) if g[-1] + 1 < len(off) else np.nan})
print("%-2s %-9s %-9s %5s %5s %10s %10s %9s %8s %10s" % ("b", "kind", "start", "secs", "edges", "max|err|", "last err", "last U", "err/U", "after"))
for e in episodes:
    print("%-2s %-9s %-9s %5d %5d %10.0f %10.0f %9d %8.3f %10.0f" % (e["board"], e["kind"], e["start"], e["seconds"], e["with_edge"],
          e["max_abs_err_ns"], e["last_err_ns"], e["last_u_ns"], e["worst_ratio"], e["after_err_ns"]))
for kk in ("holdover", "syncing"):
    sel = [e for e in episodes if e["kind"] == kk and e["with_edge"] > 0]
    if sel:
        print("%s: %d episodes, %d seconds; max |err| %.0f ns; worst err/U %.3f; returns |after| max %.0f ns" % (
            kk, len(sel), sum(e["seconds"] for e in sel), max(e["max_abs_err_ns"] for e in sel),
            np.nanmax([e["worst_ratio"] for e in sel]), np.nanmax([abs(e["after_err_ns"]) for e in sel])))
