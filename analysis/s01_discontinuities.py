"""The acquisition's discontinuities in a bench session, from the WMU time tracks of both boards.

A record flagged gap (4) follows lost or replayed samples; one flagged invalid (16) holds a sample with no value.
Each is reported at the UTC of the last record with a time before it, merged within 30 s, with the end of every
file (a file that ends early ended with a reboot or a recorder stop).

usage: python3 analysis/s01_discontinuities.py BATCH_DIR [DATE]   (the -time.csv files of s01_wmu_batch.py; DATE 20261027)"""
import csv
import datetime
import sys
from pathlib import Path


def hms(ns):
    return datetime.datetime.fromtimestamp(ns / 1e9, datetime.timezone.utc).strftime("%H:%M:%S")


date = sys.argv[2] if len(sys.argv) > 2 else "20261027"
for board in "AB":
    events = []
    for f in sorted(Path(sys.argv[1]).glob(f"{board}-{date}T*-time.csv")):
        if f.name.endswith("-gps.csv"):
            continue
        rows = list(csv.DictReader(open(f)))
        last = None
        for r in rows:
            flags, t = int(r["flags"]), int(r["first_utc_ns"])
            if t:
                last = t
            if flags & 20:
                events.append((last or 0, "gap" if flags & 4 else "invalid"))
        if rows:
            events.append((int(rows[-1]["last_utc_ns"]) or last, "file end"))
    merged = []
    for t, kind in sorted(events):
        if merged and kind != "file end" and "file end" not in merged[-1][1] and t - merged[-1][0] < 30e9:
            merged[-1][1].add(kind)
            continue
        merged.append([t, {kind}])
    print(board + ": " + "; ".join("%s %s" % (hms(t), "+".join(sorted(k))) for t, k in merged))
