// Audit-only harness: compile against unchanged repository implementation.
#define main repository_selective_suite_main
#include "../tests/selective_recovery_tests.cpp"
#undef main
int main(int argc, char** argv) {
    Instance instance(argv[1], argv[2], 10);
    auto base = rbl_test::createInitialPaths(instance,10);
    ::Path delayed;
    int a = rbl_test::findDelayableAgent(instance,base,2,delayed);
    if(a<0) return 2;
    auto padded=base;
    padded[a]=delayed;
    padded[a].push_back(padded[a].back());
    padded[a].push_back(padded[a].back());
    rbl::ResumableRepairEngine engine(instance,false,2);
    engine.setInitialSolution(padded);
    int64_t old_soc=0, new_soc=0;std::string err;
    bool old_valid=rbl::PartialAcceptanceOptimizer::validateCompletePlanIndependently(padded,instance,old_soc,err);
    std::cout<<"before valid="<<old_valid<<" stored="<<engine.getIncumbentCost()<<" authoritative="<<old_soc<<"\n";
    rbl::PartialAcceptanceResult result;
    bool committed=engine.commitPartialAcceptance({a},{base[a]},engine.getIncumbentVersion(),result);
    bool valid=rbl::PartialAcceptanceOptimizer::validateCompletePlanIndependently(engine.getIncumbentPaths(),instance,new_soc,err);
    std::cout<<"after committed="<<committed<<" valid="<<valid<<" stored="<<engine.getIncumbentCost()<<" authoritative="<<new_soc<<" verified_gain="<<result.verified_gain<<"\n";
    auto p1=base[1];p1[1].location=base[0][1].location;
    std::cout<<"selective_test4_assertion_guard="<<rbl::PartialAcceptanceOptimizer::checkPathKinematics(p1,instance.getStarts()[1],instance.getGoals()[1],instance,err)<<"\n";
    return !(old_valid && committed && valid && engine.getIncumbentCost()!=new_soc);
}
