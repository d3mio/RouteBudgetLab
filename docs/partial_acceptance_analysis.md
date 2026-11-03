# RouteBudget Lab — Partial-Acceptance Empirical & Statistical Analysis

- **Experiment ID:** `partial_acceptance_pilot` (v1.1.0)
- **Git Revision:** `917bcb725a9432b125dbf73546d41f1fbd5b50ba` on branch `research/partial-acceptance`
- **Config Fingerprint (SHA-256):** `4fe57ca11be3606519c20730f02820aa76e9d8d1e1c47955457793405c6394f9`
- **Run Reconciliation:** Expected = `192`, Completed = `192`, Validated (Internal + CLI `rbl_validate`) = `192`, Failed = `0`

## 1. Offline Opportunity Analysis (24 Validation Cells, $1.0\text{ s}$ Slice-5ms Collection)

Natural interrupted repair snapshots were captured under `RESUMPTION_NO_PA` (with partial acceptance disabled so the search trajectory is unperturbed) and replayed offline through all five selectors:

| Metric | Total Across 24 Cells | `random-64-64-10` (12 cells) | `warehouse-10-20-10-2-2` (12 cells) |
|---|---:|---:|---:|
| Total Repair Attempts | 119076 | 64100 | 54976 |
| Interrupted Slices Observed | 43607 | 9030 | 34577 |
| Snapshots with Completed Prefix ($r_{\text{done}} \ge 1$) | 43166 | 9024 | 34142 |
| Total Candidate Paths Evaluated | 215673 | 55958 | 159715 |
| Positive-Weight Candidates ($w_i > 0$) | 912 | 295 | 617 |
| Zero-Weight Candidates ($w_i = 0$) | 212005 | 54210 | 157795 |
| Negative-Weight Candidates ($w_i < 0$) | 2756 | 1453 | 1303 |
| Feasible Negative-Weight Candidates | 2645 | 1385 | 1260 |
| Forbidden Candidates (Outside-Neighborhood Conflict) | 8077 | 2016 | 6061 |
| Transitively Excluded Candidates | 372 | 162 | 210 |
| Dependency Arcs Constructed | 9135 | 4023 | 5112 |
| Snapshots with Positive Feasible Gain ($\Delta > 0$) | **170** | **36** | **134** |
| Cumulative Immediate Gain — `No-PA` | 0 | 0 | 0 |
| Cumulative Immediate Gain — `Greedy` | 3083 | 336 | 2747 |
| Cumulative Immediate Gain — `Exact Closure` | 3083 | 336 | 2747 |
| Cumulative Immediate Gain — `Nonnegative Ref` | 2928 | 308 | 2620 |
| Cumulative Immediate Gain — `Exhaustive 2^r Oracle` | 3083 | 336 | 2747 |
| Snapshots where `Exact > Greedy` | 0 | 0 | 0 |
| Negative Candidates Selected by `Exact` | 8 | 2 | 6 |
| `Exact` vs `Exhaustive Oracle` Mismatches | **0** | **0** | **0** |

### 1.1 Breakdown by Completed Prefix Length ($r_{\text{done}}$)

| $r_{\text{done}}$ | Snapshots | Pos Weights | Zero Weights | Neg Weights | Forbidden | Trans. Excluded | Dep. Arcs | Pos Gain Snaps | Exact Gain | Greedy Gain | Nonneg Ref Gain | Oracle Gain |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 783 | 1 | 782 | 0 | 71 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 | 2078 | 33 | 4113 | 10 | 407 | 7 | 40 | 5 | 268 | 268 | 268 | 268 |
| 3 | 5512 | 74 | 16365 | 97 | 1179 | 20 | 344 | 6 | 119 | 119 | 105 | 119 |
| 4 | 8291 | 143 | 32795 | 226 | 1692 | 51 | 823 | 21 | 197 | 197 | 197 | 197 |
| 5 | 8169 | 142 | 40273 | 430 | 1538 | 67 | 1366 | 28 | 454 | 454 | 389 | 454 |
| 6 | 8142 | 204 | 47905 | 743 | 1601 | 89 | 2303 | 52 | 1016 | 1016 | 985 | 1016 |
| 7 | 10191 | 315 | 69772 | 1250 | 1589 | 138 | 4259 | 58 | 1029 | 1029 | 984 | 1029 |

