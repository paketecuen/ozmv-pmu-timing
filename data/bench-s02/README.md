# Bench session 02 (2026-10-29): M4 with the branches swapped and E4 both ways

Source: Fran's session at his laboratory, following `bench/kit-fran/SESION-02.md`. His session record, incident
note and logs are in `fran/` (from his branch `fran/kit-sesion-02`). Two PB2 boards, A and B, each with its own
receiver, on the same mains phase. This session validates the GNSS timing for the paper. The bench boards'
acquisition failures while cabling, and B's `ramoops` oops at a later power-on, are the prototypes' own matters:
they are listed in `fran/incidencias.md` and left for the complete units.

The raw data stay out of the repository (9.2 GB: 21 WMU recordings with their time tracks, 21 PMU pcaps, the
boards' 2 s status logs of E4). They are Fran's Drive folder `sesion-02`, downloaded by Paco on 2026-10-30 as
`sesion-02-20261030T124230Z-1-001.zip` to `-005.zip`. Their SHA-256 match: A 22/22 plus 4 private files, B 20/20
plus 3. The status logs also hold the receivers' position, so only their timing fields are kept here
(`e4/status/`, `analysis/s02_e4_status.py`).

## Passes (UTC, from `fran/registro-sesion.md`)

| Time | What |
| --- | --- |
| 08:02 | Both recorders start, with the 64 MB memory reserve |
| 08:12-09:14 | Mains on the original branches. At times the Flukes (M2 wiring) and the oscilloscope hang on the PPS lines. Oscilloscope check b on A at about 09:00: no visible difference between the PPS wire and the 8558A's input |
| 09:49 | Branches swapped (VA_A and VA_B, same polarity), mains on |
| 09:51-10:21 | **M4-r02**: nothing but the boards on the PPS lines |
| 10:25-10:37 | E4-A cabling: IN_A takes PPS_B through its divider; A's PRU keeps PPS_A |
| 10:53-11:33 | **E4-A**: A's antenna covered for 1, 5 and 10 min; B is the reference |
| 11:34-11:36 | E4-B cabling: IN_B takes PPS_A |
| 11:38-12:42 | **E4-B**: B's antenna covered for 1, 5 and 10 min; A is the reference |

## Results

**M1 again, each board's own PPS on IN** (`analysis/s01_wmu_batch.py`, `wmu/summary.json`, `m1/`). The recordings
used end before the E4 cabling (10:25) and have at least 1,000 edges:

| Board | Recordings | Edges | Offset (ns) | Spread | t_MOD | Session 01 |
| --- | --- | --- | --- | --- | --- | --- |
| A | 4 | 7,103 | 11,867.9 | 30.6 ns | 47.85 | 11,876.9 |
| B | 4 | 7,137 | 11,830.0 | 33.5 ns | 47.70 | 11,899.5 |

The converter's extra latency is about 48 t_MOD on both boards, both days, within 70 ns. The IN step and its RC
are as in session 01 (339 and 336 mV; 32.79 and 32.67 µs).

**M4-r02, A against B with the branches swapped and nothing on the PPS lines** (`analysis/s01_ab_wmu.py`,
`analysis/s01_ab_pmu.py`, `ab/`):

| Period | Seconds (WMU) | B minus A, mean | RMS | 5-minute means |
| --- | --- | --- | --- | --- |
| 08:12-09:14, original branches, instruments at times | 3,477 | 746.7 ns | 181.5 ns | 453-960 ns |
| 09:51-10:21, M4-r02 | 1,756 | 504.8 ns | 71.5 ns | 473-529 ns |
| 10:21-12:42, swapped, during E4 (cross PPS cabling) | 5,977 | 499.7 ns | 114.8 ns | 361-765 ns |

- The PMU gives 503.6 ns for M4-r02.
- For the same seconds, the WMU and the PMU agree to 2.0 ns RMS (mean 0.0 ns).
- Amplitude B/A 0.99969; per-second noise of B minus A 17.8 ns.
- M2 (session 01) put the two VA channels 487.4 ns apart (72,712.6 against 72,225.2 ns). With nothing on the PPS lines,
  the boards disagree by 17 ns beyond that, and the swap of the branches leaves the difference where it was: it is
  in the boards' channels, not in the wiring.
- With instruments hanging on a PPS line, B minus A moves by hundreds of ns (here and in session 01). A load on the
  line delays the edge that the board sees, and the Fluke on the same node sees the same edge, so M2 is not
  affected; the time against UTC is.

**E4, the error through each loss against the other board's PPS on IN** (`analysis/s02_e4.py`, `e4/`). The error
is the offset minus the median of the 5 minutes of GPS before the loss in the same recording. The bound is the one
the board declared for that second, and the return is the board's own `LastRejoinNs`:

| Board without GNSS | Loss (s) | Error at the end | Largest error | Bound at the end | Error / bound at the end | Board's own return |
| --- | --- | --- | --- | --- | --- | --- |
| A (10:58:49) | 100 | 1,735 ns | 2,037 ns | 52.9 µs | 0.033 | 1,850 ns |
| A (11:05:36) | 349 | 13,747 ns | 13,747 ns | 206.1 µs | 0.067 | 13,632 ns |
| A (11:17:45) | 647 | 40,394 ns | 40,397 ns | 430.8 µs | 0.094 | 40,523 ns |
| B (11:49:25) | 98 | 3,573 ns | 3,573 ns | 51.6 µs | 0.069 | 3,421 ns |
| B (11:58:29) | 331 | 13,794 ns | 14,075 ns | 193.8 µs | 0.071 | 14,132 ns |
| B (12:26:06) | 660 | 22,045 ns | 22,097 ns | 440.6 µs | 0.050 | 21,965 ns |

- The declared bound covers the measured error at every second. The largest ratio, 0.60, comes in the first
  seconds of a loss, where the bound is smallest and the receivers' noise dominates the error.
- The board's own return check agrees with the independent measurement to 80-340 ns.
- After each return the labels are back on the base within ±104 ns. Returns beyond 10 µs are time steps (the WMU
  flags them as gaps: 11:11:24, 11:28:31, 12:03:59, 12:37:05).
- The error grows faster than linearly, as a drifting crystal does. The change of the measured period across each
  loss predicts, for a linear drift, 2.1, 10.6 and 28.9 µs on A and 1.4, 6.5 and 7.9 µs on B. That is the right
  order for A; B drifted more than linearly.
- `e4/curves/` holds the error and the bound second by second for each loss, for the figure.
- Shorter spontaneous holdovers of about 10 s during the day are in `e4/e4-summary.txt`, against the board's own
  PPS before the E4 cabling and the other board's after it. One of them, at 11:13, happened on both receivers at
  once.

**Discontinuities** (`analysis/s01_discontinuities.py`, `discontinuities.txt`). Lost samples come only at the
cabling (Fran's list), plus the four planned time steps of the GNSS returns above.

## Files

| Path | Contents |
| --- | --- |
| `fran/` | Fran's session record, incident note, preparation and failure logs, the M4-r02 status |
| `wmu/summary.json`, `wmu/per-second/` | Per recording: time-track counts, M1 fit, each second's IN edge against the labels |
| `m1/<board>-<file>/` | The M1 fit of each recording and its binned equivalent-time step |
| `ab/` | B minus A per UTC second, WMU and PMU, with the PMU summary |
| `e4/curves/` | Error and declared bound per second through each loss of 1, 5 and 10 minutes |
| `e4/e4-summary.txt` | Every holdover of the session: error, bound, the board's return, drift prediction |
| `e4/status/` | The 2 s status polls of both boards during E4-A and E4-B, timing fields only |
| `discontinuities.txt` | Lost samples and file ends per board |

## Reproducing

`<s02>` is the extracted `sesion-02` folder; `<work>` holds the time tracks of the first command.

```sh
OZMV=~/Documents/repos/ozmV python3 analysis/s01_wmu_batch.py <s02> <work> 20261029      # about 25 min
python3 analysis/s01_ab_wmu.py <s02> <work> ab/wmu-ab-1s.csv 20261029                    # about 15 min
python3 analysis/s01_ab_pmu.py <s02> ab/pmu-ab-1s.csv 20261029
for b in A B; do for p in e4-A-s02 e4-B-s02; do
    python3 analysis/s02_e4_status.py <s02>/$b/privado/$p.log e4/status/$b-$p-status.csv; done; done
python3 analysis/s02_e4.py <work> e4/status e4/curves
python3 analysis/s01_discontinuities.py <work> 20261029
```
