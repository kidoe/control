#!/bin/sh
# libsynth promises the core links against nothing at all: no libm, no malloc,
# no libc. That is what makes the microcontroller target possible, and it is
# invisible on a desktop build, where pulling in sinf() costs nothing and
# breaks nothing. So it gets checked rather than trusted.
#
# The archive's two objects reference each other, so a symbol only counts as
# external when nothing inside the archive defines it.
#
# Usage: check-no-external-deps.sh path/to/libsynth.a

set -e

lib="$1"
if [ -z "$lib" ] || [ ! -f "$lib" ]; then
    echo "usage: $0 path/to/libsynth.a" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

nm -u "$lib" | sed -n 's/^ *U //p' | sort -u > "$work/undefined"
nm -g --defined-only "$lib" | awk '{ if (NF >= 3) print $3 }' | sort -u > "$work/defined"
comm -23 "$work/undefined" "$work/defined" > "$work/external"

if [ -s "$work/external" ]; then
    echo "FAIL: the core depends on symbols it does not define:"
    sed 's/^/  /' "$work/external"
    echo
    echo "Anything listed above has to be reachable on every target, including"
    echo "one with no libc. Replace it with arithmetic in the core."
    exit 1
fi

echo "OK: no external dependencies"
