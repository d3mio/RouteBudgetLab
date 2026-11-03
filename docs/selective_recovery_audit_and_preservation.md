> Historical document retained for context. Current implementation, deadline-quality
> results and corrected claims are in [the corrected report](selective_recovery_technical_report.md).
> Historical saved-plan validation does not establish current publication-time behavior.

# RouteBudget Lab — Selective Recovery Audit & Preservation Document

**Date:** 2026-11-01  
**Working Title:** Constraint-Validated Resumption and Selective Recovery of Interrupted Repairs for Deadline-Aware Multi-Agent Path Finding  
**Target Branch:** `research/selective-recovery`  
**Base Commit (Historical Baseline):** `b4d9f9ef198cb2af1a14f484b0dab1099f1f63aa` (Clean audited baseline with independent oracle & 192-run pilot)  
**Pre-Audit Implementation Commit:** `07973d0434771485c2b04f114c004dff4adfdc85` (Preserved selective recovery implementation, 15-test suite, and 192-run development pilot)  
**Audited & Completed Commit:** Current HEAD (22-test suite, comprehensive analysis, correction ledger, and documentation package)  

---

## 1. System & Environment Audit

- **Operating System:** macOS (Darwin 25.3.0, arm64, Apple Silicon)
- **Compiler:** Apple Clang (supporting C++17)
- **Build Tool:** CMake 3.31.6 / 4.4.2, Make
- **Libraries & Dependencies:**
  - Boost 1.92.0 (`/opt/homebrew/opt/boost`)
  - Eigen 3.4.0 (MPL2 header-only vendored in `third_party/anytime-mapf/inc/Eigen/`)
- **Submodules & Third-Party Code:**
  - `third_party/anytime-mapf/` (verified against `47bb25e9ba58b22172e1531cb26f25af8a4531e9` via `scripts/verify_upstream.sh`)

---

## 2. Checksummed Preservation Manifest of Historical Artifacts

All existing historical results remain strictly untouched and preserved on disk:

| Artifact Path | Description | SHA-256 Checksum |
|---|---|---|
| `results/phase7_manifest.json` | 768-run reference evaluation manifest | `5b3c2d95f96f86441dd7e6e4e71dbb7c7731fae0b16e2136f8df3857efb431ca` |
| `results/phase7_summary.json` | 768-run statistical summary | `5c12027986e92a32592bc157210b749622fd5b3cfc95317bdf120fb5bdcb3187` |
| `results/partial_acceptance_offline/offline_opportunity_summary.json` | 24-cell offline opportunity summary | `65260a65905f328c93e63904af441d274624976be804f7ca86c8d0bd9d4e6b07` |
| `results/partial_acceptance_pilot/all_runs.json` | 192-run pilot full results | `cf13719e70d4f42b9cbb4497033f81426360031804883c84012d3b29242549e9` |
| `results/partial_acceptance_pilot/statistical_analysis.json` | 192-run pilot statistical report | `29a13d152ca5ce5ae93b6c63469f7613565ae03b8550f07a9e1f46613135d165` |
| `results/2026-11-01_selective_recovery/all_runs.json` | 192-run selective recovery pilot dataset | `0d774179a39ad2c327d2b931d1488ff2ae85c0295b59f6ef5d938bcaeb9ad4c1` |
| `results/2026-11-01_selective_recovery/manifest.sha256` | SHA-256 manifest of all 192 paths and raw JSONs | `4a9d65729f3bea13279b2a0454731b691dfcb01ba85a4422125068478fa61565` |
| `results/2026-11-01_selective_recovery/selective_recovery_analysis.json` | Machine-readable paired analysis and timing audit | `977314c2ec2fb21710ed6f1ce9553d39efaf3d73dad5506eb844a75420196347` |

---

## 3. Discrepancy Reconciliation & Exact Statistical Audit

### 3.1 Offline Snapshot Count (43,163 vs 43,166)
- **Verified Fact:** The 24 raw collection files in `results/partial_acceptance_offline/raw/*.json` contain **43,166** valid interrupted repair snapshots with $r_{\text{done}} \ge 1$ (215,673 total candidate paths).
  - `random-64-64-10`: 12 cells, 9,024 snapshots
  - `warehouse-10-20-10-2-2`: 12 cells, 34,142 snapshots
  - $9,024 + 34,142 = 43,166$ snapshots.
