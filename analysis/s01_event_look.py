"""Around an instant: per 100 ms, VA RMS and IN rising edges, and where the samples stop or resume.

usage: python3 analysis/s01_event_look.py STREAM.ogg TIME.csv HH:MM:SS [seconds_before seconds_after]"""
import csv, subprocess, sys, datetime
import numpy as np
ogg, tcsv, hhmmss = sys.argv[1], sys.argv[2], sys.argv[3]
before, after = (float(sys.argv[4]), float(sys.argv[5])) if len(sys.argv) > 5 else (3.0, 3.0)
rows = list(csv.DictReader(open(tcsv)))
h, m, s = map(int, hhmmss.split(":"))
te = int(datetime.datetime(2026, 10, 27, h, m, s, tzinfo=datetime.timezone.utc).timestamp()) * 10**9
known = [r for r in rows if int(r["first_utc_ns"]) > 0]
u0 = np.array([int(r["first_utc_ns"]) for r in known]); k = np.searchsorted(u0, te)
k = min(max(k, 1), len(known) - 1)
center = int(known[k]["first_sample"])
raw = subprocess.run(["ffmpeg", "-v", "error", "-i", ogg, "-map", "0:a:0", "-f", "s32le", "-acodec", "pcm_s32le", "-"], check=True, capture_output=True).stdout
x = np.frombuffer(raw, dtype="<i4").reshape(-1, 7)
fs = 31502
a, b = max(0, center - int(before * fs)), min(x.shape[0], center + int(after * fs))
first = np.array([int(r["first_sample"]) for r in rows]); flags = np.array([int(r["flags"]) for r in rows])
for i in range(a, b, fs // 10):
    va = x[i:i + fs // 10, 0] / 256 / 1e4; inn = x[i:i + fs // 10, 6] // 256
    ups = int(np.sum((inn[1:] > 170000) & (inn[:-1] <= 170000)))
    j = np.searchsorted(first, i, side="right") - 1
    jj = np.searchsorted(first, i + fs // 10, side="right") - 1
    fl = int(np.bitwise_or.reduce(flags[j:jj + 1]))
    t = known[np.searchsorted([int(r["first_sample"]) for r in known], i, side="right") - 1]
    lab = datetime.datetime.utcfromtimestamp(int(t["first_utc_ns"]) / 1e9).strftime("%H:%M:%S.%f")[:-4]
    print("%+6.1f s  ~%s  VA %6.1f V rms  IN max %4d mV, rising edges %d  flags|=%d%s" % (
        (i - center) / fs, lab, np.sqrt(np.mean(va ** 2)), inn.max() // 1000, ups, fl, "  <-- gap/invalid" if fl & 20 else ""))
