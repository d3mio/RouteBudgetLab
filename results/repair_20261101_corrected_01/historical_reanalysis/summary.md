# Corrected selective recovery analysis

{
  "total_runs": 192,
  "matched_cells": 32,
  "distinct_instances": 8,
  "distinct_layouts": 4,
  "delta_definition": "policy deadline cost minus resumption deadline cost; negative is better",
  "inference": "Descriptive development evidence. Seeds, budgets and nested agent sets are correlated. Few independent layouts limit inference. Non-significance is not equivalence.",
  "multiple_comparison": "Holm adjustment across five predeclared policy comparisons, separately for sign and signed-rank tests."
}

| Policy | Pairs | W/T/L | Mean delta | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign p | Holm rank p |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PA_EVERY_INTERRUPTION_FILTERED | 32 | 0/28/4 | 13.5625 | 108.5 | 52.9759 | 9.36491 | 0.125 | 0.125 | 0.625 | 0.625 |
| PA_EVICTION_ONLY | 32 | 0/32/0 | 0 | None | 0 | 0 | 1 | 1 | 1 | 1 |
| PA_TERMINAL_ONLY | 32 | 0/32/0 | 0 | None | 0 | 0 | 1 | 1 | 1 | 1 |
| PA_EVICTION_AND_TERMINAL | 32 | 0/32/0 | 0 | None | 0 | 0 | 1 | 1 | 1 | 1 |
| TERMINAL_RESERVE_CONTROL | 32 | 0/32/0 | 0 | None | 0 | 0 | 1 | 1 | 1 | 1 |

All-tie p=1 is a convention. Recovery gain within a run is not improvement over resumption.
See analysis.json for return-cost comparisons, map/scenario/scale/budget strata and recovery counters.
