#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The WMU time track (#190): the UTC of every FLAC frame in an ozmV WMU stream.

The WMU's Ogg stream carries the waveform as FLAC (decode it with ffmpeg as
before) and, beside it, a second logical stream whose first packet starts
with b"OZWTIME\\0" and whose other packets are one record per FLAC frame:

    first sample (u64), count (u32), flags (u32), first and last UTC ns (u64),
    expanded uncertainty ns (u32, k = 2; 0 = unknown), reserved (u32)

all little-endian. Sample i of the stream (counted from 0, the order ffmpeg
decodes them) belongs to the record with first <= i < first + count, and its
UTC is first_ns + (last_ns - first_ns) * (i - first) / (count - 1): the
samples are periodic on the ADC clock. Flags: 1 epoch confirmed, 2 fitted
DRDY, 4 gap (a discontinuity precedes or falls inside the frame: do not
interpolate across it), 8 every sample has a time, 16 a sample carries no
valid value, 32 holdover (#197: the time was extrapolated through a loss of
GPS; the epoch stays confirmed and the uncertainty grows with the loss);
bits 8-15 the time source (0 system, 1 GPS, 2 syncing).

usage: wmu-time.py STREAM.ogg [--csv RECORDS.csv]
"""
import argparse
import datetime
import struct
import sys

MAGIC = b"OZWTIME\0"
RECORD = struct.Struct("<QIIQQII")
FLAG_NAMES = ((1, "confirmed"), (2, "fitted"), (4, "gap"), (8, "time_known"), (16, "invalid"), (32, "holdover"))


def pages(data):
    """(serial, header flags, segment lengths, body) for every Ogg page."""
    pos = 0
    while True:
        at = data.find(b"OggS", pos)
        if at < 0 or at + 27 > len(data):
            return
        flags = data[at + 5]
        serial = struct.unpack_from("<I", data, at + 14)[0]
        count = data[at + 26]
        lacing = data[at + 27:at + 27 + count]
        body = at + 27 + count
        size = sum(lacing)
        if len(lacing) < count or body + size > len(data):
            return
        yield serial, flags, lacing, data[body:body + size]
        pos = body + size


def packets(data):
    """(serial, packet) in stream order; a 255 lacing value continues a packet."""
    partial = {}
    for serial, flags, lacing, body in pages(data):
        buffer = partial.pop(serial, b"") if flags & 1 else b""
        offset = 0
        for length in lacing:
            buffer += body[offset:offset + length]
            offset += length
            if length < 255:
                yield serial, buffer
                buffer = b""
        if buffer:
            partial[serial] = buffer


def records(data):
    """The time track's header (rate numerator, denominator) and its records."""
    track, rate, out = None, None, []
    for serial, packet in packets(data):
        if track is None and packet.startswith(MAGIC):
            track = serial
            version, size, numerator, denominator = struct.unpack_from("<HHII", packet, 8)
            if version != 1 or size != RECORD.size:
                raise ValueError("unsupported time track version %d (record %d bytes)" % (version, size))
            rate = (numerator, denominator)
        elif serial == track and len(packet) == RECORD.size:
            first, count, flags, first_ns, last_ns, uncertainty, _ = RECORD.unpack(packet)
            out.append({"first_sample": first, "count": count, "flags": flags, "first_utc_ns": first_ns,
                        "last_utc_ns": last_ns, "uncertainty_ns": uncertainty})
    if track is None:
        raise ValueError("no WMU time track in this stream")
    return rate, out


def utc(ns):
    return datetime.datetime.fromtimestamp(ns // 1000000000, datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%S") + ".%09dZ" % (ns % 1000000000)


def period(record):
    return (record["last_utc_ns"] - record["first_utc_ns"]) / (record["count"] - 1) if record["count"] > 1 else None


def summary(rate, rows):
    lines = []
    samples = sum(r["count"] for r in rows)
    lines.append("records %d, samples %d, declared rate %d/%d S/s" % (len(rows), samples, rate[0], rate[1]))
    timed = [r for r in rows if r["flags"] & 8]
    if timed:
        lines.append("first sample %s, last %s" % (utc(timed[0]["first_utc_ns"]), utc(timed[-1]["last_utc_ns"])))
        periods = [p for p in (period(r) for r in timed) if p]
        if periods:
            lines.append("period %.3f..%.3f ns (mean %.3f)" % (min(periods), max(periods), sum(periods) / len(periods)))
        steps = []
        for a, b in zip(timed, timed[1:]):
            if b["first_sample"] == a["first_sample"] + a["count"] and not b["flags"] & 4 and period(a):
                steps.append(b["first_utc_ns"] - a["last_utc_ns"] - period(a))
        if steps:
            lines.append("frame-to-frame continuity: residual %+.1f..%+.1f ns over %d joins"
                         % (min(steps), max(steps), len(steps)))
    bounds = [r["uncertainty_ns"] for r in rows if r["uncertainty_ns"]]
    lines.append("uncertainty (k = 2): %s; unknown in %d records" %
                 ("%d..%d ns" % (min(bounds), max(bounds)) if bounds else "none", len(rows) - len(bounds)))
    lines.append("flags: " + ", ".join("%s %d" % (name, sum(1 for r in rows if r["flags"] & bit))
                                        for bit, name in FLAG_NAMES))
    order = all(b["first_sample"] == a["first_sample"] + a["count"] for a, b in zip(rows, rows[1:]))
    lines.append("records cover the samples without overlap or hole: %s" % ("yes" if order else "NO"))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("stream", help="a saved WMU Ogg stream ('-' for stdin)")
    parser.add_argument("--csv", help="write one row per record")
    args = parser.parse_args(argv)
    data = sys.stdin.buffer.read() if args.stream == "-" else open(args.stream, "rb").read()
    rate, rows = records(data)
    print(summary(rate, rows))
    if args.csv:
        with open(args.csv, "w") as out:
            out.write("first_sample,count,flags,first_utc_ns,last_utc_ns,uncertainty_ns,period_ns\n")
            for r in rows:
                p = period(r)
                out.write("%d,%d,%d,%d,%d,%d,%s\n" % (r["first_sample"], r["count"], r["flags"], r["first_utc_ns"],
                                                     r["last_utc_ns"], r["uncertainty_ns"],
                                                     "%.3f" % p if p is not None else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
