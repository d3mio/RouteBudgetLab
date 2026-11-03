# Gate 1 Feasibility Report: Resumable Repair Checkpointing (Corrected & Verified)

**Project:** RouteBudget Lab: Bounded-Interruption Resumable Repair for MAPF-LNS Under Wall-Clock Budgets  
**Author:** P. G. Disas Sethnada (IIT: 20231323, UoW: w2119964)  
**Date:** October 21, 2026 (Audited & Corrected)  
**Status:** **PASSED (CONDITIONAL ON MACRO-PREFIX FALLBACK 1A)**  
**Traceability:** Aligns with Proposal Objective **O1**, Work Package **WP1**, Requirements **FR-02**, **NFR-02**, **NFR-03**, and Decision **D5**.

---

## 1. Executive Summary & Audited Recommendation

In accordance with Phase 2 and Work Package 1 of RouteBudget Lab, Gate 1 evaluates the engineering and computational feasibility of interrupting and checkpointing repair searches within a Multi-Agent Path Finding Large Neighborhood Search (MAPF-LNS) solver.

The proposal outlined two technical pathways for state suspension:
1. **Primary Path (Micro-Frontier Cloning):** Suspending an in-flight single-agent search mid-execution, deep-cloning its priority queue (`OPEN` heap), closed set, and expanded search node graph.
2. **Fallback 1A (Macro-Prefix Checkpointing):** Suspending a multi-agent neighborhood repair at agent boundaries or search cutoffs, preserving the completed prefix of collision-free paths $r_{done}$ for already re-planned agents, along with the neighborhood set $\mathcal{N}$, priority permutation, and incumbent context.

### Corrected Gate 1 Conclusion
**GATE 1 IS RECORDED AS PASSED (CONDITIONAL ON ADOPTING FALLBACK 1A: MACRO-PREFIX CHECKPOINTING).**

**Audit Correction on Frontier Checkpointing:**
The earlier preliminary summary reported micro-frontier cloning latency of 15.4–429.3 $\mu$s but asserted that overhead exceeded 35%. A rigorous audit of the benchmark code reveals:
- At 10,000 search nodes (670.4 KB heap), single-shot snapshot latency is **$678.3\ \mu$s**, which is **$6.78\%$ of a 10 ms slice** (and $429.3\ \mu$s was **$4.29\%$**).
- Therefore, **snapshotting alone does NOT fail the 10% target on a 10 ms slice** in isolation.
- However, when factoring in **restoration ($762.7\ \mu$s)**, the combined save+restore cost reaches **$1,440.9\ \mu$s ($14.41\%$ of 10 ms, $28.82\%$ of 5 ms)**.
- Furthermore, this was measured on a *synthetic container-copy benchmark* (cloning `boost::heap::pairing_heap` and `std::unordered_set`), rather than within the live solver where node pointer fixups and cache eviction add further latency.
- Macro-prefix checkpointing is formally selected **not because single-shot frontier snapshotting failed $<10\%$ on 10 ms**, but because:
  1. It incurs low latency (**$0.34 - 1.15\ \mu$s combined save+restore on synthetic benchmarks across $|N| \in [4, 32]$**, representing $<0.012\%$ of a 10 ms slice; specifically $0.34\ \mu$s at $|N|=4$, $0.67\ \mu$s at $|N|=16$, and $1.15\ \mu$s at $|N|=32$). In live engine integration, combined save+restore measures **$3.92\ \mu$s in Release ($0.039\%$ of 10 ms)** and **$15.62\ \mu$s in ASan/UBSan ($0.156\%$ of 10 ms)**.
  2. It has an order-of-magnitude smaller memory footprint (**$1.4 - 7.6$ KB synthetic, $1.0$ KB live measured vs $670$ KB for frontier cloning**).
  3. It uses standard C++ value semantics (`std::vector<Path>`), avoiding the raw pointer ownership and pairing heap handle tracking of micro-frontier cloning within the scope tested.

---

## 2. Checkpoint Benchmark & Overhead Audit

The checkpoint feasibility benchmark was re-audited in `src/profiler/profile_checkpoint_feasibility.cpp` using compiler memory barriers (`doNotOptimize`), high-resolution monotonic clocks (`std::chrono::steady_clock`), and separation of snapshot, restore, and cleanup costs. Raw data is stored in `results/profiling/checkpoint_feasibility.json`.

