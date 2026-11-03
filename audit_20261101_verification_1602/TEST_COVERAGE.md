# Executed tests and assertion-level coverage

Audited HEAD: fb6ba907e4ccba9ec882537c2dce2be970c91781. Paths below are relative to repository root. `S` means `tests/selective_recovery_tests.cpp`; `P` means `tests/partial_acceptance_tests.cpp`.

Fresh Release: **10/10 CTest entries PASS**, 1.95 s. Fresh ASan/UBSan: **10/10 PASS**, 3.11 s. See `test-release.log`, `test-asan.log`, full CTest logs in build directories, and `commands.jsonl`. Selective suite executes 22 functions, but those are not equivalent to 22 fully verified requirements.

Release uses `-O3 -DNDEBUG`; custom ASSERT/CHECK macros remain active. A separate audit copy of the actual ASSERT_TRUE macro fails intentionally under those flags (exit 17, `assertion-expected-failure.log`). Standard `assert()` setup/engine checks remain disabled in Release; this is not a claim that every C assertion is enabled.

Sanitizer build explicitly uses `-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1`, including engine and upstream compilation and executable linking. Flags and sanitizer symbol evidence are in `build-flags.log`; compile_commands.json files preserve every compile command. `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. No runtime sanitizer diagnostics occurred. This does not establish absence of leaks (leak checking disabled on this platform) or unexecuted bugs. Build logs contain compiler warnings, so “zero diagnostics” must be restricted to runtime sanitizers.

| Required category | Actual assertions and limits | Assessment |
|---|---|---|
| Empty / zero / negative filtering | S:117 tests negative waits, NO_GAIN, U=0, graph/selection time zero; S:363 tests equal-cost U=0; P:162 empty set | VERIFIED these cases, not all by S test1 alone |
| Positive candidate / mixed-sign dependency | S:147 synthetic graph +5 requiring -2, selects both for +3; P:327 richer cases | VERIFIED graph selection; S test2 does not execute extraction/filter |
| Transitive dependencies / cycles | S:182 positive and nonpositive two-cycles; S:756 +5,-2,-1 chain; P:286 transitive forbidden propagation | VERIFIED stated graph cases; cycles are not inherently infeasible |
| Missing replacement | S:272 begins with manually forbidden node; P:255 exercises missing dependency graph construction | VERIFIED across suites; S alone does not test dependency detection |
| Replacement-to-replacement conflicts | S:242 assertion is conditional on a forced path still being grid-valid. Audit probe reports guard=0 on the actual fixture, so no assertion executes. P:573 tests duplicates, not a conflicting distinct-agent pair | CONTRADICTED claimed meaningful dedicated conflict test; add valid positive-gain colliding replacements |
| Natural eviction ordering / actual victim | S:300 manually calls hook then save with K=1, verifies recovery and overwrite; S:847 manually repeats zero-gain failure then save | PARTIAL: neither reaches production fresh-repair eviction hook; no K=2 oldest-ID distinction or timeout case |
| Stale incumbent | S:363 changes incumbent to remove gain; S:395 rejects DISCARD_ON_CHANGE and independently evaluates prefix; P:652 stale-version commit | VERIFIED these stale-cost/version cases |
| Stage-by-stage cancellation | S:433 and S:899 deadlines already in past at entry; P:674 injects step limits inside Dinic/greedy and separates slice/global budgets | PARTIAL: selector interruption covered; no extraction, graph, validation, or publication-stage injection |
| No-gain terminal recovery | S:457 two non-improving checkpoints, skip count 2, unchanged cost/version and validity | VERIFIED |
| First commit changes second checkpoint compatibility | S:496 builds two mutually delayable agents and commits both, checks total gain 4 | UNVERIFIED adverse compatibility change; compatible cascade is covered |
| Invalid routes / goals / agent IDs | S:571 wrong goal, duplicate/out-of-range IDs, overflow; P:602 invalid start/goal/obstacle; validator fixture suite invalid moves, missing agents, malformed paths | VERIFIED across suites, S test12 description is broader than its assertions |
| Persistent goal occupancy / edge swaps | S:616 pairwise trajectory checks; P:526,555 and independent validator fixtures | VERIFIED; S does not itself invoke full independent validator here |
| Arithmetic overflow | S:571 overflowing U; S:686 checked add/sub and finite U; P:674 INT64_MIN and capacity sum failures | VERIFIED checked scalar/closure cases; 32-bit incumbent cost and path-length narrowing are not covered |
| Rollback / versions / reservations / caches | S:713 checks cost/version; its `initial_solution` is a const reference to live storage, making path comparison tautological. P:763 copies paths and checks per-agent/global versions, valid/failed commit and resume lifecycle | PARTIAL: stronger P suite, but no reservation/cache snapshot or allocation-failure rollback proof; audit padded-goal probe finds real cost mismatch |
| Feature-disabled baseline | S:637 single controlled repair compares outcome/cost and zero filters; P:878 repeats controlled-work configurations | VERIFIED tested behavior; not proof of wall-clock identity |
| Deadline-boundary / late-result accounting | S:665 calls commit after deadline; S:899 entry-expired evaluation | UNVERIFIED expensive crossing-boundary behavior, commit-completion and return-time accounting; no measured overshoot assertion |
| Greedy / exact / exhaustive | S:756 one synthetic graph compared with graph oracle. P:950 independent path-mixture oracle, 126 deterministic/random cases, explicit external validation | VERIFIED tested cases; S graph oracle is not independent path validation, greedy need not equal exact universally |
| Multiple alternatives | S:806 duplicates rejected | VERIFIED reject-duplicates contract; does not solve alternatives |
| CLI / telemetry / malformed-run analysis | S:826 parsing and string mappings only | CONTRADICTED full category claim: no CLI process, telemetry sums, pairing, or malformed/missing-run tests |

Additional audit-only executable checks (`probe.cpp`, linked to unchanged engine, both configurations): a valid padded incumbent has stored cost 220 vs authoritative 218; after a valid gain-2 commit, stored cost 218 vs authoritative 216. Both executions exit 0 because the probe successfully reproduces the defect. No original tests or implementation were modified.

The matrix's source lines are often stale (for example S test1 is :117, not :70). It also describes cycle weights differently from the source. Keep assertions as the evidence, not function names, comments, or prior PASS summaries. Existing historical logs under `logs/audit_*_test.log` contain four-suite October 21 executions; they do not attest to the November 1 22-function completion suite.
