# Partial Recovery of Interrupted MAPF-LNS Repair Checkpoints Under Changed External Trajectories: A Rigorous Mechanistic and Empirical Evaluation

**Project:** RouteBudget Lab  
**Branch:** `research/partial-prefix-recovery`  
**Author:** P. G. Disas Sethnada (IIT: 20231323, UoW: w2119964)  
**Date:** October 2026  
**Status:** Supporting Research Study & Diagnostic Evaluation  

---

## 1. Executive Summary & Context

In anytime Multi-Agent Path Finding via Large Neighborhood Search (MAPF-LNS) under hard search deadlines, repair operations on selected agent neighborhoods $\mathcal{N}$ are frequently interrupted when allotted time or expansion budgets expire. RouteBudget Lab introduced **Macro-Prefix Checkpointing**, storing the partially routed prefix of agents $P_{0 \dots r_{done}-1}$ alongside active search state $\mathcal{C}$.

Prior to this work, RouteBudget Lab evaluated two checkpoint retention policies when external incumbent paths change ($\mathcal{P}_{inc} \to \mathcal{P}'_{inc}$):
1. **Policy A (`DISCARD_ON_CHANGE`):** Resumes an interrupted checkpoint if the incumbent version is unchanged; discards the checkpoint upon any external trajectory change ($\Delta v_{inc} > 0$).
2. **Policy B (`WHOLE_CHECKPOINT_SELECTIVE`):** Validates all $r_{done}$ prefix paths against the modified incumbent; if *any* single agent path $P_i$ ($0 \le i < r_{done}$) exhibits a conflict, the *entire* checkpoint is discarded ($0$ paths salvaged).

This research investigation introduces and evaluates:
> **Policy C (`PARTIAL_PREFIX_RECOVERY`):** A conservative recovery mechanism that sequentially validates prefix paths $P_0, P_1, \dots, P_{r_{done}-1}$. If the earliest detected collision occurs at agent index $k \in \{1, \dots, r_{done}-1\}$, the engine safely truncates the prefix to $k$, salvages the verified collision-free prefix $P_{0 \dots k-1}$, discards invalid paths $P_{k \dots r_{done}-1}$, rebuilds low-level space-time reservations, and deterministically resumes search from agent $k$.

### Primary Findings of the Investigation:
- **Theoretical Scope:** We demonstrate that collision-free retained paths do *not* necessarily remain optimal after external trajectories mutate: unblocking an external corridor can open a shorter route while leaving a detour feasible. We formulate the precisely scoped **Retained-Prefix Validity Proposition**, proving spatio-temporal validity under 7 explicit assumptions without overclaiming suffix completion, fresh-search identity, cost improvement, or optimality.
- **Diagnostic Validation across Multiple Maps:** In controlled 5-agent neighborhoods ($m=5$, interrupted at $r_{done}=4$) evaluated across both `random-32-32-20` and `warehouse-10-20-10-2-1`, Policy C cleanly discriminates early ($k=0$, 0 salvaged), middle ($k=2$, 2 salvaged), and late ($k=3$, 3 salvaged) conflicts. Within each matched condition, salvaging valid prefixes reduces resumption expansions compared to full discard (e.g. 432 vs 466 expansions in Condition 3, and 389 vs 455 expansions in Condition 4 on random), with microsecond-scale validation overhead ($0.2 - 5.0\ \mu\text{s}$). Independently validated plans pass whole-plan collision checks (`rbl::checkPlan`).
- **Empirical Benchmark Reality:** Across 72 runs ($N=24$ paired conditions across 3 policies) on Moving AI validation maps, the natural occurrence rate of partial recoveries was **exactly 0**. In single-threaded sequential MAPF-LNS with small neighborhoods ($|\mathcal{N}| \in [2, 8]$), incumbent updates either leave the suspended neighborhood untouched (handled by Policy B) or invalidate the neighborhood context.
- **Academic Demarcation:** We classify partial-prefix recovery as a **supporting diagnostic and negative-result extension**. It clarifies the theoretical limits of prefix reuse and confirms the mechanics of prefix salvage, but does not provide an empirical performance breakthrough for sequential MAPF-LNS.

---

## 2. Theoretical Foundations & Scope Corrections

### 2.1 The MAPF-LNS Prioritized Planning Paradigm
In standard MAPF-LNS (Li et al., 2021), a valid incumbent solution $\mathcal{P}_{inc} = \{P_1, \dots, P_N\}$ is iteratively improved. At each iteration, a destroy heuristic selects a subset of agents $\mathcal{N} \subset \{1, \dots, N\}$ ($|\mathcal{N}| \ll N$). A repair operator plans new paths for all $a \in \mathcal{N}$ while treating non-neighborhood agents $\mathcal{A} \setminus \mathcal{N}$ as dynamic obstacles reserved in a Space-Time reservation table (`PathTable`).

Within RouteBudget Lab, the repair operator uses **Prioritized Planning (PP)** with a priority permutation $\pi = \langle a_0, a_1, \dots, a_{m-1} \rangle$, where $m = |\mathcal{N}|$. Paths are planned sequentially: agent $a_i$ searches for a shortest collision-free path in a Space-Time graph where reservations exist for:
1. Static map obstacles $\mathcal{O}_{map}$.
2. External incumbent agents $a_{ext} \in \mathcal{A} \setminus \mathcal{N}$ across their full durations.
3. Higher-priority neighborhood agents already planned in the current repair: $P_0, P_1, \dots, P_{i-1}$.

### 2.2 Counterexample: Feasible Retained Paths Can Be Suboptimal
A crucial correction to earlier drafts is that **collision freedom does not imply optimality after external trajectories mutate**.

> **Counterexample (Suboptimal Detour Retention):**  
> Consider a 2D grid containing neighborhood agent $a_0 \in \mathcal{N}$ with start location $s_0 = (0, 0)$ and goal $g_0 = (2, 0)$.  
> Suppose an external incumbent agent $a_{ext} \notin \mathcal{N}$ initially occupies location $(1, 0)$ across timesteps $t \in [0, 10]$.  
> Because the straight line $(0,0) \to (1,0) \to (2,0)$ is blocked by $a_{ext}$, Space-Time $A^*$ routes $a_0$ via a detour of length 4:
> $$P_0 = \langle (0,0), (0,1), (1,1), (2,1), (2,0) \rangle \quad (\text{cost } 4)$$
> Path $P_0$ is collision-free and optimal given the constraint obstacle at $(1, 0)$.  
> During repair, an interruption occurs after $a_0$ is routed, and $P_0$ is stored in checkpoint $\mathcal{C}$.  
> Before $\mathcal{C}$ is resumed, another LNS iteration updates the incumbent: external agent $a_{ext}$ moves away, vacating $(1, 0)$ for all $t \ge 0$.  
> When $\mathcal{C}$ is validated against the updated incumbent:
> 1. Path $P_0$ visits locations that remain completely unblocked by the new trajectory of $a_{ext}$. Thus, $P_0$ is **collision-free and feasible**.
> 2. However, a direct straight route of length 2 now exists: $P^*_0 = \langle (0,0), (1,0), (2,0) \rangle$ (cost 2).
> 
> If partial-prefix recovery salvages $P_0$, the repair engine is committed to a **suboptimal path of cost 4**, failing to exploit the newly vacated optimal route of cost 2.

Therefore, partial recovery preserves **feasibility**, not optimality. Retained paths may lock in obsolete detours.

### 2.3 Retained-Prefix Validity Proposition
We replace the previous unconstrained invariance claims with a precisely scoped proposition.

**Assumptions:**
1. **Map Identity:** The static graph $\mathcal{G} = \langle \mathcal{V}, \mathcal{E} \rangle$ and obstacles $\mathcal{O}_{map}$ are invariant between checkpoint save and resumption.
2. **Start and Goal Invariance:** For every agent $a \in \mathcal{N} \cup (\mathcal{A} \setminus \mathcal{N})$, start location $s_a$ and goal location $g_a$ are fixed.
3. **Neighborhood Membership & Priority Order:** Neighborhood set $\mathcal{N}$ and permutation $\pi = \langle a_0, \dots, a_{m-1} \rangle$ match the checkpoint.
4. **Internal Mutual Non-Collision:** For all $0 \le j < i < k$, prefix paths $P_i$ and $P_j$ have no vertex collisions, edge swaps, or post-arrival goal conflicts.
5. **External Non-Collision:** For all $0 \le i < k$ and all external agents $a_{ext} \in \mathcal{A} \setminus \mathcal{N}$, path $P_i$ has no vertex or edge collisions with updated trajectory $P'_{ext}$.
6. **Edge Swap Freedom:** For all $0 \le i < k$, $a_{ext} \notin \mathcal{N}$, and $t \ge 0$, no simultaneous transitions $(P_i(t) = P'_{ext}(t+1) \land P_i(t+1) = P'_{ext}(t))$ occur for $P_i(t) \ne P_i(t+1)$.
7. **Persistent Goal Occupancy:** All agents dwell indefinitely at their destination locations without conflict after arrival: $\forall t \ge |P_a|-1, P_a(t) = g_a$.

**Proposition 1 (Retained-Prefix Spatio-Temporal Validity):**  
*Under Assumptions 1–7, the truncated prefix $P_{0 \dots k-1}$ represents a valid, mutually collision-free joint trajectory for agents $a_0, \dots, a_{k-1}$ that is spatio-temporally collision-free with all external trajectories in $\mathcal{P}'_{ext}$.*

### 2.4 Suffix Rebuilding, Final Validation, and Explicit Disclaimers
When Proposition 1 holds, the repair engine executes the following steps:
1. Rebuilds the low-level `PathTable` by inserting all external incumbent paths $\mathcal{P}'_{ext}$ and all salvaged prefix paths $P_0, \dots, P_{k-1}$.
2. Sets the search re-entry index to $r_{done} = k$.
3. Sequentially routes suffix agents $a_k, a_{k+1}, \dots, a_{m-1}$ using Space-Time $A^*$ against this populated reservation table.
4. If all suffix agents are successfully planned within the search budget, the complete candidate plan is subjected to **independent whole-plan validation** (`rbl::checkPlan`) before being committed to the incumbent.

**Explicit Disclaimers (What is NOT Guaranteed):**
- **No Guaranteed Suffix Completion:** Suffix agents $a_k, \dots, a_{m-1}$ may fail to find paths (`FAILED_NO_PATH`) because the combination of retained paths $P_{0 \dots k-1}$ and updated external paths $\mathcal{P}'_{ext}$ may over-constrain the grid.
- **No Identity with Fresh Search:** Suffix paths found upon resumption will not necessarily match those that a fresh search from $a_0$ would produce, due to different tie-breaking, heuristic differences, and path interactions.
- **No Guaranteed Cost Improvement:** The candidate repair may complete but fail the LNS cost-reduction check ($\sum_{a \in \mathcal{N}} c(P_a) \ge c_{old}(\mathcal{N})$), resulting in rollback (`COMPLETED_NOT_IMPROVING`).
- **No Optimality:** As proven in Section 2.2, retained paths may preserve suboptimal routes that were forced by obsolete external constraints.

### 2.5 Causal Algorithmic Dependency vs Path Derivatives
Earlier drafts used notation such as $\frac{\partial P_i}{\partial P_j} = \emptyset$. We discard this notation.

In prioritized planning with Space-Time $A^*$, each path $P_i$ is generated by a deterministic algorithmic function:
$$P_i = \text{SpaceTimeAStar}(s_i, g_i, \mathcal{O}_{map}, \mathcal{P}_{ext}, P_0, \dots, P_{i-1}, \text{seed}_i)$$
Paths $P_k, \dots, P_{r_{done}-1}$ are evaluated causally *after* $P_0, \dots, P_{k-1}$. The state and output of $P_0, \dots, P_{k-1}$ have zero causal dependency on downstream paths. Discarding or re-planning downstream paths cannot alter the computational trace or validity of the preceding paths.

*Distinction:* This is a constructive algorithmic dependency argument based on the unidirectional execution flow of prioritized planning. It is an informal mathematical argument, distinct from a formal machine-checked proof (e.g. in Coq, Isabelle, or Lean).

---

## 3. Algorithmic Architecture & Verification

```
+-----------------------------------------------------------------------------------+
|                            RBL Resumable Controller                               |
|                                                                                   |
|  1. Candidate Generation -> Interrupted Checkpoint C in Store                     |
|  2. ConservativeConstraintValidator::validate(C, incumbent, RetentionPolicy)       |
+-----------------------------------------------------------------------------------+
                                         |
         +-------------------------------+-------------------------------+
         |                               |                               |
  [DISCARD_ON_CHANGE]         [WHOLE_SELECTIVE]              [PARTIAL_PREFIX]
         |                               |                               |
         v                               v                               v
 Incumbent Changed?              All r_done Valid?             Scan 0 .. r_done-1
   YES: Discard (k=0)              YES: Resume full (k=r_done)   First collision at k?
   NO:  Resume full (k=r_done)     NO:  Discard all (k=0)          k == 0: Discard (k=0)
                                                                   k > 0:  Salvage P_{0..k-1}
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                        RBL Resumable Engine (Space-Time A*)                       |
|                                                                                   |
|  1. Truncate checkpoint to valid prefix length k                                  |
|  2. Rebuild PathTable with P'_{ext} and P_{0 .. k-1}                              |
|  3. Restore RNG seed deterministically: base_seed + k * 10007                     |
|  4. Low-level Space-Time A* plans suffix agents: a_k, a_{k+1}, ..., a_{m-1}       |
|  5. Independent Whole-Plan Validation (rbl::checkPlan)                            |
|  6. If completed & improved & valid: Commit transaction to incumbent              |
+-----------------------------------------------------------------------------------+
```

### 3.1 Unit Testing Suite
A dedicated 10-test suite (`tests/partial_prefix_recovery_tests.cpp`) verifies the core mechanics under isolated conditions:
- `Test1_UnchangedContext`: Fast version check validates entire prefix ($< 0.1\ \mu\text{s}$).
- `Test2_UnrelatedExternalChange`: Non-colliding external perturbation preserves all paths.
- `Test3_ConflictAtFirstAgent`: Collision at $k=0$ cleanly discards checkpoint (0 salvaged).
- `Test4_MiddleConflictTruncationAndResumption`: Collision at $k=1$ salvages $P_0$, plans suffix, and passes independent validation.
- `Test5_LateConflictTruncation`: Collision at $k=r_{done}-1$ salvages preceding paths.
- `Test6_EdgeSwapConflictDetection`: Head-on edge swap correctly detected and truncated.
- `Test7_StayAtGoalConflictDetection`: Goal dwelling collisions detected.
- `Test8_NeighborhoodMutationRejection`: Changes to neighborhood identity strictly rejected.
- `Test9_StaleCostBoundRejection`: Prefix cost exceeding incumbent baseline rejected.
- `Test10_EndToEndIndependentValidationPass`: Full plan verified collision-free via `rbl::checkPlan`.

All 10 tests and all 8 test suites in `ctest` pass with 100% success rate.

---

## 4. Multi-Map Mechanism Diagnostic (Strengthened Diagnostic)

### 4.1 Diagnostic Methodology & Test Categorization
To evaluate genuinely distinct conflict positions, we refactored the diagnostic profiler (`src/profiler/profile_partial_recovery.cpp`) with the following methodology:
- **Neighborhood Size:** $m = 5$ agents ($\mathcal{N} = \{0, 1, 2, 3, 4\}$).
- **Deterministic Interruption:** `SearchBudget::setMaxAgents(4)` interrupts initial fresh search after exactly $r_{done} = 4$ completed agents.
- **Multiple Benchmark Topologies:**
  1. `random-32-32-20` (random grid with 20% obstacles, 10 agents).
  2. `warehouse-10-20-10-2-1` (structured warehouse layout with narrow corridors, 10 agents).
- **Matched Starting Checkpoints:** All three policies evaluate the exact same interrupted checkpoint state $\mathcal{C}$ saved at $r_{done} = 4$.

#### Rigorous Separation of Test Cases:
To ensure sound empirical interpretation, we explicitly categorize each condition:
1. **Valid External Updates Applied to a Valid Incumbent:**
   - *Condition 0 (Control: Unchanged):* Incumbent version unchanged; tests base resumption.
   - *Condition 1 (Unrelated Change):* External agent waits 1 step at start; valid move applied to a valid incumbent without colliding with $\mathcal{N}$.
   - *Condition 4 (Late Conflict, $k=3$):* Space-Time BFS routes external agent along a genuine, collision-free legal path that intersects path $P_3$ at a specific timestep without violating any other agent constraints.
2. **Fault-Injection Cases Violating Normal Kinematic Invariants:**
   - *Condition 2 (Early Conflict, $k=0$) & Condition 3 (Middle Conflict, $k=2$):* When Space-Time BFS cannot find a legal 2-leg path avoiding all other external agents, the test harness synthetically mutates an external agent's location at $t=2$ (`mod_paths[ext_a][2].location = p_target[2].location`). This injects an artificial teleportation to stress-test prefix collision detection.
3. **Completed Valid Candidate Repairs:**
   - Occur in Conditions 0, 1, and 4. All three policies produce candidate repairs that complete, improve or preserve cost, and pass independent whole-plan validation (`rbl::checkPlan`).
4. **Rejected Invalid Candidates:**
   - In Conditions 2 and 3, because the test harness injected an illegal move into the external agent's trajectory, the final whole-plan validator `rbl::checkPlan` flags `ILLEGAL_MOVE` on that agent (`plan_valid = false`).
   - *Crucial Clarification:* A failed plan-validity check here catches the deliberate kinematic fault injection; **it does not equate to proof that the underlying MAPF instance has no collision-free solution**.
5. **Unsuccessful or Interrupted Repairs:**
   - In warehouse conditions where suffix routing cannot improve upon the incumbent, repairs exit cleanly with `COMPLETED_NOT_IMPROVING` without mutating the incumbent.

---

### 4.2 Multi-Map Diagnostic Results

#### Map 1: `random-32-32-20` (Previously spent expansions before interruption: 437)

| Condition | Category | Policy | Verdict | Salvaged | Resumption Exp | Total Exp | Val Latency | Plan Valid |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **0: Control (Unchanged)** | Valid Incumbent | Policy A (`DISCARD_ON_CHANGE`) | `COMPATIBLE_UNCHANGED` | 4 / 4 | 30 | 467 | 1.5 $\mu\text{s}$ | **YES** |
| | | Policy B (`WHOLE_SELECTIVE`) | `COMPATIBLE_UNCHANGED` | 4 / 4 | 30 | 467 | 0.3 $\mu\text{s}$ | **YES** |
| | | Policy C (`PARTIAL_PREFIX`) | `COMPATIBLE_UNCHANGED` | 4 / 4 | 30 | 467 | 0.2 $\mu\text{s}$ | **YES** |
| **1: Unrelated Change** | Valid External Update | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 467 | 904 | 0.8 $\mu\text{s}$ | **YES** |
| | | Policy B (`WHOLE_SELECTIVE`) | `COMPATIBLE_PREFIX_VALID` | 4 / 4 | 30 | 467 | 2.2 $\mu\text{s}$ | **YES** |
| | | Policy C (`PARTIAL_PREFIX`) | `COMPATIBLE_PREFIX_VALID` | 4 / 4 | 30 | 467 | 0.5 $\mu\text{s}$ | **YES** |
| **2: Early Conflict ($k=0$)** | Fault Injection (Synthetic) | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 467 | 904 | 0.7 $\mu\text{s}$ | NO* |
| | | Policy B (`WHOLE_SELECTIVE`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 467 | 904 | 1.3 $\mu\text{s}$ | NO* |
| | | Policy C (`PARTIAL_PREFIX`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 467 | 904 | 0.5 $\mu\text{s}$ | NO* |
| **3: Middle Conflict ($k=2$)** | Fault Injection (Synthetic) | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 466 | 903 | 0.4 $\mu\text{s}$ | NO* |
| | | Policy B (`WHOLE_SELECTIVE`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 466 | 903 | 0.6 $\mu\text{s}$ | NO* |
| | | **Policy C (`PARTIAL_PREFIX`)** | `COMPATIBLE_PARTIAL_PREFIX` | **2 / 4** | **432** | **869** | **0.8 $\mu\text{s}$** | NO* |
| **4: Late Conflict ($k=3$)** | Valid External Update | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 455 | 892 | 0.4 $\mu\text{s}$ | **YES** |
| | | Policy B (`WHOLE_SELECTIVE`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 455 | 892 | 0.7 $\mu\text{s}$ | **YES** |
| | | **Policy C (`PARTIAL_PREFIX`)** | `COMPATIBLE_PARTIAL_PREFIX` | **3 / 4** | **389** | **826** | **1.7 $\mu\text{s}$** | **YES** |

*\*Note on Rejected Plans in Fault-Injection Cases:* In Conditions 2 and 3, the test harness injected an illegal single-timestep coordinate mutation on the external agent. When `rbl::checkPlan` was called on the final incumbent, it detected this kinematic violation. This validates that the whole-plan checker catches invalid incumbents; it does not indicate that the MAPF instance has no valid solution.

---

#### Map 2: `warehouse-10-20-10-2-1` (Previously spent expansions before interruption: 288)

| Condition | Category | Policy | Verdict | Salvaged | Resumption Exp | Total Exp | Val Latency | Plan Valid |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **0: Control (Unchanged)** | Valid Incumbent | Policy A (`DISCARD_ON_CHANGE`) | `COMPATIBLE_UNCHANGED` | 4 / 4 | 80 | 368 | 0.9 $\mu\text{s}$ | **YES** |
| | | Policy B (`WHOLE_SELECTIVE`) | `COMPATIBLE_UNCHANGED` | 4 / 4 | 80 | 368 | 0.2 $\mu\text{s}$ | **YES** |
| | | Policy C (`PARTIAL_PREFIX`) | `COMPATIBLE_UNCHANGED` | 4 / 4 | 80 | 368 | 0.2 $\mu\text{s}$ | **YES** |
| **1: Unrelated Change** | Valid External Update | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 368 | 656 | 1.2 $\mu\text{s}$ | **YES** |
| | | Policy B (`WHOLE_SELECTIVE`) | `COMPATIBLE_PREFIX_VALID` | 4 / 4 | 80 | 368 | 1.5 $\mu\text{s}$ | **YES** |
| | | Policy C (`PARTIAL_PREFIX`) | `COMPATIBLE_PREFIX_VALID` | 4 / 4 | 80 | 368 | 1.6 $\mu\text{s}$ | **YES** |
| **2: Early Conflict ($k=0$)** | Fault Injection (Synthetic) | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 436 | 724 | 2.1 $\mu\text{s}$ | NO* |
| | | Policy B (`WHOLE_SELECTIVE`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 436 | 724 | 1.4 $\mu\text{s}$ | NO* |
| | | Policy C (`PARTIAL_PREFIX`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 436 | 724 | 0.6 $\mu\text{s}$ | NO* |
| **3: Middle Conflict ($k=2$)** | Fault Injection (Synthetic) | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 397 | 685 | 5.0 $\mu\text{s}$ | NO* |
| | | Policy B (`WHOLE_SELECTIVE`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 397 | 685 | 1.1 $\mu\text{s}$ | NO* |
| | | **Policy C (`PARTIAL_PREFIX`)** | `COMPATIBLE_PARTIAL_PREFIX` | **2 / 4** | **307** | **595** | **1.2 $\mu\text{s}$** | NO* |
| **4: Late Conflict ($k=3$)** | Valid External Update | Policy A (`DISCARD_ON_CHANGE`) | `DISCARD_POLICY_DISABLED` | 0 / 4 | 378 | 666 | 1.4 $\mu\text{s}$ | **YES** |
| | | Policy B (`WHOLE_SELECTIVE`) | `DISCARD_VERTEX_CONFLICT` | 0 / 4 | 378 | 666 | 1.0 $\mu\text{s}$ | **YES** |
| | | **Policy C (`PARTIAL_PREFIX`)** | `COMPATIBLE_PARTIAL_PREFIX` | **3 / 4** | **265** | **553** | **0.9 $\mu\text{s}$** | **YES** |

---

### 4.3 Analysis of Diagnostic Findings
1. **Matched Policy Comparisons (Avoiding Invalid Monotonicity Claims):**
   - We do *not* claim that expansion savings scale monotonically across conditions, because Conditions 2, 3, and 4 inject different perturbations on different agents, creating distinct suffix subproblems with different intrinsic search difficulty.
   - Instead, we report policy comparisons strictly within each matched condition:
     - **In Condition 3 (Middle Conflict, $k=2$):** Policy C salvages 2 paths ($P_0, P_1$) and routes only agents 2, 3, 4. On `random-32-32-20`, resumption requires 432 expansions compared to 466 expansions for fresh restart under Policies A & B (a saving of 34 expansions). On `warehouse-10-20-10-2-1`, resumption requires 307 expansions compared to 397 for Policies A & B (a saving of 90 expansions).
     - **In Condition 4 (Late Conflict, $k=3$):** Policy C salvages 3 paths ($P_0, P_1, P_2$) and routes only agents 3, 4. On `random-32-32-20`, resumption requires 389 expansions compared to 455 expansions for Policies A & B (a saving of 66 expansions). On `warehouse-10-20-10-2-1`, resumption requires 265 expansions compared to 378 for Policies A & B (a saving of 113 expansions).
2. **Support for Safe-Recovery Performance Claims:**
   - Safe-recovery performance claims are supported exclusively by the applicable, independently validated conditions (Condition 0, Condition 1, and Condition 4), where external trajectories were fully legal and `rbl::checkPlan` verified the final solution as 100% collision-free.
3. **Microsecond-Scale Overhead:**
   - Across both topologies and all 5 conditions, validation latency ranges between $0.2\ \mu\text{s}$ and $5.0\ \mu\text{s}$. We accurately characterize this as **microsecond-scale** overhead. At $< 0.05\%$ of a 10 ms budget slice, it represents no practical runtime bottleneck.

---

## 5. Reconciling Policies, Manifest Dimensions, and Data Exposure

### 5.1 Policy Nomenclature Alignment
In earlier drafts and helper scripts, Policy A was inconsistently labeled `Policy_A_RestartOnly` in tables and `discard_on_change` in command flags.
- **Audited Configuration:** Policy A was executed with `--policy SCORE_BASED_ADAPTIVE --validation-policy discard_on_change`.
- **Behavioral Confirmation:** As verified in Section 4, `DISCARD_ON_CHANGE` resumes checkpoints if the incumbent version is unchanged, and discards the checkpoint only when the incumbent version changes. It is *not* a pure restart baseline that refuses to checkpoint ($K=0$).
- **Consistent Nomenclature:** All comparisons are formally labeled `DISCARD_ON_CHANGE` (Policy A), `WHOLE_CHECKPOINT_SELECTIVE` (Policy B), and `PARTIAL_PREFIX_RECOVERY` (Policy C).

### 5.2 Correct Manifest Dimensions: 72 Runs
The exact experimental matrix for RouteBudget Lab's natural evaluation comprises:
$$\text{Maps (2)} \times \text{Scenarios (3)} \times \text{Agent Scales (1)} \times \text{Budgets (2)} \times \text{Seeds (2)} \times \text{Policies (3)} = 72\ \text{runs}$$
- **Clarification:** The earlier review claiming 144 runs was an arithmetic error that mistakenly assumed 2 agent scales were prespecified. The actual prespecified matrix evaluated 1 scale (50 agents), yielding exactly 72 runs ($N=24$ paired conditions across 3 policies).
- **Accounting:** All 72 runs are fully accounted for in `results/partial_recovery_evaluation/`. Zero crashes, zero timeouts, zero missing runs, and 100% independent validation pass rate (72/72 valid).

### 5.3 Data Exposure Audit
We audited prior exposure of the benchmark instances used in the natural evaluation:
- Maps: `random-64-64-10.map` and `warehouse-10-20-10-2-2.map`.
- Scenarios: Scenarios 1, 2, and 3 from the Moving AI benchmark suite.
- **Audit Result:** During Phase 5 progress estimator fitting (`src/profiler/collect_progress_traces.cpp`, lines 117–118), Scenario 1 on both maps was utilized to collect offline feature traces.
- **Academic Disclosure:** Scenarios 1 on both maps represent **development / validation data**, not untouched confirmatory data. Scenarios 2 and 3 represent unexposed validation splits. All 72 runs are reported as validation evidence.

---

## 6. Empirical Results on Natural Benchmarks

### 6.1 Statistical Summary (72 Runs, $N=24$ Paired Conditions)

```
================================================================================
ROUTEBUDGET LAB - PARTIAL-PREFIX RECOVERY NATURAL BENCHMARK AUDIT
Total Evaluated Runs: 72 | Paired Conditions per Policy: 24
Independent Validation Pass Rate: 72/72 (100.0%)
================================================================================

Policy                     | Mean Final Cost | Mean Impr | Repeated Work Ratio | Natural Partial Rec
----------------------------------------------------------------------------------------------------
Policy A (DISCARD_ON_CHANGE) | 3584.33       | 110.33    | 57.03 %             | 0
Policy B (WHOLE_SELECTIVE)   | 3584.33       | 110.33    | 50.64 %             | 0
Policy C (PARTIAL_PREFIX)    | 3584.33       | 110.33    | 54.10 %             | 0
```

### 6.2 Breakdown by Map and Search Budget

```
Map: random-64-64-10.map (50 agents)
Budget   | Policy                       | Mean Cost  | Mean Impr  | Rep Ratio  | Partial Rec
--------------------------------------------------------------------------------------------
1.0s     | Policy A (DISCARD_ON_CHANGE) | 2120.00    | 67.33      | 49.00 %    | 0
1.0s     | Policy B (WHOLE_SELECTIVE)   | 2120.00    | 67.33      | 27.90 %    | 0
1.0s     | Policy C (PARTIAL_PREFIX)    | 2120.00    | 67.33      | 35.73 %    | 0
5.0s     | Policy A (DISCARD_ON_CHANGE) | 2120.00    | 67.33      | 30.74 %    | 0
5.0s     | Policy B (WHOLE_SELECTIVE)   | 2120.00    | 67.33      | 28.82 %    | 0
5.0s     | Policy C (PARTIAL_PREFIX)    | 2120.00    | 67.33      | 35.33 %    | 0

Map: warehouse-10-20-10-2-2.map (50 agents)
Budget   | Policy                       | Mean Cost  | Mean Impr  | Rep Ratio  | Partial Rec
--------------------------------------------------------------------------------------------
1.0s     | Policy A (DISCARD_ON_CHANGE) | 5057.00    | 145.00     | 75.80 %    | 0
1.0s     | Policy B (WHOLE_SELECTIVE)   | 5057.00    | 145.00     | 73.86 %    | 0
1.0s     | Policy C (PARTIAL_PREFIX)    | 5057.00    | 145.00     | 74.12 %    | 0
5.0s     | Policy A (DISCARD_ON_CHANGE) | 5040.33    | 161.67     | 72.59 %    | 0
5.0s     | Policy B (WHOLE_SELECTIVE)   | 5040.33    | 161.67     | 71.99 %    | 0
5.0s     | Policy C (PARTIAL_PREFIX)    | 5040.33    | 161.67     | 71.20 %    | 0
```

### 6.3 Honest Interpretation of Natural Results
1. **Zero Natural Partial Recoveries:** Across all 72 natural evaluation runs, the number of partial recovery events was **exactly 0**.
2. **Mechanistic Explanation:** In single-threaded, sequential MAPF-LNS:
   - Checkpoints are created only when a search slice expires mid-repair.
   - For an external incumbent update to create a partial conflict on a suspended checkpoint, another neighborhood repair must be selected, solved, accepted into the incumbent, and its newly routed paths must intersect agents $k, \dots, m-1$ while completely avoiding agents $0, \dots, k-1$.
   - Because neighborhood sizes in standard LNS are compact ($|\mathcal{N}| \in [2, 8]$) and LNS iteration acceptance rates are low (~1.5%), the probability of this specific geometric intersection occurring before the checkpoint is either resumed or expired is vanishingly small.
3. **Empirical Reality:** The practical benefit of partial-prefix recovery was **not exercised** in standard sequential MAPF-LNS runs.
4. **Runtime Overhead:** Profiling confirms that microsecond-scale prefix validation ($0.2 - 5.0\ \mu\text{s}$) consumes less than $0.05\%$ of active search time.

---

## 7. Reconciling Research Questions & Identity

### 7.1 Submitted Proposal Research Questions (Authoritative)
We inspected the original submitted Project Proposal (`RouteBudget_Lab_PP_Formative_Report_Final_DisasGamage_20231323:w2119964.pdf`, extracted in `docs/source_text/report.txt`, lines 968–1004). The formal question statements submitted are quoted verbatim below:

> **RQ1 (Checkpoint Feasibility & Equivalence):**  
> *"Can a low-level MAPF-LNS repair search be suspended at an arbitrary cutoff and resumed under unchanged external constraints to produce valid, collision-free solutions consistent with uninterrupted execution, and what memory and latency overheads are incurred?"*

> **RQ2 (Constraint Validation Dynamics):**  
> *"What fraction of saved repair checkpoints remain compatible with evolving external agent reservations across subsequent LNS iterations, and does conservative constraint validation prevent invalid solution commits compared with unvalidated reuse?"*

> **RQ3 (Resume-or-Start Controller Utility):**  
> *"Does an overhead-aware resume-or-start heuristic controller informed by lightweight search-progress features achieve lower deadline solution cost (Sum of Costs / Total Delay) than restart-only LNS, fixed round-robin resumption, and published adaptive baselines within identical wall-clock deadlines?"*

### 7.2 Requirements Traceability & Discrepancy Analysis
- **Requirements Document Alignment:** `docs/requirements-traceability.md` (lines 35–37) records these exact proposal statements.
- **Identified Discrepancy:** In interim study drafts, the research questions were rephrased into alternative formulations (e.g. focusing on score-based budget allocation and general runtime overhead).
- **Resolution:** There is no documented amendment or record of supervisor approval authorizing changes to the proposal's approved research questions. We strictly preserve the submitted proposal wording as the authoritative standard.

### 7.3 Separate Description of System Components (No Breakthrough Claims)
Neither the original controller nor the partial-recovery extension should be presented as a demonstrated performance breakthrough:

1. **Original Controller Results (Phase 7 Evaluation, 768 Runs):**
   - *Protocol A (Refinement Only):* FIFO resumption achieved an average improvement of +13.36 points across the 96 evaluated conditions. However, this gain was heavily concentrated on a single map (`ost003d`, +71.46 points). When `ost003d` is omitted, the mean difference drops to **-6.00 points** (a net loss). Clustered bootstrap resampling at the map level yields a 95% confidence interval spanning zero (`[-9.30, +52.15]`). Adaptive slicing and score-based feature adaptation showed negligible or inconclusive differences (-16.97 and -2.49 points).
   - *Protocol B (End-to-End):* Published BALANCE achieved lower final costs (-232.65 points) via monolithic uninstrumented search, but violated deadlines on 79.2% of runs (overshooting by up to 5.65 ms). In contrast, RBL achieved 0.00 $\mu$s mean overshoot (100.0% compliance within $\le 1.0\ \mu$s tolerance).
   - *Assessment:* This demonstrates rigorous wall-clock deadline discipline and clarifies the plateauing yield of LNS repair, but does *not* constitute a generalized cost-reduction breakthrough over unconstrained baselines.
2. **Partial-Prefix Recovery Extension (Supporting Study):**
   - In controlled fault-injection diagnostics, the mechanism successfully salvages collision-free sub-prefixes, reducing low-level expansions for the repaired suffix compared to full discard.
   - However, in 72 unconstrained sequential benchmark runs, exactly 0 natural partial recoveries occurred.
   - *Assessment:* This establishes a sound architectural diagnostic and theoretical boundary condition, but does *not* constitute an empirical performance breakthrough for sequential MAPF-LNS.

---

## 8. Academic Evaluation & Clear Conceptual Separation

We remove previously stated informal scores, percentage publishability estimates (e.g. "85% as an ablation section"), and unsupported grade classifications (e.g. "Distinction / First Class").

### 8.1 Conceptual Separation of Evaluative Judgments
To maintain academic rigor, four distinct judgments must be decoupled:
1. **Correctness $\ne$ Originality:**
   - The partial-recovery implementation compiles cleanly, passes 100% of unit tests (10/10 in `partial_prefix_recovery_tests`), passes 8/8 CTest suites, and passes independent whole-plan validation (`rbl::checkPlan`).
   - However, passing verification tests establishes internal correctness; it does not establish originality. Prioritized planning (Silver, 2005) and dynamic constraint repair are well-established paradigms.
2. **Correctness $\ne$ Practical Usefulness:**
   - The mechanism functions as intended when partial conflicts occur.
   - However, observing 0 natural partial recoveries in 72 benchmark conditions means that empirical evidence of practical usefulness in sequential single-threaded MAPF-LNS is unexercised. This limits demonstrated practical usefulness in the evaluated setting, without proving universal uselessness in other architectures (such as asynchronous multi-threaded LNS).
3. **Practical Usefulness $\ne$ Publication Readiness:**
   - Publication readiness requires demonstrating affirmative empirical advantages on representative, unconstrained workloads. Because natural sequential LNS does not exercise the mechanism, the extension is not ready for standalone conference publication, though it serves as a valuable negative result and diagnostic characterization.

---

### 8.2 Strict Gate Assessment (Out of 10)

| Criterion | Score (/10) | Detailed Evaluation & Evidence |
| :--- | :---: | :--- |
| **Suitability** | **8 / 10** | Directly addresses anytime MAPF-LNS checkpoint preservation under dynamic incumbent updates. Fits the core scope of RouteBudget Lab. |
| **Novelty** | **5 / 10** | While prefix truncation with deterministic seed recovery in prioritized planning is original in detail, prioritized planning reuse and dynamic constraint repair are well-established. Zero empirical occurrences in sequential LNS limits its novelty as an algorithmic contribution. |
| **Feasibility** | **9 / 10** | Fully implemented in C++17, builds cleanly, executes with microsecond-scale latency ($0.2 - 5.0\ \mu\text{s}$), and passes 100% of unit tests and independent plan verification. |
| **Methodological Soundness** | **9 / 10** | Rigorous distinction between synthetic fault-injection diagnostics and natural benchmark evidence; multi-map evaluation; transparent reporting of negative findings and prior data exposure. |

> **Gate Decision:** **REVISE** (Fails Gate Rule: Novelty score of 5 does not satisfy the required threshold of $\ge 7$ for a standalone primary contribution).

---

## 9. Conclusion & Research Artifact Disposition

### 9.1 Summary of Corrected Contributions
1. **Theoretical Clarity:** Disproved the claim that collision-free retained paths remain optimal; provided an explicit 2D grid counterexample; formulated the Retained-Prefix Validity Proposition under 7 explicit assumptions; replaced path-derivative notation with an algorithmic dependency argument.
2. **Strengthened Multi-Map Diagnostic:** Evaluated a 5-agent neighborhood ($m=5, r_{done}=4$) on both `random-32-32-20` and `warehouse-10-20-10-2-1`; separated valid updates from fault injections; reported policy comparisons strictly within matched conditions; used microsecond-scale latency descriptors.
3. **Reconciled Manifest & Research Identity:** Retained the correct 72-run manifest accounting; restored the approved proposal research questions (RQ1–RQ3); disclosed prior Phase 5 data exposure on Scenario 1; separated original controller findings from the partial-recovery study without claiming performance breakthroughs.

### 9.2 Disposition
We formally classify partial-prefix recovery as a **supporting negative-result and diagnostic extension** within branch `research/partial-prefix-recovery`. The main repository contribution remains the Phase 7 score-based adaptive search budget allocation framework.
