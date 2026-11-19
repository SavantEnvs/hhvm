#!/usr/bin/env bash
#
# mayhem/build.sh — build HHVM's HDF config parser (hphp/neo) harness + its known-answer driver.
#
# The full hhvm binary (folly/fbthrift/proxygen via getdeps, a Rust hackc, OCaml) is a multi-hour,
# networked build, so this integration fuzzes the self-contained part of HHVM that parses untrusted
# config text: the neo HDF library, compiled straight from hphp/neo/*.c exactly as upstream's
# hphp/neo/CMakeLists.txt does (every *.c in the directory, include root = the repo root).
# Re-runnable and offline: no downloads, objects go to mayhem/out/ (gitignored).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"
mkdir -p "$OUT/fuzz" "$OUT/test"

NEO_SRCS=(hphp/neo/*.c)

# compile_lib <objdir> <flags...>: every hphp/neo/*.c → <objdir>/libneo.a
compile_lib() {
  local dir="$1"; shift
  local objs=()
  for f in "${NEO_SRCS[@]}"; do
    local o="$dir/$(basename "${f%.c}").o"
    # shellcheck disable=SC2086
    $CC "$@" -I"$SRC" -I"$SRC/hphp/neo" -c "$f" -o "$o"
    objs+=("$o")
  done
  rm -f "$dir/libneo.a"
  ar rcs "$dir/libneo.a" "${objs[@]}"
}

# 1) the project, sanitized + DWARF 3 (the fuzzed code is instrumented, not just the harness)
# shellcheck disable=SC2086
compile_lib "$OUT/fuzz" $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link

# 2) harness: libFuzzer binary + standalone reproducer
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/fuzz/lsan_off.o"
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -I"$SRC" -c "$SRC/mayhem/harnesses/fuzz_hdf_read_string.c" \
    -o "$OUT/fuzz/fuzz_hdf_read_string.o"
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/fuzz/standalone_main.o"
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz/fuzz_hdf_read_string.o" \
    "$OUT/fuzz/libneo.a" "$OUT/fuzz/lsan_off.o" -lpthread -lm -o "$OUT/fuzz_hdf_read_string"
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/fuzz/standalone_main.o" "$OUT/fuzz/fuzz_hdf_read_string.o" \
    "$OUT/fuzz/libneo.a" "$OUT/fuzz/lsan_off.o" -lpthread -lm -o "$OUT/fuzz_hdf_read_string-standalone"

# 3) the known-answer driver for test.sh, with normal flags (independent of the sanitized build)
# shellcheck disable=SC2086
compile_lib "$OUT/test" -O2 $COVERAGE_FLAGS
# shellcheck disable=SC2086
$CC -O2 $COVERAGE_FLAGS -I"$SRC" "$SRC/mayhem/tests/hdf_kat.c" "$OUT/test/libneo.a" -lpthread \
    -o "$OUT/hdf_kat"

echo "built: $OUT/fuzz_hdf_read_string $OUT/fuzz_hdf_read_string-standalone $OUT/hdf_kat"
