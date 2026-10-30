"""E4: the holdover's error when the GNSS signal returns, against the length of the loss and the declared bound.

Input: the board's log from bench/e4_holdover_log.sh (one line every 2 s). For each loss:
- its exact length, from the samples the holdover timed (HoldoverSamples) times the measured period;
- the distance between the holdover and the GNSS time at the return (LastRejoinNs);
- the bound declared at that length, B = U0 + rho t + delta t^2 / 2 for a fresh period (Section V-A);
- the error predicted from the crystal's drift: the period measured before and after the loss gives the change
  of frequency, and a linear drift accumulates half of it over the loss.

    python3 analysis/e4_holdover.py LOG [--csv OUT.csv]"""
import argparse
import re

RATE_PPB, DRIFT_PPB_S, ORIGIN_NS = 500.0, 0.5, 85.0
FIELD = re.compile(r'"(\w+)":"?([0-9a-z.\-+e]+)"?')


def parse(path):
    rows = []
    for line in open(path):
        if not re.match(r"\d\d:\d\d:\d\d ", line):
            continue
        d = dict(FIELD.findall(line))
        d["time"] = line[:8]
        rows.append(d)
    return rows


def losses(rows):
    out, prev = [], None
    for r in rows:
        if prev is not None and "HoldoverSamples" in r and "HoldoverSamples" in prev:
            back = int(r.get("Rejoins", 0)) + int(r.get("GpsSteps", 0)) > int(prev.get("Rejoins", 0)) + int(prev.get("GpsSteps", 0))
            if back:
                start = next((q for q in reversed(rows[:rows.index(r)]) if q.get("Basis") == "gps-input-time"), rows[0])
                period_ns = float(start.get("MeasuredPeriodNs", 31742.7))
                samples = int(r["HoldoverSamples"]) - int(start["HoldoverSamples"])
                length_s = samples * period_ns * 1e-9
                bound_ns = ORIGIN_NS + RATE_PPB * length_s + 0.5 * DRIFT_PPB_S * length_s ** 2
                # The period is re-measured over the first 10 s of GNSS after the return: read it 12 s later
                later = rows[min(len(rows) - 1, rows.index(r) + 6)]
                after_ns = float(later.get("MeasuredPeriodNs", period_ns))
                drift = (period_ns - after_ns) / period_ns          # fractional frequency change over the loss
                predicted_ns = 0.5 * abs(drift) * length_s * 1e9
                out.append({"returned_utc": r["time"], "length_s": length_s, "error_ns": float(r["LastRejoinNs"]),
                            "bound_ns": bound_ns, "error_over_bound": float(r["LastRejoinNs"]) / bound_ns,
                            "drift_ppb": drift * 1e9, "predicted_ns": predicted_ns,
                            "step": int(r.get("GpsSteps", 0)) > int(prev.get("GpsSteps", 0))})
        prev = r
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--csv")
    a = ap.parse_args()
    found = losses(parse(a.log))
    print(f"{'returned':9s} {'length s':>9s} {'error ns':>9s} {'bound ns':>10s} {'err/bound':>9s} {'drift ppb':>9s} {'predicted ns':>12s}")
    for x in found:
        print(f"{x['returned_utc']:9s} {x['length_s']:9.1f} {x['error_ns']:9.0f} {x['bound_ns']:10.0f} "
              f"{x['error_over_bound']:9.3f} {x['drift_ppb']:9.1f} {x['predicted_ns']:12.0f}{'  (re-anchored)' if x['step'] else ''}")
    if a.csv:
        with open(a.csv, "w") as fh:
            fh.write("returned_utc,length_s,error_ns,bound_ns,error_over_bound,drift_ppb,predicted_ns,step\n")
            for x in found:
                fh.write(",".join(str(x[k]) for k in ("returned_utc", "length_s", "error_ns", "bound_ns",
                                                         "error_over_bound", "drift_ppb", "predicted_ns", "step")) + "\n")
