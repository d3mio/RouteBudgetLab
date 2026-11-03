> Historical document retained for context. Current implementation, deadline-quality
> results and corrected claims are in [the corrected report](selective_recovery_technical_report.md).
> Historical saved-plan validation does not establish current publication-time behavior.

# RouteBudget Lab: Phase 7 Controlled Experimental Evaluation Report

**Date:** October 2026  
**Environment:** Apple M3 (8-core), Darwin 25.3.0 arm64, Command Line Tools Clang 17.0.0 (`-O3 -DNDEBUG -std=c++17`)  
**Evaluation Git Branch:** `test`  
**Phase 7 Status:** **COMPLETED & VERIFIED — FULL 768-RUN REFERENCE DESIGN (100% INDEPENDENTLY VALIDATED)**  
*(All 768 planned conditions across 4 maps, 3 scenarios, 2 agent scales, 4 budgets, and 8 methods are completed sequentially, retained on disk as complete trajectory files, independently validated by `safe_validator.py` with 100% exact cost matches, and cryptographically recorded via SHA-256 hashes).*

---

## 1. Executive Summary & Audit Resolutions

### 1.1 Resolution of Final-Solution Validation & Trajectory Retention
During the Phase 7 experimental audit, three critical validation constraints were resolved:
1. **Retrospective Validation Fallacy:**
   The four sample solutions evaluated in `scripts/validate_sample_final_solutions.py` originated from separate exploratory reruns and **cannot retroactively validate earlier benchmark runs**, even though their reported costs matched. Independent validation evidence must link strictly to an exact run identity, a disk-persisted trajectory file, and its cryptographic SHA-256 hash.
