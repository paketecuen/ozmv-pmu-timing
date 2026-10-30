"""M2 and M3: the delay of the voltage channel against a PPS-referenced Fluke 8588A.

Bench (BANCO-para-Fran.md): the same 50 Hz voltage goes to phase A of the board and to the 8588A (master,
digitize mode); the 8558A (slave, one reading per master sample through the trigger cable) digitizes the PPS.
Both use the same sample interval Ts and an aperture ta close to Ts, so that a PPS edge almost always falls
inside some aperture and the straddling reading locates it.

The digitizer's aperture (8588A/8558A Operators Manual, Digitize, Figure 12): a reading with aperture ta is the
mean of n = ta / 200 ns + 1 samples taken 200 ns apart, the first at the trigger (within 10 ns), the last ta
after it; its value stands for the aperture's centre, ta / 2 after the trigger. A reading straddled by a PPS
edge shows the fraction f = m / n of its samples at or after the edge, so the edge lies in the 200 ns cell
between samples n - m - 1 and n - m: at (n - m - 0.5) x 200 ns after the trigger, +/- 100 ns. An edge in the
gap between the last sample of one aperture and the first of the next (Ts - ta, 1 us here) leaves no
straddling reading; it is reported with that gap as its half-width and dropped by default. The 200 ns cells
average out because the Flukes' time base is not locked to the PPS: the edge's phase slides across the cells
within a burst and from burst to burst, so the mean of many edges is unbiased and its standard error falls as
1 / sqrt(edges) from the 58 ns RMS of one cell.

- M3: the PPS into both Flukes measures the slave's lag against the master (about 0.1 us between two 8588A in
  Lapuh et al.), to be removed in M2. Run it with the master on the range that M2 uses (1000 V) as well as on
  10 V: the ranges' delays may differ. The lag can include whole sample intervals (our 8588A -> 8558A trigger
  chain: about one, 49.3 us in session 01); they are estimated from the edges and kept in the result.
- M2: in each burst the PPS edges map the master's sample index to UTC; the 8588A then gives the voltage's
  phase against UTC, the board's samples (with their per-sample UTC labels) give the same, and their difference
  is the board's delay: analogue front end plus whatever M1 found in the digital labels.

Files of one burst (written by bench/fluke_capture.py): CSV with columns k, master_v, slave_v, and a JSON with
ts_s, ta_s, host_utc_start_ns. The board's recording is the WMU stream with its time track.

    python3 analysis/m2_fluke_phase.py m3 BURST.csv ...           # slave lag
    python3 analysis/m2_fluke_phase.py m2 --lag-ns L BOARD.ogg TIME.csv BURST.csv ...
    python3 analysis/m2_fluke_phase.py --self-test                # discrete apertures, phase sweep, known lags"""
import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

SAMPLE_S = 200e-9   # the digitizer's internal sample spacing in Digitize mode (5 MHz)


def aperture_samples(ta):
    """Samples averaged by a reading of aperture ta: at 0, 200 ns, ..., ta after the trigger."""
    return int(round(ta / SAMPLE_S)) + 1


def edge_positions(x, ts, ta):
    """Rising PPS edges in a record: their positions in master samples (float) and half-widths (samples).

    A straddling reading places the edge in one 200 ns cell of its aperture (half-width 100 ns). When no
    reading straddles the edge, it fell in the gap between two apertures and gets the gap's half-width."""
    n = aperture_samples(ta)
    lo, hi = np.percentile(x, 5), np.percentile(x, 95)
    step = hi - lo
    low = x[x < lo + 0.1 * step]
    # The noise of the low level, robust to the few straddling readings that fall in this set
    noise = 1.4826 * float(np.median(np.abs(low - np.median(low)))) if low.size > 1 else 0.0
    # A reading straddles the edge when its fraction is clearly between the levels: beyond the noise and at
    # least half a cell (1 / 2n) away from either, the smallest fraction a discrete aperture produces.
    margin = max(3 * noise / step, 0.5 / n)
    above = x > lo + 0.5 * step
    starts = np.flatnonzero(~above[:-1] & above[1:]) + 1          # first reading above the middle
    gap = ts - (n - 1) * SAMPLE_S                                 # last sample of an aperture to the next trigger
    pos, half = [], []
    for k in starts:
        if k < 1:
            continue
        f = (x[k - 1:k + 1] - lo) / step                          # the two readings around the transition
        straddling = [i for i in (0, 1) if margin < f[i] < 1 - margin]
        if len(straddling) == 1:
            i = straddling[0]
            m = int(np.clip(np.rint(f[i] * n), 1, n - 1))         # samples at or after the edge
            pos.append(k - 1 + i + (n - m - 0.5) * SAMPLE_S / ts)
            half.append(0.5 * SAMPLE_S / ts)
        elif not straddling:                                      # in the gap before trigger k
            pos.append(k - 0.5 * gap / ts)
            half.append(0.5 * gap / ts)
        # Two straddling readings: not a clean edge (a slow edge, a noise burst); skipped.
    return np.array(pos), np.array(half)


