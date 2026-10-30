"""Fig.: the same 40 s of mains voltage timed four common ways, against the per-sample UTC time of this
work: (a) the time error each method makes, (b) the synchrophasor error it causes in the production
estimator (data/e3, analysis/e3_dating_strategies.cpp)."""
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

from ieee_style import COLUMN, PALETTE, apply, panel, save

DATA = Path(__file__).resolve().parents[2] / "data" / "e3"

# Strategy number in data/e3 -> label and style. The microsecond labels (5) are left to the table.
STRATEGIES = (
    (1, r"First sample, then the nominal period", PALETTE["vermillion"], "-", 1.1),
    (2, r"Re-aligned at each PPS, nominal period", PALETTE["orange"], "-", 0.9),
    (3, r"Re-aligned at each PPS, period of the previous second", PALETTE["purple"], "-", 0.6),
    (4, r"Polled ready edge, without the envelope regression", PALETTE["sky"], "-", 0.5),
)

with open(DATA / "e3-time-errors.csv") as f:
    rows = list(csv.DictReader(f))
t = np.array([float(r["t_s"]) for r in rows])
errors = {k: np.abs(np.array([float(r[f"err_ns_{k}"]) for r in rows])) / 1000 for k, *_ in STRATEGIES}


def frames(k):
    with open(DATA / f"e3-frames-{k}.csv") as f:
        rows = list(csv.DictReader(f))
    utc = np.array([int(r["utc_us"]) for r in rows])
    return utc, np.array([float(r["tve"]) for r in rows])


start_us = frames(0)[0][0] - 20000  # the first report lies one window (20 ms) after the first sample

def band(values, width_s=0.25, low=5, high=95):
    """Centre, lower and upper percentiles of a noisy series in consecutive bins of width_s."""
    edges = np.arange(0, t[-1] + width_s, width_s)
    centre, lo, hi = [], [], []
    for a, b in zip(edges, edges[1:]):
        chunk = values[(t >= a) & (t < b)]
        if len(chunk):
            centre.append((a + b) / 2); lo.append(np.percentile(chunk, low)); hi.append(np.percentile(chunk, high))
    return np.array(centre), np.array(lo), np.array(hi)


apply()
fig, (ax_t, ax_v) = plt.subplots(2, 1, figsize=(COLUMN, 2.25), sharex=True)
for k, label, color, style, width in STRATEGIES:
    if k == 4:  # Polling jitter: its 5-95 % band, not 20 000 noisy points
        centre, lo, hi = band(errors[k])
        ax_t.fill_between(centre, lo, hi, color=color, alpha=0.45, lw=0, zorder=2)
    else:
        ax_t.plot(t, errors[k], color=color, ls=style, lw=width, zorder=3)
    utc, tve = frames(k)
    ax_v.plot((utc - start_us) / 1e6, 100 * tve, color=color, ls=style, lw=width, zorder=3 if k != 4 else 2)
ax_v.axhline(1.0, color=PALETTE["gray"], lw=0.6, ls=(0, (1, 1.5)), zorder=1)
# The reference's own expanded uncertainty: differences below it are not resolved
REFERENCE_US = 0.087
ax_t.axhline(REFERENCE_US, color=PALETTE["black"], lw=0.7, ls=(0, (4, 2)), zorder=4)

ax_t.set_yscale("log")
ax_t.set_ylim(1e-3, 1e4)
ax_t.set_yticks([1e-3, 1e-2, 1e-1, 1, 1e1, 1e2, 1e3, 1e4])
ax_t.set_yticklabels([rf"$10^{{{e}}}$" for e in range(-3, 5)])
ax_t.set_ylabel(r"Time error (\si{\micro\second})")
panel(ax_t, "(a)")
ax_v.set_yscale("log")
ax_v.set_ylim(1e-6, 1e3)
ax_v.set_yticks([1e-6, 1e-4, 1e-2, 1, 1e2])
ax_v.set_yticklabels([rf"$10^{{{e}}}$" for e in range(-6, 3, 2)])
ax_v.set_ylabel(r"TVE (\si{\percent})")
panel(ax_v, "(b)")
for ax in (ax_t, ax_v):
    ax.minorticks_off()
ax_v.set_xlim(0, 40)
ax_v.set_xlabel(r"Time (\si{\second})")
handles = [Line2D([], [], color=c, ls=s, lw=max(w, 0.9)) for _, _, c, s, w in STRATEGIES]
handles[3] = plt.Rectangle((0, 0), 1, 1, color=STRATEGIES[3][2], alpha=0.45, lw=0)
handles.append(Line2D([], [], color=PALETTE["black"], lw=0.7, ls=(0, (4, 2))))
handles.append(Line2D([], [], color=PALETTE["gray"], lw=0.6, ls=(0, (1, 1.5))))
labels = [label for _, label, *_ in STRATEGIES] + [r"Uncertainty of the reference, $87$\,ns ($k=2$)",
                                                   r"P-class TVE limit ($1\,\%$)"]
fig.legend(handles, labels, loc="upper center", ncol=1, bbox_to_anchor=(0.55, 1.0),
           handlelength=2.6, labelspacing=0.3, borderaxespad=0.1)
fig.align_ylabels((ax_t, ax_v))
fig.tight_layout(h_pad=0.4, rect=(0, 0, 1, 0.765))
save(fig, "dating")
