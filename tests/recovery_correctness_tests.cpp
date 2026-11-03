#define main legacy_selective_main
#include "selective_recovery_tests.cpp"
#undef main
using namespace rbl;

bool graphProperties() {
    // Enumerate all 5^3 weight assignments and all dependency/conflict masks.
    // Every feasible oracle subset must be bounded by U. Greedy must be feasible,
    // deterministic, and no better than the exhaustive optimum.
    for (int code=0; code<125; ++code) {
        int c=code;
        DependencyGraph g; g.num_candidates=3; g.candidate_agents={0,1,2};
        g.adj.resize(3); g.forbidden.assign(3,false);
        std::vector<ReplacementCandidate> candidates(3);
        for(int i=0;i<3;++i) { g.weights.push_back(c%5-2); c/=5; candidates[i].agent_id=i; candidates[i].weight=g.weights[i]; }
        int64_t u=0; bool overflow=false;
        ASSERT_TRUE(PartialAcceptanceOptimizer::computeOptimisticUpperBound(candidates,u,overflow),"valid bound");
        for(int deps=0;deps<64;++deps) for(int conflicts=0;conflicts<8;++conflicts) {
            g.adj.assign(3,{}); g.incompatibilities.clear(); int bit=0;
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) if(i!=j) { if(deps&(1<<bit)) g.adj[i].push_back(j); ++bit; }
            bit=0; for(int i=0;i<3;++i) for(int j=i+1;j<3;++j) {if(conflicts&(1<<bit))g.incompatibilities.emplace_back(i,j); ++bit;}
            auto oracle=PartialAcceptanceOptimizer::solveExhaustiveGraphClosureOracle(g,candidates,1);
            auto greedy=PartialAcceptanceOptimizer::solveGreedyDependencyAware(g,candidates,1);
            ASSERT_TRUE(oracle.predicted_gain<=u,"U bounds every feasible optimum");
            ASSERT_TRUE(greedy.predicted_gain<=oracle.predicted_gain,"greedy not better than oracle");
            if(u==0) ASSERT_EQ(oracle.predicted_gain,0,"zero U cannot hide improvement");
            std::vector<bool> selected(3,false); int64_t gain=0;
            for(int i:greedy.selected_agent_ids){ selected[i]=true; gain+=g.weights[i]; }
            ASSERT_EQ(gain,greedy.predicted_gain,"greedy objective recomputed");
            for(int i=0;i<3;++i) if(selected[i]) for(int j:g.adj[i]) ASSERT_TRUE(selected[j],"dependency closed");
            for(auto e:g.incompatibilities) ASSERT_FALSE(selected[e.first]&&selected[e.second],"conflicts excluded");
            auto again=PartialAcceptanceOptimizer::solveGreedyDependencyAware(g,candidates,1);
            ASSERT_TRUE(again.selected_agent_ids==greedy.selected_agent_ids,"deterministic ties");
        }
    }
    DependencyGraph g; g.num_candidates=2; g.candidate_agents={0,1}; g.weights={5,4};
    g.adj.resize(2); g.forbidden.assign(2,false); g.incompatibilities={{0,1}};
    std::vector<ReplacementCandidate> c(2); c[0].agent_id=0; c[1].agent_id=1;
    auto selected=PartialAcceptanceOptimizer::solveGreedyDependencyAware(g,c,1);
    ASSERT_EQ(selected.predicted_gain,5,"conflicting positives retain safe winner");
    ASSERT_EQ(selected.selected_agent_ids.size(),1u,"one winner");
    ASSERT_EQ(PartialAcceptanceOptimizer::solveExactClosure(g,c,1).status,PartialAcceptanceStatus::INVALID_INPUT,"Dinic rejects unsupported conflicts");
    g.num_candidates=3; g.candidate_agents={0,1,2}; g.weights={5,4,4};
    g.adj.assign(3,{}); g.forbidden.assign(3,false); g.incompatibilities={{0,1},{0,2}};
    c.resize(3); c[2].agent_id=2;
    ASSERT_EQ(PartialAcceptanceOptimizer::solveGreedyDependencyAware(g,c,1).predicted_gain,5,"greedy counterexample");
    ASSERT_EQ(PartialAcceptanceOptimizer::solveExhaustiveGraphClosureOracle(g,c,1).predicted_gain,8,"oracle improves on greedy");
    return true;
}