def clean_edges(pos, half, ts):
    """The edges located to one 200 ns cell; those in the inter-aperture gap are dropped."""
    keep = half <= 0.5 * SAMPLE_S / ts * 1.001
    return pos[keep], keep


def pair_edges(a, b, tolerance=0.5):
    """Pairs of positions (one per edge) that agree within `tolerance` samples; unmatched edges are dropped."""
    pairs = []
    j = 0
    for pa in a:
        while j < b.size and b[j] < pa - tolerance:
            j += 1
        if j < b.size and abs(b[j] - pa) <= tolerance:
            pairs.append((pa, b[j]))
            j += 1
    return np.array(pairs).reshape(-1, 2)


def burst_time_base(pps, ts, ta, host_start_ns, epoch_s):
    """UTC of the master's trigger k, in seconds after the whole second epoch_s: a + b k from the clean edges,
    each at a whole second near the host's clock. Relative times keep float64 at sub-nanosecond resolution."""
    pos, half = edge_positions(pps, ts, ta)
    pos, keep = clean_edges(pos, half, ts)
    approx = (host_start_ns - epoch_s * 1_000_000_000) * 1e-9 + pos * ts
    seconds = np.round(approx)
    A = np.vstack([np.ones_like(pos), pos]).T
    (a, b), *_ = np.linalg.lstsq(A, seconds, rcond=None)
    resid = seconds - (a + b * pos)
    return a, b, resid, pos, int((~keep).sum())


def sine_phase(t, x, f):
    """Least-squares amplitude and phase of x(t) = A cos(2 pi f t + phi) + c, for a known frequency."""
    M = np.vstack([np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t), np.ones_like(t)]).T
    (c1, c2, _), *_ = np.linalg.lstsq(M, x, rcond=None)
    return np.hypot(c1, c2), np.arctan2(-c2, c1)


def sine_frequency(t, x, f0=50.0):
    """Frequency of a sinusoid by a short Gauss-Newton refinement of a 4-parameter fit."""
    f = f0
    for _ in range(8):
        A, phi = sine_phase(t, x, f)
        r = x - A * np.cos(2 * np.pi * f * t + phi)
        d = -A * 2 * np.pi * t * np.sin(2 * np.pi * f * t + phi)
        f += np.dot(d, r - r.mean()) / np.dot(d, d)
    return f


def board_delay(t_ref, v_ref, t_brd, v_brd, window_s=0.2):
    """Per window: the time by which the board's labelled samples lag the reference (s)."""
    out = []
    for w0 in np.arange(t_ref[0], t_ref[-1] - window_s, window_s):
        r = (t_ref >= w0) & (t_ref < w0 + window_s)
        b = (t_brd >= w0) & (t_brd < w0 + window_s)
        if r.sum() < 100 or b.sum() < 100:
            continue
        f = sine_frequency(t_ref[r] - w0, v_ref[r])
        _, p_ref = sine_phase(t_ref[r] - w0, v_ref[r], f)
        _, p_brd = sine_phase(t_brd[b] - w0, v_brd[b], f)
        dphi = np.angle(np.exp(1j * (p_ref - p_brd)))
        out.append(dphi / (2 * np.pi * f))      # board's labels late by this much against the reference
    return np.array(out)


def whole_reading_shift(a, b):
    """Whole master samples by which the edges in b come before those in a (median of the nearest pairs).

    PPS edges are 20 000 samples apart at 50 us, so the nearest edge is the same one for any shift well below
    that. With the master's trigger output starting the slave's next reading the slave trails by about one
    interval (session 01, 2026-10-27: 0.98 samples)."""
    if a.size == 0 or b.size == 0:
        return 0
    if b.size == 1:
        near = np.full_like(a, b[0])
    else:
        j = np.clip(np.searchsorted(b, a), 1, b.size - 1)
        near = np.where(np.abs(b[j] - a) < np.abs(b[j - 1] - a), b[j], b[j - 1])
    return int(np.rint(np.median(a - near)))