### Exact Formulas
For an active search slice $\Delta \in \{5\text{ ms}, 10\text{ ms}\}$:
$$\text{Snapshot Overhead (\%)} = \frac{T_{snapshot}}{\Delta} \times 100$$
$$\text{Restore Overhead (\%)} = \frac{T_{restore}}{\Delta} \times 100$$
$$\text{Combined Save+Restore Overhead (\%)} = \frac{T_{snapshot} + T_{restore}}{\Delta} \times 100$$

### Table 1: Audited Benchmark Measurements

| Checkpoint Mode | Configuration | Memory Retained | Snapshot Time $T_{snap}$ | Restore Time $T_{rest}$ | Cleanup Time $T_{free}$ | Combined $T_{save+rest}$ | Combined / 10 ms Slice | Combined / 5 ms Slice |
|---|---|---|---|---|---|---|---|---|
| **Macro-Prefix** | $\|N\|=4, r=2, L=50$ | 1.41 KB | 0.161 $\mu$s | 0.177 $\mu$s | 0.052 $\mu$s | 0.338 $\mu$s | **0.0034%** | **0.0068%** |
| **Macro-Prefix** | $\|N\|=8, r=4, L=50$ | 2.29 KB | 0.216 $\mu$s | 0.224 $\mu$s | 0.061 $\mu$s | 0.440 $\mu$s | **0.0044%** | **0.0088%** |
| **Macro-Prefix** | $\|N\|=16, r=8, L=50$ | 4.05 KB | 0.333 $\mu$s | 0.338 $\mu$s | 0.098 $\mu$s | 0.671 $\mu$s | **0.0067%** | **0.0134%** |
| **Macro-Prefix** | $\|N\|=32, r=16, L=50$ | 7.57 KB | 0.567 $\mu$s | 0.587 $\mu$s | 0.164 $\mu$s | 1.154 $\mu$s | **0.0115%** | **0.0231%** |
| **Micro-Frontier** | 100 nodes | 7.48 KB | 12.89 $\mu$s | 14.18 $\mu$s | 4.00 $\mu$s | 27.07 $\mu$s | 0.27% | 0.54% |
| **Micro-Frontier** | 1,000 nodes | 69.64 KB | 65.08 $\mu$s | 63.88 $\mu$s | 20.11 $\mu$s | 128.97 $\mu$s | 1.29% | 2.58% |
| **Micro-Frontier** | 5,000 nodes | 338.44 KB | 337.88 $\mu$s | 377.76 $\mu$s | 97.73 $\mu$s | 715.64 $\mu$s | 7.16% | 14.31% |
| **Micro-Frontier** | 10,000 nodes | 670.44 KB | 678.26 $\mu$s | 762.66 $\mu$s | 191.21 $\mu$s | 1440.93 $\mu$s | **14.41%** | **28.82%** |

### Distinguishing Synthetic Benchmarks from Real Integration
The micro-frontier benchmark cloned standard STL/Boost containers populated with mock `LLNode` objects. In real solver integration:
- `SpaceTimeAStar` and `SIPP` nodes contain raw parent pointers (`LLNode* parent`). Restoring this requires re-mapping all parent pointers to newly cloned nodes or deep-traversing the open heap.
- Pairing heap handles (`focal_handle`) must be invalidated and re-inserted into the auxiliary FOCAL heap.
- Under ASan, dynamic allocations for thousands of small node objects induce measurable heap fragmentation and sanitization tracking overhead.

---

## 3. Disentangling Repair Outcomes

A primary flaw in early analysis was equating whole-iteration durations with active repair search times, and confusing **LNS neighborhood rejection rates** with **search timeouts**.

To resolve this, the solver was instrumented in `src/engine/rbl_search_budget.hpp` and `src/profiler/profile_repair_outcomes.cpp` with **five mutually exclusive outcomes**:
1. **Completed and Accepted (`COMPLETED_ACCEPTED`):** All neighborhood agents successfully routed; new sum-of-costs strictly lower than incumbent; committed to incumbent.
2. **Completed but Non-Improving (`COMPLETED_NOT_IMPROVING`):** All neighborhood agents successfully routed (or pruned by cost bound), but new cost $\ge$ old cost; rolled back.
3. **Failed to Find Feasible Repair (`FAILED_NO_PATH`):** Space-Time $A^*$ proved no collision-free path exists under current reservations; rolled back.
4. **Interrupted by Active Slice (`INTERRUPTED_SLICE`):** Active low-level search budget reached cutoff mid-repair; checkpoint captured if $K > 0$; candidate state rolled back.
5. **Stopped by Global Deadline (`STOPPED_DEADLINE`):** Total solver run budget expired before repair could start.

