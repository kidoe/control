#!/bin/sh
#
# What the most expensive block costs, against the deadline it has to meet.
#
# Needs valgrind and its client-request header (valgrind/callgrind.h). The
# counts are x86 instructions, which for this library land within a per cent of
# what QEMU counts for the same C on a Cortex-M4F — see tools/bench-arm — so
# reading them against an M4F's budget is fair.
#
# Usage: libsynth/tools/bench-block/run.sh [libsynth dir]   (from the repo root)
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=${1:-libsynth}
out=${TMPDIR:-/tmp}/bench-block.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT

cc -O2 -I"$root/include" "$here/block.c" "$root/src/dsp.c" "$root/src/synth.c" \
   -o "$out/block"

# 96 frames at 48 kHz is 2 ms; a 168 MHz Cortex-M4F has 336000 cycles in that.
budget=336000

printf '%-34s %10s  %s\n' 'one 96-frame block' 'instr' 'of a 2 ms budget on a 168 MHz M4F'
for case in "silent|nothing sounding" \
            "steady|8 voices, no events" \
            "param1|8 voices, one parameter change" \
            "param16|8 voices, 16 parameter changes" \
            "notes8|8 voices, 8 notes starting" \
            "notes16|8 voices, 16 notes starting"; do
    name=${case%%|*}
    label=${case#*|}
    n=$(valgrind --tool=callgrind --instr-atstart=no --callgrind-out-file=/dev/null \
        "$out/block" "$name" 2>&1 | sed -n 's/.*I *refs: *//p' | tr -d ', ')
    printf '%-34s %10s  %s%%\n' "$label" "$n" \
        "$(awk "BEGIN { printf \"%.1f\", 100 * $n / $budget }")"
done
