#!/bin/sh
#
# Counts the instructions the core executes to render one second of audio on a
# Cortex-M0 and a Cortex-M4F, by running it on QEMU's models of both and
# counting with a TCG plugin.
#
# This exists because "an M0+ has no FPU, so floats cost more" is an assertion,
# and the whole point of the portability claims here is that they are measured.
# It answers how much more, with a number.
#
# What it needs:
#   arm-none-eabi-gcc, qemu-system-arm, and a host cc for the plugin.
#   include/qemu/qemu-plugin.h matching the QEMU in use — the script fetches it
#   if it is not beside this file, or pass its directory as $QEMU_PLUGIN_INC.
#
# Usage: libsynth/tools/bench-arm/run.sh [libsynth dir]   (from the repo root)
#
# Read the output as a LOWER BOUND on cost. QEMU counts instructions retired,
# not cycles: it models neither the flash wait states nor the multi-cycle loads
# and taken branches that make a real Cortex-M slower than one instruction per
# cycle. Real silicon is worse than this, never better.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=${1:-libsynth}
out=${TMPDIR:-/tmp}/bench-arm.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT

inc=${QEMU_PLUGIN_INC:-$here}
if [ ! -f "$inc/qemu-plugin.h" ]; then
    ver=$(qemu-system-arm --version | sed -n 's/.*version \([0-9]*\.[0-9]*\).*/\1/p' | head -1)
    echo "fetching qemu-plugin.h for QEMU $ver"
    curl -sS -f -o "$out/qemu-plugin.h" \
        "https://raw.githubusercontent.com/qemu/qemu/v$ver.0/include/qemu/qemu-plugin.h"
    inc=$out
fi
cc -shared -fPIC -O2 -I"$inc" "$here/insncount.c" -o "$out/libinsncount.so"

emit_ld() { # origin len ram_origin ram_len file
    sed -e "s/FLASH_ORIGIN/$1/" -e "s/FLASH_LEN/$2/" \
        -e "s/RAM_ORIGIN/$3/" -e "s/RAM_LEN/$4/" "$here/link.ld.in" > "$5"
}
emit_ld 0x00000000 256K 0x20000000 16K "$out/m0.ld"
emit_ld 0x00000000 4M   0x20000000 4M  "$out/m4f.ld"

run() { # target, then -D flags
    target=$1; shift
    case $target in
    m0)  cpu="-mcpu=cortex-m0 -mthumb"
         ld=$out/m0.ld;  machine="-M microbit -cpu cortex-m0" ;;
    m4f) cpu="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
         ld=$out/m4f.ld; machine="-M mps2-an386 -cpu cortex-m4" ;;
    esac
    arm-none-eabi-gcc $cpu -Os -std=c11 -ffreestanding -nostdlib \
        -Wall -Wextra -Wconversion -Werror -I"$root/include" -T "$ld" "$@" \
        "$here/bench.c" "$here/start.c" "$root/src/dsp.c" "$root/src/synth.c" \
        -o "$out/run.elf" -lgcc
    qemu-system-arm $machine -semihosting -nographic \
        -plugin "$out/libinsncount.so" -d plugin -D /dev/stdout \
        -kernel "$out/run.elf" 2>&1 | sed -n 's/^INSNS //p'
}

# Each case is measured twice, once rendering a second of audio and once
# rendering none, and the difference is the audio. Setting eight voices going
# costs 80 million instructions on an M0 on its own, which would otherwise land
# in the answer.
report() { # label, then -D flags
    label=$1; shift
    for t in m4f m0; do
        with=$(run $t "$@")
        without=$(run $t -DBENCH_BLOCKS=0 "$@")
        eval "n_$t=$((with - without))"
    done
    printf '%-30s %9d %12d\n' "$label" "$n_m4f" "$n_m0"
}

printf '%-30s %9s %12s\n' 'instructions per second of audio' 'M4F' 'M0'
report "silent, 8 empty slots"   -DBENCH_VOICES=0
report "sine, 1 voice"           -DBENCH_VOICES=1
report "sine, 8 voices"          -DBENCH_VOICES=8
report "saw, 8 voices"           -DBENCH_VOICES=8 -DBENCH_WAVE=SYNTH_WAVE_SAW
report "sine, 8 + pitch sweep"   -DBENCH_VOICES=8 -DBENCH_SWEEP=1
