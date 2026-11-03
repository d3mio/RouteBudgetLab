#include "rbl_resumable_engine.hpp"
#include <fstream>
#include <regex>
#include <iomanip>
#include <iostream>
int main(int argc,char**argv) {
    if(argc!=4)return 2;
    Instance instance(argv[1],argv[2],100);
    std::ifstream in(argv[3]); std::string line; std::vector<Path> base;
    const std::regex location("\\(([0-9]+)\\)");
    while(std::getline(in,line)) {
        Path p; for(std::sregex_iterator i(line.begin(),line.end(),location),end;i!=end;++i) p.emplace_back(std::stoi((*i)[1]));
        if(!p.empty())base.push_back(p);
    }
    int64_t soc=0;std::string error;
    if(!rbl::PartialAcceptanceOptimizer::validateCompletePlanIndependently(base,instance,soc,error))return 3;
    auto delayed=base;std::vector<int> agents;std::vector<Path> replacements;
    for(int a=0;a<100 && agents.size()<8;++a) {
        auto candidate=delayed;candidate[a].insert(candidate[a].begin(),2,candidate[a].front());
        if(rbl::PartialAcceptanceOptimizer::validateCompletePlanIndependently(candidate,instance,soc,error)) {
            delayed.swap(candidate);agents.push_back(a);replacements.push_back(base[a]);
        }
    }
    if(agents.size()!=8)return 4;
    std::cout<<std::setprecision(17)<<"{\"kind\":\"synthetic valid delayed incumbent on exposed development fixture; not benchmark performance evidence\",\"agents\":100,\"candidates\":8,\"durations_s\":[";
    for(int trial=0;trial<30;++trial) {
        rbl::ResumableRepairEngine engine(instance);engine.setInitialSolution(delayed);
        engine.setSelectiveRecoveryPolicy(rbl::SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);
        auto start=std::chrono::steady_clock::now();
        {
            rbl::SearchBudget budget(100000,1.0);budget.setGlobalDeadline(start+std::chrono::seconds(1));
            auto result=engine.evaluatePartialAcceptance(agents,replacements,&budget);
            if(!engine.commitPartialAcceptance(result.selected_agent_ids,result.selected_paths,engine.getIncumbentVersion(),result,&budget))return 5;
        }
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        if(trial)std::cout<<",";std::cout<<seconds;
    }
    std::cout<<"]}\n";
}