- **Source of Discrepancy:** An early draft text mentioned `43,163` due to an off-by-3 counting error during an uncompleted intermediate collection run. The authoritative raw evidence confirms `43,166`.

### 3.2 Candidate Weight & Selection Breakdown
- Total candidate paths: `215,673` across 43,166 snapshots
- Weight counts:
  - Positive ($w_i > 0$): `912` (0.42%)
  - Zero ($w_i = 0$): `212,005` (98.30%)
  - Negative ($w_i < 0$): `2,756` (1.28%)
- Feasible negative candidates: `2,645`
- Snapshots containing **any** negative candidate ($w_i < 0$): **2,673** snapshots
- Snapshots with positive exact closure gain ($\Delta > 0$): `170` (0.39%)
- **Distinct Snapshots Containing $\ge 1$ Selected Negative Candidate in Optimal Closure:** **8**
  - In each of these 8 snapshots, exactly 1 negative candidate was selected because its positive dependents outweighed its negative penalty, unlocking $+155$ cumulative SoC gain over nonnegative backward exclusion.
  - Greedy transitive-closure evaluation matched exact min-cut on all 8 snapshots.

### 3.3 Paired Statistical P-Value Reporting
- Never report nonzero p-values as `0.0`:
  - `EXACT_PARTIAL_ACCEPTANCE` vs `DISCARD_NO_PA`: $N = 48$ pairs, 33 wins, 14 ties, 1 loss. Exact two-sided binomial sign test: $p = 4.07 \times 10^{-9}$ (reported as $p < 10^{-6}$).
  - `RESUMPTION_NO_PA` vs `DISCARD_NO_PA`: $N = 48$ pairs, 33 wins, 14 ties, 1 loss. Exact sign test: $p = 4.07 \times 10^{-9}$ ($p < 10^{-6}$).
  - `EXACT_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA`: $N = 48$ pairs, 0 wins, 42 ties, 6 losses. Exact sign test: $p = 0.03125$ ($2 \times 0.5^6 \approx 0.03125$), Wilcoxon signed-rank $p = 0.035522$.
  - `EXACT_PARTIAL_ACCEPTANCE` vs `GREEDY_PARTIAL_ACCEPTANCE`: $N = 48$ pairs, 1 win, 46 ties, 1 loss. Exact sign test: $p = 1.0000$.

---

## 4. Problem Formulation for Selective Recovery

### 4.1 Root Cause of Previous Negative Finding
In the 192-run pilot:
- `EXACT_PARTIAL_ACCEPTANCE` ran on **every single interruption** (`203,260` online evaluations).
- Median evaluation time was $62.19\,\mu\text{s}$, consuming **10.49%** of the entire planning computation time.
- Because positive opportunities occur in only $0.39\%$ of interruptions, $99.61\%$ of these evaluations produced zero gain while stealing planning time from low-level search, causing a slight net loss against `RESUMPTION_NO_PA` ($0\text{W} / 42\text{T} / 6\text{L}$, mean $\Delta\text{SoC} = -11.75$).

### 4.2 Proposed Solution: Selective Recovery & Cheap Gain Filter
1. **Cheap Optimistic Filter (Section 5):**
   $$U = \sum_{i \in R} \max(w_i, 0)$$
   where $w_i = \text{cost}_{\text{incumbent}}(i) - \text{cost}_{\text{replacement}}(i)$.
   If $U == 0$, immediately abort before constructing dependency graphs or running min-cut!
2. **Selective Triggering (Sections 7, 8, 9):**
   Instead of running on every slice interruption, reserve recovery for:
   - **Natural Eviction:** When a suspended checkpoint is about to be evicted from the store to make room for a new one.
   - **Terminal Reserve:** During a small measured reserve (e.g. 1.0 ms) immediately before the global planning deadline expires.
   - **Ordinary Interruptions:** Follow normal resumption and leave checkpoints suspended without running recovery.
3. **Five Comparative Policies:**
   1. `RESUMPTION_NO_PA` (Baseline: normal resumption, no recovery)
   2. `PA_EVERY_INTERRUPTION_FILTERED` (Evaluates every interruption, but skips if $U == 0$)
   3. `PA_EVICTION_ONLY` (Evaluates only on natural checkpoint eviction)
   4. `PA_TERMINAL_ONLY` (Evaluates only during terminal deadline reserve)
   5. `PA_EVICTION_AND_TERMINAL` (Evaluates on both eviction and terminal reserve)
