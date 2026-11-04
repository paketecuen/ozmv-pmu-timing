#!/bin/sh
# Bench session of #193 at Fran's lab: the board records by itself from each boot, with nobody logged in.
# - the WMU with RawCurrentInputs (VA with the voltage, IN with the PPS) in 30-minute OGG files, each with its
#   own per-sample time track;
# - the PMU's C37.118 frames (50 frames/s, published to the board itself) in 30-minute pcap files;
# - one status line a minute in recorder.log (time basis, satellites, frames, dropped samples).
# It stops after HOURS hours from its start, when the folder passes LIMIT_GB, or when a file STOP appears,
# and then puts the PMU and the WMU back to their defaults. No secret is read or printed: the API helper
# logs in on the board itself. Installed on the board on 2026-10-16; remove after the session.
set -u
dir=/data/bench-193; api="sh $dir/ozmv-api.sh"; log=$dir/recorder.log   # /data is mounted noexec
HOURS=${HOURS:-10}; LIMIT_GB=${LIMIT_GB:-30}; CHUNK_S=${CHUNK_S:-1800}
say() { echo "$(date -u +%FT%TZ) $*" >> $log; }
pmu_on='{"Enabled":true,"DestIP":"127.0.0.1","Port":4713,"IDCODE":1,"DataRate":50,"NominalFrequency":50,"PhasorFormat":"float_rect","Analyzers":["PB2_1P_NOMINAL"]}'
pmu_off='{"Enabled":false,"DestIP":"127.0.0.1","Port":4713,"IDCODE":1,"DataRate":50,"NominalFrequency":50,"PhasorFormat":"float_rect","Analyzers":[]}'
wmu_on='{"Enabled":true,"Mode":"Pull","Source":"PB2","BitsPerSample":24,"CompressionLevel":2,"VoltageGain":10000.0,"CurrentGain":1000000.0,"Decimation":1,"NominalFrequency":50,"RawCurrentInputs":true}'
wmu_off='{"Enabled":false,"Mode":"Pull","Source":"","BitsPerSample":24,"CompressionLevel":2,"VoltageGain":10000.0,"CurrentGain":10000.0,"Decimation":1,"NominalFrequency":50,"RawCurrentInputs":false}'
[ -f $dir/STOP ] && { say "STOP present: not recording"; exit 0; }
say "start: boot $(cut -c1-8 /proc/sys/kernel/random/boot_id), $HOURS h, $LIMIT_GB GB, chunks of $CHUNK_S s"
i=0; until $api getMeterInfo 2>/dev/null | grep -q '"Release"'; do i=$((i+1)); [ $i -gt 60 ] && { say "the service did not answer in 5 min"; exit 1; }; sleep 5; done
$api setC37118 "$pmu_on" > /dev/null; $api setWaveform "$wmu_on" > /dev/null
say "PMU and WMU enabled: $($api getWaveform | head -1 | grep -o '"RawCurrentInputs":[a-z]*')"
tcpdump -i lo -n -U -G "$CHUNK_S" -w "$dir/pmu-%Y%m%dT%H%M%SZ.pcap" udp port 4713 2>/dev/null &
dump=$!
end=$(( $(date +%s) + HOURS * 3600 ))
(
  while [ "$(date +%s)" -lt "$end" ] && [ ! -f $dir/STOP ]; do
    d=$($api getDevice '{"Device":"PB2"}' | head -1)
    b=$(echo "$d" | grep -o '"Basis":"[a-z-]*"' | head -1)
    h=$(echo "$d" | grep -o '"Holdovers":[0-9]*\|"Rejoins":[0-9]*\|"LastRejoinNs":[0-9]*' | tr '\n' ' ')
    c=$($api getClock | head -1 | grep -o '"Satellites":[0-9]*\|"Synchronized":[a-z]*' | tr '\n' ' ')
    p=$($api getC37118Status | head -1 | grep -o '"PublishedFrames":[0-9]*\|"EstimatorResets":[0-9]*' | tr '\n' ' ')
    w=$($api getWaveform | head -1 | grep -o '"SamplesDropped":[0-9]*' | tr '\n' ' ')
    say "status $b $h| $c| $p| $w"
    sleep 60
  done
) &
mon=$!
while [ "$(date +%s)" -lt "$end" ] && [ ! -f $dir/STOP ]; do
  used=$(du -sk $dir | cut -f1)
  [ "$used" -gt $((LIMIT_GB * 1024 * 1024)) ] && { say "folder over $LIMIT_GB GB: stopping"; break; }
  f=$dir/wmu-$(date -u +%Y%m%dT%H%M%SZ).ogg
  $api getWaveform > /dev/null
  cookie=$(cat /run/ozmv-api.cookie)
  curl -s -N -X POST -H "Cookie: $cookie" -H 'Content-Type: application/json' -H 'Accept: audio/ogg' \
    --data-binary '{}' --max-time "$CHUNK_S" -o "$f" http://127.0.0.1:8080/getWaveformStream
  say "wmu file $(basename $f): $(wc -c < $f) bytes"
done
kill $dump $mon 2>/dev/null
$api setC37118 "$pmu_off" > /dev/null; $api setWaveform "$wmu_off" > /dev/null
say "stop: PMU and WMU back to the defaults"
