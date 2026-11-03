// Reuse fixture generation only; the legacy suite main is not executed here.
#define main legacy_selective_main
#include "selective_recovery_tests.cpp"
#undef main

bool correctedCosts(const Instance& instance) {
    using rbl::PartialAcceptanceOptimizer;
    auto base = rbl_test::createInitialPaths(instance, 10);
    ::Path delayed;
    int a = rbl_test::findDelayableAgent(instance, base, 2, delayed);
    ASSERT_TRUE(a >= 0, "fixture requires a delayable agent");
    auto padded = base;
    padded[a] = delayed;
    padded[a].insert(padded[a].end(), 4, delayed.back());
    rbl::ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(padded);
    int64_t soc = 0; std::string error;
    ASSERT_TRUE(PartialAcceptanceOptimizer::validateCompletePlanIndependently(padded, instance, soc, error), "valid padded plan");
    ASSERT_EQ(engine.getIncumbentCost(), soc, "initial authoritative cost");
    ASSERT_TRUE(engine.validateIncumbent(error), "cache populated");
    engine.setStoredCostForTest(soc + 1);
    ASSERT_FALSE(engine.validateIncumbent(error), "cache rejects corrupted stored cost");
    engine.setStoredCostForTest(soc);
    rbl::MacroPrefixCheckpoint checkpoint;
    checkpoint.checkpoint_id=73; checkpoint.incumbent_version=engine.getIncumbentVersion();
    checkpoint.priority_order={a}; checkpoint.neighborhood_agents={a}; checkpoint.r_done=1;
    checkpoint.prefix_paths={base[a]}; checkpoint.old_paths={padded[a]};
    engine.getCheckpointStore().saveCheckpoint(checkpoint);
    const auto before = engine.getIncumbentPaths();
    const auto reservations = engine.getPathTable();
    const auto versions = engine.getAgentVersions();
    const auto global = engine.getIncumbentVersion();
    const auto cache = engine.getValidationCacheForTest();
    for (int stage : {-4,-3,-2,-1,1,2,3,4,5}) {
        engine.setPublicationFailureStageForTest(stage);
        rbl::PartialAcceptanceResult result;
        ASSERT_FALSE(engine.commitPartialAcceptance({a}, {base[a]}, global, result), "injected stage rejects commit");
        ASSERT_EQ(engine.getIncumbentCost(), soc, "cost preserved");
        const auto* retained=engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(retained!=nullptr,"checkpoint remains suspended");
        ASSERT_EQ(retained->checkpoint_id,73u,"checkpoint identity preserved");
        ASSERT_EQ(retained->r_done,1,"checkpoint prefix count preserved");
        ASSERT_EQ(retained->incumbent_version,global,"checkpoint version preserved");
        ASSERT_TRUE(retained->priority_order==checkpoint.priority_order,"checkpoint order preserved");
        ASSERT_TRUE(isSamePath(retained->prefix_paths[0],checkpoint.prefix_paths[0]),"checkpoint routes preserved");
        ASSERT_TRUE(engine.getPublicationRecords().empty(),"failed publication not credited");
        ASSERT_EQ(engine.getIncumbentVersion(), global, "global version preserved");
        ASSERT_TRUE(engine.getAgentVersions() == versions, "agent versions preserved");
        ASSERT_TRUE(engine.getValidationCacheForTest() == cache, "cache preserved");
        ASSERT_TRUE(engine.getPathTable().table == reservations.table, "reservations preserved");
        ASSERT_TRUE(engine.getPathTable().goals == reservations.goals, "goal reservations preserved");
        ASSERT_EQ(engine.getPathTable().makespan, reservations.makespan, "makespan preserved");
        for (size_t i=0; i<before.size(); ++i) {
            ASSERT_EQ(engine.getIncumbentPaths()[i].size(), before[i].size(), "path lengths preserved");
            for (size_t t=0; t<before[i].size(); ++t)
                ASSERT_EQ(engine.getIncumbentPaths()[i][t].location, before[i][t].location, "path positions preserved");
        }
    }
    engine.setPublicationFailureStageForTest(0);
    rbl::PartialAcceptanceResult result;
    ASSERT_TRUE(engine.commitPartialAcceptance({a}, {base[a]}, global, result), "padded recovery commits");
    ASSERT_EQ(result.verified_gain, 2, "only real delay contributes gain");
    ASSERT_TRUE(PartialAcceptanceOptimizer::validateCompletePlanIndependently(engine.getIncumbentPaths(), instance, soc, error), "recovered plan valid");
    ASSERT_EQ(engine.getIncumbentCost(), soc, "recovery publishes authoritative cost");
    auto external = base[a]; external.insert(external.end(), 7, external.back());
    ASSERT_TRUE(engine.commitExternalTrajectory(a, external), "external padded update accepted");
    ASSERT_EQ(engine.getIncumbentCost(), soc, "padding has no external cost");
    engine.setInitialSolution(padded);
    rbl::SearchBudget budget(1000000, 2.0);
    auto outcome = engine.attemptFreshRepair({a}, budget, 42);
    ASSERT_EQ(outcome.type == rbl::RepairOutcomeType::COMPLETED_ACCEPTED, true, "normal repair improves delayed path");
    ASSERT_TRUE(PartialAcceptanceOptimizer::validateCompletePlanIndependently(engine.getIncumbentPaths(), instance, soc, error), "normal plan valid");
    ASSERT_EQ(engine.getIncumbentCost(), soc, "normal repair authoritative cost");
    return true;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--intentional-failure") {
        auto fail = []() { ASSERT_TRUE(false, "intentional harness check under NDEBUG"); return true; };
        return fail() ? 0 : 17;
    }
    if (argc != 3) return 2;
    int n = 99;
    if (rbl::checkedCostToInt(INT64_MAX, n) || n != 99) return 3;
    std::vector<int64_t> large{INT32_MAX, INT32_MAX, 7};
    if (rbl::checkedSumOfCosts(large, [](int64_t c){return c;}) != 2LL*INT32_MAX+7) return 4;
    bool overflow = false;
    try { rbl::checkedSumOfCosts(std::vector<int64_t>{INT64_MAX, 1}, [](int64_t c){return c;}); }
    catch (const std::overflow_error&) { overflow = true; }
    if (!overflow) return 5;
    Instance instance(argv[1], argv[2], 10);
    return correctedCosts(instance) ? 0 : 1;
}
