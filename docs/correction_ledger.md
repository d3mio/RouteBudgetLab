# Correction ledger — audited implementation repair

Starting evidence: `audit_20261101_verification_1602/AUDIT_REPORT.md` at
fb6ba907e4ccba9ec882537c2dce2be970c91781. Original results and audit files are unchanged.
Current report tables are generated from final_analysis.json; historical documents are
available at that audited Git commit.

| Audit issue | Correction and evidence |
|---|---|
| Stored 220 vs authoritative 218; then 218 vs 216 | Reproduced in before_cost_probe.log; shared arrival definition, checked totals, direct candidate SoC publication; padded initial/recovery/external/normal tests |
| Sequential live mutations called atomic | Full staged reservations/paths/versions/cache; nonthrowing swaps, explicit single-thread confinement and rollback at late boundary; allocation/cancellation injections |
| Version-only validation cache | Cache binds version and authoritative cost; corrupted stored-cost regression rejects cache hit |
| Pair conflicts reject all candidates | Incompatibility edges constrain greedy closures; real crossing-map fixture selects a safe improving subset |
| Incoming zero-prefix can bypass profitable victim | Production hook independent of incoming prefix; K=1/2 success, zero-bound, timeout and invalid-victim tests |
| Inadequate deadline evidence | Stage injection, fresh recomputation after commits and actual publication timestamps; cooperative deadline limits explicitly retained |
| Descending-gain terminal order | FIFO ascending checkpoint ID, at most K=2 eligible checkpoints |
| Unmeasured reserve | Exposed development stress calls; frozen 13 ms heuristic before matched runs; no hard guarantee |
| Unsupported ENABLE_SANITIZERS | Actual compile and link option for Clang/GCC, all targets; unsupported compiler fails; flags/symbols and runtime tests recorded |
| Wilcoxon small-sample p=1 fallback | Exact random-sign DP with doubled average ranks; historical four losses reproduce .125; Holm .625 across five comparisons |
| Correlated cells labeled independent | Eight agent-count instances, four layouts, two maps; seeds/budgets/nested populations correlated |
| Stale raw JSON manifests | Hash after enrichment/serialization; verify completed manifest; reject existing output directory |
| Historical provenance overclaimed | Current raw source/binary/compiler/config/input metadata recorded; missing historical identity remains unavailable |
| All 192 zero overshoot | Historical audit establishes four refinement overruns; corrected measured counts generated from raw runs |
| Exact nanosecond latency / comparable 95.6% reduction | Removed: individually clocked tiny operations and differing workloads/selectors do not support those claims |
| Completely eliminated overhead | Extraction, validation, selection, copying and bookkeeping still consume budget; report end-to-end and stage boundaries |
| “No evictions” | Separate natural capacity evictions, capacity-recovery attempts, stagnation discards and invalid removals |
| Recovery gain equated with policy advantage | Within-run recovered gain is separate from policy-minus-resumption deadline cost |
| Broad safety/novelty/submission claims | Use discrete spatio-temporal collision validity; no hard real-time, first-ever, novelty-score, publication or official-submission claim |
| Historical sign convention | Phase 7 original mean was control−treatment; reanalysis uses treatment−control and retains W/T/L from actual costs |
| Independent validator accepted padded-length equality | CLI fails authoritative reported-cost mismatch; Python wrapper no longer accepts length-only equality |
| Sanitizer latency assertion | Original failure retained; latency reported, but fixed machine-dependent threshold replaced with finite/nonnegative assertion |

The repair log records commands and failures. Logical local commits separate cost/transaction,
recovery/deadline, tests/build and runner/analysis work. No pushes or merges occurred.
