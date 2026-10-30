"""M1 robustness: the same analysis with other level windows, and on each half of the record.

usage: python3 analysis/m1_windows.py STREAM.s32 TIME_GPS.csv"""
import sys
from pathlib import Path

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import m1_pps_delay as m1  # noqa: E402

t, v, base = m1.load(sys.argv[1], sys.argv[2], 6)
for name, low, high in (
                        ("near the edge", (-0.45e-3, -0.10e-3), (0.30e-3, 0.70e-3)),
                        ("far levels", (-3.0e-3, -1.0e-3), (1.0e-3, 3.0e-3))):
    m1.LOW, m1.HIGH = low, high
    s = m1.analyse(t, v, True)
    print("%-14s edges %d: offset %.1f +/- %.1f ns, RC %.3f us, centroid %.0f ns, residual %.2e" % (
        name, s["edges"], s["delay_offset_ns"], s["delay_offset_u_ns"], s["rc_us"], s["centroid_ns"], s["residual_rms_of_step"]))
m1.LOW, m1.HIGH = (-1.2e-3, -0.25e-3), (0.35e-3, 1.2e-3)
half = t.size // 2
for name, sl in (("first half", slice(0, half)), ("second half", slice(half, None))):
    s = m1.analyse(t[sl], v[sl], True)
    print("%-14s edges %d: offset %.1f +/- %.1f ns, RC %.3f us" % (name, s["edges"], s["delay_offset_ns"], s["delay_offset_u_ns"], s["rc_us"]))
