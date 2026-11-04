# ozmV: UTC Timestamping of a Free-Running Delta–Sigma ADC

Supplementary material, data and analysis programs for the manuscript

> F. G. Montoya, F. J. Moreno-Manzano, A. Alcayde, M. A. Andres-Asensio and
> F. M. Arrabal-Campos, "UTC Timestamping of a Free-Running Delta–Sigma ADC With Evaluated Uncertainty: Method and Validation on the ozmV Synchrophasor Instrument," submitted to *IEEE Transactions on
> Instrumentation and Measurement*, 2026.

All authors are with the Universidad de Almería, Spain. Corresponding author:
Francisco G. Montoya.

The manuscript treats the sampling instant of a free-running delta–sigma ADC as a
measurand. In ozmV, a synchrophasor, waveform and power-quality instrument, the
programmable real-time unit (PRU) of the processor timestamps every conversion and every
GNSS pulse per second (PPS) on one counter. A lower-envelope regression compensates the
polling delay, the PPS reference points map the counter to UTC, and the ADC delay,
measured with the PPS applied to one of its inputs, is compensated. Every sample carries
an expanded uncertainty (k = 2) evaluated according to the GUM: 83–85 ns for the
conversion instant and 140 ns for the input-referred time of a voltage sample, validated
with PPS-triggered reference digitizers. A Taylor–Fourier synchrophasor estimator uses
these timestamps without resampling, and a holdover with a declared error bound was
verified with a second prototype through GNSS outages of up to 11 minutes.

## Contents

| Path | Content |
|---|---|
| `supplementary.pdf` | Supplementary material, sections S-I to S-VII: the laboratory setup and its measurement passes, the ADC delay and the checks of its fit, two prototypes on one signal, the effect of the timestamping method on recorded data, the numerical P-class tests, reporting latency and computational load, the holdover through GNSS outages, and the measurements before GNSS time acquisition. |
| `data/` | The data behind every figure and table, each folder with a README of its origin and of the commands that reproduce its results. |
| `analysis/` | The programs behind the numbers (Python 3 with numpy and scipy; C++17 for the estimator runs). |
| `algorithms/` | Verbatim copies of the sources of ozmV that implement the algorithms of the paper (sample timing, estimator), with their commit. |
| `bench/` | The capture programs of the laboratory sessions: the two Fluke digitizers (`fluke_capture.py`), the recorder of the prototype and the holdover logger. |
| `figures/` | The figure scripts (`src/`, matplotlib with the text set by LaTeX) and the figures as vector PDF (`out/`), for the paper and the supplement. |

### Where each result comes from

| Paper | Supplement | Data | Program |
|---|---|---|---|
| Sec. II-B, compensation of the polling delay | — | `data/raw/` | `analysis/e3_dating_strategies.cpp` (ablation) |
| Sec. III-B, the ADC delay (Fig. 2, Table III) | S-II (Fig. S1, Table S-IV) | `data/bench-s01/m1/`, `data/bench-s02/m1/` | `analysis/s01_wmu_batch.py`, `m1_pps_delay.py`, `m1_models.py`, `m1_windows.py` |
| Sec. III-C, the analog front-end delay (Tables II and III) | S-I (Tables S-I to S-III) | `data/bench-s01/m2/`, `m3/` | `analysis/s01_m2.py`, `s01_m3.py`, `m2_fluke_phase.py` |
| Sec. III-E, two prototypes on one signal | S-III (Table S-V) | `data/bench-s01/ab/`, `data/bench-s02/ab/` | `analysis/s01_ab_wmu.py`, `s01_ab_pmu.py` |
| Sec. IV-B/C, dual-order estimator and numerical P-class tests (Fig. 3, Table IV) | S-V | `data/e5/`, `data/e7/` | `analysis/e5_pclass.cpp`, `e5_factorial.sh`, `e7_channel_snr.cpp`, `phasor_filters.py` |
| Sec. IV-E, the timestamping method on recorded data (Fig. 4) | S-IV | `data/e3/` | `analysis/e3_dating_strategies.cpp` |
| Sec. V-A, the drift allowance | S-VI | `data/gps-sky-20261015/` | `analysis/crystal_drift.py` |
| Sec. V-A, one hour of holdover | S-VI (Fig. S2) | `data/holdover-r23/` | `figures/src/fig_holdover.py` |
| Sec. V-C, outages of 1, 5 and 10 min (Fig. 5) | S-VI (Table S-VI) | `data/bench-s02/e4/` | `analysis/s02_e4.py`, `s02_e4_status.py` |
| Sec. V-D, measurements before GNSS time acquisition | S-VII (Fig. S3) | `data/boot-*.csv` | `figures/src/fig_startup.py` |

## Reproducing

```sh
make figures        # figures/out/*.pdf (Python 3 with numpy and matplotlib; TeX Live with newtx and siunitx)
```

The commands that produce each data folder from the raw recordings are in that folder's
README. The raw bench recordings (11 GB of waveform streams and C37.118 captures per
session) are not in this repository; `data/bench-s01/README.md` and
`data/bench-s02/README.md` list them with their checksums, and they are available from the
corresponding author. `analysis/wmu-time.py`, the reader of the time track of the waveform
stream, is a copy of the tool of ozmV.

## Licenses

The programs are released under the GNU General Public License v3.0 or later (`LICENSE`);
the data and the figures under the Creative Commons Attribution 4.0 International License
(`LICENSE-DATA.md`). The software and firmware of ozmV will be released separately at a
later stage.
