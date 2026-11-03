#!/usr/bin/env bash
# Shared helpers for RouteBudget Lab scripts. Source this file; do not execute it.
#
#  * Sets ROOT and a toolchain fallback for macOS machines whose Xcode licence has
#    not been accepted (the /usr/bin shims refuse to run). We NEVER accept the licence
#    or use sudo; we call the Command Line Tools binaries directly with an explicit SDK.
#  * run_logged NAME CMD...  runs CMD with ALL output going to logs/NAME.log, records the
#    REAL exit code of CMD (not of tee/grep/tail) in logs/exit_codes.tsv, prints the log tail
#    on failure, and returns CMD's exit code.
set -uo pipefail
if [ -n "${ZSH_VERSION:-}" ]; then
  ROOT="$(cd "$(dirname "${(%):-%N}")/.." && pwd)"
else
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
LOGDIR="$ROOT/logs"
mkdir -p "$LOGDIR"

TOOLCHAIN_EXTRA=()   # extra cmake args for compilers/SDK
TOOLCHAIN_NOTE="system default toolchain"
if [ "$(uname)" = "Darwin" ]; then
  CLT=/Library/Developer/CommandLineTools
  if ! xcrun --show-sdk-path >/dev/null 2>&1; then
    TOOLCHAIN_NOTE="Command Line Tools used directly (xcrun shim unavailable; Xcode licence not accepted)"
    export PATH="$CLT/usr/bin:$PATH"
    export SDKROOT="$CLT/SDKs/MacOSX.sdk"
    TOOLCHAIN_EXTRA+=(-DCMAKE_C_COMPILER="$CLT/usr/bin/clang" -DCMAKE_CXX_COMPILER="$CLT/usr/bin/clang++" -DCMAKE_AR="$CLT/usr/bin/ar" -DCMAKE_RANLIB="$CLT/usr/bin/ranlib")
  fi
  [ -d /opt/homebrew/opt/boost ] && TOOLCHAIN_EXTRA+=(-DBOOST_ROOT=/opt/homebrew/opt/boost)
fi
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)"

run_logged() {
  local name="$1"; shift
  local log="$LOGDIR/$name.log"
  {
    echo "# $(date -u +%FT%TZ)  cwd=$(pwd)"
    echo "# command: $*"
  } > "$log"
  "$@" >> "$log" 2>&1
  local rc=$?
  printf '%s\t%s\t%s\t%s\n' "$(date -u +%FT%TZ)" "$name" "$rc" "$*" >> "$LOGDIR/exit_codes.tsv"
  echo "[$name] exit code $rc  (log: logs/$name.log)"
  if [ $rc -ne 0 ]; then echo "---- last 25 log lines ----"; tail -25 "$log"; fi
  return $rc
}
