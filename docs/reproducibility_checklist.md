# Reproducibility checklist

Run from repository root. Exact full commands appear in README.md and the technical report.
Use new experiment/analysis output directories; existing outputs are intentionally rejected.

| Item | Evidence / verification |
|---|---|
| Starting state | repair result starting_state.txt; audited commit fb6ba907e4ccba9ec882537c2dce2be970c91781 |
| Historical preservation | repair result preservation.json and final preservation verification |
| Vendored source identity | starting tree IDs and scripts/verify_upstream.sh; no vendor modifications |
| Release | build/corrected-release; test-final-counters.log |
| ASan/UBSan | ENABLE_SANITIZERS=ON; sanitizer_instrumentation.json; test-asan-final-counters.log |
| Assertions active | intentional-harness-failure.json, exit 17 under -DNDEBUG |
| Independent validator | authoritative-cost mismatch rejects; Python/CTest logs and every run's validation JSON |
| Frozen protocol | docs/corrected_experiment_protocol.md; configs/2026-11-02_corrected/ |
| Measured reserve | reserve-random.json, reserve-warehouse.json, reserve_decision.json |
| Execution order | each pilot's execution_order.json, actual cell_index/policy_position in raw files |
| Source/build provenance | each raw run's provenance; source_hashes.json; executable/library/compiler/link records |
| Matched inputs | map/scenario/initial-plan SHA-256 per run; common seeds/budgets/k/K/slice/control cutoff |
| Event reconciliation | runner reconcile(); mutually exclusive outcomes and actual commit timestamps |
| Timing boundaries | provenance.timing_definitions, refinement overshoot, serialization sidecars, parent process elapsed |
| Completed serialization | raw enrichment precedes manifest generation; shasum -a 256 -c manifest.sha256 |
| Strict analysis | analyze_selective_recovery.py requires complete cells, validated costs, known policies, unique pairs |
| Historical corrections | historical_reanalysis/ and primary_history_rescored/, separate from original results |
| Same report/presentation numbers | docs/render_corrected_package.py --verify and final_analysis.json |
| Local-only work | logical local commits; no push, merge, publication, submission or email |

Recorded process elapsed includes initialization/output/teardown; refinement overshoot is
relative to the global refinement budget. These are different measurements. A failed or
incomplete run is not silently treated as valid, and incomplete cells cannot enter analysis.
The pre-existing audit directories and .DS_Store edit remain outside the repair commits.
