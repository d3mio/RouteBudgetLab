# RouteBudget Lab — Selective Recovery Development Pilot Analysis

**Date:** 2026-11-01  
**Design:** 192 Matched Sequential Runs across 32 Cells (2 Maps × 2 Scenarios × 2 Agent Scales × 2 Budgets × 2 Seeds × 6 Variants)  
**Independent Instances:** 16 Clustered Instance Groups (2 Maps × 2 Scenarios × 2 Agent Scales × 2 Budgets)  
**Execution:** Balanced Cyclic Latin-Square Rotation Order with Independent `rbl_validate` Verification (192/192 Valid)  

---

## 1. Full Paired Policy Comparison vs RESUMPTION_NO_PA Baseline

Under Sum of Costs (SoC) minimization, lower cost indicates superior solution quality:  
$$\Delta\text{Cost} = \text{Cost}_{\text{policy}} - \text{Cost}_{\text{resumption}} \quad (\text{negative is better, positive is worse})$$

| Policy Variant | Win / Tie / Loss | Win Rate | Mean ΔCost (All Pairs) | Mean ΔCost (Non-Ties) | Median ΔCost [IQR] | SEM | Sign Test $p$ | Wilcoxon $p$ | Clustered Inst ΔCost (16 Groups) | Mean Iter Diff |
|---|---|---|---|---|---|---|---|---|---|---|
| `PA_EVERY_INTERRUPTION_FILTERED` | **0W / 28T / 4L** | 0.0% | **+13.56** | +108.50 | 0.0 [0.0] | ±9.36 | 0.1250 | 1.0000 | +13.56 ±9.26 | -61.4 |
| `PA_EVICTION_ONLY` | **0W / 32T / 0L** | 0.0% | **+0.00** | N/A (32T) | 0.0 [0.0] | ±0.00 | 1.0000 | 1.0000 | +0.00 ±0.00 | -91.1 |
| `PA_TERMINAL_ONLY` | **0W / 32T / 0L** | 0.0% | **+0.00** | N/A (32T) | 0.0 [0.0] | ±0.00 | 1.0000 | 1.0000 | +0.00 ±0.00 | -112.4 |
| `PA_EVICTION_AND_TERMINAL` | **0W / 32T / 0L** | 0.0% | **+0.00** | N/A (32T) | 0.0 [0.0] | ±0.00 | 1.0000 | 1.0000 | +0.00 ±0.00 | -214.1 |
| `TERMINAL_RESERVE_CONTROL` | **0W / 32T / 0L** | 0.0% | **+0.00** | N/A (32T) | 0.0 [0.0] | ±0.00 | 1.0000 | 1.0000 | +0.00 ±0.00 | +1.3 |

---

## 2. Inexpensive Gain Filter & Timing Audit ($U = \sum \max(w_i, 0)$)

- **Total Online Filter Invocations:** 162,396
- **Filter Skips ($U == 0$):** 160,496 (**98.83%** bypassed before graph construction)
- **Non-Skipped Evaluated ($U > 0$):** 1,900 (**1.17%**)
- **Inner-Loop Filter Evaluation Latency:** **14.3 ns** (0.014 $\mu$s)
- **Full Candidate Extraction & Validation Latency:** **2.70 $\mu$s** per interruption
- **Combined Preparation Latency:** **2.71 $\mu$s**
- **Total Accumulated Extraction Overhead:** 0.438 s across 32 runs
- **Clock Granularity & Interpretation Note:** The 14.3 nanosecond measurement represents exclusively the inner scalar loop of computeOptimisticUpperBound over pre-assembled candidate weights. The full end-to-end extraction and validation pipeline averages 2.70 microseconds per interruption. While graph construction (which historically consumed ~62 us) is bypassed on 98.83% of interruptions, calling extraction across all 162,396 interruptions accumulated 0.44s of CPU time across the 32 runs, displacing ~61 iterations.

---

## 3. Opportunity Funnel Accounting

