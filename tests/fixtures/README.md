# Validator fixtures
Map `tiny.map` is 6x3 (`@` = obstacle, coordinates are (x=column, y=row)):
```
y=0  ......
y=1  .@@@..
y=2  ......
```
- `tiny.scen`: A0 (0,0)->(5,0); A1 (5,2)->(0,2); A2 (0,1)->(0,0).
- `pair.scen`: A0 (0,0)->(1,0); A1 (1,0)->(0,0)  (used for the edge swap).
- `park.scen`: A0 (0,0)->(1,0) finishes at t=1 and stays; A1 (3,0)->(0,1) later walks through (1,0).
`valid.paths` costs 5+6+1 = 12 (A1 waits once). Every `bad_*` file breaks exactly one rule; the expected
verdict for each is in `tests/validator_tests.cpp`.
