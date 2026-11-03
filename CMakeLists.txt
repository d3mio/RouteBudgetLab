cmake_minimum_required(VERSION 3.19)
project(routebudget_lab CXX)

# Our own code is C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

option(ENABLE_SANITIZERS "Instrument all compiled sources with ASan and UBSan" OFF)
if(ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(FATAL_ERROR "ENABLE_SANITIZERS requires Clang or GCC")
  endif()
endif()

# Compatibility definition for libc++ when compiling upstream C++14 code in C++17 mode
add_compile_definitions(_LIBCPP_ENABLE_CXX17_REMOVED_RANDOM_SHUFFLE)
add_compile_definitions(EIGEN_MPL2_ONLY)

# Expose vendored Eigen as <eigen3/Eigen/Core>
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include)
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_SOURCE_DIR}/third_party/eigen
    ${CMAKE_CURRENT_BINARY_DIR}/include/eigen3)

# Find Boost
find_package(Boost REQUIRED COMPONENTS program_options system filesystem)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/validator
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/eigen
    ${CMAKE_SOURCE_DIR}/third_party/anytime-mapf/inc
    ${CMAKE_SOURCE_DIR}/third_party/anytime-mapf/inc/CBS
    ${CMAKE_SOURCE_DIR}/third_party/anytime-mapf/inc/PIBT
    ${Boost_INCLUDE_DIRS}
)

# 1. Independent Path Validator
add_executable(rbl_validate src/validator/rbl_validate.cpp)

# 2. Checkpoint Feasibility Spike
add_executable(rbl_profile_checkpoint src/profiler/profile_checkpoint_feasibility.cpp)

# 3. Upstream Anytime-MAPF Core Library (excluding driver.cpp)
file(GLOB UPSTREAM_SRC
    "third_party/anytime-mapf/src/*.cpp"
    "third_party/anytime-mapf/src/CBS/*.cpp"
    "third_party/anytime-mapf/src/PIBT/*.cpp"
)
list(FILTER UPSTREAM_SRC EXCLUDE REGEX ".*/driver\\.cpp$")

add_library(anytime_mapf_core STATIC ${UPSTREAM_SRC})
target_link_libraries(anytime_mapf_core PUBLIC ${Boost_LIBRARIES})

# 4. RouteBudget Lab Resumable Repair Engine (Component 1)
add_library(rbl_engine STATIC
    src/engine/rbl_resumable_engine.cpp
    src/engine/rbl_partial_acceptance.cpp
)
target_link_libraries(rbl_engine PUBLIC anytime_mapf_core)

# Repair Outcome Profiler
add_executable(rbl_profile_outcomes src/profiler/profile_repair_outcomes.cpp)
target_link_libraries(rbl_profile_outcomes PRIVATE rbl_engine)

# Search-Progress Trace Collector (Phase 5 / Component 3)
add_executable(rbl_collect_traces src/profiler/collect_progress_traces.cpp)
target_link_libraries(rbl_collect_traces PRIVATE rbl_engine)

# Gate 2 Integration & Overhead Audit Profiler (Phase 6 / WP4)
add_executable(rbl_gate2_audit src/profiler/profile_gate2_controller.cpp)
target_link_libraries(rbl_gate2_audit PRIVATE rbl_engine)

# Evaluation Runner (Phase 7 / E1-E6)
add_executable(rbl_eval_runner src/profiler/eval_runner.cpp)
target_link_libraries(rbl_eval_runner PRIVATE rbl_engine)

# Controlled Perturbation Profiler: Partial-Prefix Recovery (Research Extension)
add_executable(rbl_profile_partial_recovery src/profiler/profile_partial_recovery.cpp)
target_link_libraries(rbl_profile_partial_recovery PRIVATE rbl_engine)

# Maximum-Gain Partial Acceptance Profiler (Research Extension)
add_executable(rbl_profile_partial_acceptance src/profiler/profile_partial_acceptance.cpp)
target_link_libraries(rbl_profile_partial_acceptance PRIVATE rbl_engine ${Boost_LIBRARIES})

# 5. Automated Tests
enable_testing()

