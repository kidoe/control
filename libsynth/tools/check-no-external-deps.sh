#!/bin/sh
# libsynth promises the core links against nothing it does not need: no libm, no
# malloc, no OS. That is what makes the microcontroller target possible, and it
# is invisible on a desktop build, where pulling in sinf() costs nothing and
# breaks nothing. So it gets checked rather than trusted.
#
# Two kinds of undefined symbol have to be told apart, which a cross build makes
# obvious:
#
#   Compiler runtime. On a core with no FPU every float operation becomes a call
#   to __aeabi_fmul and friends, and struct assignment becomes memcpy. These come
#   from libgcc and the freestanding part of libc, they exist on every toolchain,
#   and no amount of rewriting removes them. Reported, not failed.
#
#   Everything else. A real dependency the target has to provide. Failed.
#
# __atomic_* is deliberately NOT excused: those are libatomic calls, they appear
# when code asks for an atomic wider than the target can do in one instruction,
# and on a Cortex-M0+ there is no instruction to build one from at all. That is a
# portability bug in this library, not a fact about the toolchain.
#
# Usage: check-no-external-deps.sh path/to/libsynth.a [nm-binary]

set -e

lib="$1"
nm_bin="${2:-nm}"

if [ -z "$lib" ] || [ ! -f "$lib" ]; then
    echo "usage: $0 path/to/libsynth.a [nm-binary]" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$nm_bin" -u "$lib" | sed -n 's/^ *U //p' | sort -u > "$work/undefined"
"$nm_bin" -g --defined-only "$lib" | awk '{ if (NF >= 3) print $3 }' | sort -u > "$work/defined"
comm -23 "$work/undefined" "$work/defined" > "$work/external"

grep -E '^(__aeabi_|__gnu_|mem(cpy|set|move|cmp)$)' "$work/external" > "$work/runtime" || true
grep -vE '^(__aeabi_|__gnu_|mem(cpy|set|move|cmp)$)' "$work/external" > "$work/real" || true

if [ -s "$work/runtime" ]; then
    echo "compiler runtime, expected on this target:"
    sed 's/^/  /' "$work/runtime"
fi

if [ -s "$work/real" ]; then
    echo "FAIL: the core depends on symbols it does not define:"
    sed 's/^/  /' "$work/real"
    echo
    echo "Anything listed above has to be reachable on every target, including"
    echo "one with no libc. Replace it with arithmetic in the core."
    exit 1
fi

echo "OK: no external dependencies beyond compiler runtime"
