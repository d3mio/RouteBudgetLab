# Presentation outline — RouteBudget Lab

1. Problem: complete valid MAPF plans under bounded planning time.
2. Primary method: completed-prefix checkpointing, fresh compatibility validation, safe resumption.
3. Contract: authoritative arrival SoC, complete-plan validation, staged publication and late rollback.
4. Extension: U bound, signed dependencies, replacement conflicts and deterministic greedy closures.
5. Trigger diagram: real FIFO victim before overwrite; bounded FIFO terminal recovery.
6. Methodology: exposed development layouts, matched shared initial plans, sequential cyclic order,
   frozen 13 ms heuristic reserve, control cutoff, all work charged to refinement.
7. Primary results (negative delta favors resumption over restart):

| Policy versus DISCARD_NO_PA | Pairs | Wins / ties / losses | Mean ΔSoC | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign / rank p |
|---|---|---|---|---|---|---|---|---|---|
| RESUMPTION_NO_PA | 32 | 22 / 9 / 1 | -212.812 | -296.087 | 329.516 | 58.2508 | 5.72205e-06 | 4.76837e-07 | 5.72205e-06 / 4.76837e-07 |

8. Selective results (negative delta favors policy over resumption):

| Policy versus RESUMPTION_NO_PA | Pairs | Wins / ties / losses | Mean ΔSoC | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign / rank p |
|---|---|---|---|---|---|---|---|---|---|
| PA_EVERY_INTERRUPTION_FILTERED | 32 | 1 / 28 / 3 | 10.0625 | 80.5 | 75.0711 | 13.2708 | 0.625 | 0.625 | 1 / 1 |
| PA_EVICTION_ONLY | 32 | 2 / 29 / 1 | -10.125 | -108 | 44.8451 | 7.92756 | 1 | 0.5 | 1 / 1 |
| PA_TERMINAL_ONLY | 32 | 3 / 29 / 0 | -14 | -149.333 | 49.547 | 8.75875 | 0.25 | 0.25 | 1 / 1 |
| PA_EVICTION_AND_TERMINAL | 32 | 3 / 29 / 0 | -14 | -149.333 | 49.547 | 8.75875 | 0.25 | 0.25 | 1 / 1 |
| TERMINAL_RESERVE_CONTROL | 32 | 3 / 29 / 0 | -14.5312 | -155 | 50.2202 | 8.87775 | 0.25 | 0.25 | 1 / 1 |

9. Recovery funnel and measured overshoot:

| Policy | Capacity recovery attempts / commits | Terminal examined / commits | Recorded refinement overruns / runs | Maximum overshoot (ms) | Recovered gain | Recovery wrapper time (s) | Mean iteration Δ |
|---|---|---|---|---|---|---|---|
| RESUMPTION_NO_PA | 0 / 0 | 0 / 0 | 0 / 32 | 0 | 0 | 0 | 0 |
| PA_EVERY_INTERRUPTION_FILTERED | 0 / 0 | 0 / 0 | 2 / 32 | 0.105292 | 2974 | 3.7828 | -236.125 |
| PA_EVICTION_ONLY | 0 / 0 | 0 / 0 | 0 / 32 | 0 | 0 | 0 | -49.3125 |
| PA_TERMINAL_ONLY | 0 / 0 | 15 / 0 | 0 / 32 | 0 | 0 | 0.00118191 | -0.46875 |
| PA_EVICTION_AND_TERMINAL | 0 / 0 | 12 / 0 | 0 / 32 | 0 | 0 | 0.000189375 | -391.312 |
| TERMINAL_RESERVE_CONTROL | 0 / 0 | 0 / 0 | 0 / 32 | 0 | 0 | 0 | 38.3125 |

10. Interpretation: The five selective/control comparisons contain 12 wins and 4 losses in total; these correlated comparisons are not independent observations. Recovery remains an optional evaluated extension; RESUMPTION_NO_PA remains the default. No eviction or terminal recovery committed a route; numerical wins in those policies cannot be attributed to recovered-route publication. The reserve control and timing variation must be considered.
11. Limitations: four layouts, correlated cells, wall-clock jitter, no equivalence/novelty guarantee;
    saved-plan validation is distinct from historical publication-time evidence.
12. Reproduction, AI assistance, and future untouched confirmation work.

Tables are generated from the same final_analysis.json as the report. This outline has not been submitted.