bool productionEviction(const Instance& instance) {
    auto base=rbl_test::createInitialPaths(instance,10); ::Path delayed;
    int a=rbl_test::findDelayableAgent(instance,base,2,delayed);
    ASSERT_TRUE(a>=0,"delay fixture");
    for(int capacity:{1,2}) for(int mode=0;mode<4;++mode) {
        auto initial=base; initial[a]=delayed;
        ResumableRepairEngine e(instance,false,capacity); e.setInitialSolution(initial);
        e.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVICTION_ONLY);
        MacroPrefixCheckpoint cp; cp.checkpoint_id=20; cp.priority_order={a}; cp.neighborhood_agents={a};
        cp.r_done=1; cp.prefix_paths={base[a]}; cp.old_paths={delayed}; cp.incumbent_version=e.getIncumbentVersion();
        e.getCheckpointStore().saveCheckpoint(cp);
        if(capacity==2) { cp.checkpoint_id=10; e.getCheckpointStore().saveCheckpoint(cp); }
        int victim=capacity-1;
        auto* v=e.getCheckpointStore().getCheckpoint(victim);
        if(mode==1) v->prefix_paths={delayed}; // U=0
        if(mode==3) v->prefix_paths[0].back().location=instance.getStarts()[a]; // invalid
        SearchBudget budget(100000,2.0); budget.setMaxAgents(0); // zero-prefix incoming
        if(mode==2) budget.expireAtStageForTest(RecoveryStage::Extraction);
        auto old=e.getIncumbentCost();
        e.attemptFreshRepair({a},budget,42);
        ASSERT_EQ(e.getEvictionRecoveryAttempts(),1,"production hook attempts actual victim once");
        ASSERT_EQ(e.getCheckpointStore().getTotalEvictions(),1,"original eviction always completes");
        ASSERT_EQ(e.getCheckpointStore().getCheckpoint(victim)->r_done,0,"zero-prefix incoming overwrites victim");
        if(capacity==2) ASSERT_EQ(e.getCheckpointStore().getCheckpoint(0)->checkpoint_id,20u,"newer checkpoint preserved");
        ASSERT_EQ(e.getEvictionRecoveryCommitted(),mode==0?1:0,"outcome reconciles");
        ASSERT_EQ(e.getIncumbentCost(),old-(mode==0?2:0),"only successful victim changes plan");
    }
    return true;
}

