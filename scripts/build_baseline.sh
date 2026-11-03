#!/usr/bin/env bash
# Build the pinned upstream baseline (thomyphan/anytime-mapf, "balance") out of source.
#
#   scripts/build_baseline.sh [release|asan] [--std 14|17]
#
#  release / --std 14 : the REFERENCE baseline, exactly upstream's own configuration (C++14).
#  --std 17           : same unmodified sources compiled as C++17 (our implementation language).
#  asan               : Debug-style build with -fsanitize=address,undefined (correctness only).
# Output: build/baseline-<mode>-cxx<std>/balance. Full logs: logs/build_baseline_*.log
# Upstream sources are never edited; the standard is changed only by a CMake override file.
source "$(dirname "$0")/common.sh"

MODE=release; STD=14
while [ $# -gt 0 ]; do
  case "$1" in
    release|asan) MODE="$1" ;;
    --std) shift; STD="$1" ;;
    *) echo "unknown argument: $1" >&2; exit 64 ;;
  esac; shift
done
case "$STD" in 14|17) ;; *) echo "--std must be 14 or 17" >&2; exit 64 ;; esac

SRC="$ROOT/third_party/anytime-mapf"
EIGEN="$ROOT/third_party/eigen"
TAG="baseline-$MODE-cxx$STD"
BUILD="$ROOT/build/$TAG"

FLAGS="-Wno-deprecated-declarations -D_LIBCPP_ENABLE_CXX17_REMOVED_RANDOM_SHUFFLE"
CMAKE_MODE=Release
if [ "$MODE" = "asan" ]; then CMAKE_MODE=Debug; FLAGS="$FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"; fi

# Upstream does #include <eigen3/Eigen/Core>, so expose the vendored copy as .../eigen3/.
mkdir -p "$ROOT/build/eigen-include" "$ROOT/build/eigen-config"
ln -sfn "$EIGEN" "$ROOT/build/eigen-include/eigen3"
cat > "$ROOT/build/eigen-config/Eigen3Config.cmake" <<EOF
set(Eigen3_VERSION 3.4.0)
if(NOT TARGET Eigen3::Eigen)
  add_library(Eigen3::Eigen INTERFACE IMPORTED)
  set_target_properties(Eigen3::Eigen PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "$EIGEN")
endif()
EOF
echo 'set(PACKAGE_VERSION 3.4.0)
set(PACKAGE_VERSION_COMPATIBLE TRUE)' > "$ROOT/build/eigen-config/Eigen3ConfigVersion.cmake"

EXTRA=("${TOOLCHAIN_EXTRA[@]}" -DEigen3_DIR="$ROOT/build/eigen-config" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
       -DCMAKE_CXX_FLAGS="$FLAGS -I$ROOT/build/eigen-include -I$EIGEN -DEIGEN_MPL2_ONLY")
if [ "$STD" = "17" ]; then
  # Upstream CMakeLists hard-codes CMAKE_CXX_STANDARD 14, so a deferred override is applied to the target.
  EXTRA+=(-DRBL_CXX_STANDARD=17 -DCMAKE_PROJECT_INCLUDE="$ROOT/cmake/override_std.cmake")
fi

rm -rf "$BUILD"
run_logged "build_baseline_${TAG}_configure" cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE="$CMAKE_MODE" "${EXTRA[@]}" || exit $?
run_logged "build_baseline_${TAG}_compile" cmake --build "$BUILD" -j"$NCPU" || exit $?

# Prove which language standard / flags were really used (from the compiler command database).
python3 - "$BUILD/compile_commands.json" "$TAG" > "$LOGDIR/build_baseline_${TAG}_effective_flags.txt" <<'PY'
import json, sys
db = json.load(open(sys.argv[1]))
cmd = next(e for e in db if e["file"].endswith("src/LNS.cpp"))["command"]
print("build tag:", sys.argv[2]); print("compile command for LNS.cpp:"); print(cmd)
std = [t for t in cmd.split() if t.startswith("-std=")]
print("EFFECTIVE -std FLAGS:", std)
PY
cat "$LOGDIR/build_baseline_${TAG}_effective_flags.txt" | grep "EFFECTIVE"
# Upstream prints its option list and returns 1 for --help, so check the text, not the exit code.
HELP_TEXT="$("$BUILD/balance" --help 2>&1 || true)"
grep -q -e "--map" <<< "$HELP_TEXT" || { echo "built binary did not print its help"; exit 1; }
echo "Built: $BUILD/balance"
