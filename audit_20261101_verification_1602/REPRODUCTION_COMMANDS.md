# Commands and execution evidence

Repository root: `/Users/disasgamage/Documents/GitHub/RouteBudgetLab`. Actual commands, return codes, elapsed times, and stdout/stderr are preserved by `run.py` in `commands.jsonl` and named `.log` files. Probe compile/link command arrays are in `probe_commands.jsonl`. `provenance.log` records AppleClang **21.0.0**, CMake **4.4.2**, Python **3.13.0**, Darwin **25.3.0 arm64**; CMake found Boost **1.92.0**. SciPy **1.17.1**. Historical reports' AppleClang17/M3 identity is not established by these current observations. Hardware-brand sysctl was denied by the sandbox; unnecessary for the completed audit, and no permission bypass was attempted.

All work was local; no network installs, commits, pushes or source patches. No pilot rerun. No use of the earlier untracked audit's summaries or builds as evidence. A Python import generated an analysis bytecode cache; it was relocated into this audit directory and future imports disabled bytecode writing. Pre-existing other caches were preserved.

Actual main commands (already executed):

```sh
cmake -S . -B audit_20261101_verification_1602/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build audit_20261101_verification_1602/build-release -j 4
ctest --test-dir audit_20261101_verification_1602/build-release --output-on-failure
cmake -S . -B audit_20261101_verification_1602/build-asan -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build audit_20261101_verification_1602/build-asan -j 4
env ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir audit_20261101_verification_1602/build-asan --output-on-failure
python3 audit_20261101_verification_1602/recompute.py
python3 audit_20261101_verification_1602/supplement.py
python3 audit_20261101_verification_1602/compile_probe.py
audit_20261101_verification_1602/probe-release data/sample/random-32-32-20.map data/sample/random-32-32-20-random-1.scen
env ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 audit_20261101_verification_1602/probe-asan data/sample/random-32-32-20.map data/sample/random-32-32-20-random-1.scen
bash scripts/verify_upstream.sh
```

For another audit execution, **use a new uniquely named sibling audit directory**, copy the scripts there, and substitute that directory in commands. Do not rerun the historical experiment runner defaults: they overwrite historical raw paths. The scripts above intentionally direct analysis outputs to their own directory; a replay in the same directory would replace this audit's generated outputs.

`recompute.py`: individual raw-to-cell reconstruction; exact sign/rank enumeration; sample SD/SEM; independent path validation; original manifest checking; offline raw counters. `supplement.py`: initial fixture format conversion/validation, historical overhead recomputation, repository analysis function replay with outputs redirected, Git provenance capture. No source behavior is changed.

Failures retained rather than hidden:

- First audit recompute script had a missing closing bracket; `recompute.log` exit1. Corrected audit script then ran successfully (`recompute-v2.log`). No repository script was changed.
- Initial fixtures use `id:(linear_location)` internal serialization; direct independent CLI input returned exit2 PATHS_PARSE for all8. `supplement.py` converts to `Agent id:(row,column)` only in the audit directory; all8 pass afterward. Original and converted logs retained.
- `-DENABLE_SANITIZERS=ON` configure exits0 but warns unused variable; generated flags lack sanitizers. This deliberately reproduced the documented-command problem. The genuine sanitizer build uses explicit flags.
- Audit-only intentional assertion probe exits17 under `-O3 -DNDEBUG`, confirming ASSERT_TRUE executes. It is an expected failure, not a suite failure.
- Padding-cost probe exits0 on reproducing stored/authoritative mismatch; both configurations confirm it. It is a regression diagnostic, not a solver fix.

Hash files: `EVIDENCE_SHA256.txt` hashes tracked repository evidence (relative to repository root), including raw files, source, configs, logs, reports and vendored code. `AUDIT_SHA256.txt` hashes reviewable audit outputs (relative to this audit directory; excludes its own manifest, ZIP, executables and build trees). Build provenance remains in source-side compile_commands/flags/link files, copies in the review bundle, and logs. `REVIEW_BUNDLE.zip.sha256` covers the ZIP. Verify with `shasum -a 256 -c <manifest>` from the appropriate directory.
