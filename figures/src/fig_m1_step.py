"""Two figures of the converter's step response in equivalent time (M1, data/bench-s01/m1). The PPS goes through
a divider into a converter input; the samples around every rising edge, against their own times, are superposed
over a 30-minute recording on each of two converters of the same design. The fit of a sinc^3 step through the
input's RC network gives the offset between the labels and the input, the converter's delay beyond its filter's
group delay.

m1_step.pdf (the paper): the binned samples of both converters and the same model with the offset at zero,
what the computed delay alone predicts.
m1_residual.pdf (the supplement): the residual of each converter's fit, in bins of 2 us."""
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from ieee_style import COLUMN, PALETTE, apply, save

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis"))
from m1_pps_step_sim import T_MOD_NOM, response  # noqa: E402

DATA = ROOT / "data" / "bench-s01" / "m1"
RECORDINGS = {"A": "A-20261027T082028Z", "B": "B-20261027T091857Z"}   # 1746 and 1780 edges
STYLE = {"A": dict(color=PALETTE["vermillion"], ls="-"), "B": dict(color="black", ls=(0, (3, 1.5)))}

apply()
fig, ax = plt.subplots(figsize=(COLUMN, 2.5))
fig_r, bx = plt.subplots(figsize=(COLUMN, 1.7))
grid = np.linspace(-150e-6, 250e-6, 4001)
for board, name in RECORDINGS.items():
    d = np.loadtxt(DATA / name / "m1-binned-step.csv", delimiter=",", skiprows=1)
    s = json.loads((DATA / name / "m1-summary.json").read_text())
    tau, y = d[:, 0] * 1e-9, d[:, 1]
    offset, rc = s["delay_offset_ns"] * 1e-9, s["rc_us"] * 1e-6
    fit = response(tau - offset, T_MOD_NOM, rc)
    ax.plot(tau * 1e6, y, lw=0.9, zorder=3, label=f"Converter {board} ({s['edges']} edges)", **STYLE[board])
    n = (y.size // 40) * 40                                   # the 50-ns bins averaged into 2-us bins
    r = (y - fit)[:n].reshape(-1, 40).mean(axis=1) * 1e3
    t_r = tau[:n].reshape(-1, 40).mean(axis=1) * 1e6
    bx.plot(t_r, r, lw=0.8, zorder=3, label=f"Converter {board}", **STYLE[board])
    if board == "A":
        model = response(grid - offset, T_MOD_NOM, rc)
        none = response(grid, T_MOD_NOM, rc)
        ax.plot(grid * 1e6, none, color=PALETTE["gray"], lw=0.9, ls=(0, (1, 1.5)), zorder=2,
                label="Same model, no delay beyond the group delay")
        x_fit = grid[np.argmin(np.abs(model - 0.5))] * 1e6
        x_none = grid[np.argmin(np.abs(none - 0.5))] * 1e6
        ax.annotate("", xy=(x_fit, 0.5), xytext=(x_none, 0.5),
                    arrowprops=dict(arrowstyle="<->", lw=0.7, color=PALETTE["blue"], shrinkA=0, shrinkB=0))
        ax.text((x_fit + x_none) / 2, 0.57, rf"${offset * 1e6:.1f}$\,\si{{\micro\second}}", fontsize=7,
                color=PALETTE["blue"], ha="center", va="bottom")
for a in (ax, bx):
    a.set_xlim(-60, 160)
    a.set_xlabel(r"Sample time, from the pulse's UTC second (\si{\micro\second})")
ax.set_ylim(-0.04, 1.06)
ax.set_ylabel("Normalized step")
handles, labels = ax.get_legend_handles_labels()
order = [i for i, l in enumerate(labels) if l.startswith("Converter")] + [i for i, l in enumerate(labels) if not l.startswith("Converter")]
fig.legend([handles[i] for i in order], [labels[i] for i in order], loc="upper center", ncol=1,
           bbox_to_anchor=(0.55, 1.0), handlelength=2.6, labelspacing=0.3, borderaxespad=0.1)
fig.tight_layout(rect=(0, 0, 1, 0.8))
save(fig, "m1_step")
bx.axhline(0, color=PALETTE["gray"], lw=0.5)
bx.set_ylim(-1.0, 1.0)
bx.set_ylabel(r"Residual ($10^{-3}$)")
bx.legend(loc="upper right", handlelength=2.4, labelspacing=0.3)
fig_r.tight_layout()
save(fig_r, "m1_residual")
