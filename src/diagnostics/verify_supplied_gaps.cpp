#include "verification/bounded_gap_verifier.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace pgn::verification;
int main(int argc,char** argv){
    if(argc!=2)return 1;
    std::error_code error;const auto bytes=std::filesystem::file_size(argv[1],error);
    if(error||bytes>65536)return 2;
    std::ifstream source(argv[1]);std::vector<std::pair<std::string,std::string>> pairs;
    std::string line;
    while(std::getline(source,line)){
        std::istringstream fields(line);std::string lower,upper,extra;
        if(!(fields>>lower>>upper)||(fields>>extra)||pairs.size()>=256)return 3;
        // Reject malformed fields before generating JSON; no string escaping
        // assumption is applied to an arbitrary input line.
        if(!isCanonicalDecimal(lower)||!isCanonicalDecimal(upper))return 4;
        pairs.emplace_back(std::move(lower),std::move(upper));
    }
    if(!source.eof()||pairs.empty())return 5;
    unsigned verified=0;
    for(const auto& [lower,upper]:pairs){
        const auto start=std::chrono::steady_clock::now();
        const auto result=verifyGap(lower,upper,10000);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        const bool okay=result.verdict==GapVerdict::VerifiedWithinBound;
        if(okay)++verified;
        std::cout<<"{\"lower\":\""<<lower<<"\",\"upper\":\""<<upper
            <<"\",\"method\":\""<<VerificationMethod<<"\",\"strict_bound\":\""<<PrimeBound
            <<"\",\"verified\":"<<(okay?"true":"false")<<",\"verdict\":"<<int(result.verdict)
            <<",\"checked_interior\":"<<result.checkedInterior<<",\"elapsed_ns\":"<<ns
            <<",\"network_credit\":false,\"range_coverage_claim\":false}\n";
    }
    std::cerr<<"supplied="<<pairs.size()<<" verified="<<verified<<" method="<<VerificationMethod
             <<" NO_MINING_NO_NETWORK_CREDIT_NO_COVERAGE_OR_AVERAGED_RATE\n";
    return verified==pairs.size()?0:6;
}
