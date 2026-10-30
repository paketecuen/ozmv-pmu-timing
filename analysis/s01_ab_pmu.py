"""M4 for the PMU: A's and B's C37.118 frames paired by their UTC stamp (SOC + FRACSEC), not by their arrival.

The bench recorder's frames: 82 bytes, one PMU, 7 float rectangular phasors (VA first), FREQ and DFREQ as floats,
TIME_BASE 1e6, captured on the board's loopback (Ethernet header, IPv4, UDP). STAT bits 15-14 stay "test" until the
measurement is qualified (by design), so a frame counts as locked when STAT bit 13 is clear. A pair is compared
only when both are locked and both see the mains (|VA| > 100 V: the end of the session removed it).

B minus A is the time by which B's phasor angle lags A's: delay_B - delay_A in M2's sense.

usage: python3 analysis/s01_ab_pmu.py SESSION_DIR OUT_1S.csv [DATE]   (DATE 20261027 by default)"""
import math
import struct
import sys
from pathlib import Path

import numpy as np


DATE = sys.argv[3] if len(sys.argv) > 3 else "20261027"


def frames(board_dir):
    """{UTC us: (STAT, MSG_TQ, VA phasor, FREQ, DFREQ)} from every pcap of the board, and the duplicates."""
    out, dup = {}, 0
    for p in sorted(Path(board_dir).glob(f"pmu-{DATE}T*.pcap")):
        data = p.read_bytes()
        pos = 24
        while pos + 16 <= len(data):
            incl = struct.unpack_from("<I", data, pos + 8)[0]
            if pos + 16 + incl > len(data):
                break                                   # a file cut by a reboot
            pkt = data[pos + 16:pos + 16 + incl]; pos += 16 + incl
            if len(pkt) < 14 + 20 + 8 + 82:
                continue
            ip = pkt[14:]; pay = ip[(ip[0] & 15) * 4 + 8:]
            if len(pay) != 82 or pay[0] != 0xAA:
                continue
            soc, fs = struct.unpack_from(">II", pay, 6)
            stat = struct.unpack_from(">H", pay, 14)[0]
            re_, im = struct.unpack_from(">ff", pay, 16)
            freq, dfreq = struct.unpack_from(">ff", pay, 72)
            key = soc * 1000000 + (fs & 0xFFFFFF)
            dup += key in out
            out[key] = (stat, fs >> 24, complex(re_, im), freq, dfreq)
    return out, dup


def describe(name, f, dup):
    k = np.array(sorted(f))
    steps = np.diff(k)
    stat = np.array([f[x][0] for x in k])
    print("%s: %d frames, %d duplicates, %d steps != 20 ms (%d frames missing), locked %.1f %%" % (
        name, k.size, dup, int(np.sum(steps != 20000)), int(np.sum(steps[steps > 20000] // 20000 - 1)),
        100 * np.mean(((stat >> 13) & 1) == 0)))


a, da = frames(Path(sys.argv[1]) / "A")
b, db = frames(Path(sys.argv[1]) / "B")
describe("A", a, da)
describe("B", b, db)
rows = []
for k in sorted(set(a) & set(b)):
    sa, _, pa, fa, ra = a[k]
    sb, _, pb, fb, rb = b[k]
    if (sa >> 13) & 1 or (sb >> 13) & 1 or abs(pa) < 100 or abs(pb) < 100:
        continue
    dphi = math.atan2((pb / pa).imag, (pb / pa).real)          # B's angle minus A's
    rows.append((k, abs(pb) / abs(pa), -dphi / (2 * math.pi * fa) * 1e9, fb - fa, rb - ra))
r = np.array(rows)
x = r[:, 2]
print("pairs both locked with the mains: %d; B minus A %.1f ns mean, median %.1f, RMS %.1f, p2.5..97.5 %.1f..%.1f, "
      "max |dev| %.0f; |V| B/A %.5f; dF RMS %.2e Hz (max %.2e); dROCOF RMS %.4f Hz/s (max %.3f)" % (
          x.size, x.mean(), np.median(x), x.std(), np.percentile(x, 2.5), np.percentile(x, 97.5),
          np.max(np.abs(x - np.median(x))), np.median(r[:, 1]), r[:, 3].std(), np.max(np.abs(r[:, 3])),
          r[:, 4].std(), np.max(np.abs(r[:, 4]))))
# One row per UTC second: the means of the pairs inside it
sec = (r[:, 0] // 1000000).astype(np.int64)
u, inv, n = np.unique(sec, return_inverse=True, return_counts=True)
means = [np.bincount(inv, r[:, j]) / n for j in (1, 2, 3, 4)]
with open(sys.argv[2], "w") as fo:
    fo.write("utc_s,pairs,mag_ratio_b_over_a,b_minus_a_ns,freq_b_minus_a_hz,rocof_b_minus_a\n")
    for i, s in enumerate(u):
        fo.write("%d,%d,%.6f,%.1f,%.7f,%.5f\n" % (s, n[i], means[0][i], means[1][i], means[2][i], means[3][i]))
