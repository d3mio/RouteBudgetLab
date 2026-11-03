# Implementation status — corrected development package

Primary contribution: constraint-validated checkpoint resumption. Optional extension:
selective recovery of completed routes. Default remains RESUMPTION_NO_PA.

## Executed verification

- Release: 13/13 CTest entries passed; ASan/UBSan: 13/13 passed after correction of a
  machine-dependent latency assertion. The original failed sanitizer log is preserved.
- 59 compilation units have sanitizer flags; linked sanitizer symbols verified.
- Intentional Release harness failure returns 17 under -DNDEBUG.
- Independent-validator Python tests: 6/6; experiment/statistics tools: 7/7.
- Historical Phase 7: 768/768 final plans valid with authoritative cost matches.
- Corrected selective/primary pilots: 192/64 runs respectively; every final plan independently
  validated, event counts reconciled, final manifests verified.
- Costs, staged reservations/versions/cache, conflicting closures, real eviction hooks,
  adverse terminal cascade and injected stage cancellation have direct assertions.

Test counts are not a claim of exhaustive correctness. See the assertion-level matrix and
final audit for coverage boundaries. General OS scheduling/allocator tails remain outside
hard real-time guarantees. The adaptive estimator/controller is retained and unit-tested;
its historical final-plan results remain historical, not corrected-current runtime evidence.

## Generated deadline-quality results

| Policy versus DISCARD_NO_PA | Pairs | Wins / ties / losses | Mean ΔSoC | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign / rank p |
|---|---|---|---|---|---|---|---|---|---|
| RESUMPTION_NO_PA | 32 | 22 / 9 / 1 | -212.812 | -296.087 | 329.516 | 58.2508 | 5.72205e-06 | 4.76837e-07 | 5.72205e-06 / 4.76837e-07 |

| Policy versus RESUMPTION_NO_PA | Pairs | Wins / ties / losses | Mean ΔSoC | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign / rank p |
|---|---|---|---|---|---|---|---|---|---|
| PA_EVERY_INTERRUPTION_FILTERED | 32 | 1 / 28 / 3 | 10.0625 | 80.5 | 75.0711 | 13.2708 | 0.625 | 0.625 | 1 / 1 |
| PA_EVICTION_ONLY | 32 | 2 / 29 / 1 | -10.125 | -108 | 44.8451 | 7.92756 | 1 | 0.5 | 1 / 1 |
| PA_TERMINAL_ONLY | 32 | 3 / 29 / 0 | -14 | -149.333 | 49.547 | 8.75875 | 0.25 | 0.25 | 1 / 1 |
| PA_EVICTION_AND_TERMINAL | 32 | 3 / 29 / 0 | -14 | -149.333 | 49.547 | 8.75875 | 0.25 | 0.25 | 1 / 1 |
| TERMINAL_RESERVE_CONTROL | 32 | 3 / 29 / 0 | -14.5312 | -155 | 50.2202 | 8.87775 | 0.25 | 0.25 | 1 / 1 |

The five selective/control comparisons contain 12 wins and 4 losses in total; these correlated comparisons are not independent observations. Recovery remains an optional evaluated extension; RESUMPTION_NO_PA remains the default. No eviction or terminal recovery committed a route; numerical wins in those policies cannot be attributed to recovered-route publication. The reserve control and timing variation must be considered.

## Provenance and package

Starting audited commit fb6ba907e4ccba9ec882537c2dce2be970c91781 on research/selective-recovery.
Repair branch research/selective-recovery-corrected. Experiment source commit:
`ac01fb47445a1bc6e4522de549e11b48c26e83a5`. Each raw run records dirty status, source/build/input
hashes and complete compiler/linker commands. The pre-existing .DS_Store edit and two
untracked audit directories were preserved. No push, merge, submission or publishing occurred.

Current tables and explanations supersede the audited completion claims. Historical raw
artifacts remain unchanged; old document versions remain available at the audited Git commit.
