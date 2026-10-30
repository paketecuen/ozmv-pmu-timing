#!/bin/sh
# E5 factorial: the two changes of #196 taken apart. The estimator copy in algorithms/estimator is patched
# at build time (never in place) into four variants and run through analysis/e5_pclass.cpp:
#   full-A   second-order phasor, frequency/ROCOF from phase A         (the production before #196)
#   full-PS  second-order phasor, frequency/ROCOF from the positive sequence
#   sub-A    first-order phasor,  frequency/ROCOF from phase A
#   sub-PS   first-order phasor,  frequency/ROCOF from the positive sequence (production since #196)
# at 50 and 60 Hz, without noise and with white noise 60 dB below the RMS. Results: data/e5/factorial/.
# Run from the paper folder: sh analysis/e5_factorial.sh
set -e
cd "$(dirname "$0")/.."
src=algorithms/estimator/C37118Estimator.h
build=analysis/build/e5-factorial
mkdir -p "$build" data/e5/factorial
for variant in full-A full-PS sub-A sub-PS; do
    dir="$build/$variant"
    mkdir -p "$dir"
    cp algorithms/estimator/*.h "$dir/"
    python3 - "$src" "$dir/C37118Estimator.h" "$variant" <<'EOF'
import sys
src, dst, variant = sys.argv[1:]
t = open(src, encoding="utf-8").read()
phasor, reference = variant.split("-")
# The reference for frequency and ROCOF: the positive sequence (when three voltages) or phase A always.
old = "    if(pPrevious.VoltageChannels == 3) {\n      const std::complex<double> sequence = Positive(fit, 1) * rmsScale;"
assert t.count(old) == 1, "reference switch point not found"
if reference == "A":
    t = t.replace(old, old.replace("if(pPrevious.VoltageChannels == 3)", "if(false && pPrevious.VoltageChannels == 3)"))
# The phasors: the first-order sub-model (one more 9x9 solve) or the full model's own coefficients.
old = ("    double phasors[kColumns - 2][7];\n    if(!Solve(finalFit.Gram, finalFit.Rhs, phasors)) {\n"
       "      ++pStats.ConditioningFailures;\n      return false;\n    }\n")
assert t.count(old) == 1, "phasor switch point not found"
if phasor == "full":
    t = t.replace(old, "    double phasors[kColumns][7]; // the full model's own solution, back in the peak-normalised scale of Solve()\n"
                   "    for(unsigned i = 0; i < kColumns; ++i) for(unsigned c = 0; c < 7; ++c)\n"
                   "      phasors[i][c] = finalFit.Peaks[c] > 0 ? finalFit.Coefficients[i][c] / finalFit.Peaks[c] : 0;\n")
open(dst, "w", encoding="utf-8").write(t)
EOF
    c++ -std=c++17 -O2 -I "$dir" -o "$dir/e5" analysis/e5_pclass.cpp
    for f0 in 50 60; do
        out="data/e5/factorial/$variant-$f0"; mkdir -p "$out"
        "$dir/e5" "$f0" none 0 > "$out/result.txt" || true   # e5 exits 1 when a group fails
        out="data/e5/factorial/$variant-$f0-60-0"; mkdir -p "$out"
        "$dir/e5" "$f0" 60 0 > "$out/result.txt" || true
        printf '%s %s Hz: %s | 60 dB: %s\n' "$variant" "$f0" "$(tail -1 data/e5/factorial/$variant-$f0/result.txt)" \
            "$(tail -1 data/e5/factorial/$variant-$f0-60-0/result.txt)"
    done
done
