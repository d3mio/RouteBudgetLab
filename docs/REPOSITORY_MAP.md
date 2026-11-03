# RouteBudget Lab repository map

Resumption is the default; selective recovery is an optional evaluated extension.
No scientific results changed during local housekeeping.

| Area | Authoritative entry points |
|---|---|
| Implementation | `src/engine/rbl_resumable_engine.cpp`, `rbl_cost.hpp`, `rbl_partial_acceptance.cpp`, `rbl_search_budget.hpp` |
| Independent validator | `src/validator/`, `scripts/safe_validator.py` |
| Build/tests | `CMakeLists.txt`, `tests/`, README build commands |
| Frozen experimental protocol | `docs/corrected_experiment_protocol.md`, `configs/2026-11-02_corrected/` |
| Runner/statistics | `scripts/run_selective_recovery_pilot.py`, `scripts/analyze_selective_recovery.py` |
| Final report/presentation | `docs/selective_recovery_technical_report.md`, `docs/selective_recovery_presentation_outline.md` |
| Audit and correction history | `docs/final_completion_audit.md`, `docs/repair_log.md`, `docs/correction_ledger.md` |
| Final machine-readable analysis | `results/repair_20261101_corrected_01/final_analysis.json` |
| Final raw experiments | `results/2026-11-02_corrected_selective_pilot_03/`, `results/2026-11-02_corrected_primary_pilot_03/` |
| Historical experiments | `results/phase7/`, `results/partial_acceptance_pilot/`, earlier corrected repetitions; retained in place |
| Original independent audit | `audit_20261101_verification_1602/AUDIT_REPORT.md`, supporting reports, scripts, logs, probes and checksums in both audit directories |
| Fixtures/dependencies | `data/`, `tests/fixtures/`, `third_party/` and their attribution/version files |
| Local backup/archived builds | [LOCAL_ARCHIVE.md](LOCAL_ARCHIVE.md) |

Audit build directories were externally archived; original audit reproduction commands
requiring those binaries need the documented restore step first. The final corrected
package verifier and normal build/test commands use retained files/builds and need no restore.

Historical `project_files.sha256` records the documentation/source at commit
`013a2381b78ee6a385fd8666eff0ce08efd363d3`, not later housekeeping edits. Do not update that
historical checksum list to describe current README or Git hygiene. Experiment manifests,
source hashes and raw results remain unchanged. `render_corrected_package.py --verify`
is read-only; its generation mode rewrites current documents with the frozen research template.

Local branches: `main` is fast-forwarded to the cleaned validated project;
`research/selective-recovery-corrected` remains the working research branch.
`research/preemption-aware-ordering` is retained because it contains an independent,
unmerged implementation and experiment; it has not been integrated or revalidated here.
Four merged branches are recoverable through archive tags and the external Git bundle.
Remote branches/settings are untouched and may still appear in GitHub Desktop.
