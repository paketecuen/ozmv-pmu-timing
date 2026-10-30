"""Two figures of the holdover's error through losses of the GNSS signal.

holdover.pdf (the paper): six losses of 1, 5 and 10 minutes made by covering the antenna of one of two
instruments while the other kept its signal (bench session 02, data/bench-s02/e4/curves): the error second by
second against the other instrument's pulse, recorded on a converter input, and the bound the instrument
declared for each second.

holdover_hour.pdf (the supplement): one hour on the bench with three losses (data/holdover-r23): the holdover's
declared bound, the time-quality class carried by every C37.118 frame of the longest loss, and the distance
between the holdover and the GNSS time at each return."""
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

from ieee_style import COLUMN, PALETTE, apply, save

DATA = Path(__file__).resolve().parents[2] / "data"

apply()

# The paper: the six losses of session 02
fig, bx = plt.subplots(figsize=(COLUMN, 2.4))
style = {"A": dict(color=PALETTE["vermillion"], ls="-"), "B": dict(color="black", ls=(0, (3, 1.5)))}
longest, bound_curve = 0, None
for f in sorted((DATA / "bench-s02" / "e4" / "curves").glob("*-loss.csv")):
    rows = [r for r in csv.DictReader(open(f)) if r["error_ns"]]
    t_l = np.array([float(r["t_since_loss_s"]) for r in rows])
    e_l = np.abs(np.array([float(r["error_ns"]) for r in rows])) / 1000
    b_l = np.array([float(r["declared_bound_ns"]) for r in rows]) / 1000
    keep = t_l >= 1
    bx.plot(t_l[keep], e_l[keep], lw=0.8, zorder=3, **style[f.name[0]])
    if t_l[-1] > longest:
        longest, bound_curve = t_l[-1], (t_l[keep], b_l[keep])
bx.plot(*bound_curve, color=PALETTE["blue"], lw=1.2, zorder=2)
bx.set_xscale("log")
bx.set_yscale("log")
bx.set_xlim(1, 1000)
bx.set_ylim(0.02, 1000)
bx.set_xticks([1, 10, 100, 1000])
bx.set_xticklabels(["$1$", "$10$", "$100$", "$1000$"])
bx.set_yticks([0.1, 1, 10, 100, 1000])
bx.set_yticklabels(["$0.1$", "$1$", "$10$", "$100$", "$1000$"])
bx.minorticks_off()
bx.set_xlabel(r"Time since the GNSS signal was lost (\si{\second})")
bx.set_ylabel(r"Time error (\si{\micro\second})")
bx.legend([Line2D([], [], color=PALETTE["vermillion"], lw=0.8),
           Line2D([], [], color="black", lw=0.8, ls=(0, (3, 1.5))),
           Line2D([], [], color=PALETTE["blue"], lw=1.2)],
          [r"A, against B's pulse", r"B, against A's pulse", r"Declared bound"],
          loc="upper left", handlelength=2.4, labelspacing=0.3)
fig.tight_layout()
save(fig, "holdover")

# The supplement: one hour on the bench (release 23)
HOUR = DATA / "holdover-r23"
# The holdover's bound (repository: service/Source/PB2Timeline.h): the origin's own uncertainty
# (85 ns, k = 2, typical of the run), a rate term of 0.5 ppm and a drift of 0.5 ppb/s.
ORIGIN_NS, RATE_PPB, DRIFT_PPB_S = 85.0, 500.0, 0.5
# C37.118.2, Table A.6: the PMU time quality code is the decade that bounds the error
CLASS_BOUND_US = {1: 0.1, 2: 1.0, 3: 10.0, 4: 100.0, 5: 1000.0, 6: 10000.0}
with open(HOUR / "unlocked-frames.csv") as f:
    frames = [r for r in csv.DictReader(f) if r["loss"] == "2"]  # the longest loss, 36.8 s
t_frame = np.array([float(r["time_since_loss_s"]) for r in frames])
class_us = np.array([CLASS_BOUND_US[int(r["pmu_time_quality"])] for r in frames])
with open(HOUR / "rejoins.csv") as f:
    rejoins = list(csv.DictReader(f))

fig, ax = plt.subplots(figsize=(COLUMN, 2.3))
t = np.linspace(0, 40, 801)
bound_us = (ORIGIN_NS + RATE_PPB * t + 0.5 * DRIFT_PPB_S * t ** 2) / 1000
keep = t_frame >= 0
ax.step(t_frame[keep], class_us[keep], where="post", color=PALETTE["gray"], lw=0.9, ls=(0, (3, 1.5)), zorder=2)
ax.plot(t, bound_us, color=PALETTE["blue"], lw=1.2, zorder=3)
ax.axvline(10, color=PALETTE["gray"], lw=0.5, ls=(0, (1, 1.5)), zorder=1)
ax.text(10.5, 150, r"unlocked for $10$\,s", fontsize=7, color=PALETTE["gray"], ha="left", va="center")
for r in rejoins:
    x, y = float(r["holdover_s"]), float(r["rejoin_ns"]) / 1000
    ax.plot(x, y, marker="D", ms=4, color=PALETTE["vermillion"], mec="white", mew=0.4, ls="none", zorder=4)
    right = x > 30  # Keep the label inside the axes
    ax.text(x - 0.8 if right else x + 0.8, y, rf"${y:.2f}$\,\si{{\micro\second}}", fontsize=7,
            color=PALETTE["vermillion"], ha="right" if right else "left", va="center")
ax.set_yscale("log")
ax.set_xlim(0, 40)
ax.set_ylim(0.05, 300)
ax.set_yticks([0.1, 1, 10, 100])
ax.set_yticklabels(["$0.1$", "$1$", "$10$", "$100$"])
ax.minorticks_off()
ax.set_xlabel(r"Time since the GNSS signal was lost (\si{\second})")
ax.set_ylabel(r"Time error (\si{\micro\second})")
handles = [Line2D([], [], color=PALETTE["blue"], lw=1.2),
           Line2D([], [], color=PALETTE["gray"], lw=0.9, ls=(0, (3, 1.5))),
           Line2D([], [], marker="D", ms=4, color=PALETTE["vermillion"], mec="white", mew=0.4, ls="none")]
labels = [r"Bound declared by the holdover",
          r"Time-quality class in the C37.118 frames",
          r"Holdover error when the signal returned"]
fig.legend(handles, labels, loc="upper center", ncol=1, bbox_to_anchor=(0.55, 1.0), handlelength=2.6,
           labelspacing=0.3, borderaxespad=0.1)
fig.tight_layout(rect=(0, 0, 1, 0.82))
save(fig, "holdover_hour")
