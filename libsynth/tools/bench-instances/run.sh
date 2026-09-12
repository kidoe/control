#!/bin/sh
#
# What several engines cost per second of audio, and how that splits between
# voices that sound and instances that merely exist.
#
# Needs valgrind and its client-request header (valgrind/callgrind.h). The counts
# are x86 instructions; see tools/bench-arm for what the same C costs on a
# Cortex-M0 and a Cortex-M4F, which is within about 10% either way.
#
# Usage: libsynth/tools/bench-instances/run.sh [libsynth dir]  (from the repo root)
#        SYNTH_MAX_VOICES=2 libsynth/tools/bench-instances/run.sh
#
# The second form is the point of the exercise: the idle cost of an instance is
# proportional to its voice count, because every block scans every slot, so a
# voice pool sized to one track rather than to the whole instrument is cheaper
# per part.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=${1:-libsynth}
out=${TMPDIR:-/tmp}/bench-instances.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT

voices=${SYNTH_MAX_VOICES:-8}

cc -O2 -DSYNTH_MAX_VOICES="$voices" -I"$root/include" \
   "$here/instances.c" "$root/src/dsp.c" "$root/src/synth.c" -o "$out/instances"

printf '%-38s %12s\n' "one second of audio, $voices voices each" 'instr'
for case in "one8|one instance, 8 notes" \
            "eight1|eight instances, 1 note each" \
            "eight0|eight instances, all silent" \
            "eight8|eight instances, 8 notes each"; do
    name=${case%%|*}
    label=${case#*|}
    n=$(valgrind --tool=callgrind --instr-atstart=no --callgrind-out-file=/dev/null \
        "$out/instances" "$name" 2>&1 | sed -n 's/.*I *refs: *//p' | tr -d ', ')
    printf '%-38s %12s\n' "$label" "$n"
done
