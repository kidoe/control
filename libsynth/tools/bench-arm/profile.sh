#!/bin/sh
#
# Where the instructions go, per function, rendering a second of audio on a
# Cortex-M0 or Cortex-M4F. run.sh says how many there are; this says whose they
# are, which is the difference between "soft float is expensive" and "20% of
# everything is __aeabi_fdiv, and here is where the divisions are".
#
# Usage: libsynth/tools/bench-arm/profile.sh [m0|m4f] [libsynth dir]
set -e

here=$(cd "$(dirname "$0")" && pwd)
target=${1:-m0}
root=${2:-libsynth}
out=${TMPDIR:-/tmp}/bench-arm-prof.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT

inc=${QEMU_PLUGIN_INC:-$here}
if [ ! -f "$inc/qemu-plugin.h" ]; then
    ver=$(qemu-system-arm --version | sed -n 's/.*version \([0-9]*\.[0-9]*\).*/\1/p' | head -1)
    curl -sS -f -o "$out/qemu-plugin.h" \
        "https://raw.githubusercontent.com/qemu/qemu/v$ver.0/include/qemu/qemu-plugin.h"
    inc=$out
fi
cc -shared -fPIC -O2 -I"$inc" "$here/insnprof.c" -o "$out/libinsnprof.so"

case $target in
m0)  cpu="-mcpu=cortex-m0 -mthumb"
     flash=256K; ram=16K; machine="-M microbit -cpu cortex-m0" ;;
m4f) cpu="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
     flash=4M; ram=4M; machine="-M mps2-an386 -cpu cortex-m4" ;;
*)   echo "usage: $0 [m0|m4f] [libsynth dir]" >&2; exit 2 ;;
esac
sed -e "s/FLASH_ORIGIN/0x00000000/" -e "s/FLASH_LEN/$flash/" \
    -e "s/RAM_ORIGIN/0x20000000/" -e "s/RAM_LEN/$ram/" "$here/link.ld.in" > "$out/t.ld"

arm-none-eabi-gcc $cpu -Os -std=c11 -ffreestanding -nostdlib \
    -Wall -Wextra -Wconversion -Werror -I"$root/include" -T "$out/t.ld" \
    "$here/bench.c" "$here/start.c" "$root/src/dsp.c" "$root/src/synth.c" \
    -o "$out/prof.elf" -lgcc

# QEMU reports the guest's semihosting exit reason as its own status, so a
# clean run still ends non-zero. The check that it worked is the output.
qemu-system-arm $machine -semihosting -nographic \
    -plugin "$out/libinsnprof.so" -d plugin -D "$out/prof.txt" \
    -kernel "$out/prof.elf" > /dev/null 2>&1 || :
if [ ! -s "$out/prof.txt" ]; then
    echo "no profile came back: did QEMU run the image?" >&2
    exit 1
fi
arm-none-eabi-nm -n -S "$out/prof.elf" | grep -i ' [tT] ' > "$out/syms.txt"

python3 - "$out/syms.txt" "$out/prof.txt" "$target" <<'PY'
import collections, sys

syms = sorted((int(l.split()[0], 16), l.split()[-1]) for l in open(sys.argv[1]))
addrs = [a for a, _ in syms]

def owner(pc):
    lo, hi = 0, len(addrs) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if addrs[mid] <= pc:
            lo = mid
        else:
            hi = mid - 1
    return syms[lo][1] if addrs and addrs[lo] <= pc else "?"

total = collections.Counter()
grand = 0
for line in open(sys.argv[2]):
    if not line.startswith("TB "):
        continue
    _, pc, insns, count = line.split()
    n = int(insns) * int(count)
    grand += n
    total[owner(int(pc, 16))] += n

print("%s, one second of audio, 8 sine voices: %d instructions\n"
      % (sys.argv[3], grand))
print("%-28s %14s %8s" % ("function", "instructions", "share"))
for name, n in total.most_common(15):
    print("%-28s %14d %7.1f%%" % (name, n, 100.0 * n / grand))
helpers = sum(n for k, n in total.items() if k.startswith("__"))
print("\ncompiler-runtime float helpers: %.1f%%" % (100.0 * helpers / grand))
PY