add_executable(rbl_validator_tests tests/validator_tests.cpp)
target_compile_definitions(rbl_validator_tests PRIVATE FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_test(NAME validator_unit_tests COMMAND rbl_validator_tests)

# Real BALANCE output (50 agents, seed 0, 10 s) checked in as a regression fixture.
set(REAL ${CMAKE_SOURCE_DIR}/tests/fixtures/real)
set(REAL_ARGS --map ${REAL}/random-32-32-20.map --scen ${REAL}/random-32-32-20-random-1.scen --agents 50 --paths ${REAL}/baseline_50agents_seed0.paths)
add_test(NAME real_solver_output_is_valid COMMAND rbl_validate ${REAL_ARGS} --reported-cost 1147)
add_test(NAME real_solver_output_misread_as_xy_is_rejected COMMAND rbl_validate ${REAL_ARGS} --coords xy)
set_tests_properties(real_solver_output_misread_as_xy_is_rejected PROPERTIES WILL_FAIL TRUE)

# Experiment E1: Resumption Correctness & Equivalence Test Suite
add_executable(rbl_e1_tests tests/e1_resumption_tests.cpp)
target_link_libraries(rbl_e1_tests PRIVATE rbl_engine)
add_test(NAME experiment_e1_resumption_tests COMMAND rbl_e1_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)

# Experiment E4: Conservative Constraint Compatibility Test Suite (Phase 4)
add_executable(rbl_e4_tests tests/e4_compatibility_tests.cpp)
target_link_libraries(rbl_e4_tests PRIVATE rbl_engine)
add_test(NAME experiment_e4_compatibility_tests COMMAND rbl_e4_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)

# Search-Progress Feature Extraction & Estimator Test Suite (Phase 5)
add_executable(rbl_estimator_tests tests/estimator_tests.cpp)
target_link_libraries(rbl_estimator_tests PRIVATE rbl_engine)
add_test(NAME estimator_unit_tests COMMAND rbl_estimator_tests)

# Overhead-Aware Resume-or-Start Controller Test Suite (Phase 6)
add_executable(rbl_controller_tests tests/controller_tests.cpp)
target_link_libraries(rbl_controller_tests PRIVATE rbl_engine)
add_test(NAME controller_unit_tests COMMAND rbl_controller_tests)
set_tests_properties(controller_unit_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Partial-Prefix Recovery Correctness Test Suite
add_executable(rbl_partial_recovery_tests tests/partial_prefix_recovery_tests.cpp)
target_link_libraries(rbl_partial_recovery_tests PRIVATE rbl_engine)
add_test(NAME partial_prefix_recovery_tests COMMAND rbl_partial_recovery_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)

# Maximum-Gain Partial Acceptance Test Suite (Upgrade Guide: Section 9, 17 Required Tests)
add_executable(rbl_partial_acceptance_tests tests/partial_acceptance_tests.cpp)
target_link_libraries(rbl_partial_acceptance_tests PRIVATE rbl_engine)
add_test(NAME partial_acceptance_tests COMMAND rbl_partial_acceptance_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)

# Selective Recovery Test Suite (Claude Prompt: Section 11, 15 Required Tests)
add_executable(rbl_selective_recovery_tests tests/selective_recovery_tests.cpp)
target_link_libraries(rbl_selective_recovery_tests PRIVATE rbl_engine)
add_test(NAME selective_recovery_tests COMMAND rbl_selective_recovery_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)




add_executable(rbl_cost_transaction_tests tests/cost_transaction_tests.cpp)
target_link_libraries(rbl_cost_transaction_tests PRIVATE rbl_engine)
add_test(NAME cost_transaction_tests COMMAND rbl_cost_transaction_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)
add_test(NAME release_harness_intentional_failure COMMAND rbl_cost_transaction_tests --intentional-failure)
set_tests_properties(release_harness_intentional_failure PROPERTIES WILL_FAIL TRUE)
add_executable(rbl_recovery_correctness_tests tests/recovery_correctness_tests.cpp)
target_link_libraries(rbl_recovery_correctness_tests PRIVATE rbl_engine)
add_test(NAME recovery_correctness_tests COMMAND rbl_recovery_correctness_tests ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20.map ${CMAKE_SOURCE_DIR}/data/sample/random-32-32-20-random-1.scen)

set_tests_properties(recovery_correctness_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Development-only reserve measurement; not a benchmark result or CTest threshold.
add_executable(rbl_reserve_calibration tests/reserve_calibration.cpp)
target_link_libraries(rbl_reserve_calibration PRIVATE rbl_engine)