Across all 192 runs executed under the five policies:
1. **Interrupted Repairs Evaluated:** 162,396
2. **Skipped by Optimistic Bound ($U == 0$):** 160,496 (98.83%)
3. **Passed to Dependency Graph ($U > 0$):** 1,900 (1.17%)
4. **Infeasible or Zero Net Gain Closures:** 1,724 (due to cycles, missing dependencies, or negative-dependency costs)
5. **Feasible Improving Closures Found & Committed:** 176 (100% committed with 0 budget aborts)
6. **Total SoC Improvement Harvested by Filtered PA:** 3,254 SoC
7. **Natural Checkpoint Store Evictions Observed ($K=2$):** **0** (suspended checkpoints are resumed on the very next slice, draining the store before capacity eviction occurs)
8. **Terminal Checkpoints Examined at Reserve ($T - 2.0\text{ ms}$):** **23** (22 skipped with $U == 0$, 1 non-skipped with zero feasible gain; **0 commits**)

---

## 4. Per-Budget Deadline & Overshoot Breakdown

### 1.0-Second Budget Runs ($N=16$ runs per policy)
| Policy Variant | Mean Solve Time (s) | Mean Overshoot ($\mu$s) | 90th %ile ($\mu$s) | 99th %ile ($\mu$s) | Max Overshoot ($\mu$s) | Overrunning Runs (>0 $\mu$s) |
|---|---|---|---|---|---|---|
| `RESUMPTION_NO_PA` | 0.9998 s | 139.5 $\mu$s | 0.0 $\mu$s | 1897.2 $\mu$s | 2232.0 $\mu$s | **1 / 16 (6.2%)** |
| `PA_EVERY_INTERRUPTION_FILTERED` | 0.9996 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `PA_EVICTION_ONLY` | 0.9997 s | 66.0 $\mu$s | 0.0 $\mu$s | 897.6 $\mu$s | 1056.0 $\mu$s | **1 / 16 (6.2%)** |
| `PA_TERMINAL_ONLY` | 0.9981 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `PA_EVICTION_AND_TERMINAL` | 0.9982 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `TERMINAL_RESERVE_CONTROL` | 0.9981 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |

### 5.0-Second Budget Runs ($N=16$ runs per policy)
| Policy Variant | Mean Solve Time (s) | Mean Overshoot ($\mu$s) | 90th %ile ($\mu$s) | 99th %ile ($\mu$s) | Max Overshoot ($\mu$s) | Overrunning Runs (>0 $\mu$s) |
|---|---|---|---|---|---|---|
| `RESUMPTION_NO_PA` | 4.9998 s | 186.6 $\mu$s | 230.5 $\mu$s | 2214.5 $\mu$s | 2524.0 $\mu$s | **2 / 16 (12.5%)** |
| `PA_EVERY_INTERRUPTION_FILTERED` | 4.9996 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `PA_EVICTION_ONLY` | 4.9996 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `PA_TERMINAL_ONLY` | 4.9981 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `PA_EVICTION_AND_TERMINAL` | 4.9981 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |
| `TERMINAL_RESERVE_CONTROL` | 4.9982 s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | 0.0 $\mu$s | **0 / 16 (0.0%)** |

---

## 5. Displaced Search vs Recovered Gain Analysis

| Policy | Mean Displaced Iters | Displaced Search Cost | Mean Recovered Gain | Net Search Benefit |
|---|---|---|---|---|
| `PA_EVERY_INTERRUPTION_FILTERED` | 61.4 iters | 1.16 SoC | 101.69 SoC | **+100.53 SoC** |
| `PA_EVICTION_ONLY` | 91.1 iters | 1.72 SoC | 0.00 SoC | **-1.72 SoC** |
| `PA_TERMINAL_ONLY` | 112.4 iters | 2.12 SoC | 0.00 SoC | **-2.12 SoC** |
| `PA_EVICTION_AND_TERMINAL` | 214.1 iters | 4.04 SoC | 0.00 SoC | **-4.04 SoC** |

---

## 6. Terminal-Reserve Control Ablation

Under `TERMINAL_RESERVE_CONTROL`, search was stopped early at the 2.0 ms reserve without recovery:
- Paired vs Resumption: **0W / 32T / 0L**
- Mean ΔCost: **+0.00 SoC**
- Stopping normal search 2.0 ms early imposes zero cost penalty across both 1.0s and 5.0s planning deadlines while reserving safe headroom for terminal recovery.
