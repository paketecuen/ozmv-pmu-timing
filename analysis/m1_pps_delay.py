"""M1: the converter's delay from the GPS pulse recorded on the neutral-current input IN.

On the bench the PPS goes through a divider into IN (connector CN5, AIN0), and the WMU records it with the
RawCurrentInputs option (ADC volts in the current slots, CurrentGain 1e6) together with its per-sample time
track. Every sample's label is its fitted conversion-ready time minus the computed filter delay
D = 190.5 t_MOD. The rising edge of the PPS is at a whole UTC second, so the samples around each second trace
the converter's step response against the labels. Superposed over many seconds (the crystal moves the edge
across the sample grid), they give that response in equivalent time. The fit of a sinc3 step, optionally
through an RC input network, returns the offset between the labels and the input: zero if D is right, about
+10.9 us if the converter has the 44 t_MOD latency the datasheet leaves open.

Inputs: the WMU stream (.ogg as received, or the raw s32le decoded from it) and its time track (CSV from the
ozmV tool tools/wmu-time.py --csv). Output: a summary, the equivalent-time step (CSV) for the paper's figure.

    python3 analysis/m1_pps_delay.py STREAM.ogg TIME.csv --out data/m1 [--fit-rc] [--channel 6]
    python3 analysis/m1_pps_delay.py --self-test          # synthetic data with a known delay"""
import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m1_pps_step_sim import T_MOD_NOM, response, simulate  # noqa: E402

CHANNELS = 7                 # VA VB VC IA IB IC IN
IN_CHANNEL = 6
LOW = (-1.2e-3, -0.25e-3)    # where each edge's low level is taken, seconds from the edge
HIGH = (0.35e-3, 1.2e-3)     # and its high level (the PPS stays high for 100 ms)
WINDOW = (-0.12e-3, 0.20e-3)  # the samples that trace the step (sinc3 spans 95 us, plus the RC)


