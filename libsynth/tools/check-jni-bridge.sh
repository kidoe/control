#!/bin/sh
# The JNI contract has no compiler. A native method whose Java signature and C
# definition disagree links fine, loads fine, and then reads its arguments off
# by one at runtime: a track index arrives where a note number is expected and
# the app plays the wrong thing, or nothing. A name that does not match at all
# is an UnsatisfiedLinkError the first time the app touches it. Neither shows up
# in any build this repository can run, because building the real bridge needs
# the Android NDK.
#
# So take javac's own word for what the C side has to look like: javac -h emits
# a prototype per native method, and this compares those with the definitions in
# synth_jni.c, names, return types and argument types alike.
#
# Then it compiles the bridge, with the JDK's real jni.h and the stand-in AAudio
# headers in tools/jni-stubs, at every track count that changes the code path.
# That is not a substitute for an NDK build, and jni-stubs/aaudio/AAudio.h says
# so; what it does mean is that the bridge's own C never goes unchecked.
#
# Needs a JDK and a C compiler.
#
# Usage: tools/check-jni-bridge.sh [path to libsynth]
set -e

root=${1:-$(dirname "$0")/..}
java=$root/backends/android/java/com/kidoe/synth/SynthEngine.java
jni=$root/backends/android/synth_jni.c
javac=${JAVAC:-javac}

if ! command -v "$javac" > /dev/null 2>&1; then
    echo "check-jni-bridge: no $javac on PATH; a JDK is what generates the" \
         "reference prototypes, so this check cannot be faked without one." >&2
    exit 1
fi

work=${TMPDIR:-/tmp}/check-jni-bridge.$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT

"$javac" -d "$work" -h "$work" "$java"

# Both sides reduced to "name return (argument types)": flatten the file, cut it
# at each JNIEXPORT, keep what looks like a prototype, then drop the argument
# names the C file has and the generated header does not.
normalize() {
    tr '\n' ' ' < "$1" \
        | sed 's/JNIEXPORT/\
JNIEXPORT/g' \
        | sed -n 's/^JNIEXPORT *\([A-Za-z_][A-Za-z0-9_]*\) *JNICALL *\(Java_[A-Za-z0-9_]*\) *(\([^)]*\)).*/\2 \1 (\3)/p' \
        | awk '{
            name = $1;
            ret = $2;
            args = substr($0, index($0, "(") + 1);
            sub(/\).*/, "", args);
            n = split(args, a, ",");
            out = "";
            for (i = 1; i <= n; ++i) {
                t = a[i];
                gsub(/\*/, " * ", t);
                gsub(/^[ \t]+|[ \t]+$/, "", t);
                gsub(/[ \t]+/, " ", t);
                m = split(t, w, " ");
                # a trailing identifier is a parameter name, not part of the type
                if (m > 1 && w[m] ~ /^[A-Za-z_][A-Za-z0-9_]*$/) {
                    --m;
                }
                t = "";
                for (j = 1; j <= m; ++j) {
                    t = t (j > 1 ? " " : "") w[j];
                }
                out = out (i > 1 ? ", " : "") t;
            }
            printf "%s %s (%s)\n", name, ret, out;
        }' \
        | sort
}

normalize "$work"/com_kidoe_synth_SynthEngine.h > "$work/want"
normalize "$jni" > "$work/have"

# A pass with nothing in it would be the worst outcome of all: silence that
# reads as agreement. Both sides have to be non-empty before a diff means
# anything.
for side in want have; do
    if [ ! -s "$work/$side" ]; then
        echo "check-jni-bridge: found no prototypes in the $side side;" \
             "the extraction is broken, not the bridge." >&2
        exit 1
    fi
done

status=0
if ! diff -u "$work/want" "$work/have" > "$work/diff"; then
    echo "synth_jni.c does not implement what SynthEngine.java declares:"
    echo "  '-' is what javac -h says the C side must be,"
    echo "  '+' is what synth_jni.c actually defines."
    sed -n '4,$p' "$work/diff"
    status=1
else
    echo "OK: $(wc -l < "$work/want" | tr -d ' ') native methods match their JNI definitions"
fi

# The JDK that owns javac is the one whose jni.h has to be used, whether or not
# JAVA_HOME is set to it.
jdk=${JAVA_HOME:-}
if [ -z "$jdk" ] || [ ! -f "$jdk/include/jni.h" ]; then
    jdk=$(command -v "$javac")
    while [ -L "$jdk" ]; do
        link=$(readlink "$jdk")
        case $link in
            /*) jdk=$link ;;
            *) jdk=$(dirname "$jdk")/$link ;;
        esac
    done
    jdk=$(dirname "$(dirname "$jdk")")
fi
if [ ! -f "$jdk/include/jni.h" ]; then
    echo "check-jni-bridge: no jni.h under $jdk; cannot compile the bridge." >&2
    exit 1
fi

for tracks in 1 4 8; do
    ${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror -fsyntax-only \
        -DSYNTH_TRACKS="$tracks" \
        -I"$root/include" -I"$(dirname "$0")/jni-stubs" \
        -I"$jdk/include" -I"$jdk/include/linux" \
        "$jni"
done
echo "OK: the bridge compiles at 1, 4 and 8 tracks"

exit $status
