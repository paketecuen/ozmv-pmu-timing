# Bench session 01 (2026-10-27): two ozmV on the laboratory mains

Source: Fran's session at his laboratory, following `BANCO-para-Fran.md` (revision 2026-10-20). Two PB2 boards,
A and B, each with its old-main ADC board and its own GNSS receiver, measured the same mains phase. Each board's PPS
went to its PRU and, through a divider, to its IN input (CN5, AIN0); the WMU recorded IN in ADC volts
(`RawCurrentInputs`). The Fluke 8588A (master) digitised the mains on 1000 V, or the PPS for M3; the 8558A (slave,
triggered by the master) digitised the PPS on 10 V. Analysed on 2026-10-28.

The raw data stay out of the repository (11 GB: 24 WMU recordings with their time tracks, 24 PMU pcaps, 1,000 Fluke
burst files). They are Fran's Drive folder `sesion-01`, downloaded by Paco on 2026-10-28 as
`sesion-01-20261028T211656Z-1-001.zip` to `-007.zip`; every recording matches `fran/A-SHA256SUMS` and
`fran/B-SHA256SUMS`.

## Passes (UTC)

| Time | Pass | PPS on the 8558A | Trigger out |
| --- | --- | --- | --- |
| 07:50 | Both recorders start (WMU, PMU, status each minute) | | |
| 08:23-08:27, 08:39-08:43 | M3 on 10 V and 1000 V, twice | B, on both Flukes | ACO |
| 08:55-09:14, 09:17-09:36 | M2-A, twice | A | ACO |
| 10:50-11:01 | Aperture diagnostics (`fran/fluke-logs/d*.log`), not a pass | B, on both | ACO |
| 11:02-11:06 | M3 on 10 V and 1000 V | A, on both | APE |
| 11:14-11:33 | M2-A | A | APE |
| 11:38-11:57 | M2-B | B | APE |
| 12:06-12:28 | M3 on 10 V and 1000 V | B, on both | APE |
| 12:02-12:18 | Copy of the recordings by `scp`: B reboots at 12:11, A at 12:18 | | |
| 12:32 | `STOP`; recorders closed at 12:42 (B) and 12:49 (A) | | |
| 12:38:46 | Mains removed from both boards | | |

M1 ran through the whole day: each board's own PPS stayed on its IN.

## Results

**M3, the slave's lag behind the master** (`analysis/s01_m3.py`, `m3/m3-results.txt`). The 8588A's default trigger
output, ACO, is a pulse at the end of each acquisition, so the slave starts one aperture (49 µs) late; with APE (a
square wave active during the acquisition) it starts with the master. Fran found it (`fran/M3-nota.md`).

| Pass | Master range | Trigger out | Edges | Lag (ns) |
| --- | --- | --- | --- | --- |
| m3-10V-antes, -r02 | 10 V | ACO | 53, 59 | 49,124.5 ± 21.0, 49,183.1 ± 14.7 |
| m3-1000V-antes, -r02 | 1000 V | ACO | 57, 54 | 49,329.8 ± 32.2, 49,322.2 ± 42.4 |
| m3-10V-ape, -despues-ape | 10 V | APE | 58, 57 | 124.1 ± 12.9, 112.3 ± 13.3 |
| m3-1000V-ape, -despues-ape | 1000 V | APE | 52, 552 | 315.4 ± 38.2, 326.4 ± 8.9 |

M2 subtracts 325.8 ns (APE, the weighted mean on 1000 V) or 49,327.0 ns (ACO on 1000 V). The master's 1000 V range
adds about 200 ns against 10 V.

**M1, the labels against each board's own PPS on IN** (`analysis/s01_wmu_batch.py`, `wmu/summary.json`,
`m1/<board>-<file>/`). The offset is how late the labels are against the input, beyond the 190.5 t_MOD the instrument
subtracts (t_MOD = 248.0 ns), fitted with a sinc3 step through a one-pole RC.

| Board | Recordings | Edges | Offset (ns) | Spread between files | t_MOD | RC of IN (µs) |
| --- | --- | --- | --- | --- | --- | --- |
| A | 10 | 16,493 | 11,876.9 | 20.2 ns (11,839.8-11,897.2) | 47.89 | 32.775 |
| B | 9 | 15,240 | 11,899.5 | 28.6 ns (11,851.3-11,930.8) | 47.98 | 32.668 |

- Counted: recordings with at least 1,000 edges. B's first recording (07:50-08:20) is left out: its PPS on IN bounced
  and changed level while the M3 cables were connected.
