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
| PA_EVERY_INTERRUPTION_FILTERED | 32 | 0/26/6 | 23.875 | 127.33333333333333 | 76.8416 | 13.5838 | 0.03125 | 0.03125 | 0.15625 | 0.15625 |
| PA_EVICTION_ONLY | 32 | 0/29/3 | 4.875 | 52 | 18.4858 | 3.26786 | 0.25 | 0.25 | 1 | 1 |
| PA_TERMINAL_ONLY | 32 | 0/29/3 | 5.0625 | 54 | 18.9105 | 3.34294 | 0.25 | 0.25 | 1 | 1 |
| PA_EVICTION_AND_TERMINAL | 32 | 1/30/1 | 0.4375 | 7 | 3.06844 | 0.542429 | 1 | 1 | 1 | 1 |
| TERMINAL_RESERVE_CONTROL | 32 | 0/30/2 | 4.34375 | 69.5 | 18.37 | 3.2474 | 0.5 | 0.5 | 1 | 1 |

All-tie p=1 is a convention. Recovery gain within a run is not improvement over resumption.
See analysis.json for return-cost comparisons, map/scenario/scale/budget strata and recovery counters.
