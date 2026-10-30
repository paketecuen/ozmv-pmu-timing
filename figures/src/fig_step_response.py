"""Fig.: response of the synchrophasor estimator to the P-class step tests, before and after the
dual-order fit. Positive-sequence phasor at 50 Hz, interleaved from 20 step positions per reporting
interval as in IEC/IEEE 60255-118-1, Annex C.4 (data/e5, analysis/e5_pclass.cpp)."""
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

from ieee_style import COLUMN, PALETTE, apply, panel, save

DATA = Path(__file__).resolve().parents[2] / "data" / "e5"


def response(folder, kind, column):
    """Time from the step (ms) and the stepped quantity normalized to a 0-to-1 step."""
    with open(DATA / folder / f"e5-step-{kind}.csv") as f:
        rows = list(csv.DictReader(f))
    tau = np.array([float(r["tau_ms"]) for r in rows])
    value = np.array([float(r[column]) for r in rows])
    order = np.argsort(tau)
    tau, value = tau[order], value[order]
    before, after = value[tau < -50].mean(), value[tau > 50].mean()
    return tau, (value - before) / (after - before)


apply()
fig, axes = plt.subplots(2, 1, figsize=(COLUMN, 2.75), sharex=True)
cases = ((axes[0], 0, "mag_ps", "(a)", r"Magnitude step of $+10\,\%$"),
         (axes[1], 2, "phase_ps_rad", "(b)", r"Phase step of $+10^{\circ}$"))
for ax, kind, column, label, text in cases:
    t_old, y_old = response("e5-50", kind, column)    # second-order phasor
    t_new, y_new = response("e5v3-50", kind, column)  # first-order phasor of the dual-order fit
    for limit in (1.05, -0.05):
        ax.axhline(limit, color=PALETTE["gray"], lw=0.6, ls=(0, (1, 1.5)), zorder=1)
    ax.axvline(0, color=PALETTE["gray"], lw=0.5, zorder=1)
    ax.plot(t_old, y_old, color=PALETTE["vermillion"], ls=(0, (4, 1.5)), lw=1.0, zorder=3,
            label="Second-order phasor")
    ax.plot(t_new, y_new, color=PALETTE["blue"], lw=1.2, zorder=4,
            label="First-order phasor (dual-order fit)")
    ax.set_xlim(-30, 30)
    ax.set_ylim(-0.22, 1.24)
    ax.set_yticks([0, 0.5, 1])
    ax.set_yticklabels(["$0$", "$0.5$", "$1$"])
    ax.set_ylabel("Normalized response")
    panel(ax, label, text)
    # The largest excursion beyond the final value, before the fix
    peak = np.argmax(y_old)
    ax.annotate(rf"${100 * (y_old[peak] - 1):.1f}\,\%$", xy=(t_old[peak], y_old[peak]), xytext=(t_old[peak] + 6, 1.13),
                fontsize=7, color=PALETTE["vermillion"], ha="left", va="center",
                arrowprops=dict(arrowstyle="-", lw=0.5, color=PALETTE["vermillion"], shrinkA=0, shrinkB=1.5))
axes[1].set_xlabel(r"Time from the step (\si{\milli\second})")
handles, labels = axes[0].get_legend_handles_labels()
# Column-major legend: row 1 holds the two estimators, row 2 the limit
handles.insert(1, Line2D([], [], color=PALETTE["gray"], lw=0.6, ls=(0, (1, 1.5))))
labels.insert(1, r"P-class limit ($5\,\%$ of the step)")
fig.legend(handles, labels, loc="upper center", ncol=2, bbox_to_anchor=(0.54, 1.0), handlelength=2.6,
           columnspacing=1.2, labelspacing=0.3, borderaxespad=0.1)
fig.align_ylabels(axes)
fig.tight_layout(h_pad=0.4, rect=(0, 0, 1, 0.91))
save(fig, "step_response")
