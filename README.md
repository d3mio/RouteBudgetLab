# RouteBudget Lab

**Dependency-Bounded Profitable-Group Completion and Constraint-Validated Resumption for Preempted Anytime MAPF-LNS**

- **Author:** P. G. Disas Sethnada (Informatics Institute of Technology / University of Westminster)
- **Supervisor:** Mr. Maninda Edirisooriya

---

## Repository Structure

```text
RouteBudgetLab/
├── CMakeLists.txt              # C++17 build & CTest configuration
├── cmake/                      # CMake helper modules
├── src/
│   ├── engine/                 # Resumable LNS engine, checkpoint store (K<=2), compatibility & gated closure
│   ├── profiler/               # Benchmark & trace profilers (rbl_profile_partial_acceptance, eval_runner)
│   └── validator/              # Independent MAPF plan validator (rbl_validate)
├── third_party/                # Vendored anytime-mapf core & Eigen
├── tests/                      # C++ unit/integration tests + Python pytest suite
├── scripts/                    # Validation, opportunity audit, confirmation eval, and analysis scripts
├── configs/                    # Experiment configuration JSONs
├── data/                       # Moving AI Lab MAPF maps & scenarios
├── results/
│   └── ieee_access_final/      # Verified compact summary CSVs & JSON metrics
├── viewer/                     # Local 2D trajectory replay viewer
├── run_demo.sh                 # Quick 2-agent fixture demo & validation
├── verify_all.sh               # Release build + CTest (13/13) + Pytest (18/18)
├── reproduce_evaluation.sh     # Reproduce opportunity audit, unseen eval, and statistics
├── requirements.txt            # Python dependencies
└── LICENSE
```

---

## Dependencies

- **C++ Toolchain:** C++17 compiler (Apple Clang / GCC / Clang), CMake $\ge 3.16$, Boost (`program_options`)
- **Python:** Python $\ge 3.10$ with packages in `requirements.txt`:
  ```sh
  pip install -r requirements.txt
  ```

---

## Fresh-Clone Build & Verification

```sh
# 1. Configure and build Release
cmake -S . -B build/reconcile_release -DCMAKE_BUILD_TYPE=Release
cmake --build build/reconcile_release -j 4

# 2. Run C++ test suite (13/13) and Python test suite (18/18)
ctest --test-dir build/reconcile_release --output-on-failure
pytest tests/

# 3. Run quick 2-agent crossing demo
./run_demo.sh
```

---

## Benchmark Dataset

All grid maps and scenarios under `data/` are from the **Moving AI Lab MAPF Benchmarks**:  
<https://movingai.com/benchmarks/mapf.html>