def slave_lag(master, slave, ts, ta, info=None):
    """M3 from one burst: per-edge lag of the slave behind the master (s), from paired clean edges.

    The whole-sample part of the lag is estimated from the edges and removed before pairing within half a
    sample, then added back: the result is the full lag that M2 subtracts (--lag-ns), small or not."""
    pm, hm = edge_positions(master, ts, ta)
    ps, hs = edge_positions(slave, ts, ta)
    pm, _ = clean_edges(pm, hm, ts)
    ps, _ = clean_edges(ps, hs, ts)
    shift = whole_reading_shift(pm, ps)
    pairs = pair_edges(pm - shift, ps)
    if info is not None:
        info["whole_reading_shift"] = shift
    dropped = pm.size + ps.size - 2 * pairs.shape[0]
    return (pairs[:, 0] + shift - pairs[:, 1]) * ts, dropped   # a later slave sees the edge at a smaller index


def load_burst(path):
    meta = json.loads(Path(path).with_suffix(".json").read_text())
    k, mv, sv = [], [], []
    with open(path) as fh:
        for row in csv.DictReader(fh):
            k.append(int(row["k"])); mv.append(float(row["master_v"])); sv.append(float(row["slave_v"]))
    return np.array(k), np.array(mv), np.array(sv), meta


