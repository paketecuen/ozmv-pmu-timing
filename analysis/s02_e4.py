"""E4: the time error through each loss of the GNSS, against the other board's PPS recorded on IN.

In E4 the board under test keeps its own receiver on its PRU, but its IN records the reference board's PPS. Its
labels against that pulse (s01_wmu_batch.py's per-second offsets) give the error second by second through the
whole loss, not only at the return. The error is the offset minus the median offset of the confirmed GPS seconds
of the same recording in the 5 minutes before the loss (which absorbs M1 and the receivers' constant difference).
Beside it: the bound the board declared for that second (the time track's U), and the board's own return distance
(LastRejoinNs, from the 2 s status poll, s02_e4_status.py), and the error that the change of the crystal's
measured period across the loss predicts for a linear drift.

usage: python3 analysis/s02_e4.py BATCH_DIR STATUS_DIR OUT_DIR
(writes one CSV per loss, t since the loss began, error and bound, and prints the summary)"""
import csv
import datetime
import sys
from pathlib import Path

import numpy as np

batch, status_dir, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)
# What each board's IN recorded (Fran's session record, data/bench-s02/fran/registro-sesion.md)
CROSS_FROM = {"A": "10:37", "B": "11:36"}                # from then on, the other board's PPS


def hms(s):
    return datetime.datetime.fromtimestamp(s, datetime.timezone.utc).strftime("%H:%M:%S")


def utc_s(text):
    return int(datetime.datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc).timestamp())


def status(board):
    """The board's polls: (UTC s, basis, LastRejoinNs, measured period ns)."""
    rows_ = []
    for f in sorted(status_dir.glob(f"{board}-e4-*-status.csv")):
        for r in csv.DictReader(open(f)):
            rows_.append((utc_s(r["utc"]), r["basis"], r["last_rejoin_ns"],
                          float(r["measured_period_ns"]) if r["measured_period_ns"] else None))
    return sorted(rows_)


def returns(polls):
    """(UTC s, LastRejoinNs) whenever the last return changes: a rejoin within 10 us, or a GPS step beyond it."""
    out_, prev, first = [], None, True
    for t, _, last, _ in polls:
        if last and last != prev and not first:
            out_.append((t, int(last)))
        prev, first = last or prev, False
    return out_


def drift_prediction(polls, t0, t1):
    """Error a linear frequency drift over the loss accumulates (ns): half the period's change times the length.

    The period is measured over 10 s of confirmed GPS: the last value before the loss and the first one 20 s or
    more after the return."""
    before = [p for t, b, _, p in polls if t < t0 and b == "gps-input-time" and p]
    after = [p for t, b, _, p in polls if t > t1 + 20 and b == "gps-input-time" and p]
    if not before or not after:
        return None
    return -0.5 * (after[0] - before[-1]) / before[-1] * (t1 - t0) * 1e9   # a shorter period: labels late


print("%-2s %-8s %-9s %5s %6s %10s %10s %10s %7s %7s %10s %10s %10s %s" % (
    "b", "start", "reference", "secs", "edges", "max|err|", "last err", "last bound", "err/B", "end e/B", "board ret",
    "drift pred", "after", "base"))
for board in "AB":
    polls = status(board)
    rets = returns(polls)
    for f in sorted(batch.glob(f"{board}-20261029T*-per-second.csv")):
        rows = list(csv.DictReader(open(f)))
        t = np.array([int(r["utc_s"]) for r in rows])
        off = np.array([float(r["offset_ns"]) if r["offset_ns"] else np.nan for r in rows])
        fl = np.array([int(r["flags_or"]) for r in rows])
        u = np.array([int(r["uncertainty_ns"]) for r in rows])
        gps = fl == 267
        hold = (fl & 32) > 0
        idx = np.where(hold)[0]
        for g in np.split(idx, np.where(np.diff(idx) > 1)[0] + 1):
            if g.size == 0:
                continue
            t0 = t[g[0]]
            base_sel = gps & (t >= t0 - 300) & (t <= t0 - 10) & np.isfinite(off)
            if base_sel.sum() < 60:
                continue                                 # no clean base in this recording
            base = np.median(off[base_sel])
            e = off[g] - base
            ok = np.isfinite(e)
            if not ok.any():
                continue
            ref = "other" if hms(t0)[:5] >= CROSS_FROM[board] else "own"
            after = [i for i in range(g[-1] + 1, min(g[-1] + 4, len(t))) if gps[i] and np.isfinite(off[i])]
            ret = [d for ts, d in rets if t[g[-1]] <= ts <= t[g[-1]] + 30]
            last = np.where(ok)[0][-1]
            pred = drift_prediction(polls, t0, t[g[-1]] + 1)
            print("%-2s %-8s %-9s %5d %6d %10.0f %10.0f %10d %7.3f %7.3f %10s %10s %10s %.0f" % (
                board, hms(t0), ref, g.size, ok.sum(), np.nanmax(np.abs(e)), e[last], u[g][last],
                np.nanmax(np.abs(e[ok]) / np.maximum(u[g][ok], 1)), abs(e[last]) / max(u[g][last], 1),
                ret[0] if ret else "-", "%.0f" % pred if pred is not None else "-",
                "%.0f" % (off[after[0]] - base) if after else "-", base))
            if g.size >= 30:
                with open(out / f"{board}-{hms(t0).replace(':', '')}-loss.csv", "w") as fo:
                    fo.write("t_since_loss_s,error_ns,declared_bound_ns\n")
                    for i in g:
                        fo.write("%d,%s,%d\n" % (t[i] - t0, "" if np.isnan(off[i]) else "%.1f" % (off[i] - base), u[i]))
