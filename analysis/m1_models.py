"""M1 sensitivity: the label offset under one- and two-pole input networks, on the equivalent-time step.

usage: python3 analysis/m1_models.py m1-equivalent-time-step.csv"""
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares
sys.path.insert(0, str(Path(__file__).resolve().parent))
from m1_pps_step_sim import T_MOD_NOM, TAPS  # noqa: E402

d = np.loadtxt(sys.argv[1], delimiter=",", skiprows=1)
tau, y = d[:, 0] * 1e-9, d[:, 1]
k = np.arange(TAPS.size); offs = (k - (TAPS.size - 1) / 2) * T_MOD_NOM

def resp(tau_s, net):
    t = tau_s[:, None] - offs[None, :]
    tp = np.clip(t, 0, None)
    x = net(tp)
    x = np.where(t > 0, x, 0.0)
    return x @ TAPS

def one(p):
    return lambda t: 1 - np.exp(-t / p)
def two(p1, p2):
    if abs(p1 - p2) < 1e-12: p2 = p1 * 1.0001
    return lambda t: 1 - (p1 * np.exp(-t / p1) - p2 * np.exp(-t / p2)) / (p1 - p2)
def lead(p, f):   # one pole plus a fraction f that passes at once (a capacitive feed-through)
    return lambda t: f + (1 - f) * (1 - np.exp(-t / p))

models = {
    "1 pole":            (lambda q: resp(tau - q[0], one(q[1])) - y, [11.9e-6, 32.7e-6], [1e-6, 1e-6]),
    "2 poles":           (lambda q: resp(tau - q[0], two(q[1], q[2])) - y, [10.9e-6, 31e-6, 1.0e-6], [1e-6, 1e-6, 1e-7]),
    "1 pole + feed":     (lambda q: resp(tau - q[0], lead(q[1], q[2])) - y, [11.9e-6, 32.7e-6, 0.0], [1e-6, 1e-6, 0.01]),
}
for name, (f, p0, sc) in models.items():
    best = None
    starts = [p0]
    if name == "2 poles":
        starts = [[o, a, b] for o in (9e-6, 10.9e-6, 11.9e-6) for a, b in ((31e-6, 0.3e-6), (31e-6, 1e-6), (30e-6, 2e-6), (28e-6, 4e-6))]
    for s in starts:
        lb = [-1e-3] + [1e-9] * (len(s) - 1); ub = [1e-3] + [100e-6] * (len(s) - 1)
        if name == "1 pole + feed": lb[2], ub[2] = -0.2, 0.2
        sol = least_squares(f, s, x_scale=sc, bounds=(lb, ub))
        if best is None or sol.cost < best.cost: best = sol
    r = best.fun
    J = best.jac; cov = np.linalg.inv(J.T @ J) * (2 * best.cost / (r.size - len(best.x)))
    e = np.sqrt(np.diag(cov))
    print("%-14s offset %8.1f ns (+/- %.1f fit) = %.2f t_MOD;  params %s;  residual RMS %.3e" % (
        name, best.x[0] * 1e9, e[0] * 1e9, best.x[0] / T_MOD_NOM, ", ".join("%.4g" % v for v in best.x[1:]), np.sqrt(np.mean(r ** 2))))
    # binned residual pattern
    bins = np.arange(-120e-6, 200e-6 + 1e-9, 20e-6)
    idx = np.digitize(tau, bins)
    print("   binned residual (x1e-4) per 20 us from -120 us:", " ".join("%+.1f" % (1e4 * r[idx == j].mean()) for j in range(1, bins.size)))
