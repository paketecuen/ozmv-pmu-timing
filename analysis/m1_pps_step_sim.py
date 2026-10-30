"""M1 in simulation: how precisely the PPS edge seen by the ADC gives the converter's delay.

Fran's concern (2026-10-16): with output samples every 31.7 us, can an edge be located to better than 250 ns?
This simulates the measurement with the converter as it is: a discrete-time (switched-capacitor) delta-sigma
modulator that samples its input every t_MOD = 248 ns, followed by the sinc3 decimation filter (R = 128), whose
output every 31.744 us is a weighted average of 382 modulator samples. The modulator runs 40 ppm fast against
UTC, so each PPS edge falls at a different phase of the sample grid.

Each output sample carries the label the instrument gives it: its conversion time minus the computed delay
D = 190.5 t_MOD, plus the timing errors measured on the board (27-29 ns RMS per second from the tick-to-UTC
model, 2.8 ns per sample from the envelope regression). The true delay is set 44 t_MOD (10.9 us) longer, the
case the datasheet leaves open. The analysis superposes the edges in equivalent time, fits the step response
(sinc3, optionally with an RC input network) and recovers the offset between label and input time.

Run from the paper folder: python3 analysis/m1_pps_step_sim.py"""
import numpy as np
from scipy.optimize import least_squares

R = 128
T_MOD_NOM = 31744e-9 / R                 # 248 ns at the nominal rate
PPM_FAST = 40e-6                         # the bench crystal
D_ASSUMED = 1.5 * (R - 1)                # in t_MOD: what the instrument subtracts
TAPS = np.convolve(np.convolve(np.ones(R), np.ones(R)), np.ones(R))
TAPS /= TAPS.sum()                       # sinc3, 3R - 2 taps, unit DC gain
STEP_V = 0.6                             # PPS step at the converter input
NOISE_V = 21.3e-6                        # datasheet noise at OSR128, gain 1 (Table 7-1)
MODEL_RMS_S = 28e-9                      # tick-to-UTC prediction error, per second
FIT_RMS_S = 2.8e-9                       # envelope regression, per sample


def response(tau_s, t_mod, rc_s):
    """Expected output for a step at tau = 0, against the sample's input time tau (centre of the filter)."""
    k = np.arange(TAPS.size)
    offs = (k - (TAPS.size - 1) / 2) * t_mod            # each modulator sample relative to the centre
    t = tau_s[:, None] - offs[None, :]                   # time of each modulator sample after the edge
    if rc_s > 0:
        x = np.where(t > 0, 1 - np.exp(-np.clip(t, 0, None) / rc_s), 0.0)
    else:  # a sharp edge: averaged over its unknown phase within one modulator period
        x = np.clip(t / t_mod + 0.5, 0.0, 1.0)
    return x @ TAPS


def simulate(n_edges, extra_delay_tmod, rc_s, noise_v, rng, span=4):
    """Labels and values of the output samples around n_edges PPS rising edges."""
    t_mod = T_MOD_NOM * (1 - PPM_FAST)                   # the true modulator period, 40 ppm short
    taus, values = [], []
    phase0 = rng.uniform(0, t_mod)                       # where the first modulator sample falls
    for s in range(n_edges):
        model_err = rng.normal(0, MODEL_RMS_S)
        # Modulator samples m * t_mod + phase0 (UTC); the edge is at UTC second s.
        first = int(np.floor((s - phase0) / t_mod))
        # Output samples whose filter window covers the edge, and a few either side
        for j in range(-span, span + 1):
            m_last = (first // R + j) * R + (TAPS.size - 1)  # last modulator sample of this output
            m = m_last - np.arange(TAPS.size)                    # its 382 modulator samples
            t_samples = m * t_mod + phase0 - s                   # their times relative to the edge
            x = STEP_V * (np.where(t_samples > 0, 1 - np.exp(-np.clip(t_samples, 0, None) / rc_s), 0.0)
                          if rc_s > 0 else (t_samples > 0).astype(float))
            value = TAPS[::-1] @ x + rng.normal(0, noise_v)
            # Data ready: the window's last modulator sample plus a pipeline the datasheet does not state
            ready = (m_last + extra_delay_tmod) * t_mod + phase0
            label = ready - D_ASSUMED * T_MOD_NOM + model_err + rng.normal(0, FIT_RMS_S)
            taus.append(label - s)
            values.append(value)
    return np.array(taus), np.array(values)


def fit(taus, values, fit_rc):
    """Least-squares fit of offset, level, step and delay (and the RC time constant if asked)."""
    def resid(p):
        delay, base, amp = p[:3]
        rc = abs(p[3]) if fit_rc else 0.0
        return base + amp * response(taus - delay, T_MOD_NOM, rc) - values
    p0 = [taus[np.argmin(np.abs(values - values.mean()))], values.min(), np.ptp(values)] + ([1e-6] if fit_rc else [])
    sol = least_squares(resid, p0, x_scale=[1e-6, 0.1, 0.1] + ([1e-6] if fit_rc else []))
    return sol.x


if __name__ == "__main__":
    rng = np.random.default_rng(20261016)
    extra = 44.0                                         # the case left open by the datasheet
    true_offset = extra * T_MOD_NOM * (1 - PPM_FAST) + D_ASSUMED * (T_MOD_NOM * (1 - PPM_FAST) - T_MOD_NOM)
    print(f"true offset between label and input time: {true_offset * 1e9:.1f} ns (44 t_MOD more than computed)")
    for rc_s, fit_rc in ((0.0, False), (2e-6, True)):
        for noise_v in (NOISE_V, 200e-6):
            print(f"\ninput RC {rc_s * 1e6:.0f} us, noise {noise_v * 1e6:.0f} uV RMS:")
            for n_edges in (1, 10, 60, 600):
                errors = []
                for _ in range(20 if n_edges < 600 else 6):
                    taus, values = simulate(n_edges, extra, rc_s, noise_v, rng)
                    errors.append(fit(taus, values, fit_rc)[0] - true_offset)
                errors = np.array(errors) * 1e9
                print(f"  {n_edges:4d} edges: error of the delay {np.mean(errors):+7.1f} ns mean, "
                      f"{np.sqrt(np.mean(errors ** 2)):6.1f} ns RMS")