def self_test():
    """Synthetic readings with the digitizer's discrete aperture: a phase sweep, known lags, and an M2 burst."""
    rng = np.random.default_rng(1016)
    ts, ta = 50e-6, 49e-6
    n = aperture_samples(ta)
    offsets = np.arange(n) * SAMPLE_S

    def pps(t):                                                 # 3.3 V for the first 100 ms of every second
        return 3.3 * ((t - np.floor(t)) < 0.1)

    def reading(fun, triggers, noise=0.0):                      # the digitizer: the mean of n samples
        values = fun(triggers[:, None] + offsets[None, :]).mean(axis=1)
        return values + (rng.normal(0, noise, values.size) if noise else 0)

    # 1. One edge at every phase of the sample interval: where does each edge land, how many fall in the gap?
    phases = (np.arange(4000) + 0.5) / 4000 * ts
    errors, gap_edges = [], 0
    for phase in phases:
        T = 1.0 - 4 * ts + phase + np.arange(8) * ts            # eight triggers around the edge at t = 1 s
        pos, half = edge_positions(reading(pps, T, 1e-4), ts, ta)
        pos_c, keep = clean_edges(pos, half, ts)
        gap_edges += int((~keep).sum())
        true = (1.0 - T[0]) / ts
        errors += list((pos_c - true) * ts)
    errors = np.array(errors)
    print(f"sweep: {errors.size} edges located, {gap_edges} in the inter-aperture gap "
          f"(expected {100 * (ts - (n - 1) * SAMPLE_S) / ts:.1f} %); error {np.sqrt(np.mean(errors ** 2)) * 1e9:.1f} ns RMS, "
          f"{np.max(np.abs(errors)) * 1e9:.0f} ns max, mean {np.mean(errors) * 1e9:+.1f} ns")

    # 2. M3 with known lags: edges at random phases (uniform within the 200 ns cells, unlike a regular
    # sweep), the slave late by the lag; the last two trail by about one and two whole samples, as the
    # trigger chain of session 01 does
    for lag in (0.0, 105e-9, 333e-9, 1000e-9, 49.33e-6, 99.6e-6):
        lags = []
        extra = int(lag // ts)                                  # keep low readings before a late slave's edge
        for phase in rng.uniform(0, ts, 1500):
            T = 1.0 - (4 + extra) * ts + phase + np.arange(8 + extra) * ts
            d, _ = slave_lag(reading(pps, T, 1e-4), reading(pps, T + lag, 1e-4), ts, ta)
            lags += list(d)
        lags = np.array(lags)
        print(f"M3: slave lag {lags.mean() * 1e9:.1f} ns +/- {lags.std(ddof=1) / np.sqrt(lags.size) * 1e9:.1f} "
              f"(true {lag * 1e9:.0f} ns; {lags.size} edges, {lags.std(ddof=1) * 1e9:.0f} ns RMS each)")

    # 3. An M2 burst: the voltage on the master, the PPS on the slave, the board late by a known delay
    lag, front, digital = 105e-9, 21.4e-6, 0.0
    f_mains, amp = 50.01, 325.0
    epoch = 1792100000                                          # a whole UTC second; times are relative to it
    t0_utc = 0.123456                                           # the burst starts here, 10 s long
    fluke_rate_err = 2.7183e-6                                  # the Fluke's time base runs 2.7 ppm slow
    k = np.arange(int(10.0 / ts))
    T = t0_utc + k * ts * (1 + fluke_rate_err)                  # master triggers (UTC)
    volts = lambda t: amp * np.cos(2 * np.pi * f_mains * t + 0.7)
    master = reading(volts, T, 0.01)
    slave = reading(pps, T + lag, 1e-4)
    d_lag, dropped = slave_lag(reading(pps, T, 1e-4), slave, ts, ta)
    lag_est = d_lag.mean()
    print(f"M3 in the burst: {d_lag.size} edges, {dropped} dropped, lag {lag_est * 1e9:.1f} ns (true {lag * 1e9:.0f})")
    a, b, resid, pos, gaps = burst_time_base(slave, ts, ta, epoch * 1_000_000_000 + int(t0_utc * 1e9), epoch)
    t_master = a + b * k - lag_est + ta / 2                     # the 8588A readings' instants (aperture centres)
    tb_true = np.arange(t0_utc, t0_utc + 10.0, 31742.72e-9)
    v_board = volts(tb_true - front) + rng.normal(0, 0.05, tb_true.size)   # its input sees the voltage late
    labels = tb_true - digital + rng.normal(0, 28e-9, tb_true.size)
    d = board_delay(t_master - t0_utc, master, labels - t0_utc, v_board)
    print(f"M2: {d.size} windows, board delay {d.mean() * 1e9:.1f} ns +/- {d.std(ddof=1) / np.sqrt(d.size) * 1e9:.1f} ns "
          f"(true {(front + digital) * 1e9:.1f} ns); time base from {pos.size} edges ({gaps} in the gap), "
          f"residual {np.sqrt(np.mean(resid ** 2)) * 1e9:.1f} ns RMS")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", nargs="?", choices=["m2", "m3"])
    ap.add_argument("files", nargs="*")
    ap.add_argument("--lag-ns", type=float, default=0.0, help="slave lag from M3")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test()
    elif a.mode == "m3":
        lags, dropped, shifts = [], 0, []
        for path in a.files:
            k, mv, sv, meta = load_burst(path)
            info = {}
            d, n_dropped = slave_lag(mv, sv, meta["ts_s"], meta["ta_s"], info)
            lags += list(d)
            dropped += n_dropped
            shifts.append(info["whole_reading_shift"])
        lags = np.array(lags)
        print(json.dumps({"edges": int(lags.size), "edges_dropped": dropped,
                          "whole_reading_shifts": sorted(set(shifts)), "slave_lag_ns": lags.mean() * 1e9,
                          "slave_lag_u_ns": lags.std(ddof=1) / np.sqrt(lags.size) * 1e9,
                          "per_edge_rms_ns": lags.std(ddof=1) * 1e9}, indent=2))
    elif a.mode == "m2":
        from m1_pps_delay import load
        board_file, time_csv, bursts = a.files[0], a.files[1], a.files[2:]
        tb, vb, base = load(board_file, time_csv, 0)          # VA
        delays, residuals, gaps = [], [], 0
        for path in bursts:
            k, mv, sv, meta = load_burst(path)
            ts, ta = meta["ts_s"], meta["ta_s"]
            aa, bb, resid, _, n_gap = burst_time_base(sv, ts, ta, meta["host_utc_start_ns"], base // 1_000_000_000)
            gaps += n_gap
            residuals += list(resid)
            t_master = aa + bb * k - a.lag_ns * 1e-9 + ta / 2
            delays += list(board_delay(t_master, mv, tb, vb))
        delays, residuals = np.array(delays), np.array(residuals)
        print(json.dumps({"windows": int(delays.size), "board_delay_ns": delays.mean() * 1e9,
                          "board_delay_u_ns": delays.std(ddof=1) / np.sqrt(delays.size) * 1e9,
                          "edges_in_gap_dropped": gaps,
                          "time_base_residual_rms_ns": float(np.sqrt(np.mean(residuals ** 2))) * 1e9}, indent=2))
    else:
        ap.print_help()
