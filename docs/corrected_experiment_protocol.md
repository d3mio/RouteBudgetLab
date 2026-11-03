# Frozen corrected development protocol — 2026-11-02

Primary question: can constraint-validated interrupted repairs resume safely, and does
optional recovery improve the complete valid solution available at the shared deadline?
Resumption without partial acceptance remains the default. No confirmation data are used.

The selective design is the existing 32 development cells: two exposed maps, two scenarios,
50/100 agents, seeds 0/1, budgets 1/5 seconds; six policies in the configuration
`configs/2026-11-02_corrected/pilot.json`. All policies share stored initial paths, build,
K=2 (except the separate restart diagnostic), k=8, 5 ms slices and 500-expansion cap.
Initial plan loading and validation are outside refinement time for all policies.
Each cell runs all six policies sequentially in cyclic order stored before execution.
Rotations 0 and 1 occur six times, rotations 2–5 five times: approximate balance only.
Background OS load is not eliminated. No selective reruns or parameter tuning after results.

A development stress measurement used 100-agent fixtures on both exposed maps, eight
completed candidates and 30 repeated recovery calls per map. Valid start delays were
introduced solely to exercise costly improving recovery, not to create benchmark wins.
Maximum measured recovery time was 6.042625 ms (warehouse); random maximum 2.226209 ms.
Frozen reserve: ceil(2 × maximum observed time in milliseconds) = 13 ms for at most two
FIFO checkpoints. This empirical heuristic is not a worst-case or real-time guarantee.
Control uses the same normal-search cutoff with recovery disabled. Ordinary resumption
and nonterminal policies retain their 0.5 ms normal-search safety reserve.

Primary outcome: authoritative SoC at actual on-time publication. delta = policy minus
resumption; negative is better. Return cost is separate. Report W/T/L, mean all/non-ties,
sample SD and standard error, exact two-sided sign and signed-rank tests (zeros removed,
average ranks for equal magnitudes), Holm correction across the five comparisons.
These 32 cells reuse eight agent-count instances and four layouts; inference is descriptive.
Non-significance is not equivalence. No one-sided confirmatory claims are predeclared.

Report eviction/terminal funnels, commits, gains, stage overhead, iterations/expansions,
overshoot and failures. Do not interpret cumulative recovered gain as advantage over
resumption. Every final path is independently validated with authoritative cost matching.
All stdout/stderr, failures, configuration/order/source/binary/input hashes are retained.
Manifests are generated after serialization and verified. Existing directories are rejected.

Mandatory code/test gates must pass before a matched pilot. A --smoke-cells run is a
pipeline check, never the final pilot. No benchmark results may be used to adjust reserve.

A separate primary-method development comparison uses the same 32 cells and binary:
`DISCARD_NO_PA` (fresh-only K=0) versus `RESUMPTION_NO_PA` (K=2). The two-policy
cyclic order is exactly balanced over 32 cells. Both use the 0.5 ms normal-search cutoff,
no recovery, and the same shared initial paths. This comparison is necessary because
staged publication changes runtime behavior; historical and corrected binaries will
not be mixed in pairs. Primary delta is resumption minus restart-only deadline cost.
The one comparison uses descriptive two-sided exact tests with no five-policy correction.
Configuration: `configs/2026-11-02_corrected/primary.json`. No tuning after execution.

## Telemetry completion repetition (before examining comparative quality)
The first complete corrected selective (192) and primary (64) runs are preserved in
`results/2026-11-02_corrected_*_pilot_01`. Closing review found incomplete removal
classification and early-cancellation stage timing. The final `_02` repetition runs
both entire designs after those corrections, with unchanged data, reserve and parameters.
It also records return-boundary time after local destruction, separately from refinement
and output serialization. This is a full instrumentation correction repetition, not
selective rerunning of losing policies. Both datasets will remain available.

A final counter audit found the resumed prefix non-improvement pruning branch still used
the generic invalid-removal category. `_02` remains preserved. `_03` repeats both entire
designs with that branch classified as normal consumption and exports deadline removals.
No search, selection, reserve, dataset or order parameter changes accompany this counter
correction. No further recovery-feature development is justified by these opportunities.
