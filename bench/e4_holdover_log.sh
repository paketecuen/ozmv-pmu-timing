#!/bin/sh
# E4 on the board: the holdover against the length of the loss. Run it, then cover the antenna for 1, 5 and
# 10 minutes with a few minutes of open sky between them (the period must be re-measured: 10 s of GNSS).
# Every 2 s one line logs the PQ timeline's state: its basis, the samples timed by the holdover (whose
# increase gives each loss's exact length), the last return's distance and the bound checks, and the
# measured period (whose change during a loss is the crystal's drift). No secret is read or printed.
# usage (on the board): nohup sh /run/e4_holdover_log.sh MINUTES > /dev/null 2>&1 &
set -u
m=${1:-40}; api=/run/ozmv-api.sh; log=/run/e4-holdover.log
: > $log
end=$(( $(date +%s) + m * 60 ))
while [ "$(date +%s)" -lt "$end" ]; do
  d=$($api getDevice '{"Device":"PB2"}' | head -1)
  b=$(echo "$d" | grep -o '"Basis":"[a-z-]*"' | head -1)   # the live basis (TimelineBasis is a description)
  t=$(echo "$d" | grep -o '"Holdovers":[0-9]*\|"HoldoverSamples":[0-9]*\|"Rejoins":[0-9]*\|"LastRejoinNs":[0-9]*\|"GpsSteps":[0-9]*\|"HoldoverExpiries":[0-9]*\|"RejoinsOverBound":[0-9]*\|"WorstRejoinOfBound":[0-9.e+-]*\|"MeasuredPeriodNs":[0-9.]*' | tr '\n' ' ')
  echo "$(date -u +%T) $b $t" >> $log
  sleep 2
done
echo "$(date -u +%T) done, $(wc -l < $log) lines" >> $log
