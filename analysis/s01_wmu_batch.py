"""Every WMU file of the session: time-track summary, PPS on IN, M1 fit (GPS records only) and per-second offsets.

usage: OZMV=<ozmV checkout> python3 analysis/s01_wmu_batch.py SESSION_DIR OUT_DIR [DATE_GLOB]
(DATE_GLOB selects the recordings, 2026102[27] by default: session 01 and its boot file; 20261029 for session 02)
(decodes each file to a temporary s32 in OUT_DIR and deletes it after; about 1 min per 30-min file)"""
import csv, json, subprocess, sys, datetime, os
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
sys.path.insert(0, str(Path(__file__).resolve().parent))
import m1_pps_delay as m1  # noqa: E402
from m1_pps_step_sim import T_MOD_NOM, response  # noqa: E402

session, out = Path(sys.argv[1]), Path(sys.argv[2])
date_glob = sys.argv[3] if len(sys.argv) > 3 else "2026102[27]"
out.mkdir(parents=True, exist_ok=True)
_HERE = Path(__file__).resolve().parent
WMU_TIME = str(_HERE / "wmu-time.py") if (_HERE / "wmu-time.py").exists() else \
    str(Path(os.environ.get("OZMV", "../ozmV")) / "tools" / "wmu-time.py")   # a copy beside this script, or the ozmV checkout
SRC = {0: "system", 1: "gps", 2: "syncing"}

