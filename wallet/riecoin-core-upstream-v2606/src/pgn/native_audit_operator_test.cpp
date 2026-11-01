#undef NDEBUG
#include <pgn/native_report_audit.h>
#include <cassert>
using namespace pgn::core;
int main(){
 NativeAuditLedger ledger;const CScript owner{OP_1},actor{OP_2},alias{OP_3},second{OP_4},owner_alias{OP_5};
 const auto owner_id=*uint256::FromHex(std::string(64,'a')),actor_id=*uint256::FromHex(std::string(64,'b')),second_id=*uint256::FromHex(std::string(64,'c'));
 const auto enroll=[&](const CScript& key,const uint256& id,bool allowed){ledger.participants.emplace(Bytes{key.begin(),key.end()},NativeAuditMembership{id,allowed});};
 enroll(owner,owner_id,false);enroll(owner_alias,owner_id,true);enroll(actor,actor_id,true);enroll(alias,actor_id,true);enroll(second,second_id,true);
 const auto request=*uint256::FromHex(std::string(64,'e'));NativeReportAudit report;report.owner=owner;report.report.height=1;ledger.emplace(request,report);
 assert(!SelectNativeAudit(ledger,owner_alias,10,1000));assert(SelectNativeAudit(ledger,actor,10,1000)->second==0);
 auto& current=ledger.at(request);current.jobs[0]=NativeAuditJob{actor,0,2,100};current.jobs[0]->stage=NativeAuditJob::Committed;
 assert(!SelectNativeAudit(ledger,actor,10,1000));assert(!SelectNativeAudit(ledger,alias,10,1000));assert(SelectNativeAudit(ledger,second,10,1000)->second==1);
 current.jobs[1]=NativeAuditJob{second,0,2,100};assert(NativeAuditOperatorsIndependent(ledger,current));
 ledger.participants.at(Bytes{second.begin(),second.end()}).operator_id=actor_id;assert(!NativeAuditOperatorsIndependent(ledger,current));
 current.jobs[1].reset();current.jobs[0]->stage=NativeAuditJob::Assigned;current.jobs[0]->deadline=5;
 assert(SelectNativeAudit(ledger,actor,10,1000)->second==0);assert(SelectNativeAudit(ledger,alias,10,1000)->second==0);
 return 0;
}
