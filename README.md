# RouteBudget Lab

**Constraint-Validated Resumption of Interrupted Repairs for Deadline-Aware Multi-Agent Path Finding**

RouteBudget Lab implements constraint-validated resumption of interrupted Multi-Agent Path Finding repairs and evaluates selective recovery of completed replacement routes under a shared global time budget. It combines fresh incumbent compatibility checks, a checked optimistic gain filter, dependency- and conflict-aware greedy subset selection, independent complete-plan validation and staged transactional publication. Controlled development experiments measure recovery against ordinary resumption after accounting for its work and displaced search.

Default: `RESUMPTION_NO_PA`. The five selective/control comparisons contain 12 wins and 4 losses in total; these correlated comparisons are not independent observations. Recovery remains an optional evaluated extension; RESUMPTION_NO_PA remains the default. No eviction or terminal recovery committed a route; numerical wins in those policies cannot be attributed to recovered-route publication. The reserve control and timing variation must be considered.

This corrected development package preserves the original Phase 7, partial-acceptance,
selective pilot and independent audit artifacts. It includes 192 corrected selective runs,
a separate 64-run primary resumption comparison, authoritative revalidation of 768
historical final plans, and executed Release/ASan/UBSan tests. It does not claim a hard
real-time guarantee, global greedy optimality, physical robot safety or publication novelty.

- [Technical report](docs/selective_recovery_technical_report.md)
- [Generated analysis](docs/selective_recovery_analysis.md)
- [Presentation outline](docs/selective_recovery_presentation_outline.md)
- [Frozen protocol](docs/corrected_experiment_protocol.md)
- [Assertion-level test matrix](docs/requirements_to_tests_matrix.md)
- [Repair log](docs/repair_log.md) and [final audit](docs/final_completion_audit.md)
- [Machine-readable final analysis](results/repair_20261101_corrected_01/final_analysis.json)
- Corrected raw data/manifests: `results/2026-11-02_corrected_selective_pilot_03/` and
  `results/2026-11-02_corrected_primary_pilot_03/`.
- Historical evidence: `results/phase7/`, `results/partial_acceptance_pilot/`,
  `results/2026-11-01_selective_recovery/`, `audit_20261101_verification_1602/`.

C++17, CMake, Boost and Python 3 are required. Eigen and the upstream MAPF sources are
vendored. Corrected experiment/analysis scripts use the Python standard library.

## Build, test and reproduce (repository root)

```sh
cmake -S . -B build/corrected-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/corrected-release -j 4
ctest --test-dir build/corrected-release --output-on-failure
cmake -S . -B build/corrected-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build/corrected-asan -j 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/corrected-asan --output-on-failure
RBL_VALIDATOR=build/corrected-release/rbl_validate python3 tests/test_safe_validation.py
python3 tests/test_corrected_experiments.py
python3 scripts/run_selective_recovery_pilot.py --config configs/2026-11-02_corrected/pilot.json --build-dir build/corrected-release --output-dir results/reproduction_selective_01
python3 scripts/analyze_selective_recovery.py --input results/reproduction_selective_01/raw --output-dir results/reproduction_selective_analysis_01
python3 scripts/run_selective_recovery_pilot.py --primary --config configs/2026-11-02_corrected/primary.json --build-dir build/corrected-release --output-dir results/reproduction_primary_01
python3 scripts/analyze_selective_recovery.py --primary --input results/reproduction_primary_01/raw --output-dir results/reproduction_primary_analysis_01
(cd results/reproduction_selective_01 && shasum -a 256 -c manifest.sha256)
(cd results/reproduction_primary_01 && shasum -a 256 -c manifest.sha256)
python3 docs/run_offline_verification.py --output-dir results/reproduction_offline_01
python3 docs/render_corrected_package.py --verify
python3 docs/verify_corrected_package.py
```
Use a new output name for each reproduction; existing directories are deliberately rejected.
Leak checking is disabled here for the executed macOS arm64 sanitizer configuration; that is
not a blanket recommendation for supported Linux leak-sanitizer environments. The CMake
option instruments engine, vendored compilation and test executables; compiler warnings
are retained. No runtime sanitizer diagnostics were observed in the final executed tests.


The package verifier above checks the stored corrected runs and their original build identity;
a reproduction build may differ in binary hash despite matching source. It also verifies
the originally supplied audit evidence, now tracked at its original paths.

The offline historical viewer remains available through `python3 viewer/server.py --port 8080`.
Its replays are historical saved trajectories, not live execution of the corrected solver.
Scheduling was not registered by this repair session; see repair_log.md for continuation notes.


## Navigate and run a small example

Start with [REPOSITORY_MAP.md](docs/REPOSITORY_MAP.md). It identifies the authoritative
code, reports, fixtures and final evidence without moving historical experiment files.
After building Release, run this two-agent fixture (output stays outside the repository):

```sh
build/corrected-release/rbl_profile_partial_acceptance --map tests/fixtures/recovery_cross.map --scen tests/fixtures/recovery_cross.scen --agents 2 --neighborhood-size 2 --policy RESUMPTION_NO_PA --budget 0.1 --slice 0.005 --output /tmp/routebudget-example.json --dump-final-paths /tmp/routebudget-example.paths
build/corrected-release/rbl_validate --map tests/fixtures/recovery_cross.map --scen tests/fixtures/recovery_cross.scen --agents 2 --paths /tmp/routebudget-example.paths
```

Independent audit reports, scripts, validation outputs and checksum dependencies stay
at their original paths. Only six bulky audit build trees were moved to a verified local
external backup; [LOCAL_ARCHIVE.md](docs/LOCAL_ARCHIVE.md) gives its location and exact
restoration commands. Current corrected build directories remain available. Historical
manifests and scientific findings were not rewritten for this cleanup.

The development layouts are few and correlated, greedy selection is not globally optimal,
natural recovery opportunities are scarce, and cooperative deadlines can overrun. Untouched
confirmation testing remains future work. Retaining negative results is intentional.