def load(stream, time_csv, channel):
    """Per-sample UTC times (s, float64 relative to the first whole second) and the channel's values."""
    stream = Path(stream)
    if stream.suffix == ".ogg":
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(stream), "-f", "s32le", "-acodec", "pcm_s32le", "-"],
                             check=True, capture_output=True).stdout
    else:
        raw = stream.read_bytes()
    values = np.frombuffer(raw, dtype="<i4").reshape(-1, CHANNELS)[:, channel].astype(np.float64)
    first, count, t0, t1 = [], [], [], []
    with open(time_csv) as f:
        for row in csv.DictReader(f):
            first.append(int(row["first_sample"])); count.append(int(row["count"]))
            t0.append(int(row["first_utc_ns"])); t1.append(int(row["last_utc_ns"]))
    base = (t0[0] // 1_000_000_000) * 1_000_000_000          # keep float precision: times from this second
    times = np.full(values.size, np.nan)
    for a, n, u0, u1 in zip(first, count, t0, t1):
        idx = np.arange(a, min(a + n, values.size))
        frac = (idx - a) / max(n - 1, 1)
        times[idx] = ((u0 - base) + frac * (u1 - u0)) * 1e-9
    good = np.isfinite(times)
    return times[good], values[good], base


def edges(times, values):
    """For every whole second inside the record: the normalised samples around its rising edge."""
    taus, ys, used, levels = [], [], [], []
    for s in range(int(np.ceil(times[0] + 2e-3)), int(np.floor(times[-1] - 2e-3)) + 1):
        a, b = np.searchsorted(times, [s + LOW[0], s + HIGH[1]])
        tau = times[a:b] - s
        vals = values[a:b]
        lo = vals[(tau > LOW[0]) & (tau < LOW[1])]
        hi = vals[(tau > HIGH[0]) & (tau < HIGH[1])]
        win = (tau > WINDOW[0]) & (tau < WINDOW[1])
        if lo.size < 10 or hi.size < 10 or not win.any():
            continue
        step = hi.mean() - lo.mean()
        if step <= 20 * max(lo.std(), hi.std()):
            continue                                         # no rising edge here: noise, or the pulse is missing
        taus.append(tau[win]); ys.append((vals[win] - lo.mean()) / step)
        used.append(s); levels.append((lo.mean(), hi.mean(), lo.std(), hi.std()))
    if not used:
        raise SystemExit("no PPS edge found in the record")
    steps = np.array([h - l for l, h, _, _ in levels])
    keep = np.abs(steps - np.median(steps)) < 0.2 * np.median(steps)   # drop edges with a disturbed level
    taus = [t for t, k in zip(taus, keep) if k]; ys = [y for y, k in zip(ys, keep) if k]
    used = [u for u, k in zip(used, keep) if k]; levels = [v for v, k in zip(levels, keep) if k]
    return taus, ys, used, levels


def fit(tau, y, fit_rc, start=None):
    """Offset of the labels against the input (s) and the RC time constant, best of several starts."""
    def resid(p):
        rc = p[1] if fit_rc else 0.0
        return response(tau - p[0], T_MOD_NOM, rc) - y
    centre = tau[np.argmin(np.abs(y - 0.5))]
    best = None
    starts = [start] if start is not None else [[centre - rc0 * np.log(2)] + ([rc0] if fit_rc else [])
                                                 for rc0 in ([0.2e-6, 1e-6, 4e-6, 15e-6] if fit_rc else [0.0])]
    for p0 in starts:
        bounds = ([-1e-3, 0.0], [1e-3, 60e-6]) if fit_rc else ([-1e-3], [1e-3])
        sol = least_squares(resid, p0, bounds=bounds, x_scale=[1e-6] * len(p0))
        if best is None or sol.cost < best.cost:
            best = sol
    J = best.jac
    dof = max(1, tau.size - len(best.x))
    cov = np.linalg.inv(J.T @ J) * (2 * best.cost / dof)
    return best.x, np.sqrt(np.diag(cov)), np.sqrt(2 * best.cost / tau.size)


def centroid(tau, y, lo=-150e-6, hi=250e-6, bin_s=50e-9):
    """Model-free delay: the centroid of the equivalent-time impulse response, from the step's area."""
    order = np.argsort(tau)
    t, v = tau[order], y[order]
    edges_ = np.arange(lo, hi + bin_s, bin_s)
    idx = np.digitize(t, edges_) - 1
    s = np.array([v[idx == k].mean() if np.any(idx == k) else np.nan for k in range(edges_.size - 1)])
    centres = edges_[:-1] + bin_s / 2
    ok = np.isfinite(s)
    s = np.interp(centres, centres[ok], s[ok])
    area = np.trapz(s, centres) + bin_s / 2 * (s[0] + s[-1])    # the step's area from lo to hi
    return hi - area, centres, s


def analyse(times, values, fit_rc, out=None):
    taus, ys, used, levels = edges(times, values)
    tau = np.concatenate(taus); y = np.concatenate(ys)
    best, err, rms = fit(tau, y, fit_rc)
    delay, rest = best[0], list(best[1:])
    rc = rest[0] if fit_rc else 0.0
    # Edges are the independent units: a jackknife over them gives the delay's standard uncertainty
    n = len(taus)
    groups = np.array_split(np.arange(n), min(n, 20))
    jack = []
    for g in groups:
        keep = np.setdiff1d(np.arange(n), g)
        x, _, _ = fit(np.concatenate([taus[k] for k in keep]), np.concatenate([ys[k] for k in keep]), fit_rc,
                      start=list(best))
        jack.append(x[0])
    jack = np.array(jack)
    u_jack = np.sqrt((len(jack) - 1) / len(jack) * np.sum((jack - jack.mean()) ** 2))
    c, centres, s = centroid(tau, y)
    step_v = np.median([h - l for l, h, _, _ in levels])
    noise_v = np.median([0.5 * (a + b) for _, _, a, b in levels])
    summary = {
        "edges": n, "samples_on_steps": int(tau.size), "step_adc_units": step_v, "noise_adc_units": noise_v,
        "delay_offset_ns": delay * 1e9, "delay_offset_u_ns": max(err[0], u_jack) * 1e9,
        "delay_offset_fit_u_ns": err[0] * 1e9, "delay_offset_jackknife_u_ns": u_jack * 1e9,
        "rc_us": rc * 1e6 if fit_rc else None, "rc_u_us": err[1] * 1e6 if fit_rc else None,
        "centroid_ns": c * 1e9, "residual_rms_of_step": rms,
        "computed_D_ns": 190.5 * T_MOD_NOM * 1e9,
        "reading": ("D as computed (190.5 t_MOD)" if abs(delay) < 2e-6 else
                    "D about 44 t_MOD longer (234.5 t_MOD)" if abs(delay - 44 * T_MOD_NOM) < 2e-6 else
                    "neither: look at the step and the input network"),
    }
    if out:
        out = Path(out); out.mkdir(parents=True, exist_ok=True)
        (out / "m1-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        with open(out / "m1-equivalent-time-step.csv", "w") as f:
            f.write("tau_ns,normalised\n")
            order = np.argsort(tau)
            for t, v in zip(tau[order], y[order]):
                f.write(f"{t * 1e9:.1f},{v:.7f}\n")
        with open(out / "m1-binned-step.csv", "w") as f:
            f.write("tau_ns,normalised\n")
            for t, v in zip(centres, s):
                f.write(f"{t * 1e9:.1f},{v:.7f}\n")
    return summary


def self_test():
    """Synthetic record: a known 44 t_MOD offset, with and without an RC network, recovered by the analysis."""
    rng = np.random.default_rng(1016)
    for rc_s, fit_rc in ((0.0, False), (2e-6, True)):
        taus, values = simulate(300, 44.0, rc_s, 21.3e-6, rng, span=40)
        # simulate() returns, per edge, labels relative to that edge's second; rebuild absolute times
        per_edge = 81
        times = np.concatenate([taus[k * per_edge:(k + 1) * per_edge] + k for k in range(len(taus) // per_edge)])
        summary = analyse(times, values[:times.size], fit_rc)
        truth = 44 * T_MOD_NOM * (1 - 40e-6) + 190.5 * (T_MOD_NOM * (1 - 40e-6) - T_MOD_NOM)
        print(f"RC {rc_s * 1e6:.0f} us: {summary['edges']} edges, offset {summary['delay_offset_ns']:.1f} ns "
              f"+/- {summary['delay_offset_u_ns']:.1f} ns (true {truth * 1e9:.1f} ns), "
              f"RC {summary['rc_us']} us, centroid {summary['centroid_ns']:.0f} ns -> {summary['reading']}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stream", nargs="?")
    ap.add_argument("time_csv", nargs="?")
    ap.add_argument("--channel", type=int, default=IN_CHANNEL)
    ap.add_argument("--fit-rc", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test()
    else:
        if not (a.stream and a.time_csv):
            ap.error("STREAM and TIME_CSV are required")
        t, v, base = load(a.stream, a.time_csv, a.channel)
        print(json.dumps(analyse(t, v, a.fit_rc, a.out), indent=2))
