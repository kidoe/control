#!/bin/sh
# The Java side of the bridge repeats the parameter enum as integer constants,
# because a host builds its UI from them. Nothing in either language notices
# when the two drift: a stale constant silently edits the wrong parameter, and a
# missing one is simply unreachable from the app. This compares them.
#
# Usage: tools/check-java-constants.sh [path to libsynth]
set -e

root=${1:-$(dirname "$0")/..}
header=$root/include/synth/synth.h
java=$root/backends/android/java/com/kidoe/synth/SynthEngine.java

# The enum, in declaration order, as "index NAME" with the SYNTH_PARAM_ prefix
# dropped. SYNTH_PARAM_COUNT is the terminator, not a parameter.
sed -n '/^typedef enum {/,/} synth_param_t;/p' "$header" \
    | sed -n 's/^ *SYNTH_PARAM_\([A-Z0-9_]*\)\( *=[^,]*\)\{0,1\},.*/\1/p' \
    | grep -v '^COUNT$' \
    | awk '{ print NR - 1, $0 }' > /tmp/synth-enum.$$

# The Java constants, as "value NAME".
sed -n 's/^ *public static final int PARAM_\([A-Z0-9_]*\) *= *\([0-9]*\);.*/\2 \1/p' \
    "$java" | sort -n > /tmp/synth-java.$$

status=0
if ! diff -u /tmp/synth-enum.$$ /tmp/synth-java.$$ > /tmp/synth-diff.$$; then
    echo "SynthEngine.java does not match synth_param_t:"
    echo "  '-' is in the C enum only, '+' is in the Java file only."
    sed -n '4,$p' /tmp/synth-diff.$$
    status=1
else
    echo "OK: $(wc -l < /tmp/synth-enum.$$ | tr -d ' ') parameter constants match synth_param_t"
fi

rm -f /tmp/synth-enum.$$ /tmp/synth-java.$$ /tmp/synth-diff.$$
exit $status
