# Corrected selective recovery analysis

{
  "total_runs": 192,
  "matched_cells": 32,
  "distinct_instances": 8,
  "distinct_layouts": 4,
  "baseline": "RESUMPTION_NO_PA",
  "delta_definition": "policy deadline cost minus baseline deadline cost; negative is better",
  "inference": "Descriptive development evidence. Seeds, budgets and nested agent sets are correlated. Few independent layouts limit inference. Non-significance is not equivalence.",
  "multiple_comparison": "Holm adjustment across 5 predeclared policy comparisons, separately for sign and signed-rank tests."
}

| Policy | Pairs | W/T/L | Mean delta | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign p | Holm rank p |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PA_EVERY_INTERRUPTION_FILTERED | 32 | 0/27/5 | 21.9688 | 140.6 | 71.6688 | 12.6694 | 0.0625 | 0.0625 | 0.3125 | 0.3125 |
| PA_EVICTION_ONLY | 32 | 1/30/1 | 1.65625 | 26.5 | 18.8176 | 3.32652 | 1 | 1 | 1 | 1 |
| PA_TERMINAL_ONLY | 32 | 1/30/1 | -1.3125 | -21 | 7.60915 | 1.34512 | 1 | 1 | 1 | 1 |
| PA_EVICTION_AND_TERMINAL | 32 | 1/31/0 | -1.34375 | -43 | 7.6014 | 1.34375 | 1 | 1 | 1 | 1 |
| TERMINAL_RESERVE_CONTROL | 32 | 1/31/0 | -1.34375 | -43 | 7.6014 | 1.34375 | 1 | 1 | 1 | 1 |

All-tie p=1 is a convention. Recovery gain within a run is not improvement over resumption.
See analysis.json for return-cost comparisons, map/scenario/scale/budget strata and recovery counters.
