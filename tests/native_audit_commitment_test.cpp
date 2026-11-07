#include <pgn/rpc_audit_commitment.h>
#include <iostream>
#include <stdexcept>
using namespace pgn::core;

static NativeAuditJob job(const std::string& state,const std::string& digest)
{
    NativeAuditJob out;
    if(state=="assigned")out.stage=NativeAuditJob::Assigned;
    else if(state=="committed")out.stage=NativeAuditJob::Committed;
    else if(state=="passed")out.stage=NativeAuditJob::Passed;
    else if(state=="failed")out.stage=NativeAuditJob::Failed;
    else throw std::invalid_argument("Unknown public audit stage");
    if(!digest.empty()&&(digest.size()!=64||digest.find_first_not_of("0123456789abcdef")!=std::string::npos))throw std::invalid_argument("Canonical fixture digest required");
    out.commitment=digest;
    out.reveal="PRIVATE-NONCE-AND-COUNTS-MUST-NOT-LEAK";
    return out;
}

int main(int argc,char** argv)
{
    try {
        if(argc==4&&std::string(argv[1])=="--project") {
            const auto value=job(argv[2],argv[3]);
            const auto digest=RpcAuditCommitment(value);
            std::cout<<"{\"slot\":0,\"attempt\":0,\"state\":\""<<argv[2]<<"\"";
            if(digest)std::cout<<",\"commitment\":\""<<*digest<<"\"";
            std::cout<<"}\n";
            return 0;
        }
        unsigned checks=0;
        const auto check=[&](bool okay){++checks;if(!okay)throw std::runtime_error("Audit commitment check "+std::to_string(checks));};
        const std::string digest(64,'a');
        check(!RpcAuditCommitment(job("assigned","")));
        check(!RpcAuditCommitment(job("assigned",digest)));
        for(const auto* stage:{"committed","passed","failed"}) {
            auto value=job(stage,digest);
            auto published=RpcAuditCommitment(value);
            check(published&&*published==digest);
            check(published->find("PRIVATE")==std::string_view::npos);
            value.commitment.clear();check(!RpcAuditCommitment(value));
        }
        std::cout<<"PASS "<<checks<<" actual NativeAuditJob public-digest checks; no expected counts or nonce\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
