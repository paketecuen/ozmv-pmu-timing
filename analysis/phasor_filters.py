"""Why the second-order phasor overshoots a step and the first-order one does not (Section IV-B).

The weighted least-squares fit of the production estimator (two-cycle Hann window, the 11 columns of
service/Source/C37118Estimator.h, the decimated point rate of the PB2 stream, nominal 50 Hz) is a linear filter
of the samples. For each model this prints the envelope filter of its phasor (the weight each point of the window
gets, demodulated), how much of it is negative, and the overshoot of a 10 % amplitude step swept over every
position in the window. Run from the paper folder: python3 analysis/phasor_filters.py"""
import numpy as np

f0, fs = 50.0, 31502.016 / 16
H = 1 / f0
t = np.arange(-H, H + 1e-12, 1 / fs)
t = t - t.mean()
u = t / H
w = 0.5 * (1 + np.cos(np.pi * u))
q = 2 * np.pi * f0 * t
cols = [np.ones_like(t), np.cos(q), np.sin(q), u * np.cos(q), u * np.sin(q), u * u * np.cos(q), u * u * np.sin(q),
        np.cos(2 * q), np.sin(2 * q), np.cos(3 * q), np.sin(3 * q)]
models = {"second-order (11 columns)": np.column_stack(cols),
          "first-order sub-model (9 columns)": np.column_stack([c for i, c in enumerate(cols) if i not in (5, 6)])}

print(f"{len(t)} points in the window, {fs:.3f} points/s")
for name, A in models.items():
    R = np.linalg.solve(A.T @ (w[:, None] * A), A.T * w)        # coefficients = R @ samples
    h = R[1] * np.cos(q) + R[2] * np.sin(q)                     # envelope filter of the phasor
    negative = max(0.0, -h[h < 0].sum() / h[h > 0].sum() * 100)
    steps = []
    for m in range(len(t) + 1):
        c = R @ (np.where(np.arange(len(t)) >= m, 1.1, 1.0) * np.cos(q))
        steps.append(np.hypot(c[1], c[2]))
    steps = np.array(steps)
    over = max(0.0, (steps.max() - 1.1) / 0.1 * 100)
    under = max(0.0, (1.0 - steps.min()) / 0.1 * 100)
    print(f"{name}: filter minimum {h.min():+.5f} at u = {u[h.argmin()]:+.2f}, negative lobes {negative:.1f} % of "
          f"the positive area; 10 % step: overshoot {over:.2f} %, undershoot {under:.2f} % of the step")

S2, S4 = (np.sum(w * u ** k) / np.sum(w) for k in (2, 4))
print(f"Zeroth- and second-order terms alone: the phasor filter is w(u)(S4 - S2 u^2), with S2 = {S2:.4f} and "
      f"S4 = {S4:.4f}; it is negative for |u| > {np.sqrt(S4 / S2):.2f}")