- The offset does not change with the boot (A booted five times).
- The datasheet's open 44 t_MOD exists; the converter's latency is about 48 t_MOD.
- The split between latency and RC rests on the one-pole model (`m1/robustness.txt`). The data support it: a free
  second pole collapses to 1 ns, and a feed-through variant gives 11,594 ns with a negative (unphysical) feed.
- Level windows move the offset by 11,867-11,932 ns. The model-free centroid, offset plus RC, is 44.73 µs (A) and
  44.64 µs (B).
- Second by second (one file): the edges on the bare sample grid scatter 32.7 ns (measurement and pulse), on the
  labels 37.8 ns. The labels' own noise is 14.6 ns.

**M2, the delay of VA against the Fluke** (`analysis/s01_m2.py`, `m2/`). The labels of the board's VA samples are
late by:

| Pass | Board | Trigger out | Windows of 0.2 s | Delay (ns) | u from 60 bursts (ns) |
| --- | --- | --- | --- | --- | --- |
| m2-A-VA | A | ACO | 2,895 | 72,158.9 | 11.2 |
| m2-A-VA-r02 | A | ACO | 2,768 | 72,174.4 | 12.9 |
| m2-A-VA-ape | A | APE | 2,940 | 72,225.2 | 4.9 |
| m2-B-VA-ape | B | APE | 2,882 | 72,712.6 | 8.0 |

- The ACO passes also carry the lag's 26 ns.
- Without M1's digital part, VA's analogue chain (divider, AMC1350, RC) takes about 60.35 µs on A and 60.81 µs on B.
  At 50 Hz, the whole 72 µs is 1.3°: VA needs a phase calibration per board.
- Not yet bounded: the skew of the PPS between the board's input and the 8558A's input (session 02 measures it with
  the oscilloscope).

**A against B with independent receivers** (`analysis/s01_ab_wmu.py`, `analysis/s01_ab_pmu.py`, `ab/`). This is M4
without the swap of the voltage branches. 15,765 s of the WMU with the mains on both, and 793,382 PMU frame pairs
locked on both:

- B's phase lags A's by 584.8 ns (WMU) and 586.5 ns (PMU) on average, 209 ns RMS, 2.5-97.5 % between 261 and 1,003 ns.
  Its second-to-second noise is 21.7 ns.
- For the same seconds, the PMU and the WMU give the same B minus A to 2.0 ns RMS (mean 0.0 ns).
- Amplitude B/A 0.99971; frequency 1.0e-4 Hz RMS (largest 5.2e-4 Hz); ROCOF 0.023 Hz/s RMS (largest 0.11 Hz/s).
- Each board's labels against its own PPS stay within ±50 ns all day (10-minute medians of M1). The slow wander of
  B minus A, 0.28-1.06 µs in 5-minute means, is therefore the receivers or the PPS distribution. B minus A stepped by
  -0.27 µs at 11:35, when the 8558A moved from PPS_A to PPS_B. M2 gives an instrument difference of 0.49 µs.

**Holdover and provisional time against the board's own PPS** (`analysis/s01_holdover.py`,
`holdover/episodes.txt`, `holdover/rejoins.txt`):

- In 45 holdovers of 10-29 s (838 s in all) the receiver kept its pulse on IN. The largest error was 993 ns. The
  worst fraction of the declared bound was 0.49, apart from one disturbed second during the M3 cabling (B, 08:17:33:
  319 ns against 107 ns). The GNSS returned within 126 ns.
- The board's own return distance (`LastRejoinNs` in `recorder.log`) agrees with the IN measurement to 10-70 ns; the
  board measures one second later.
- A lost the GNSS for 313 s (09:56:27-10:01:40; satellites 3 to none). Its receiver stopped the pulse, so IN gives no
  reference there. The board's return distance was 38 ns, against a declared bound of 182 µs.
- In 21 provisional (syncing) stretches after restarts, the labels stayed within 135 ns of the PPS, 704 ns during
  the cabling.

