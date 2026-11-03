# Constraint-validated resumption and optional selective recovery

Current specification, corrected 2026-11-02. The technical report and frozen protocol
contain the generated empirical results. Historical versions remain at audited commit
fb6ba907e4ccba9ec882537c2dce2be970c91781.

## Pipeline

Benchmark maps/scenarios → parsing and scenario validation → common complete initial
plan → bounded-slice anytime LNS with Space–Time A* → completed-prefix checkpoint →
current-incumbent compatibility validation → safe resumption → optional recovery at an
already-selected eviction victim or near the global deadline → checked gain bound →
dependency/conflict analysis → greedy closure selection → independent full-plan validation
→ staged strictly improving publication → complete discrete collision-valid final plan →
matched deadline-quality, provenance and sanitizer evaluation.

## State and cost contract

A checkpoint holds a neighborhood, priority order, completed-agent prefix, incumbent and
per-agent version context, RNG restart information and accumulated work. It does not save
the unfinished agent's live search frontier. Prefix routes are complete individual routes;
`prefix_accumulated_cost` sums their arrival costs. A search node's `g` is the unfinished
agent's accumulated search cost and is never a complete-plan cost.

For a complete route P ending at goal g, arrival(P) is one plus the last index with
P[t] != g, or zero if there is no such index. This also handles starting at the goal,
leaving it and returning. Repeated terminal goal waits do not add cost. Authoritative SoC
is the checked signed-64-bit sum. Unsupported timestep ranges and overflow fail closed.
The standalone validator shares this scalar contract but has separate parsing and
collision checks. A reported cost matching only padded length is rejected.

## Compatibility and primary controller

Before resumption, current map/problem identity, neighborhood and ordering, completed
prefix validity and external trajectory compatibility must hold. Changed incumbents do
not automatically invalidate all useful work, but stale/incompatible prefixes cannot be
blindly reused. The corrected development controller chooses FIFO compatible resumption
or fresh repair. Scored/adaptive selection and the remaining-work estimator remain
separate historical controller components; these pilots do not claim to reevaluate them.

## Filter and subset selection

Eligible agents have exactly one complete valid replacement. Reject duplicates.
Recompute w_i = arrival(P_i) − arrival(Q_i) against the current incumbent and
U = Σ max(w_i,0), with checked subtraction and addition. Extraction/route validation is
separate from this scalar bound. U=0 implies Σ_{i∈S} w_i ≤ U=0 for every subset S;
feasibility restrictions cannot increase that upper bound. Overflow is not a no-gain proof.
Negative weights remain eligible when U>0.

Build i→j when Q_i conflicts with old P_j. If j has no eligible replacement, forbid i.
Build an incompatibility pair for conflicting Q_i,Q_j. These pairs constrain selections;
they do not invalidate the whole input. Positive seeds are ordered by descending weight,
then input candidate index. Form each seed's complete transitive dependency closure,
including cycles and required negative weights. Reject closures with forbidden/missing
members, internal incompatibilities or incompatibilities with the selected set. Add a
closure only for strictly positive checked marginal gain. Independently validate the
selected mixed complete plan. Greedy is not globally optimal.

Dinic maximum-weight closure applies only when there are no pairwise incompatibilities.
The exhaustive small graph oracle handles dependencies and incompatibilities. A separate
path-mixture oracle validates complete mixed plans without using the dependency graph.

## Transaction boundary

Search uses private reservation copies. Publication prepares complete candidate paths,
authoritative SoC, a complete reservation table, versions, cache metadata and publication
record before final version/deadline checks. Nonthrowing swaps/scalar stores publish the
state in this single-threaded engine. Postpublication deadline failure swaps it back and
removes the publication record. The stored cost is the validated candidate SoC, never
an unchecked subtraction from a stale total. Cache hits bind version and authoritative cost.
Checkpoint saves stage copies before replacing slots. Diagnostic trajectory/cost setters
are explicitly testing APIs and are excluded from production invariants.

## Trigger rules and timing

Natural recovery uses exactly the store's chosen oldest capacity victim, before overwrite.
Incoming zero-prefix checkpoints do not bypass that hook. Recovery failure/timeout keeps
the incumbent and continues the original eviction/save. A guard prevents recursive recovery.
Capacity eviction is distinct from stagnation discard, invalid-checkpoint removal and
normal consumption through resumption.

Terminal recovery visits at most two eligible suspended checkpoints in FIFO ID order.
Fresh extraction/gains/dependencies are recomputed for each against any newly published
incumbent. Terminal reserve control has the same cutoff and performs no recovery.
The frozen development reserve is 13 ms, empirically selected from expensive exposed
fixtures; it is not a hard bound. Ordinary nonterminal policies retain the 0.5 ms cutoff.

One steady-clock global refinement deadline governs extraction, dependency/conflict scans,
selection, validation and publication. Cooperative checks and stage injection support
boundary tests. Required recovery cleanup is included in refinement elapsed time. Output
serialization and parent-observed process elapsed time are recorded separately. Allocations
and scheduling remain nonpreemptive; overshoot is measured, never assumed zero.

## Evidence boundaries

Default is RESUMPTION_NO_PA. The corrected pilot tests whether recovery improves deadline
SoC beyond that default; cumulative within-run recovered gain is a different quantity.
No original dataset, manufactured benchmark eviction, parallel worker or learned component
was introduced to obtain favorable results. See the requirements matrix for actual assertions.
