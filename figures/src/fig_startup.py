"""Fig.: measuring at once and time-stamping later after a reboot (20 reboots after the change; data/boot-after-r20.csv).
A typical boot, from the means of the 20: the error of the time stored with each measurement, before and
after the change. The gap the stored data show across each reboot (data/boot-baseline-r19.csv against
boot-after-r20.csv) is given in the text; it was panel (b) of this figure until the paper was trimmed."""
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

from ieee_style import COLUMN, PALETTE, apply, save

DATA = Path(__file__).resolve().parents[2] / "data"


def load(name):
    with open(DATA / name) as f:
        return list(csv.DictReader(f))


after = load("boot-after-r20.csv")


def mean(rows, key):
    return float(np.mean([float(r[key]) for r in rows]))


floor_s = mean(after, "clock_floor_mono_s")      # the clock restored to the time saved at shutdown
start_s = mean(after, "service_start_mono_s")    # the measurement service starts
commit_s = start_s + mean(after, "window_s")     # the GNSS time is known: the held window is time-stamped
step_s = mean(after, "chrony_step_mono_s")       # chrony steps the system clock
error_s = mean(after, "step_s")                  # how far behind the restored clock was

apply()
fig, ax = plt.subplots(figsize=(COLUMN, 2.05))

# The error of the time stored with the data, against the time since the kernel started
ax.axvspan(start_s, commit_s, color=PALETTE["blue"], alpha=0.12, lw=0, zorder=1)
ax.text((start_s + commit_s) / 2, -7.2, "held, then\ntime-stamped", fontsize=7, ha="center", va="center",
        color=PALETTE["blue"], linespacing=1.0)
ax.plot([floor_s, step_s, step_s, 35], [-error_s, -error_s, 0, 0], color=PALETTE["gray"], lw=0.9,
        ls=(0, (1, 1.5)), zorder=2)
ax.plot([start_s, commit_s, commit_s, 35], [-error_s, -error_s, 0, 0], color=PALETTE["vermillion"], lw=1.1,
        ls=(0, (4, 1.5)), zorder=3)
ax.plot([start_s, 35], [0, 0], color=PALETTE["blue"], lw=1.3, zorder=4)
# The events, written where they happen on the curves
ax.text(floor_s + 0.2, -error_s + 0.7, "clock restored", fontsize=6.5, ha="left", va="bottom")
ax.text(start_s + 0.25, -error_s - 0.7, "service starts", fontsize=6.5, ha="left", va="top")
ax.text(step_s + 0.4, -error_s / 2, "clock stepped\nby chrony", fontsize=6.5, ha="left", va="center",
        linespacing=1.0)
ax.set_xlim(8, 35)
ax.set_ylim(-18, 2.5)
ax.set_yticks([-15, -10, -5, 0])
ax.set_yticklabels(["$-15$", "$-10$", "$-5$", "$0$"])
ax.set_xlabel(r"Time since the kernel started (\si{\second})")
ax.set_ylabel(r"Time error (\si{\second})")

handles = [Line2D([], [], color=PALETTE["gray"], lw=0.9, ls=(0, (1, 1.5))),
           Line2D([], [], color=PALETTE["vermillion"], lw=1.1, ls=(0, (4, 1.5))),
           Line2D([], [], color=PALETTE["blue"], lw=1.3)]
labels = ["System clock", "Stored time, labelled by the system clock until GNSS time",
          "Stored time, measured at once and time-stamped later"]
fig.legend(handles, labels, loc="upper center", ncol=1, bbox_to_anchor=(0.55, 1.0), handlelength=2.8,
           labelspacing=0.3, borderaxespad=0.1)
fig.tight_layout(rect=(0, 0, 1, 0.776))
save(fig, "startup")