## 2. Matched 192-Run Live Pilot Summary

| Policy | Runs (Valid) | Mean Init SoC | Mean SoC @ Deadline | Mean Final SoC | Mean Red. @ Deadline | Median Red. (IQR) | Mean Attempts | PA Commits (Gain) | PA Time % | Median PA Latency ($\mu$s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `DISCARD_NO_PA` | 48 (48) | 5482.67 | 5420.58 | 5420.58 | **62.08** | 30.5 (0.0–124.8) | 12490.2 | 0 (0) | 0.00% | 0.00 |
| `RESUMPTION_NO_PA` | 48 (48) | 5482.67 | 5227.46 | 5227.46 | **255.21** | 132.0 (66.5–326.0) | 14239.5 | 0 (0) | 0.00% | 0.00 |
| `GREEDY_PARTIAL_ACCEPTANCE` | 48 (48) | 5482.67 | 5240.94 | 5240.94 | **241.73** | 132.0 (66.5–316.8) | 12462.4 | 247 (4613) | 10.55% | 61.90 |
| `EXACT_PARTIAL_ACCEPTANCE` | 48 (48) | 5482.67 | 5239.21 | 5239.21 | **243.46** | 132.0 (66.5–316.8) | 12439.6 | 246 (4591) | 10.49% | 62.19 |

## 3. Paired Statistical Comparisons ($N = 48$ Matched Instance-Seed-Budget Pairs)

Positive $\Delta\text{SoC} = \text{SoC}(B) - \text{SoC}(A)$ indicates Policy $A$ achieved lower (better) Sum of Costs at the deadline than Policy $B$.

| Comparison ($A$ vs $B$) | Pairs | Wins / Ties / Losses | Mean $\Delta\text{SoC}$ [95% Boot CI] | Median $\Delta\text{SoC}$ (IQR) | Sign Test $p$ | Wilcoxon $p$ ($n_{\neq 0}$) |
|---|---:|---:|---:|---:|---:|---:|
| `EXACT_PARTIAL_ACCEPTANCE` vs `DISCARD_NO_PA` | 48 | 33 / 14 / 1 | **+181.38** [+108.62, +264.81] | +24.5 (+0.0 to +199.2) | 0.0000 | 0.0000 (34) |
| `EXACT_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 48 | 0 / 42 / 6 | **-11.75** [-29.88, -0.81] | +0.0 (+0.0 to +0.0) | 0.0312 | 0.0355 (6) |
| `EXACT_PARTIAL_ACCEPTANCE` vs `GREEDY_PARTIAL_ACCEPTANCE` | 48 | 1 / 46 / 1 | **+1.73** [-1.38, +6.56] | +0.0 (+0.0 to +0.0) | 1.0000 | 1.0000 (2) |
| `GREEDY_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 48 | 0 / 42 / 6 | **-13.48** [-35.52, -0.35] | +0.0 (+0.0 to +0.0) | 0.0312 | 0.0355 (6) |
| `RESUMPTION_NO_PA` vs `DISCARD_NO_PA` | 48 | 33 / 14 / 1 | **+193.12** [+113.66, +285.19] | +24.5 (+0.0 to +199.2) | 0.0000 | 0.0000 (34) |

### 3.1 Stratified Paired Comparisons by Map ($N = 24$ Pairs per Map)

| Map | Comparison ($A$ vs $B$) | Wins / Ties / Losses | Mean $\Delta\text{SoC}$ [95% Boot CI] | Median $\Delta\text{SoC}$ | Sign Test $p$ | Wilcoxon $p$ |
|---|---|---:|---:|---:|---:|---:|
| `random-64-64-10` | `EXACT_PARTIAL_ACCEPTANCE` vs `DISCARD_NO_PA` | 9 / 14 / 1 | **+4.50** [+1.42, +8.17] | +0.0 | 0.0215 | 0.0118 |
| `random-64-64-10` | `EXACT_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 0 / 23 / 1 | **-0.04** [-0.12, +0.00] | +0.0 | 1.0000 | 1.0000 |
| `random-64-64-10` | `EXACT_PARTIAL_ACCEPTANCE` vs `GREEDY_PARTIAL_ACCEPTANCE` | 0 / 24 / 0 | **+0.00** [+0.00, +0.00] | +0.0 | 1.0000 | 1.0000 |
| `random-64-64-10` | `GREEDY_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 0 / 23 / 1 | **-0.04** [-0.12, +0.00] | +0.0 | 1.0000 | 1.0000 |
| `warehouse-10-20-10-2-2` | `EXACT_PARTIAL_ACCEPTANCE` vs `DISCARD_NO_PA` | 24 / 0 / 0 | **+358.25** [+242.46, +486.79] | +240.5 | 0.0000 | 0.0000 |
| `warehouse-10-20-10-2-2` | `EXACT_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 0 / 19 / 5 | **-23.46** [-59.75, -3.00] | +0.0 | 0.0625 | 0.0591 |
| `warehouse-10-20-10-2-2` | `EXACT_PARTIAL_ACCEPTANCE` vs `GREEDY_PARTIAL_ACCEPTANCE` | 1 / 22 / 1 | **+3.46** [-2.75, +13.12] | +0.0 | 1.0000 | 1.0000 |
| `warehouse-10-20-10-2-2` | `GREEDY_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 0 / 19 / 5 | **-26.92** [-71.54, -1.17] | +0.0 | 0.0625 | 0.0591 |

### 3.2 Stratified Paired Comparisons by Budget ($N = 24$ Pairs per Budget)

| Budget (s) | Comparison ($A$ vs $B$) | Wins / Ties / Losses | Mean $\Delta\text{SoC}$ [95% Boot CI] | Median $\Delta\text{SoC}$ | Sign Test $p$ | Wilcoxon $p$ |
|---|---:|---:|---:|---:|---:|---:|
| `1.0s` | `EXACT_PARTIAL_ACCEPTANCE` vs `DISCARD_NO_PA` | 17 / 6 / 1 | **+178.25** [+80.70, +290.79] | +39.0 | 0.0001 | 0.0003 |
| `1.0s` | `EXACT_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 0 / 19 / 5 | **-20.50** [-55.29, -0.12] | +0.0 | 0.0625 | 0.0579 |
| `1.0s` | `EXACT_PARTIAL_ACCEPTANCE` vs `GREEDY_PARTIAL_ACCEPTANCE` | 1 / 22 / 1 | **+3.46** [-2.75, +13.12] | +0.0 | 1.0000 | 1.0000 |
| `5.0s` | `EXACT_PARTIAL_ACCEPTANCE` vs `DISCARD_NO_PA` | 16 / 8 / 0 | **+184.50** [+79.46, +308.83] | +24.5 | 0.0000 | 0.0005 |
| `5.0s` | `EXACT_PARTIAL_ACCEPTANCE` vs `RESUMPTION_NO_PA` | 0 / 23 / 1 | **-3.00** [-9.00, +0.00] | +0.0 | 1.0000 | 1.0000 |
| `5.0s` | `EXACT_PARTIAL_ACCEPTANCE` vs `GREEDY_PARTIAL_ACCEPTANCE` | 0 / 24 / 0 | **+0.00** [+0.00, +0.00] | +0.0 | 1.0000 | 1.0000 |

## 4. Online Overhead, Telemetry & Necessity of General Max-Flow

- **Online Candidate Weight Distribution (`EXACT_PARTIAL_ACCEPTANCE` across 48 runs):**
  - Total PA evaluations: `203260`, total candidate paths: `1018672`
  - Weight signs: `3316` positive ($w_i > 0$), `1001902` zero ($w_i = 0$), `13454` negative ($w_i < 0$, of which `12839` were non-forbidden/feasible).
  - Nonnegative fast-path eligibility (`feasible_negative_weights == 0`): `190849` / `203260` attempts (93.89%).
  - Selected replacement weights in committed closures: `246` positive, `187` zero, `16` negative.
- **Latency & Deadline Accounting (`EXACT_PARTIAL_ACCEPTANCE`):**
  - Median graph build time: `55.77 us`, median Dinic selection time: `0.875 us`, median whole-plan validation time (on commit): `44.77 us`, median total PA overhead per attempt: `62.19 us` (p95: `159.97 us`, max: `26529.42 us`).
  - `cost_at_deadline != final_cost` occurrences: `0` / `48` runs.

