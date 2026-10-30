#!/bin/sh
# Bench capture on the board's side, for M1 and M2 at once: the WMU stream (VA with the voltage, IN with the
# PPS) and its per-sample time track, streamed over SSH straight to this computer's disk.
#
# The board needs ozmV release pb2-wmu-bench-20261016-1 or later (the RawCurrentInputs option, ozmV #198) and
# the API helper /run/ozmv-api.sh, which logs in on the board itself (the password never leaves it).
#
# usage: sh bench/board_wmu_capture.sh SSH_HOST SECONDS OUTDIR [SSH_CONFIG]
#   e.g. sh bench/board_wmu_capture.sh adc-new 720 data/bench/m1m2 ssh_config
# Then: python3 $OZMV/tools/wmu-time.py OUTDIR/wmu.ogg --csv OUTDIR/wmu-time.csv
#       python3 analysis/m1_pps_delay.py OUTDIR/wmu.ogg OUTDIR/wmu-time.csv --fit-rc --out OUTDIR
set -eu
host=$1 seconds=$2 out=$3 cfg=${4:-}
ssh_opts="-o ConnectTimeout=8"
[ -n "$cfg" ] && ssh_opts="$ssh_opts -F $cfg"
mkdir -p "$out"
# The bench configuration: current slots as ADC volts in microvolts (CurrentGain 1e6), everything else as usual
bench='{"Enabled":true,"Mode":"Pull","Source":"PB2","BitsPerSample":24,"CompressionLevel":2,"VoltageGain":10000.0,"CurrentGain":1000000.0,"Decimation":1,"NominalFrequency":50,"RawCurrentInputs":true}'
plain='{"Enabled":false,"Mode":"Pull","Source":"","BitsPerSample":24,"CompressionLevel":2,"VoltageGain":10000.0,"CurrentGain":10000.0,"Decimation":1,"NominalFrequency":50,"RawCurrentInputs":false}'
echo "== $(date -u +%FT%TZ) enable the WMU with RawCurrentInputs"
ssh $ssh_opts "$host" "/run/ozmv-api.sh setWaveform '$bench' | tail -1; /run/ozmv-api.sh getWaveform | head -1 | grep -o '\"RawCurrentInputs\":[a-z]*\|\"CurrentGain\":[0-9.e+]*'" \
  | tee "$out/capture.log"
echo "== $(date -u +%FT%TZ) streaming $seconds s to $out/wmu.ogg" | tee -a "$out/capture.log"
ssh $ssh_opts "$host" "/run/ozmv-api.sh getWaveform >/dev/null; cookie=\$(cat /run/ozmv-api.cookie); \
  curl -s -N -X POST -H \"Cookie: \$cookie\" -H 'Content-Type: application/json' -H 'Accept: audio/ogg' \
  --data-binary '{}' --max-time $seconds http://127.0.0.1:8080/getWaveformStream" > "$out/wmu.ogg" || true
echo "== $(date -u +%FT%TZ) status, then back to the plain configuration" | tee -a "$out/capture.log"
ssh $ssh_opts "$host" "/run/ozmv-api.sh getWaveform | head -1 | grep -o '\"SamplesStreamed\":[0-9]*\|\"SamplesDropped\":[0-9]*\|\"Records\":[0-9]*'; \
  /run/ozmv-api.sh setWaveform '$plain' | tail -1" | tee -a "$out/capture.log"
ls -l "$out/wmu.ogg" | awk '{print $5 " bytes"}' | tee -a "$out/capture.log"
