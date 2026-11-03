# Final correction audit — RouteBudget Lab

Status: **required development verification executed**. Final matched experiments,
selector replay, preservation/source checks, exact statistical reconstruction and
document-table verification passed. Package checksums accompany the final artifacts.
It is an assistant-performed completion audit, not a new independent human audit.

The original independent audits are preserved in their supplied directories. Current
claims are supported by executable assertions and recorded outputs; finite regression
coverage does not establish the absence of all bugs or hard real-time compliance.

## Identity and preservation

Started from `research/selective-recovery` at
`fb6ba907e4ccba9ec882537c2dce2be970c91781`. Repairs are on
`research/selective-recovery-corrected`. Final experiment source commit:
`ac01fb47445a1bc6e4522de549e11b48c26e83a5`. The final documentation commit is the
branch HEAD after packaging; use `git log` to resolve its identity. Existing `.DS_Store` changes and both untracked
audit directories remain outside repair commits. No destructive reset/clean, push, merge,
submission, publication or email was performed. Historical file hashes are retained in
`results/repair_20261101_corrected_01/preservation.json`.

## Correctness gates

| Gate | Evidence |
|---|---|
| Authoritative cost and overflow | cost_transaction_tests; padded initial/recovery/external/normal plans, cache mismatch, checked large total and narrowing |
| Transaction failure preservation | staged paths/reservations/versions/cost/cache; injected allocation and publication failures; deadline rollback |
| Bound and feasible selection | 64,000 graph property cases, crossing-route fixture, signed dependencies/cycles, explicit greedy-suboptimal example |
| Real eviction | K=1/2, actual oldest victim, incoming zero prefix, four outcomes, exactly one eviction/attempt |
| Deadline boundaries | eight injected phases; actual phase reached and no late trajectory publication |
| Adverse cascade | first FIFO commit changes second checkpoint compatibility; second evaluated afresh |
| Release | test-final-counters.log: 13/13 |
| Instrumented ASan/UBSan | test-asan-final-counters.log: 13/13; compile/link flags and symbols verified |
| Intentional NDEBUG failure | intentional-harness-failure.json: exit 17; expected-failure CTest entry |
| Validator and tool tests | 6 validator tests per build, 7 experiment/statistics tests; qualified logs |
| Historical primary plans | 768/768 independently valid and authoritative costs match |

No runtime sanitizer diagnostics were observed in these executed tests. Leak detection
was disabled for this macOS arm64 run; this is not a claim that leaks or unexecuted memory
errors do not exist. Compiler warnings and the earlier latency-test failure remain logged.

## Experimental and reporting gates

Final design: 192 selective runs and 64 fresh-only/resumption runs, executed sequentially
with frozen shared initial plans, seeds, slices, budgets, reserve and cyclic policy order.
Earlier `_01` and `_02` repetitions remain preserved and separately analyzed. Counter
corrections prompted full repetitions; no losing-policy-only reruns or reserve tuning.
No untouched confirmation instances were opened.

The preservation check verified 5,354 historical/audit files unchanged. All six full
pilot manifests (`_01`–`_03`, both designs) verify and analyses reproduce from raw files.
Offline replay captured 20 random-map and 72 warehouse snapshots; greedy matched the
independent oracle in all 92, with one positive warehouse opportunity (gain 2). This
does not establish greedy optimality; the explicit test counterexample is 5 versus 8.

The final machine-readable output is `results/repair_20261101_corrected_01/final_analysis.json`.
Report/presentation tables are generated together and checked against that output.
The verifier independently reconstructs analyses from raw run files, checks matched inputs,
validation logs, complete manifests, preserved historical hashes and source/binary identity.

## Explicit scope limits

- Deadline checks are cooperative. Refinement, output and return-boundary measurements
  are distinct; return-boundary timing excludes its own sidecar and runtime exit.
- General allocator/OS failures outside staged publication are not exhaustively injected.
- Greedy is not globally optimal. Dinic is valid only without replacement incompatibilities.
- Four reused layouts, seeds/budgets and nested agent sets limit independent inference.
  Non-significant differences are not equivalence, and within-run recovered gain is not
  evidence of superiority over resumption.
- Historical final-plan rescoring preserves final-cost findings, but cannot reconstruct
  missing historical publication timestamps or executable provenance. Scored/adaptive
  controller performance requires a separate corrected study.
- No automatic 21:00 continuation was registered; no scheduling tool was available.
- Untouched confirmation studies, exhaustive allocation-fault exploration and additional
  platforms are future work, not claimed completed verification.

See README.md for exact build/run commands, `requirements_to_tests_matrix.md` for assertion
coverage, and `repair_log.md` for source-change reasons and retained verification history.