**Discontinuities and reboots** (`analysis/s01_discontinuities.py`, `discontinuities.txt`, `fran/incidencias.md`;
ozmV issue #203):

- A rebooted at 08:52, 09:41, 11:12 and 12:18, B at 08:48 and 12:11. Each reboot followed a fourth acquisition
  failure in its boot; the M4 opens three source recovery windows per boot.
- Five failures hit both boards in the same second: 08:14:29, 08:47:16, 08:51:15-17, 11:11:15-28 and 11:35:08-16.
  The PPS and the mains are clean in both recordings at those instants.
- Fran links one to suspending the PC that both boards were attached to, and another to releasing the Flukes'
  ground clips.
- With the recorder running: about 10 MB free and 790 MB of page cache on both boards. In the same second as lost
  packets, the kernel logged `SLUB ... GFP_ATOMIC ... skbuff_head_cache`.
- The `1 ms` disagreement between the PMUs from 12:39 to 12:42 is noise, not time: the mains had been removed
  (|VA| 0.05 V).

## Files

| Path | Contents |
| --- | --- |
| `wmu/summary.json` | Per recording: time-track counts (GPS, holdover, gap, invalid, without time), M1 fit, per-second statistics |
| `wmu/per-second/` | Per recording: each second's IN edge against the labels (RC fixed at that recording's fit), the flags, the declared U |
| `m1/<board>-<file>/` | The M1 fit of each recording and its binned equivalent-time step (the figure's data) |
| `m1/robustness.txt` | One recording under other input models, level windows and halves; grid against labels |
| `m2/` | Per pass: each burst's mean delay, its PPS edges, the time base residual and the PC clock against the PPS; `m2-results.txt` |
| `m3/m3-results.txt` | Every M3 pass |
| `ab/` | B minus A per UTC second: WMU (`wmu-ab-1s.csv`) and PMU (`pmu-ab-1s.csv`), with their summaries |
| `holdover/` | Every holdover and provisional stretch against the PPS; the board's own return distances |
| `discontinuities.txt` | Lost samples and file ends per board |
| `fran/` | Fran's notes (`incidencias.md`, `M3-nota.md`), checksums, copy logs, `recorder.log` of both boards (NUL bytes removed) and the Fluke logs (UTF-16 converted to UTF-8) |

## Reproducing

`<s01>` is the extracted `sesion-01` folder; `<work>` holds the time tracks the first command writes (3 GB of
temporary decoding per recording, deleted as it goes).

```sh
OZMV=~/Documents/repos/ozmV python3 analysis/s01_wmu_batch.py <s01> <work>       # about 30 min
python3 analysis/s01_m3.py <s01>/fluke m3-10V-antes m3-1000V-antes m3-10V-antes-r02 m3-1000V-antes-r02 \
    m3-10V-ape m3-1000V-ape m3-10V-despues-ape m3-1000V-despues-ape
python3 analysis/s01_m2.py <s01>/fluke/m2-A-VA-ape 325.8 m2/m2-A-VA-ape-bursts.csv \
    <s01>/A/wmu-20261027T111335Z.ogg <work>/A-20261027T111335Z-time-gps.csv
python3 analysis/s01_m2.py <s01>/fluke/m2-B-VA-ape 325.8 m2/m2-B-VA-ape-bursts.csv \
    <s01>/B/wmu-20261027T111954Z.ogg <work>/B-20261027T111954Z-time-gps.csv \
    <s01>/B/wmu-20261027T115008Z.ogg <work>/B-20261027T115008Z-time-gps.csv
python3 analysis/s01_m2.py <s01>/fluke/m2-A-VA 49327.0 m2/m2-A-VA-bursts.csv \
    <s01>/A/wmu-20261027T085307Z.ogg <work>/A-20261027T085307Z-time-gps.csv
python3 analysis/s01_m2.py <s01>/fluke/m2-A-VA-r02 49327.0 m2/m2-A-VA-r02-bursts.csv \
    <s01>/A/wmu-20261027T085307Z.ogg <work>/A-20261027T085307Z-time-gps.csv \
    <s01>/A/wmu-20261027T092254Z.ogg <work>/A-20261027T092254Z-time-gps.csv
python3 analysis/s01_ab_wmu.py <s01> <work> ab/wmu-ab-1s.csv                       # about 15 min
python3 analysis/s01_ab_pmu.py <s01> ab/pmu-ab-1s.csv
python3 analysis/s01_holdover.py <work>
python3 analysis/s01_discontinuities.py <work>
python3 analysis/m1_models.py <work>/B-20261027T084909Z-m1/m1-equivalent-time-step.csv
```

`analysis/m1_windows.py` and `analysis/s01_grid_vs_labels.py` take the decoded stream (`ffmpeg ... -f s32le`) and
its GPS-only time track; `analysis/s01_event_look.py` prints the mains and IN around an instant.

A lesson that cost a wrong first result: absolute UTC labels (1.79e18 ns) must be differenced as integers before
any float64 arithmetic, which has 256 ns steps at that size.