### Empirical Training Partition Profile
50 iterations of LNS neighborhood repair ($|N|=8$) were profiled across all 4 training partition maps under:
- `UNCONSTRAINED_REPAIR` ($\Delta = \infty$, pure natural repair dynamics)
- `ARTIFICIAL_SLICE_10MS` ($\Delta = 10$ ms)
- `ARTIFICIAL_SLICE_5MS` ($\Delta = 5$ ms)

Results are recorded in `results/profiling/repair_outcomes_summary.json`:

### Table 2: Outcome Breakdown Across Training Partition

| Map | Agents | Slice Mode | Completed Accepted | Completed Non-Improving | Failed (No Path) | Interrupted (Slice) | Interrupted w/ Reusable Prefix ($r_{done} > 0$) |
|---|---|---|---|---|---|---|---|
| `warehouse-10-20-10-2-1` | 50 | Unconstrained | 0 (0%) | 50 (100%) | 0 (0%) | 0 (0%) | N/A |
| `warehouse-10-20-10-2-1` | 50 | Slice 10ms | 0 (0%) | 50 (100%) | 0 (0%) | 0 (0%) | N/A |
| `warehouse-10-20-10-2-1` | 50 | Slice 5ms | 0 (0%) | 50 (100%) | 0 (0%) | 0 (0%) | N/A |
| `warehouse-10-20-10-2-1` | 100 | Unconstrained | 1 (2%) | 49 (98%) | 0 (0%) | 0 (0%) | N/A |
| `warehouse-10-20-10-2-1` | 100 | Slice 10ms | 1 (2%) | 48 (96%) | 0 (0%) | 1 (2%) | **1 / 1 (100%)** |
| `warehouse-10-20-10-2-1` | 100 | Slice 5ms | 1 (2%) | 47 (94%) | 0 (0%) | 2 (4%) | **2 / 2 (100%)** |
| `random-32-32-20` | 50 | Unconstrained | 0 (0%) | 48 (96%) | 2 (4%) | 0 (0%) | N/A |
| `random-32-32-20` | 50 | Slice 10ms | 0 (0%) | 48 (96%) | 2 (4%) | 0 (0%) | N/A |
| `random-32-32-20` | 50 | Slice 5ms | 0 (0%) | 48 (96%) | 0 (0%) | 2 (4%) | **2 / 2 (100%)** |
| `random-32-32-20` | 100 | Unconstrained | 0 (0%) | 50 (100%) | 0 (0%) | 0 (0%) | N/A |
| `random-32-32-20` | 100 | Slice 10ms | 0 (0%) | 50 (100%) | 0 (0%) | 0 (0%) | N/A |
| `random-32-32-20` | 100 | Slice 5ms | 0 (0%) | 50 (100%) | 0 (0%) | 0 (0%) | N/A |
| `maze-32-32-2` | 50 | Unconstrained | 0 (0%) | 44 (88%) | 6 (12%) | 0 (0%) | N/A |
| `maze-32-32-2` | 50 | Slice 10ms | 0 (0%) | 44 (88%) | 3 (6%) | 3 (6%) | **3 / 3 (100%)** |
| `maze-32-32-2` | 50 | Slice 5ms | 0 (0%) | 44 (88%) | 3 (6%) | 3 (6%) | **3 / 3 (100%)** |
| `maze-32-32-2` | 100 | Unconstrained | 2 (4%) | 32 (64%) | 16 (32%) | 0 (0%) | N/A |
| `maze-32-32-2` | 100 | Slice 10ms | 2 (4%) | 32 (64%) | 1 (2%) | **15 (30%)** | **15 / 15 (100%)** |
| `maze-32-32-2` | 100 | Slice 5ms | 2 (4%) | 27 (54%) | 1 (2%) | **20 (40%)** | **20 / 20 (100%)** |
| `empty-32-32` | 50 | Unconstrained | 2 (4%) | 48 (96%) | 0 (0%) | 0 (0%) | N/A |
| `empty-32-32` | 50 | Slice 10ms | 2 (4%) | 48 (96%) | 0 (0%) | 0 (0%) | N/A |
| `empty-32-32` | 50 | Slice 5ms | 2 (4%) | 48 (96%) | 0 (0%) | 0 (0%) | N/A |
| `empty-32-32` | 100 | Unconstrained | 1 (2%) | 49 (98%) | 0 (0%) | 0 (0%) | N/A |
| `empty-32-32` | 100 | Slice 10ms | 1 (2%) | 49 (98%) | 0 (0%) | 0 (0%) | N/A |
| `empty-32-32` | 100 | Slice 5ms | 1 (2%) | 49 (98%) | 0 (0%) | 0 (0%) | N/A |

