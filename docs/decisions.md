# Current research and engineering decisions — 2026-11-02

This corrected decision record supersedes audited completion claims. The historical
chronology remains in Git at fb6ba907e4ccba9ec882537c2dce2be970c91781; original experiment
and audit artifacts remain on disk. See correction_ledger.md for explicit corrections.

## D1 — Primary contribution

Use the title “Constraint-Validated Resumption of Interrupted Repairs for Deadline-Aware
Multi-Agent Path Finding.” The primary mechanism is completed-prefix checkpointing,
fresh compatibility validation and safe resumption. Selective recovery is an evaluated
extension. Preserve negative and neutral findings; do not recenter the project on recovery.

## D2 — Authoritative objective

Arrival cost excludes unnecessary waits after the final arrival at goal. All complete-plan
costs use checked int64 totals; published cost is the independently validated candidate
SoC. Search g-values and completed-agent prefix aggregates have separate meanings.
Version-only cache validity is insufficient; cached authoritative cost must agree too.
The independent CLI rejects reported cost that matches padded length but not arrival SoC.

## D3 — Publication and failure scope

Prepare complete paths, reservations, versions, cost/cache and publication record privately.
Use nonthrowing swaps and scalar writes in a single-threaded transaction boundary, with
rollback when the deadline crosses publication. Search reservation copies protect the
live incumbent during interrupted/failed repair. Checkpoint save copies precede replacement.
Do not describe this as concurrent atomic publication or claim whole-program exception safety.

## D4 — Selection

Compute checked U before dependency and replacement-pair analysis. Keep negative candidates
when U>0. Greedy seeds use descending positive individual gain with candidate-index ties;
closures include cycles/transitive dependencies. Reject incompatible closures, not all
input candidates. Exact Dinic applies only without incompatibilities; exhaustive small
oracles support conflicts. Greedy is not globally optimal.

## D5 — Triggers and deadline

Use only the actual natural capacity victim before overwrite, independent of incoming
prefix length. Do not manufacture benchmark pressure. Terminal order is FIFO, K≤2, with
fresh recomputation after each publication. One monotonic global refinement deadline
covers recovery work and cleanup. Publication timestamps—not wrapper return times—define
on-time quality. Serialization/output and parent-observed process elapsed time are separate.
The 13 ms reserve was frozen from exposed development stress measurements before the new
matched experiments; it is a heuristic rather than a scheduling guarantee.

## D6 — Default, protocol and inference

Retain RESUMPTION_NO_PA by default. Recovery remains optional unless broader frozen evidence
justifies a later change. Match all inputs/build/parameters; run all policies sequentially
in stored cyclic order. Preserve failed runs. Do not selectively rerun losers. Report
policy-minus-baseline deadline SoC, separate return cost, exact two-sided tests, Holm over
five extension comparisons, SD and SE. Repeated cells are not independent layouts and
non-significance is not equivalence. No untouched confirmation instances were opened.

## D7 — Primary historical evidence

Re-score saved historical plans instead of discarding their evidence: all 768 Phase 7 final
plans match authoritative costs. Reconstruct signs from raw costs. Historical final-plan
findings remain historical; runtime-changing fixes require new matched primary comparison.
The old estimator/scored/adaptive controller remains implemented and unit-tested, with its
performance claims restricted to its original binary/data until separately reevaluated.

## D8 — Reproducibility and disclosure

Record source/build/compiler/library/configuration/input hashes per raw run, exact order,
structured outcomes and independent validation. Hash after final serialization and verify.
Use the real ENABLE_SANITIZERS option and inspect instrumentation. Report observed runtime
diagnostics only. AI wrote substantial implementation, tests, analysis and documentation;
this is disclosed without claiming official academic approval or submission.
