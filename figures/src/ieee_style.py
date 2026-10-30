"""Matplotlib style for IEEE Transactions figures: text, numbers and symbols typeset
by LaTeX in the journal's Times at 8 pt, one-column (3.5 in) and two-column
(7.16 in) widths, vector PDF.

Usage in a figure script:
    from ieee_style import apply, COLUMN, DOUBLE, save
    apply()
    fig, ax = plt.subplots(figsize=(COLUMN, 2.2))
    ...
    save(fig, "boot_timeline")      # writes figures/out/boot_timeline.pdf
"""
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt

COLUMN = 3.5    # in, IEEE single column
DOUBLE = 7.16   # in, IEEE double column
OUT = Path(__file__).resolve().parent.parent / "out"


# Every text, number and symbol is typeset by LaTeX with the paper's own fonts:
# newtx gives the Times text and math of IEEEtran, siunitx the units. Strings in
# the figure scripts are therefore LaTeX ("10\\,\\%", "$\\Delta t$", "\\si{\\milli\\second}").
PREAMBLE = r"\usepackage[T1]{fontenc}\usepackage{newtxtext}\usepackage{newtxmath}\usepackage{siunitx}"


def apply():
    mpl.rcParams.update({
        "text.usetex": True,
        "text.latex.preamble": PREAMBLE,
        "font.family": "serif",
        "font.size": 8,
        "axes.labelsize": 8,
        "axes.titlesize": 8,
        "legend.fontsize": 7,
        "xtick.labelsize": 7,
        "ytick.labelsize": 7,
        "axes.linewidth": 0.6,
        "lines.linewidth": 1.0,
        "lines.markersize": 3,
        "xtick.major.width": 0.6,
        "ytick.major.width": 0.6,
        "xtick.direction": "in",
        "ytick.direction": "in",
        "axes.grid": True,
        "grid.linewidth": 0.3,
        "grid.alpha": 0.5,
        "legend.frameon": False,
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.02,
        # LaTeX embeds its Type 1 fonts, as IEEE PDF eXpress expects.
    })


# Okabe-Ito colours: distinguishable with colour-vision deficiencies. Every
# series also differs in line style or marker, so the figures read in grayscale.
PALETTE = {
    "blue": "#0072B2",
    "vermillion": "#D55E00",
    "green": "#009E73",
    "orange": "#E69F00",
    "sky": "#56B4E9",
    "purple": "#CC79A7",
    "black": "#000000",
    "gray": "#7F7F7F",
}


def panel(ax, label, text=None):
    """IEEE-style panel label, e.g. "(a)", optionally followed by a short description."""
    ax.text(0.015, 0.97, label + ("  " + text if text else ""), transform=ax.transAxes,
            ha="left", va="top", fontsize=8)


def save(fig, name):
    OUT.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT / f"{name}.pdf")
    plt.close(fig)
