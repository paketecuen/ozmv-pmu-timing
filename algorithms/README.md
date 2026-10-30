# The algorithms of the paper

Verbatim copies of the ozmV sources that implement what the paper describes, taken from the ozmV repository
at commit `4e0b056e` (2026-10-15, release `pb2-pmu-holdover-20261015-1`). They are not edited here: a change
belongs in ozmV first, and is copied again with its new commit.

## `estimator/`: compiles on its own

Used by `analysis/e3_dating_strategies.cpp` and `analysis/e5_pclass.cpp` (`-I algorithms/estimator`).

| File | Paper | What it implements |
| --- | --- | --- |
| `C37118Decimator.h` | IV-A | The Kaiser low-pass and 16:1 decimation; each point's tap-weighted mean time and its time quality |
| `C37118Estimator.h` | IV-B | The weighted Taylor–Fourier fit: frequency and ROCOF from the second-order model (positive sequence or phase A), the phasor from the first-order sub-model; report timing and time quality |
| `C37118Frame.h`, `MeasurementSample.h` | — | The types the estimator uses |

## `timing/`: reference copies, built inside ozmV

They depend on other ozmV sources (the sample transport, the broker's NMEA parser and a JSON library) and are
kept here to be read, not built.

| File | Paper | What it implements |
| --- | --- | --- |
| `PB2CadenceFit.h` | II-B, II-D | The lower-envelope regression of the polled data-ready stamps and its uncertainty; the sinc³ filter delay |
| `PB2SampleBatch.h` | II-C | The tick-to-UTC model as the service applies it to each sample (`TimeModel::Map`) |
| `live_clock.h`, `live_clock.cpp` | II-C | The broker's anchors, the epoch confirmation by receiver profile, the two-second model and its measured prediction error |
| `PB2SampleState.h` | II-E | Each sample's time and its declared uncertainty |
| `PB2Timeline.h` | V-A, V-D | The holdover and its declared bound, the check at each return of the signal, the provisional window at start-up |
| `WaveformTime.h` | V-B | The waveform stream's per-sample time track and its flags |
