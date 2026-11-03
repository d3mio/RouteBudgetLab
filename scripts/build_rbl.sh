#!/usr/bin/env bash
# Build and test RouteBudget Lab's own code (C++17).
#   scripts/build_rbl.sh [release|asan]
# asan = AddressSanitizer + UndefinedBehaviorSanitizer (correctness runs; never used for benchmarks).
source "$(dirname "$0")/common.sh"
MODE="${1:-release}"
FLAGS=""; CMAKE_MODE=Release
if [ "$MODE" = "asan" ]; then CMAKE_MODE=Debug; FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"; fi
BUILD="$ROOT/build/rbl-$MODE"
run_logged "build_rbl_${MODE}_configure" cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE="$CMAKE_MODE" -DCMAKE_CXX_FLAGS="$FLAGS" "${TOOLCHAIN_EXTRA[@]}" || exit $?
run_logged "build_rbl_${MODE}_compile" cmake --build "$BUILD" -j"$NCPU" || exit $?
run_logged "test_rbl_${MODE}" ctest --test-dir "$BUILD" --output-on-failure || exit $?
echo "OK: $BUILD"
