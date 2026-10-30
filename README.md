# ozmV: Per-Sample UTC Time With Declared Uncertainty

Supplementary material, data and analysis programs for the manuscript

> F. G. Montoya *et al.*, "ozmV: Per-Sample UTC Time With Declared Uncertainty in an
> Open Synchrophasor and Waveform Instrument," submitted to *IEEE Transactions on
> Instrumentation and Measurement*, 2026.

All authors are with the Universidad de Almería, Spain. Corresponding author:
Francisco G. Montoya.

The manuscript treats the UTC time of each sample of ozmV, an open synchrophasor,
waveform and power-quality instrument built on a free-running delta–sigma converter,
as a measurand. A real-time unit stamps every conversion and every GNSS pulse per
second on one counter; a lower-envelope regression removes the polling delay, a model
built from the pulses maps ticks to UTC, and the converter's delay, measured with the
pulse applied to one of its inputs, is subtracted. Every sample carries an expanded
uncertainty evaluated in service (81–87 ns, k = 2, for the instant of conversion;
140 ns for the input time of a voltage sample, validated against a PPS-triggered
reference digitizer). A synchrophasor estimator uses these times without resampling,
and the time survives losses of the satellite signal with a declared bound that a
second instrument confirmed through losses of up to 11 minutes.

## Contents

| Path | Content |
|---|---|
| `supplementary.pdf` | Supplementary material, sections S-I to S-VII: the bench and its passes, the converter's delay and the checks of its fit, two instruments on one signal, the effect of the time-stamping method, the P-class tests, latency and cost, the holdover through losses of the signal, the start-up window. |
| `data/` | The data behind every figure and table, each folder with a README of its origin and of the commands that reproduce its results. |
| `analysis/` | The programs behind the numbers (Python 3 with numpy and scipy; C++17 for the estimator runs). |
| `algorithms/` | Verbatim copies of the instrument's sources that implement the paper's algorithms (sample timing, estimator), with their commit. |
| `bench/` | The capture programs of the bench sessions: the two Fluke digitizers (`fluke_capture.py`), the board's recorder and the holdover logger. |
| `figures/` | The figure scripts (`src/`, matplotlib with the text set by LaTeX) and the figures as vector PDF (`out/`), for the paper and the supplement. |

### Where each result comes from

| Paper | Supplement | Data | Program |
|---|---|---|---|
| Sec. II-B, polling lateness and regression | — | `data/raw/` | `analysis/e3_dating_strategies.cpp` (ablation) |
| Sec. III-A, the converter's delay (Fig. 2, Table I) | S-II | `data/bench-s01/m1/`, `data/bench-s02/m1/` | `analysis/s01_wmu_batch.py`, `m1_pps_delay.py`, `m1_models.py`, `m1_windows.py` |
| Sec. III-B, the channel's delay (Table I) | S-I | `data/bench-s01/m2/`, `m3/` | `analysis/s01_m2.py`, `s01_m3.py`, `m2_fluke_phase.py` |
| Sec. III-B, two instruments on one signal | S-III | `data/bench-s01/ab/`, `data/bench-s02/ab/` | `analysis/s01_ab_wmu.py`, `s01_ab_pmu.py` |
| Sec. IV-B/C, dual-order fit and P-class tests (Fig. 3, Table II) | S-V | `data/e5/`, `data/e7/` | `analysis/e5_pclass.cpp`, `e5_factorial.sh`, `e7_channel_snr.cpp`, `phasor_filters.py` |
| Sec. IV-D, the time-stamping method on real data (Fig. 4) | S-IV | `data/e3/` | `analysis/e3_dating_strategies.cpp` |
| Sec. V-A, the drift allowance | S-VI | `data/gps-sky-20261015/` | `analysis/crystal_drift.py` |
| Sec. V-B, one hour of holdover | S-VI (Fig. S2) | `data/holdover-r23/` | `figures/src/fig_holdover.py` |
| Sec. V-B, losses of 1, 5 and 10 min (Fig. 5) | S-VI (Table S-VI) | `data/bench-s02/e4/` | `analysis/s02_e4.py`, `s02_e4_status.py` |
| Sec. V-C, the start-up window | S-VII (Fig. S3) | `data/boot-*.csv` | `figures/src/fig_startup.py` |

## Reproducing

```sh
make figures        # figures/out/*.pdf (Python 3 with numpy and matplotlib; TeX Live with newtx and siunitx)
```

The commands that produce each data folder from the raw recordings are in that folder's
README. The raw bench recordings (11 GB of waveform streams and C37.118 captures per
session) are not in this repository; `data/bench-s01/README.md` and
`data/bench-s02/README.md` list them with their checksums, and they are available from the
corresponding author. `analysis/wmu-time.py`, the reader of the waveform stream's time
track, is a copy of the instrument's own tool.

## Licenses

The programs are released under the GNU General Public License v3.0 or later (`LICENSE`);
the data and the figures under the Creative Commons Attribution 4.0 International License
(`LICENSE-DATA.md`). The instrument's own repository (firmware, software and hardware
design) is published separately.
