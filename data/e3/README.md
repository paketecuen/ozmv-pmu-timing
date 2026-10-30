# E3: dating strategies on the same real samples

Source: the WMU recording of release 17 (2026-10-14, 40 s of mains on channel 0, 31,502.016 S/s;
`data/raw/r17-wmu.ogg` with its time track `r17-wmu-time.csv`, 80-81 ns k = 2). Program:
`analysis/e3_dating_strategies.cpp`, which runs the production estimator (`service/Source/C37118Estimator.h`,
decimated Taylor-Fourier, 50 frames/s) once per dating strategy.

The reference is our per-sample GPS time. For every other strategy the program gives:
- its time error per sample;
- the error of each report against the reference report with the same UTC label;
- the reports it loses.

The bench crystal runs 40 ppm fast, so a strategy that trusts the nominal period drifts 40 µs a second.

- **`e3-summary.csv`.** One row per strategy: time error RMS and max, reports, reports lost, estimator
  restarts, TVE, FE and RFE (RMS and max).
- **`e3-frames-K.csv`.** Strategy K's per-report errors, for the figures. K follows the summary's order:
  0. ours;
  1. the first sample plus the nominal period (Singh & Parida 2022);
  2. re-alignment at each PPS with the nominal period (SMU; OpenPMU instead locks its trigger to the PPS with a PLL);
  3. re-alignment at each PPS with the previous second's period (Lythgoe 2025);
  4. the polled DRDY tick without the lower-envelope fit (our ablation);
  5. microsecond labels.

## Rerun with the estimator of the paper (2026-10-15, night)

The files here now come from the production estimator after #196, the dual-order fit (main `4e0b056e`); `e3-output.txt` has the console table. The first run, made with the estimator before #196, is in `before-dual-order/`. The results agree to three significant figures. The only visible change is the polled-tick ablation's largest TVE, 2.56e-4 instead of 2.64e-4: the first-order phasor rejects the polling jitter a little better.

`e3-time-errors.csv` is new: every strategy's signed time error against our per-sample time, every 63rd sample (2 ms). Its columns 1-5 follow the order of the strategies above.