### Critical Observations from Outcome Separation
1. **Rejection Rate is NOT Timeout Rate:** In unconstrained execution, 0% of repairs timed out. 64%–100% of iterations were simply rejected as non-improving (`COMPLETED_NOT_IMPROVING`). In LNS, exploring and discarding non-improving neighborhoods is expected behavior, not an algorithmic failure.
2. **Where Slices Actually Cut Off:** Active search slices cut off almost exclusively in dense, corridor-constrained maps (`maze-32-32-2` with 100 agents), where 30%–40% of repairs exceed 5–10 ms due to high conflict avoidance table density.
3. **Useful Prefix Availability:** Across all sampled configurations, in **43 / 43 (100%) of interrupted repair cases**, at least one agent had already finished routing ($r_{done} \ge 1$, `has_reusable_prefix = true`). However, this non-empty prefix is classified strictly as *potentially reusable state*, NOT as a demonstrated performance benefit. Whether prefix reuse yields measurable solution quality improvements over restart ($K=0$) is Research Question **RQ1**, which will be evaluated empirically with full statistical evidence in Phase 7.

---

## 4. Exact Checkpoint Contract for Phase 3

Under Fallback 1A, the checkpoint tuple $C_i$ stored in the bounded manager ($K \in \{0, 1, 2\}$) is defined as:

$$C_i = \langle \text{id}, \text{state}, \mathcal{N}_i, \text{ord}(\mathcal{N}_i), r_{done}, P_{done}, P_{old}, c_{old}, v_{inc}, \text{seed}_{unfinished}, t_{active}, \text{exp}_{active} \rangle$$

Where:
- **`id`**: Unique 64-bit monotonic checkpoint sequence number.
- **`state`**: Lifecycle state (`EMPTY`, `ACTIVE`, `SUSPENDED`, `COMMITTED`, `DISCARDED`, `EXPIRED`).
- **`neighborhood_agents` ($\mathcal{N}_i$)**: Set of agent IDs selected for repair.
- **`priority_order` ($\text{ord}(\mathcal{N}_i)$)**: Deterministic planning permutation used for prioritized repair.
- **`r_done`**: Number of completed prefix agents ($0 \le r_{done} < |\mathcal{N}_i|$).
- **`prefix_paths` ($P_{done}$)**: Valid, collision-free paths for agents $\text{ord}(\mathcal{N}_i)[0 \dots r_{done}-1]$.
- **`old_paths` ($P_{old}$)**: Snapshot of incumbent paths for $\mathcal{N}_i$ to enable transactional rollback.
- **`old_cost` ($c_{old}$)**: Neighborhood incumbent sum-of-costs (pruning ceiling).
- **`incumbent_version` ($v_{inc}$)**: Monotonic version of incumbent plan when checkpoint was captured.
- **`rng_seed_at_unfinished_agent`**: Seed state for deterministic tie-breaking of the unfinished agent.
- **`accumulated_active_time_s`**: Active CPU search time accumulated across slices.
- **`accumulated_expansions`**: Node expansions accumulated prior to interruption.

---

## 5. Summary of Phase 3 Verification & Gate 1 Exit Status

All criteria for Gate 1 and prerequisites for Phase 3 are satisfied:
1. Overhead calculations audited and corrected with exact formulas and numbers.
2. Distinct repair outcomes instrumented and benchmarked on training maps.
3. Macro-prefix checkpointing verified as low-overhead, transactional, and ASan/UBSan-clean within the scope of tested maps and configurations.
4. Experiment E1 test suite passing 100% (10/10 test categories) with independent validator verification.
