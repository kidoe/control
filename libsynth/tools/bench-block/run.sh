#!/bin/sh
#
# What the most expensive block costs, against the deadline it has to meet.
#
# Needs valgrind and its client-request header (valgrind/callgrind.h). The
# counts are x86 instructions, which for this library land within about 10%
# either way of what QEMU counts for the same C on a Cortex-M4F — see
# tools/bench-arm — so reading them against an M4F's budget is indicative
# rather than exact.
#
# Usage: libsynth/tools/bench-block/run.sh [libsynth dir]   (from the repo root)
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=${1:-libsynth}
out=${TMPDIR:-/tmp}/bench-block.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT

# Polyphony is a compile-time constant and the idle floor is proportional to it,
# which is the whole question when several instances are mixed. Default 8, as
# the rest of the documentation quotes; SYNTH_MAX_VOICES=4 in the environment
# re-runs the table for a build sized to one track.
voices=${SYNTH_MAX_VOICES:-8}

cc -O2 -DSYNTH_MAX_VOICES="$voices" -I"$root/include" \
   "$here/block.c" "$root/src/dsp.c" "$root/src/synth.c" -o "$out/block"

# 96 frames at 48 kHz is 2 ms; a 168 MHz Cortex-M4F has 336000 cycles in that.
budget=336000

printf '%-38s %10s  %s\n' "one 96-frame block, $voices voices" 'instr' \
    'of a 2 ms budget on a 168 MHz M4F'
for case in "silent|nothing sounding" \
            "steady|8 voices, no events" \
            "param1|8 voices, one parameter change" \
            "param16|8 voices, 16 parameter changes" \
            "notes8|8 voices, 8 notes starting" \
            "notes16|8 voices, 16 notes starting" \
            "mix4|4 instances mixed, 2 voices each" \
            "mix4full|4 instances mixed, every slot full" \
            "mix4idle|4 instances mixed, all silent"; do
    name=${case%%|*}
    label=${case#*|}
    n=$(valgrind --tool=callgrind --instr-atstart=no --callgrind-out-file=/dev/null \
        "$out/block" "$name" 2>&1 | sed -n 's/.*I *refs: *//p' | tr -d ', ')
    printf '%-38s %10s  %s%%\n' "$label" "$n" \
        "$(awk "BEGIN { printf \"%.1f\", 100 * $n / $budget }")"
done
