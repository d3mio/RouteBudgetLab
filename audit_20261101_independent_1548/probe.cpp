#define main original_test_main
#include "../tests/selective_recovery_tests.cpp"
#undef main
#include <mach/mach_time.h>
int main(int argc,char**argv){
 Instance instance(argv[1],argv[2],10);
 auto base=rbl_test::createInitialPaths(instance,10); ::Path delayed;
 int a=rbl_test::findDelayableAgent(instance,base,2,delayed);
 auto padded=base;padded[a]=delayed;
 for(int j=0;j<3;j++)padded[a].push_back(padded[a].back());
 rbl::ResumableRepairEngine e(instance,false,2);e.setInitialSolution(padded);
 int64_t soc=0;std::string err;
 rbl::PartialAcceptanceOptimizer::validateCompletePlanIndependently(padded,instance,soc,err);
 std::cout<<"before stored="<<e.getIncumbentCost()<<" arrival="<<soc<<"\n";
 rbl::PartialAcceptanceResult result;
 bool ok=e.commitPartialAcceptance({a},{base[a]},e.getIncumbentVersion(),result);
 rbl::PartialAcceptanceOptimizer::validateCompletePlanIndependently(e.getIncumbentPaths(),instance,soc,err);
 std::cout<<"commit="<<ok<<" after stored="<<e.getIncumbentCost()<<" arrival="<<soc<<" gain="<<result.verified_gain<<" cached_valid="<<e.validateIncumbent(err)<<"\n";
 mach_timebase_info_data_t info;mach_timebase_info(&info);std::cout<<"mach_timebase numer="<<info.numer<<" denom="<<info.denom<<"\n";
 return ok && e.getIncumbentCost()!=soc ? 0:1;
}
