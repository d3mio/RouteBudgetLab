# Requirements to assertions matrix — corrected package

Evidence is the assertion, not the test-function count. Final executed logs are under
`results/repair_20261101_corrected_01/`; original independent audit coverage is preserved.
C = `tests/cost_transaction_tests.cpp`; R = `tests/recovery_correctness_tests.cpp`;
P = `tests/partial_acceptance_tests.cpp`; S = `tests/selective_recovery_tests.cpp`;
E = `tests/test_corrected_experiments.py`.

| Requirement | Concrete asserted evidence | Boundary |
|---|---|---|
| Terminal padding cost | C requires independently validated initial SoC equal stored cost, and gain exactly 2 despite four terminal waits | Complete routes on executable fixtures |
| Recovery cost publication | C requires recovered plan independently valid and stored SoC equal recomputed SoC | Does not rely on subtraction from stored total |
| External padded update | C appends seven goal waits and requires unchanged authoritative SoC | Worsening diagnostic API remains explicitly allowed when requested |
| Normal repair cost | C requires COMPLETED_ACCEPTED and matching independently recomputed SoC | One deterministic delayed-agent repair; E1 adds resumed parity |
| Large total / overflow / narrowing | C sums two INT32_MAX values plus 7; requires overflow exception for INT64_MAX+1 and unchanged conversion destination on narrowing failure | Uses scalar accumulation, not impossible multi-exabyte path allocation |
| Cache mismatch | C changes stored cost after cache population and requires validateIncumbent=false | Public paths are otherwise immutable |
| Publication rollback | C injects bad_alloc before four stages and rejection at five stages; compares paths, reservations/goals/makespan, agent/global versions, cost/cache and retained checkpoint identity/routes | Simulated allocation failure, not exhaustive OS allocator fault exploration |
| Deadline publication | R expires at Extraction, Dependency, Conflict, Selection, Validation, Preparation, BeforePublication and AfterPublication; requires no publication record and unchanged incumbent/caches/reservations | Cooperative stage injection, not an OS real-time proof |
| U=0 soundness | R enumerates 125 weight vectors × 64 dependency masks × 8 conflict masks; requires oracle gain≤U and zero oracle gain when U=0 | All 64,000 supported three-candidate graph cases |
| Conflicting positive replacements | R uses a real 3×3 crossing map; requires exactly one selected agent, gain 4 and validated mixed-plan cost 6 | Unconditional assertions replace reliance on S's formerly vacuous guard |
| Negative dependencies, transitive closure, cycles | R checks every selected dependency and exact selected-weight sum across exhaustive graphs; P/S include explicit signed chains and cycles | Greedy is feasible, not asserted globally optimal |
| Missing dependency | P's graph construction checks non-candidate conflicts become forbidden; R tests forbidden-free graph combinations separately | Distinguish graph construction from selection-only fixtures |
| Greedy versus oracle | R requires greedy gain≤exhaustive optimum, verifies selected feasibility and repeat-call determinism; explicit 5-versus-8 counterexample; P independently validates path mixtures | Equal gains on some data do not establish global optimality |
| Offline snapshot replay | run_offline_verification asserts nonempty capture, k≤8, oracle executed, greedy≤oracle, exact=oracle and validated exact mixtures for every snapshot | Natural jointly planned prefixes; incompatible replacements are exercised by R instead |
| Exact selector applicability | R requires Dinic INVALID_INPUT when pairwise conflicts exist | Dinic retained only for closure-only cases |
| Duplicate alternatives | P/S and engine extraction require duplicate-agent input rejection | Multiple alternatives are unsupported |
| Production capacity eviction | R calls attemptFreshRepair with K=1/2 and zero-prefix incoming checkpoint; requires actual oldest victim replacement, newer slot retained, exactly one attempt and one store eviction | No artificial eviction added to benchmark controller |
| Eviction success/U=0/timeout/invalid route | R repeats production hook for all four outcomes; requires gain only on success and original save in every case | Guard and single-attempt assertion cover non-recursive hook behavior |
| Adverse terminal cascade | R FIFO first commit succeeds; second is re-evaluated and NO_GAIN; exactly two examined/one committed, final SoC 8 | Second replacement becomes incompatible with newly committed route |
| Discrete collision rules | Validator suite and P cover malformed paths, goals, moves, vertex/swap conflicts and persistent occupancy | No continuous dynamics or robot safety claim |
| Resumption/compatibility | E1 requires uninterrupted/resumed path-by-path parity and repeated-work accounting; E4 checks compatible retention/stale rejection | Deterministic mechanism evidence, not all-instance performance proof |
| Feature-disabled default | P compares a controlled 25-step sequence and S compares a controlled repair with disabled recovery | Does not imply identical wall-clock scheduling |
| Actual CLI | E launches profiler with unknown policy and requires exit 2; six-policy smoke and full runs execute real policy/flag commands | Terminal reserve control is a baseline policy plus CLI control flag |
| Telemetry reconciliation | E rejects attempt mismatch and late return-cost change; runner checks every trigger count, commit count, sign count and on-time commit for every raw run | Mutually exclusive status categories; partial-stage elapsed times retained on cancellation; R removalAccounting asserts exclusive lifecycle categories |
| Missing/malformed experiments | E requires rejection of empty, incomplete, duplicate, unvalidated and invalid-cost input | Analysis fails closed rather than filling missing cells |
| Manifest serialization | E hashes enriched output, verifies it, mutates bytes and requires mismatch rejection; runner verifies completed manifests | Files added after manifest creation require a new manifest |
| Exact statistics | E verifies four-loss sign/rank .125, tied-magnitude exact cases, Holm .625 and separate return-cost ties | Descriptive inference for correlated development cells |
| Release assertions | C --intentional-failure returns 17 under -DNDEBUG; CTest WILL_FAIL entry also checks nonzero exit | Does not reactivate standard C assert in all legacy code |
| Sanitizers | Compilation/link flag audit across 59 units and linked symbols; 13/13 final sanitizer CTest pass | No runtime sanitizer diagnostics observed; unexecuted bugs/leaks not ruled out |
| Historical primary evidence | 768 independent CLI validations and authoritative cost matches, separate corrected final-cost analysis | Exact historical publication timestamps/provenance remain unavailable |

The original S replacement-conflict function is not counted as evidence for valid conflicting
alternatives; the independent R production fixture supplies that assertion coverage.
Finite examples and fault injection are not a formal proof over all states or all allocation
failures. Partial stage durations are retained on cancellation. Zero duration for a stage never
entered is distinct from zero total overhead; full refinement elapsed time includes
bookkeeping outside the separately timed stage loops. These are explicit coverage and
measurement limits, not a blanket “all requirements exhaustively tested” claim.