2. **Trajectory Retention Outside Planning Timers:**
   Both solvers have been configured to retain complete final trajectory files to disk outside the declared planning timer (charging zero overhead to the algorithm's solve time):
   - **Upstream BALANCE:** Native `--outputPaths <file>` argument invokes `LNS::writePathsToFile` after `lns.run()` has completed. Outputs Moving AI grid coordinates in `(row, col)` format.
   - **RouteBudget Lab:** `--dump-final-paths <file>` argument invokes `saveMovingAIPathsToFile` after `controller.run()` has completed. Outputs Moving AI grid coordinates in `(col, row)` format.
3. **Safe Independent Validator:**
   `scripts/safe_validator.py` enforces POSIX return code == 0, strict JSON decoding, exact Boolean equality `status == "VALID"` (eliminating the naive `'VALID' in out` substring flaw which falsely passed `'INVALID_PLAN'`), and verifies reported costs against independent arrival-time sums. All 24 initial plan fixtures in `results/phase7/init_paths/` were independently verified as `VALID` (100.0% pass rate).

### 1.2 Honest Run Category Accounting
To prevent conflating process completion or solver flags with independent verification, all benchmark runs are categorized across four distinct states:

| Run Classification Category | Preliminary Sequential Batch (`results/phase7/sequential_reference/`) | Verified Reference Batch (`results/phase7/verified_reference/`) | Archived Concurrent Batch (`results/phase7/concurrent_batch/`) |
|---|---|---|---|
| **1. Process-Completed Runs** | 256 / 256 (100.0%) | **768 / 768 (100.0%)** | 672 / 672 (100.0%) |
| **2. Solver-Reported Valid Runs** | 256 / 256 (100.0%) | **768 / 768 (100.0%)** | 672 / 672 (100.0%) |
| **3. Independently Validated Retained Trajectories** | **0 / 256 (0.0%)** *(trajectories not persisted to disk)* | **768 / 768 (100.0%)** *(SHA-256 hashed & validated)* | **0 / 672 (0.0%)** *(trajectories not persisted to disk)* |
| **4. Unavailable Trajectories (`UNVERIFIED_PATH_NOT_RETAINED`)** | **256 / 256 (100.0%)** | **0 / 768 (0.0%)** | **672 / 672 (100.0%)** |

> [!IMPORTANT]
> Process completion and solver internal flags do not constitute independent verification. The preliminary 256-run batch is strictly reported as having unverified retained trajectories, while verified reference runs retain complete `.paths` files with SHA-256 hashes and independent validator passes.

### 1.3 Full Reference Design Execution (768 Runs)
- **Authorized Reference Design:**
  $4\ \text{maps} \times 3\ \text{scenarios} \times 2\ \text{agent counts} \times 4\ \text{budgets} \times 8\ \text{configurations} = 768\ \text{runs}$.
- **Execution & Scope Reconciliation:**
  The full 768-run reference evaluation was executed sequentially under `scripts/run_phase7_verified_reference.py` across all three scenarios:
  - **Scenario 1:** 256 runs (completed and validated).
  - **Scenario 2:** 256 runs (completed and validated).
  - **Scenario 3:** 256 runs (completed and validated).
  All 768 runs have retained trajectories on disk (`results/phase7/verified_reference/paths/*.paths`), 100% collision-free verification by `safe_validator.py`, exact cost matches, and matching SHA-256 checksums in `verified_reference_manifest.json`.

---

## 2. Clock Semantics & Deadline Accounting Audit

### 2.1 Upstream BALANCE Timing & Clock Monotonicity Verification
A source-level inspection of upstream `anytime-mapf` (`third_party/anytime-mapf`) clarified clock behavior:
- In `inc/common.h` and `src/LNS.cpp`:
  ```cpp
  typedef std::chrono::high_resolution_clock Time;
  typedef std::chrono::duration<float> fsec;
  ...
  start_time = Time::now();
  ...
  while (runtime < time_limit && iteration_stats.size() <= num_of_iterations) {
      runtime = ((fsec)(Time::now() - start_time)).count();
  ```
- **Monotonicity Verification on Target Build:**
  The property `is_steady` was inspected on Apple Clang 17 (macOS arm64 libc++):
  - `std::chrono::steady_clock::is_steady == 1`
  - `std::chrono::high_resolution_clock::is_steady == 1`
  - In libc++, `std::chrono::high_resolution_clock` is a direct typedef alias for `std::chrono::steady_clock`. Both are strictly monotonic.
- Internal references to ANSI C `clock()` in `anytime-mapf` (`SIPP.cpp`, `CBS/PBS.cpp`, `MutexReasoning.cpp`) are purely diagnostic counters for logging component breakdowns (`runtime_build_CT`, `runtime_build_CAT`) and do not govern budget cutoff. Both BALANCE and RBL operate on wall-clock monotonic time.

### 2.2 Correction of Unsupported Deadline Claims
The following premature claims from earlier drafts have been corrected:
1. *Claim that sequential execution eliminates jitter, preemption, or core migration:*
   **Correction:** Sequential execution reduces benchmark-process contention on the CPU, but operating system thread scheduling, background service preemption, and platform power/thermal management remain active.
2. *Claim that RBL guarantees deadline compliance:*
   **Correction:** RBL does not mathematically guarantee deadline compliance. On `room-64-64-8` at 30.0s (100 agents), `SCORE_BASED_ADAPTIVE` experienced an overshoot of **11.8 ms** (30.0118s runtime) due to tail latency in an active repair slice during severe agent crowding.
3. *Claim that a 0.5 ms reserve guarantees timely completion:*
   **Correction:** The prospective safety reserve margin $t_{\text{reserve}} = 0.5$ ms substantially reduces overshoot frequency, but does not provide a hard mathematical guarantee under operating system scheduling. In the preliminary sequential batch, 223 of 224 RBL runs complied with deadlines while one run exhibited an 11.8 ms tail overshoot. In the verified replacement reference batch, all 224 RBL runs complied with deadlines within a $\le 1.0\ \mu$s tolerance (0 overrunning runs $> 1.0\ \mu$s). Sequential execution and prospective reserve margins mitigate process contention, but cannot eliminate OS scheduling preemption.

### 2.3 Protocol Boundary Specifications
| Dimension | Protocol A: Controlled Refinement | Protocol B: End-to-End Planning |
|---|---|---|
| **Intended Comparison** | Internal algorithmic ablation (E2–E5) | Systems-level baseline comparison (E6) |
| **Initial Plan Source** | Precomputed shared fixture (`results/phase7/init_paths/*.paths`) | Generated on-the-fly at runtime |
| **Initial Plan Provenance** | Verified byte-identical via SHA-256 hash | Distinct solver initialization routines |
| **Initialization Charging** | **Excluded** from refinement budget $B$ | **Charged** against total planning budget $B$ |
| **Timer Start Point** | At entry to `ResumeOrStartController::run()` | Before initial solution generation |
| **Timer Stop Point** | When remaining budget $b_k \le 0$ | When total runtime $\ge B$ |
| **In-Run Operations Charged** | Active search, controller scoring, feature extraction, save/restore latency, in-memory validation | Initial plan search + all refinement search, controller work, validation |
| **Excluded Operations** | Map/scenario I/O, process startup, post-run serialization, external validation | Map/scenario I/O, process startup, post-run serialization, external validation |
| **Overshoot Handling** | Prospective reserve margin $t_{\text{reserve}} = 0.5$ ms ($\Delta + \hat{O} + t_{\text{reserve}} \le b_k$) | RBL uses $t_{\text{reserve}} = 0.5$ ms; BALANCE checks retrospectively between iterations |

---

## 3. Protocol Separation and Execution Verification

### 3.1 Evaluated Benchmark Configurations
- **Held-Out Test Maps (4):** `warehouse-20-40-10-2-1`, `random-64-64-20`, `room-64-64-8`, `ost003d`.
- **Scenarios (3):** 1, 2, 3 (25 scenarios available per map; scenarios 1–3 systematically evaluated).
- **Agent Scales (2):** 50, 100 agents.
- **Budgets (4):** 1.0s, 5.0s, 10.0s, 30.0s.
- **Protocol A Configurations (6 configs $\times$ 96 conditions = 576 runs):**
  1. `RESTART_ONLY_K0`: Matched control ($K=0$, fixed 10 ms slice).
  2. `FIXED_FIFO_RESUME`: Matched control ($K=2$, fixed 10 ms slice, FIFO selection).
  3. `SCORE_BASED_FIXED`: Matched control ($K=2$, fixed 10 ms slice, score-based selection).
  4. `SCORE_BASED_ADAPTIVE`: Proposed method ($K=2$, adaptive $\{2, 5, 10, 20, 50\}$ ms slices).
  5. `ELAPSED_TIME_ONLY`: Feature ablation ($K=2$, adaptive slices, progress features masked).
  6. `DISCARD_ON_CHANGE`: Compatibility ablation ($K=2$, adaptive slices, discard-on-change policy).
- **Protocol B Configurations (2 configs $\times$ 96 conditions = 192 runs):**
  7. `RBL_END_TO_END`: Proposed method with initialization charged to budget.
  8. `BASELINE_BALANCE`: Upstream BALANCE C++14 Release binary.
- **Total Reference Runs:** $576 + 192 = 768$ runs.

---

## 4. Empirical Statistical Evaluation

All comparisons are paired across identical problem conditions. Cost differences are reported as:
$$\Delta \text{Cost} = \text{Cost}(\text{Control}) - \text{Cost}(\text{Treatment})$$
A positive value indicates that Treatment achieved a lower (better) solution cost than Control.

### 4.1 Clustered Bootstrap Resampling Protocol
Conditions sharing a map and scenario across multiple budgets are not independent replicates. To respect instance clustering, non-parametric 95% Confidence Intervals are computed via **Clustered Bootstrap** (resampling entire map clusters with replacement, $B = 5,000$ iterations). Both Cluster-by-Map and Cluster-by-(Map, Agents) intervals are computed.

### 4.2 Protocol A: Controlled Refinement Comparisons (E2–E5)
$N = 96$ paired conditions across all 4 held-out maps and 3 scenarios. Initial plans verified identical by SHA-256 fixture hash.

| Exp | Comparison (Control $\to$ Treatment) | Wins | Losses | Ties | Mean $\Delta$ Cost | Median $\Delta$ Cost | Mean % Imp | Cluster-by-Map 95% CI | Qualified Finding |
|---|---|---|---|---|---|---|---|---|---|
| **E2** | `RESTART_ONLY_K0` $\to$ `FIXED_FIFO_RESUME` | 23 | 24 | 49 | **+13.36** | 0.00 | +0.145% | **[-9.30, +52.15]** | Positive sample improvement, concentrated on `ost003d` in the measured sample (CI spans zero; not proven exclusive) |
| **E3a** | `FIXED_FIFO_RESUME` $\to$ `SCORE_BASED_FIXED` | 29 | 21 | 46 | -10.17 | 0.00 | -0.040% | [-39.53, +10.80] | Inconclusive (CI spans zero) |
| **E3b** | `SCORE_BASED_FIXED` $\to$ `SCORE_BASED_ADAPTIVE` | 22 | 30 | 44 | -16.97 | 0.00 | -0.194% | [-40.42, +4.19] | Inconclusive (CI spans zero) |
| **E4** | `DISCARD_ON_CHANGE` $\to$ `SCORE_BASED_ADAPTIVE` | 3 | 7 | 86 | -0.59 | 0.00 | -0.008% | [-0.73, -0.36] | Small negative difference (interval below zero; 86 ties, 3 wins, 7 losses) |
| **E5** | `ELAPSED_TIME_ONLY` $\to$ `SCORE_BASED_ADAPTIVE` | 7 | 13 | 76 | -2.49 | 0.00 | -0.029% | [-6.48, +0.14] | Inconclusive (CI spans zero) |

### 4.3 Protocol B: End-to-End Baseline Comparison (E6)
Matched comparison: $N = 96$ paired conditions. Both systems generate initial solutions charged to budget.

| Exp | Comparison (Control $\to$ Treatment) | Wins | Losses | Ties | Mean $\Delta$ Cost | Median $\Delta$ Cost | Mean % Imp | Cluster-by-Map 95% CI | Qualified Finding |
|---|---|---|---|---|---|---|---|---|---|
| **E6** | `BASELINE_BALANCE` $\to$ `RBL_END_TO_END` | 13 | 67 | 16 | **-232.65** | -71.50 | -2.108% | **[-434.78, -72.16]** | Baseline achieves lower cost |

> [!NOTE]
> A confidence interval spanning zero indicates that the observed sample difference is not statistically distinguishable from zero under clustered resampling; it does **not** prove equivalence between methods.

---

## 5. Topology Breakdown & Sensitivity Analysis

### 5.1 Paired Cost Reduction by Map Topology ($\Delta \text{Cost}$, $n=24$ per map)
| Comparison | `warehouse` ($n=24$) | `random` ($n=24$) | `room` ($n=24$) | `ost003d` ($n=24$) | Overall ($N=96$) |
|---|---|---|---|---|---|
| **E2 (Resumption Effect)** | -12.50 | +0.29 | -5.79 | **+71.46** | +13.36 |
| **E3a (Controller Scoring, 10ms)** | +16.67 | +3.25 | -6.79 | -53.79 | -10.17 |
| **E3b (Adaptive Slicing)** | -23.67 | -3.92 | +12.29 | -52.58 | -16.97 |
| **E4 (Selective Compatibility)** | -0.67 | -0.75 | -0.25 | -0.71 | -0.59 |
| **E5 (Progress Features)** | -0.92 | -1.33 | +0.62 | -8.33 | -2.49 |
| **E6 (End-to-End Comparison)** | -241.12 | -15.83 | -140.88 | -532.75 | -232.65 |

### 5.2 Leave-One-Map-Out (LOMO) Sensitivity Analysis for E2
To evaluate how strongly the resumption advantage depends on individual map topologies across the full $N=96$ design, a Leave-One-Map-Out (LOMO) analysis was conducted:

| Omitted Map | Remaining Conditions | Mean $\Delta \text{Cost}$ Without Omitted Map | Resumption Status |
|---|---|---|---|
| *None (Full Evaluation Sample)* | $N = 96$ | **+13.36** | Positive sample improvement |
| Omitting `warehouse-20-40-10-2-1` | $N = 72$ | +21.99 | Positive sample improvement |
| Omitting `random-64-64-20` | $N = 72$ | +17.72 | Positive sample improvement |
| Omitting `room-64-64-8` | $N = 72$ | +19.75 | Positive sample improvement |
| **Omitting `ost003d`** | $N = 72$ | **-6.00** | **Net negative (Restart outperforms Resumption)** |

**Critical Finding:**
Within the measured 96-condition sample, the average FIFO resumption benefit (+13.36 points) was concentrated on the organic bottleneck topology `ost003d` (+71.46 points); it is not proven to occur exclusively on that topology. On open grids (`random-64-64-20`), resumption and restart performed similarly (+0.29 points). On regular corridor/room topologies (`warehouse` and `room`), resumption incurred average losses in the sample (-12.50 and -5.79 points), for which repeated node expansion overhead upon restarting unfinished search is a possible explanation. Omitting `ost003d` in LOMO sensitivity reverses the sign of the overall sample mean to **-6.00 points**. Findings on `ost003d` cannot be generalized to all complex terrain.

---

## 6. Telemetry, Overhead, and Matched Deadline Accounting

### 6.1 Telemetry Profiles & Lifecycle Dynamics
- **Decision Counts:** `SCORE_BASED_ADAPTIVE` performs significantly more discrete scheduling decisions than fixed-slice modes because 2 ms slices are frequently scheduled during tight remaining-budget windows.
- **Repeated Work Overhead:** Discarding the unfinished search frontier upon suspension leads to a measured repeated work ratio of 78%–83% upon restarting the unfinished agent. Repeated work overhead is a possible explanation for resumption underperforming restart on open and regular grids, but this causal attribution has not been experimentally isolated from other instance dynamics.
- **Stagnation Evictions:** In low-yield repair regimes, unadvanced checkpoints reach the 2-cycle stagnation threshold and are cleanly evicted, bounding wasted execution without memory leaks.

### 6.2 Mechanistic Telemetry Explanation for E4 Results
In Experiment E4, comparing `SCORE_BASED_ADAPTIVE` against `DISCARD_ON_CHANGE` yielded a small negative cost difference (mean $\Delta \text{Cost} = -0.59$, 95% CI `[-0.73, -0.36]`, with 86 ties, 3 wins, and 7 losses). Rather than indicating zero quality difference, the bootstrap confidence interval lies strictly below zero. Telemetry inspection confirmed:
- Internal decision counts and checkpoint retention differed between policies as expected.
- **Why final costs were predominantly tied with a slight negative skew:** MAPF-LNS repair acceptance rates plateau early. Across the evaluated benchmark instances, only a tiny fraction of resumed actions discovered strictly improving global incumbents over fresh repairs, while retained checkpoints frequently aged to stagnation and were evicted, occasionally incurring slight search divergence relative to immediate discard.

### 6.3 Matched Protocol B Deadline Compliance Comparison
Protocol B deadline performance is compared strictly between matched runs ($N = 96$ runs of `RBL_END_TO_END` vs $N = 96$ runs of `BASELINE_BALANCE`). Compliance is evaluated using the declared empirical tolerance of $\le 1.0\ \mu$s, which does not establish a hard mathematical or real-time OS scheduling guarantee under general operating system preemption.

| Measurement Dimension | `BASELINE_BALANCE` (Protocol B) | `RBL_END_TO_END` (Protocol B) |
|---|---|---|
| **Measured Runs** | 96 | 96 |
| **Overrunning Runs (> 1.0 $\mu$s)** | **76 of 96 (79.2%)** | **0 of 96 (0.0%)** |
| **Compliance Rate ($\le 1.0\ \mu$s Tolerance)** | **20.8%** (20 of 96) | **100.0%** (96 of 96) |
| **Mean Overshoot (All Runs)** | **224.79 $\mu$s** | **0.00 $\mu$s** |
| **Mean Overshoot (Overrunning Runs Only)** | **283.95 $\mu$s** | **0.00 $\mu$s** |
| **Maximum Overshoot** | **5,650.00 $\mu$s (5.65 ms)** | **0.00 $\mu$s (0.00 ms)** |
| **Quantile P50 (Median)** | 100.00 $\mu$s | 0.00 $\mu$s |
| **Quantile P90** | 400.00 $\mu$s | 0.00 $\mu$s |
| **Quantile P95** | 605.00 $\mu$s | 0.00 $\mu$s |
| **Quantile P99** | 2,277.50 $\mu$s | 0.00 $\mu$s |

**Trade-off Visibility & Hypothesis Testing:**
Published BALANCE achieved lower final solution costs (mean advantage of 232.65 points) across the 96 conditions. Under **Hypothesis 1 (Uninstrumented Search Depth Advantage / Baseline Overhead)**, it is hypothesized that BALANCE's advantage arises because its monolithic LNS loop avoids interruption checkpoints, feature extraction, and safety reserve margins. Repeated work and baseline overhead are possible explanations unless isolated experimentally. However, BALANCE overran the declared deadline on 79.2% of runs (76 of 96, with tail overshoots up to 5.65 ms). In contrast, RBL complied with the declared $\le 1.0\ \mu$s tolerance on all 96 Protocol B conditions in the sample (0 runs overrunning $> 1.0\ \mu$s). This empirical compliance is subject to the declared tolerance threshold and does not constitute a hard real-time scheduling guarantee under non-real-time operating systems.

---

## 7. Answers to Approved Research Questions (RQ1–RQ3)

The project research questions are evaluated against their original approved wording from `docs/source_text/report.txt`:

### RQ1 (Checkpoint Feasibility & Equivalence)
> *"Can a low-level MAPF-LNS repair search be suspended at an arbitrary cutoff and resumed under unchanged external constraints to produce valid, collision-free solutions consistent with uninterrupted execution, and what memory and latency overheads are incurred?"*

- **Status: SUPPORTED (within evaluated scope).**
- Evaluated in E1 (`tests/e1_resumption_tests.cpp`, 10 test categories). Under unchanged external constraints, macro-prefix resumption restores search invariants, restarts the unfinished agent deterministically, and produces valid, collision-free paths with cell-by-cell equivalence to uninterrupted execution.
- Measured live integrated overhead: save latency $0.79\ \mu$s (Release) / $3.75\ \mu$s (ASan); restore latency $3.12\ \mu$s (Release) / $11.88\ \mu$s (ASan); combined latency $3.92\ \mu$s (<0.04% of a 10 ms slice). Checkpoint memory footprint: 1,024 bytes (1 KB).

### RQ2 (Constraint Validation Dynamics)
> *"What fraction of saved repair checkpoints remain compatible with evolving external agent reservations across subsequent LNS iterations, and does conservative constraint validation prevent invalid solution commits compared with unvalidated reuse?"*

- **Status: EVALUATED & QUALIFIED.**
- Evaluated in E4 unit test suites (`tests/e4_compatibility_tests.cpp`) and benchmark runs. The two-stage validator checks version counters in Stage 1 ($0.04 - 0.38\ \mu$s) and evaluates vertex, edge swap, stay-at-goal, and neighborhood mutations in Stage 2 ($0.24 - 0.71\ \mu$s), successfully detecting all simulated conflicts with 0 invalid commits.
- In benchmark execution, while selective retention preserved checkpoints across intervening updates, E4 produced a small negative cost difference (mean $\Delta \text{Cost} = -0.59$, 95% CI `[-0.73, -0.36]`, with 86 ties, 3 wins, 7 losses) with the interval lying strictly below zero due to low accepted repair yields from retained checkpoints and stagnation evictions.

### RQ3 (Resume-or-Start Controller Utility)
> *"Does an overhead-aware resume-or-start heuristic controller informed by lightweight search-progress features achieve lower deadline solution cost (Sum of Costs / Total Delay) than restart-only LNS, fixed round-robin resumption, and published adaptive baselines within identical wall-clock deadlines?"*

- **Status: QUALIFIED / UNSUPPORTED against Published Baseline.**
- **Resumption vs Restart (E2):** FIFO resumption achieved a positive average improvement in the evaluated sample (+13.36 points across the 96-condition sample), but this benefit was concentrated on `ost003d` (+71.46 points across $n=24$); it is not proven to occur exclusively on that topology. When `ost003d` is omitted in Leave-One-Map-Out sensitivity, the average improvement becomes negative (-6.00 points). Clustered bootstrap 95% CI spans zero ([-9.30, +52.15]).
- **Controller Scoring & Adaptive Slicing (E3a, E3b, E5):** Inconclusive. Under matched 10 ms slices, score-based selection yielded -10.17 points difference (CI: [-39.53, +10.80]). Variable slice grids ($\{2, 5, 10, 20, 50\}$ ms) frequently selected 2 ms slices, increasing decision churn without discovering lower-cost global paths (-16.97 points). Progress features yielded -2.49 points difference compared to elapsed-time-only scoring (CI: [-6.48, +0.14]).
- **Comparison Against Published BALANCE (E6):** Unsupported on solution quality. Under Protocol B where initialization is charged against the budget, published BALANCE achieved lower solution costs on 67 of 96 conditions (mean difference -232.65 points). Under Hypothesis 1, BALANCE's cost advantage is hypothesized to stem from monolithic search depth without preemption, but this came at the expense of deadline non-compliance on 79.2% of runs (up to 5.65 ms), where RBL maintained 100.0% empirical compliance within the declared $\le 1.0\ \mu$s tolerance (not establishing a hard real-time guarantee).

---

## 8. Remaining Limitations & Tested Hypotheses

1. **Hypothesis 2 (Macro-Prefix Repeated Work Overhead):**
   Because macro-prefix mode discards the unfinished agent's open-list frontier, resuming search incurs repeated node expansions (measured repeated work ratio 78%–83%). Repeated work overhead is a possible explanation for why resumption underperformed restart on non-bottleneck terrain in our sample, but this causal attribution has not been experimentally isolated from other instance dynamics.
2. **Hypothesis 3 (Sparse Improvement & Plateau Regime):**
   In near-optimal initial plans, accepted improvements occur in fewer than 3% of repair iterations. Regression models predicting accepted improvement suffer from severe zero-inflation, rendering feature-based scoring only marginally distinguishable from heuristic elapsed-time baselines.
3. **Hypothesis 1 (Uninstrumented Search Depth Advantage vs Baseline Overhead):**
   The explanation that BALANCE's solution quality advantage is caused by uninstrumented search depth and baseline overhead (monolithic execution avoiding preemption checks and feature extraction) is a plausible hypothesis supported by telemetry, but remains an unisolated possible explanation rather than a proven single-variable causal effect. Statements attributing performance to "zero preemption checks" or "100% time window utilization" are explicitly categorized as hypotheses.

---

## 9. Current Status & Phase 8 Transition

**Current Status:** **Phase 7 Completed & Verified (768/768 Runs Validated); Phase 8 Completed & Verified (Viewer & Replays Operational).**

1. **Full 768-Run Reference Dataset (Verified on Disk):** All 768 reference records, retained trajectory files (`results/phase7/verified_reference/paths/*.paths`), and cryptographic SHA-256 hashes are available on disk. 768 of 768 runs (100.0%) were independently validated as collision-free with exact arrival-time cost matches.
2. **Analysis Refreshed:** Clustered bootstrap confidence intervals, LOMO sensitivity, and Protocol B deadline accounting are fully updated across all 96 problem conditions.
3. **Phase 8 Replay Viewer & Packaging:** All 768 replays exported, cataloged, and verified by automated regression test suite (`tests/test_viewer_and_replays.py`, 5/5 test groups passed). Self-contained packaging script operational.
4. **Research Demarcation:** Implementation, experimental evaluation, automated verification, and reproducible packaging are complete. Dissertation writing, document preparation, and university thesis submission remain independent academic activities outside software development.
