# E5: P class in simulation (IEC/IEEE 60255-118-1:2018)

Program: `analysis/e5_pclass.cpp`. It feeds the estimator with the PB2 stream (1e9/31744 S/s) and the standard's balanced three-phase test signals, and checks the P-class limits of Tables 2-10. Every test is evaluated on phases A, B and C and on the positive sequence, and the worst result is reported.

Steps follow 5.2.3, 5.2.4 and Annex C.4:
- equivalent-time sampling with n = 20 step offsets per reporting interval;
- linear interpolation between interleaved points;
- state levels from the first and last 1 s of a 2 s + 4 × (response-time limit) epoch;
- state boundaries at ±0.5 % of the step;
- overshoot and undershoot measured both before and after the transition.

## Folders

Each folder is named `<estimator>-<f0>[-<noise dB below the RMS>-<jitter ns RMS>]`.

The estimators:
- **`e5`:** the estimator before #196, `service/Source/C37118Estimator.h` at main 17128af0.
- **`e5v3`:** the variant that became production. It was prototyped as `analysis/e5_variant.patch` built with `-DVARIANT=3`, and is in production since `dee75179` (#196; releases 21 and 22, main `539e6c49`). The repo's code gives the same numbers; `71525221` only moved the fit's sums back to local variables, for speed (on the A53 the direct fit went from 21.7 % back to ~19 % of a core). Its phasor comes from the first-order sub-model of the same weighted fit (one more 9×9 solve per report). Its frequency and ROCOF come from the positive sequence of the three voltage fits, as in the reference model of Annex D.4. Production adds one rule: it uses the positive sequence only when √3·|V1| > |VA|, and otherwise phase A.

Each folder holds:
- `result.txt`;
- `e5-step-K.csv`, the interleaved step waveforms. K is 0 for magnitude +10 %, 1 for magnitude -10 %, 2 for phase +10° and 3 for phase -10°.

## Results

**Before (17128af0).** Every P-class test passes with wide margins except the steps:
- overshoot and undershoot are 7.1-7.6 % on the positive sequence and up to 11.2 % on a single phase, against a 5 % limit;
- the response time is 17-32 ms, within 2/f0;
- the delay is 1.0-1.4 ms, within 1/(4 Fs).

The cause is the second-order envelope fitted over a two-cycle Hann window. Its zeroth-order filter has negative lobes near the window edges, which ring before and after the step. The decimation does not cause it: the result is the same with decimation off. Nor does the timing: the samples are ideal.

**After (variant 3, production since dee75179).**
- Overshoot and undershoot are 0 %, and every test passes at 50 and 60 Hz.
- Harmonic TVE falls from 7.7e-5 to 7.3e-7.
- Modulation and ramp TVE rise to at most 1.8 % of their limits.
- CPU rises by 1.5 % relative to production (Mac bench, 120 s of signal).

**Noise.**
- ROCOF is the estimate that noise limits. Its worst RFE scales linearly with the noise amplitude.
- At 60 dB below the RMS, the estimator before #196 gives 0.51 Hz/s at 50 Hz and 0.66 Hz/s at 60 Hz. It therefore needs at least about 62 dB at 50 Hz and 64 dB at 60 Hz to meet the 0.4 Hz/s limit.
- Variant 3 averages three phases, so it needs about 5 dB less: about 57 dB at 50 Hz and 60 dB at 60 Hz.
- The acquisition measures 75-77 dB (`../e7`). Annex E.4.2 f recommends declaring this SNR requirement.

## Factorial: the two changes of #196 taken apart

`analysis/e5_factorial.sh` patches the estimator copy at build time (never in place) into the four combinations of phasor model (second-order `full`, first-order sub-model `sub`) and reference for frequency and ROCOF (phase A `A`, positive sequence `PS`), and runs each at 50 and 60 Hz without noise and with white noise 60 dB below the RMS. Results in `factorial/<variant>-<f0>[-60-0]/result.txt`. `full-A` reproduces `e5-50`/`e5-60` (the production before #196) to the last digit; `sub-PS` reproduces `e5v3-*`.

| Effect | Phasor order (full -> sub) | Reference (A -> PS) |
| --- | --- | --- |
| Step over/undershoot | 9.9 % / 7.3 % (A / PS) -> 0 % | none |
| Harmonics 1 %, TVE | 7.7e-5 -> 7.3e-7 | none |
| Amplitude modulation, TVE | 1.2e-5 -> 4.6e-4 | none |
| Ramp 1 Hz/s, TVE | 4.5e-8 -> 1.7e-4 | none |
| Ramp 1 Hz/s, RFE | none | 2.8e-5 -> 1.6e-7 Hz/s |
| RFE with noise 60 dB below RMS, worst case | none | 0.53 -> 0.25 Hz/s at 50 Hz; 0.64 -> 0.39 Hz/s at 60 Hz |

Each factor leaves the other's figures untouched (to three digits), so the step improvement is the phasor order's and the noise and ramp-RFE improvement is the reference's. The noise figures here (no time jitter, seed 12345) differ slightly from the `-60-3` folders above, which add 3 ns RMS of time jitter.