bool deadlineStages(const Instance& instance) {
    auto base=rbl_test::createInitialPaths(instance,10); ::Path delayed;
    int a=rbl_test::findDelayableAgent(instance,base,2,delayed); ASSERT_TRUE(a>=0,"delay fixture");
    int b=(a+1)%10;
    for(auto stage:{RecoveryStage::Extraction,RecoveryStage::Dependency,RecoveryStage::Conflict,RecoveryStage::Selection,
                    RecoveryStage::Validation,RecoveryStage::Preparation,RecoveryStage::BeforePublication,RecoveryStage::AfterPublication}) {
        auto initial=base; initial[a]=delayed;
        ResumableRepairEngine e(instance); e.setInitialSolution(initial); e.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);
        auto cost=e.getIncumbentCost(); auto version=e.getIncumbentVersion(); auto table=e.getPathTable(); auto cache=e.getValidationCacheForTest();
        SearchBudget budget(1000000,2); budget.expireAtStageForTest(stage, (stage == RecoveryStage::BeforePublication || stage == RecoveryStage::AfterPublication) ? 0 : 1);
        auto result=e.evaluatePartialAcceptance({a,b},{base[a],base[b]},&budget);
        if(result.status==PartialAcceptanceStatus::OPTIMAL_SELECTION)
            ASSERT_FALSE(e.commitPartialAcceptance(result.selected_agent_ids,result.selected_paths,version,result,&budget),"stage cancellation rejects publication");
        ASSERT_TRUE(budget.deadline_exceeded,"requested stage actually reached");
        ASSERT_TRUE(e.getPublicationRecords().empty(),"late result never enters deadline trajectory");
        ASSERT_EQ(e.getIncumbentCost(),cost,"deadline cost preserved"); ASSERT_EQ(e.getIncumbentVersion(),version,"version preserved");
        ASSERT_TRUE(e.getPathTable().table==table.table && e.getPathTable().goals==table.goals,"reservations unchanged");
        ASSERT_TRUE(e.getValidationCacheForTest()==cache,"cache unchanged");
    }
    return true;
}
bool actualReplacementConflict() {
    Instance instance("tests/fixtures/recovery_cross.map","tests/fixtures/recovery_cross.scen",2);
    auto path=[](std::initializer_list<int> locations) { ::Path p; for(int loc:locations) p.emplace_back(loc); return p; };
    std::vector<::Path> old{path({3,3,3,4,5}),path({1,1,1,1,1,4,7})};
    std::vector<::Path> replacements{path({3,4,5}),path({1,4,7})};
    ResumableRepairEngine engine(instance); engine.setInitialSolution(old);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);
    auto selected=engine.evaluatePartialAcceptance({0,1},replacements);
    ASSERT_EQ(selected.status,PartialAcceptanceStatus::OPTIMAL_SELECTION,"conflicting replacements retain feasible subset");
    ASSERT_EQ(selected.selected_agent_ids.size(),1u,"select exactly one conflicting replacement");
    ASSERT_EQ(selected.predicted_gain,4,"choose higher individual gain");
    ASSERT_EQ(selected.selected_agent_ids[0],1,"deterministic winner");
    ASSERT_TRUE(engine.commitPartialAcceptance(selected.selected_agent_ids,selected.selected_paths,engine.getIncumbentVersion(),selected),"independently validated subset commits");
    ASSERT_EQ(engine.getIncumbentCost(),6,"mixed plan authoritative cost");
    return true;
}
bool adverseTerminalCascade() {
    Instance instance("tests/fixtures/recovery_cross.map","tests/fixtures/recovery_cross.scen",2);
    auto path=[](std::initializer_list<int> locations) { ::Path p; for(int loc:locations) p.emplace_back(loc); return p; };
    std::vector<::Path> old{path({3,3,3,4,5}),path({1,1,1,1,1,4,7})};
    ResumableRepairEngine engine(instance); engine.setInitialSolution(old);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_TERMINAL_ONLY);
    MacroPrefixCheckpoint cp; cp.checkpoint_id=1; cp.incumbent_version=engine.getIncumbentVersion();
    cp.priority_order={0};cp.neighborhood_agents={0};cp.r_done=1;cp.prefix_paths={path({3,4,5})};
    engine.getCheckpointStore().saveCheckpoint(cp);
    cp.checkpoint_id=2;cp.priority_order={1};cp.neighborhood_agents={1};cp.prefix_paths={path({1,4,7})};
    engine.getCheckpointStore().saveCheckpoint(cp);
    SearchBudget budget(100000,1);
    ASSERT_TRUE(engine.attemptTerminalRecovery(budget),"FIFO first checkpoint improves");
    ASSERT_EQ(engine.getTerminalRecoveryExamined(),2,"second checkpoint re-evaluated");
    ASSERT_EQ(engine.getTerminalRecoveryCommitted(),1,"second now conflicts with fresh incumbent");
    ASSERT_EQ(engine.getIncumbentCost(),8,"only first FIFO gain credited");
    ASSERT_EQ(engine.getRecoveryEvents().size(),2u,"both outcomes recorded");
    ASSERT_EQ(engine.getRecoveryEvents()[1].result.status,PartialAcceptanceStatus::NO_GAIN,"stale graph not reused");
    ASSERT_TRUE(engine.getCheckpointStore().getCheckpoint(1)!=nullptr,"failed checkpoint retained");
    return true;
}
bool removalAccounting() {
    BoundedCheckpointStore store(1); MacroPrefixCheckpoint cp; cp.checkpoint_id=1;
    for(auto reason:{CheckpointRemoval::Consumption,CheckpointRemoval::Stagnation,CheckpointRemoval::Invalid,CheckpointRemoval::Recovery}) {
        store.saveCheckpoint(cp); store.markDiscarded(0,reason); store.markDiscarded(0,reason);
        ASSERT_EQ(store.getRemovalCount(reason),1u,"mutually exclusive removal, no duplicate counting");
    }
    return true;
}
int main(int argc,char**argv) {
    if(argc!=3)return 2;
    Instance instance(argv[1],argv[2],10);
    return removalAccounting()&&adverseTerminalCascade()&&actualReplacementConflict()&&graphProperties()&&productionEviction(instance)&&deadlineStages(instance)?0:1;
}
