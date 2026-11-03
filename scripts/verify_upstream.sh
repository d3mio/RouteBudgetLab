#!/usr/bin/env bash
# Confirms third_party/anytime-mapf is byte-identical to the pinned upstream commit
# (compares against the SHA-256 list taken from a clean checkout of that commit).
set -uo pipefail
cd "$(dirname "$0")/../third_party/anytime-mapf" || exit 2
bad=0
while read -r sum file; do
  [ "$(shasum -a 256 "$file" 2>/dev/null | cut -d' ' -f1)" = "$sum" ] || { echo "MODIFIED/MISSING: $file"; bad=1; }
done < UPSTREAM_FILES.sha256
extra=$(find . -type f ! -name UPSTREAM_FILES.sha256 ! -name UPSTREAM.json | sed 's|^\./||' | sort | comm -13 <(awk '{print $2}' UPSTREAM_FILES.sha256 | sed 's|^\./||' | sort) -)
[ -n "$extra" ] && { echo "EXTRA FILES: $extra"; bad=1; }
[ $bad -eq 0 ] && echo "third_party/anytime-mapf == upstream 47bb25e9 (no local modifications)"
exit $bad