def utc(ns):
    return datetime.datetime.fromtimestamp(ns // 10**9, datetime.timezone.utc).strftime("%H:%M:%S")

def per_second(x, rows, rc):
    """Offset of each whole second's IN edge against the labels, with RC fixed; all records with a time."""
    rows = [r for r in rows if int(r["first_utc_ns"]) > 0]
    first = np.array([int(r["first_sample"]) for r in rows]); count = np.array([int(r["count"]) for r in rows])
    u0 = np.array([int(r["first_utc_ns"]) for r in rows]); u1 = np.array([int(r["last_utc_ns"]) for r in rows])
    fl = np.array([int(r["flags"]) for r in rows]); unc = np.array([int(r["uncertainty_ns"]) for r in rows])
    res = []
    for s in range(int(u0[0] // 10**9) + 1, int(u1[-1] // 10**9)):
        te = s * 10**9
        k = np.searchsorted(u0, te) - 1
        if k < 6 or k + 7 >= len(rows):
            continue
        idx, tau, f = [], [], []
        for j in range(k - 6, k + 7):
            n = count[j]; p = (u1[j] - u0[j]) / (n - 1)
            base = int(u0[j] - te)                      # exact integer ns before any float
            i = np.arange(n); t = base + i * p
            m = (t >= -1.2e6) & (t <= 1.2e6)
            idx.extend(first[j] + i[m]); tau.extend(t[m] * 1e-9); f.extend([fl[j]] * int(m.sum()))
        idx = np.array(idx); tau = np.array(tau)
        if idx.size < 50 or np.any(np.diff(idx) != 1):
            continue
        v = x[idx, 6].astype(np.float64)
        lo = v[(tau > -1.2e-3) & (tau < -0.25e-3)]; hi = v[(tau > 0.35e-3) & (tau < 1.2e-3)]
        if lo.size < 10 or hi.size < 10:
            continue
        step = hi.mean() - lo.mean(); fo = int(np.bitwise_or.reduce(np.array(f)))
        if step <= 20 * max(lo.std(), hi.std()):
            res.append((s, np.nan, step / 256e3, fo, int(unc[k]))); continue
        w = (tau > -0.12e-3) & (tau < 0.20e-3)
        y = (v[w] - lo.mean()) / step
        sol = least_squares(lambda q: response(tau[w] - q[0], T_MOD_NOM, rc) - y, [11.9e-6], x_scale=[1e-6])
        res.append((s, sol.x[0] * 1e9, step / 256e3, fo, int(unc[k])))
    return res

summary = []
for ogg in sorted(session.glob(f"[AB]/wmu-{date_glob}T*.ogg")):
    board, name = ogg.parent.name, ogg.stem
    tag = f"{board}-{name[4:]}"
    tcsv = out / f"{tag}-time.csv"
    txt = subprocess.run(["python3", "-I", WMU_TIME, str(ogg), "--csv", str(tcsv)], capture_output=True, text=True)
    rows = list(csv.DictReader(open(tcsv))) if tcsv.exists() else []
    info = {"board": board, "file": ogg.name, "wmu_time": txt.stdout.strip().splitlines(), "records": len(rows)}
    if not rows:
        info["error"] = txt.stderr.strip()[-300:]; summary.append(info); print(json.dumps(info)); continue
    fl = np.array([int(r["flags"]) for r in rows])
    info["gps_records_pct"] = round(100 * np.mean(fl == 267), 2)
    info["holdover_records"] = int(np.sum(fl & 32 > 0)); info["gap_records"] = int(np.sum(fl & 4 > 0))
    info["invalid_records"] = int(np.sum(fl & 16 > 0)); info["no_time_records"] = int(np.sum(fl & 8 == 0))
    info["syncing_records"] = int(np.sum((fl >> 8) == 2)); info["system_records"] = int(np.sum(((fl >> 8) == 0) & (fl & 8 > 0)))
    known = [r for r in rows if int(r["first_utc_ns"]) > 0]
    info["first_utc"] = utc(int(known[0]["first_utc_ns"])) if known else None
    info["last_utc"] = utc(int(known[-1]["last_utc_ns"])) if known else None
    s32 = out / "tmp.s32"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(ogg), "-map", "0:a:0", "-f", "s32le", "-acodec", "pcm_s32le", str(s32)], check=True)
    x = np.memmap(s32, dtype="<i4", mode="r").reshape(-1, 7)
    info["samples_decoded"] = int(x.shape[0]); info["samples_in_track"] = int(sum(int(r["count"]) for r in rows))
    gcsv = out / f"{tag}-time-gps.csv"
    with open(tcsv) as fi, open(gcsv, "w") as fo:
        for i, line in enumerate(fi):
            if i == 0 or line.split(",")[2] == "267":
                fo.write(line)
    try:
        t, v, base = m1.load(str(s32), str(gcsv), 6)
        s = m1.analyse(t, v, True, out / f"{tag}-m1")
        info["m1"] = {k: s[k] for k in ("edges", "delay_offset_ns", "delay_offset_u_ns", "rc_us", "centroid_ns", "residual_rms_of_step", "step_adc_units")}
        info["m1"]["step_mV"] = s["step_adc_units"] / 256e3
        rc = s["rc_us"] * 1e-6
    except SystemExit as e:
        info["m1"] = {"error": str(e)}; rc = None
    except Exception as e:
        info["m1"] = {"error": repr(e)[:300]}; rc = None
    if rc:
        res = per_second(x, rows, rc)
        with open(out / f"{tag}-per-second.csv", "w") as f:
            f.write("utc_s,offset_ns,step_mV,flags_or,uncertainty_ns\n")
            for r in res:
                f.write("%d,%s,%.2f,%d,%d\n" % (r[0], "" if np.isnan(r[1]) else "%.1f" % r[1], r[2], r[3], r[4]))
        o = np.array([r[1] for r in res if r[3] == 267 and np.isfinite(r[1])])
        if o.size:
            info["per_second_gps"] = {"n": int(o.size), "median_ns": float(np.median(o)), "rms_about_median_ns": float(np.sqrt(np.mean((o - np.median(o)) ** 2)))}
        info["seconds_without_edge"] = int(sum(1 for r in res if not np.isfinite(r[1])))
    del x
    os.remove(s32)
    summary.append(info)
    print(json.dumps(info), flush=True)
json.dump(summary, open(out / "summary.json", "w"), indent=1)
