"""E4: the boards' status every 2 s (the GUIA-OPUS poll, bench/kit-fran e4-poll.sh) as one CSV row per poll.

Each poll is a UTC line, /proc/uptime, then getDevice, getClock, getC37118Status and getWaveform as JSON. Only the
timing fields are kept; the raw logs also hold the receiver's position, which stays out of the repository.

usage: python3 analysis/s02_e4_status.py E4_LOG OUT.csv"""
import csv
import json
import sys


def get(o, path, default=None):
    for k in path.split("."):
        if not isinstance(o, dict) or k not in o:
            return default
        o = o[k]
    return o


FIELDS = [
    ("basis", "dev", "Status.Analyzer.Timeline.Basis"),
    ("holdovers", "dev", "Status.Analyzer.Timeline.Holdovers"),
    ("holdover_samples", "dev", "Status.Analyzer.Timeline.HoldoverSamples"),
    ("holdover_expiries", "dev", "Status.Analyzer.Timeline.HoldoverExpiries"),
    ("rejoins", "dev", "Status.Analyzer.Timeline.Rejoins"),
    ("last_rejoin_ns", "dev", "Status.Analyzer.Timeline.LastRejoinNs"),
    ("rejoins_over_bound", "dev", "Status.Analyzer.Timeline.RejoinsOverBound"),
    ("worst_rejoin_of_bound", "dev", "Status.Analyzer.Timeline.WorstRejoinOfBound"),
    ("gps_steps", "dev", "Status.Analyzer.Timeline.GpsSteps"),
    ("measured_period_ns", "dev", "Status.Analyzer.Timeline.MeasuredPeriodNs"),
    ("fix", "dev", "Status.GPS.Fix"),
    ("satellites", "dev", "Status.GPS.Satellites"),
    ("pps_age_ms", "dev", "Status.Timing.PPSAgeMs"),
    ("pps_count", "dev", "Status.Timing.PPSCount"),
    ("reference_state", "dev", "Status.Timing.ReferenceState"),
    ("sample_time_basis", "dev", "Status.SampleTime.Basis"),
    ("clock_synchronized", "clk", "Synchronized"),
    ("clock_reference", "clk", "GPS.Reference"),
    ("pmu_locked", "pmu", "TimeQuality.Locked"),
    ("pmu_max_error_ns", "pmu", "TimeQuality.MaximumErrorNs"),
    ("pmu_unlocked_us", "pmu", "TimeQuality.UnlockedDurationUs"),
    ("pmu_frames", "pmu", "PublishedFrames"),
    ("pmu_estimator_resets", "pmu", "EstimatorResets"),
    ("wmu_last_frame_u_ns", "wmu", "Status.TimeTrack.LastFrameUncertaintyNs"),
    ("wmu_samples_dropped", "wmu", "Status.SamplesDropped"),
]

rows, poll = [], None
for line in open(sys.argv[1], errors="replace"):
    line = line.strip()
    if len(line) == 20 and line.endswith("Z") and line[4] == "-" and line[10] == "T":
        if poll:
            rows.append(poll)
        poll = {"utc": line, "objs": []}
    elif poll is not None and line.startswith("{"):
        try:
            poll["objs"].append(json.loads(line))
        except ValueError:
            poll["objs"].append(None)                    # a reply cut by the next poll or an API error
    elif poll is not None and not poll.get("uptime") and line[:1].isdigit():
        poll["uptime"] = line.split()[0]
if poll:
    rows.append(poll)

with open(sys.argv[2], "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["utc", "uptime_s"] + [name for name, _, _ in FIELDS])
    for p in rows:
        objs = dict(zip(("dev", "clk", "pmu", "wmu"), p["objs"] + [None] * (4 - len(p["objs"]))))
        w.writerow([p["utc"], p.get("uptime", "")] + [get(objs[src], path, "") if objs[src] else "" for _, src, path in FIELDS])
print("%s: %d polls" % (sys.argv[1], len(rows)))
