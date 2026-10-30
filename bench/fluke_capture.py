"""Bench capture for M2 and M3: the Fluke 8588A (master) and 8558A (slave), cable-synchronised, digitize mode.

The master samples on its own timer and pulses its trigger output once per sample; the slave takes exactly one
reading per pulse, so reading k of both instruments is taken at the same instant (up to the slave's fixed lag,
which M3 measures). Both use the same interval and an aperture just below it, so a PPS edge always lands inside
an aperture and the straddling reading locates it (analysis/m2_fluke_phase.py).

    M2: the 50 Hz voltage on the master (1000 V range), the PPS on the slave (10 V range)
    M3: the PPS on both (split with a T, equal cables)

Each burst is written as BURST.csv (k, master_v, slave_v) and BURST.json (interval, aperture, the host's UTC at
the start, the instruments' identities and error queues). The host clock must be within 0.5 s of UTC (NTP):
it only names the second of each edge.

SCPI facts from the 2026-09-18 bench (raw socket, port 3490, prompt "N> ", one session per instrument):
SYST:REM before READ?/INIT; FUNC "DIG:VOLT"; TRIG:SOUR TIM with TRIG:TIM for timed sampling (IMM free-runs);
slave TRIG:SOUR EXT; arm the slave with INIT, then the master's READ?, then poll the slave's STAT:OPER:COND?
(32 = waiting for trigger) before FETC?, which would block forever on an armed slave; FORM:DATA ASC only.

    python3 bench/fluke_capture.py --out data/bench/m2 --bursts 60 --seconds 10 [--dry-run]
    python3 bench/fluke_capture.py --out data/bench/m3 --bursts 6 --seconds 10 --master-range 10"""
import argparse
import json
import re
import socket
import time
from pathlib import Path

PROMPT = re.compile(rb"\d+> $")


class Instrument:
    def __init__(self, host, name, dry_run, port=3490, timeout=60.0):
        self.name, self.dry_run = name, dry_run
        if not dry_run:
            self.sock = socket.create_connection((host, port), timeout=timeout)
            self._read()                                   # the greeting and the first prompt

    def _read(self):
        buf = b""
        while not PROMPT.search(buf):
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise ConnectionError(f"{self.name}: connection closed")
            buf += chunk
        return PROMPT.sub(b"", buf).decode(errors="replace").strip()

    def send(self, command):
        print(f"{self.name} << {command}")
        if self.dry_run:
            return ""
        self.sock.sendall(command.encode() + b"\n")
        return self._read()

    def errors(self):
        out = []
        for _ in range(20):
            e = self.send("SYST:ERR?")
            if self.dry_run or e.startswith("0") or e.startswith("+0"):
                break
            out.append(e)
        return out


def configure(inst, rng, ts, ta, n, master, filt):
    inst.send("SYST:REM")
    inst.send("*CLS")
    inst.send('FUNC "DIG:VOLT"')
    inst.send(f"DIG:VOLT:RANG {rng}")
    inst.send(f"DIG:APER {ta:.9g}")
    inst.send(f"DIG:FILT {filt}")
    inst.send("FORM:DATA ASC")
    inst.send(f"TRIG:COUN {n}")
    if master:
        inst.send("TRIG:SOUR TIM")
        inst.send(f"TRIG:TIM {ts:.9g}")
    else:
        inst.send("TRIG:SOUR EXT")


def numbers(text):
    return [float(v) for v in text.replace("\n", ",").split(",") if v.strip()]


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--master", default="192.168.1.201")
    ap.add_argument("--slave", default="192.168.1.202")
    ap.add_argument("--out", required=True)
    ap.add_argument("--bursts", type=int, default=60)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--interval-us", type=float, default=50.0)
    ap.add_argument("--aperture-us", type=float, default=49.0, help="just below the interval: no gap for the edge")
    ap.add_argument("--master-range", default="1000", help="1000 for the mains voltage (M2), 10 for the PPS (M3)")
    ap.add_argument("--slave-range", default="10")
    ap.add_argument("--filter", default="OFF", help="input filter; OFF avoids the 1.6 us of the 100 kHz pole")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    ts, ta = a.interval_us * 1e-6, a.aperture_us * 1e-6
    n = int(round(a.seconds / ts))
    out = Path(a.out)
    if not a.dry_run:
        out.mkdir(parents=True, exist_ok=True)
    master = Instrument(a.master, "8588A", a.dry_run)
    slave = Instrument(a.slave, "8558A", a.dry_run)
    ids = {"master": master.send("*IDN?"), "slave": slave.send("*IDN?")}
    configure(master, a.master_range, ts, ta, n, True, a.filter)
    configure(slave, a.slave_range, ts, ta, n, False, a.filter)
    setup_errors = {"master": master.errors(), "slave": slave.errors()}
    if setup_errors["master"] or setup_errors["slave"]:
        raise SystemExit(f"the instruments rejected the set-up: {setup_errors}")
    try:
        for burst in range(a.bursts):
            slave.send("INIT")                                     # armed: waits for the master's pulses
            start_ns = time.time_ns()
            mv = numbers(master.send("READ?"))
            for _ in range(600):                                   # the slave finishes with the last pulse
                cond = slave.send("STAT:OPER:COND?")
                if a.dry_run or not (int(float(cond or 0)) & 32):
                    break
                time.sleep(0.05)
            sv = numbers(slave.send("FETC?"))
            if a.dry_run:
                print(f"burst {burst}: would write {n} rows")
                break
            if len(mv) != n or len(sv) != n:
                raise SystemExit(f"burst {burst}: {len(mv)} master and {len(sv)} slave readings, expected {n}")
            stem = out / f"burst-{burst:03d}"
            with open(stem.with_suffix(".csv"), "w") as fh:
                fh.write("k,master_v,slave_v\n")
                for k, (x, y) in enumerate(zip(mv, sv)):
                    fh.write(f"{k},{x:.9g},{y:.9g}\n")
            stem.with_suffix(".json").write_text(json.dumps({
                "ts_s": ts, "ta_s": ta, "host_utc_start_ns": start_ns, "filter": a.filter,
                "master_range": a.master_range, "slave_range": a.slave_range, "instruments": ids,
                "errors": {"master": master.errors(), "slave": slave.errors()}}, indent=2) + "\n")
            print(f"burst {burst}: {n} readings, {stem}.csv")
    finally:
        for inst in (master, slave):
            inst.send("SYST:LOC")
