#undef NDEBUG
#include <pgn/native_work_duty_admission.h>
#include <cassert>
#include <iostream>
using namespace pgn::core;
static uint256 id(unsigned char n){uint256 out;out.begin()[0]=n;return out;}
int main(){
    NativeAuditLedger ledger;NativeDutySpent spent;
    const CScript miner{OP_1},owner{OP_2},other{OP_3},alias{OP_4};
    const auto enroll=[&](const CScript& a,uint256 op,bool allowed){ledger.participants.emplace(Bytes{a.begin(),a.end()},NativeAuditMembership{op,allowed});};
    enroll(miner,id(1),true);enroll(alias,id(1),true);enroll(owner,id(2),false);enroll(other,id(3),true);
    uint256 parent_hash=id(10);CBlockIndex parent;parent.phashBlock=&parent_hash;parent.nHeight=10;parent.nTime=1000;parent.nStatus=BLOCK_VALID_SCRIPTS;
    const auto inspect=[&](std::vector<NativeDutyReference> refs){return InspectNativeWorkDuty(ledger,parent,miner,refs,spent,0);};
    unsigned checks=0;
    auto check=[&](bool ok){assert(ok);++checks;};
    check(!ConfiguredNativeWorkDutyActivation());
    NativeWorkDutyActivation activation{id(1),id(2),id(3),1,2,0};check(activation.Valid());
    activation.height=1;check(!activation.Valid());activation.height=2;
    activation.first_assignment_height=3;check(!activation.Valid());
    parent.nStatus=BLOCK_VALID_TREE;check(inspect({}).verdict==NativeDutyVerdict::InvalidContext);parent.nStatus=BLOCK_VALID_SCRIPTS;
    check(inspect({}).verdict==NativeDutyVerdict::Ready); // Empty independent pool.
    check(InspectNativeWorkDuty(ledger,parent,owner,{},spent,0).verdict==NativeDutyVerdict::NotEligible);
    NativeReportAudit report;report.owner=owner;report.report.height=1;ledger.emplace(id(20),report);
    check(inspect({}).verdict==NativeDutyVerdict::AuditRequired);
    auto& first=ledger.at(id(20));first.jobs[0]=NativeAuditJob{miner,0,2,100};
    check(inspect({}).verdict==NativeDutyVerdict::AuditRequired); // Existing own assignment cannot be waived.
    first.jobs[0]->deadline=5;
    check(inspect({}).verdict==NativeDutyVerdict::AuditRequired); // Expiry requires reassignment, not free credit.
    first.jobs[0]->stage=NativeAuditJob::Committed;first.jobs[0]->commitment=std::string(64,'a');
    auto a=NativeDutyRef(id(20),0,*first.jobs[0]);
    check(inspect({}).verdict==NativeDutyVerdict::MissingClaim);
    check(inspect({a}).verdict==NativeDutyVerdict::Ready); // No other independent task for this miner.
    ledger.emplace(id(21),report);
    check(inspect({a}).verdict==NativeDutyVerdict::AuditRequired);
    auto& second=ledger.at(id(21));second.jobs[0]=NativeAuditJob{miner,1,3,100};
    second.jobs[0]->stage=NativeAuditJob::Passed;second.jobs[0]->commitment=std::string(64,'b');
    auto b=NativeDutyRef(id(21),0,*second.jobs[0]);
    check(inspect({a}).verdict==NativeDutyVerdict::MissingClaim);
    auto ready=inspect({a,b});check(ready.verdict==NativeDutyVerdict::Ready&&ready.proof.has_value());
    auto automatic=InspectAutomaticNativeWorkDuty(ledger,parent,miner,spent,0);
    check(automatic.checked.verdict==NativeDutyVerdict::Ready&&automatic.references==std::vector<NativeDutyReference>{a,b});
    check(inspect({a,a}).verdict==NativeDutyVerdict::InvalidClaim);
    auto bad=a;bad.attempt++;check(inspect({bad,b}).verdict==NativeDutyVerdict::InvalidClaim);
    bad=a;bad.commitment[0]='c';check(inspect({bad,b}).verdict==NativeDutyVerdict::InvalidClaim);
    bad=a;bad.slot=2;check(inspect({bad,b}).verdict==NativeDutyVerdict::InvalidClaim);
    check(InspectNativeWorkDuty(ledger,parent,alias,std::array{a,b},spent,0).verdict==NativeDutyVerdict::InvalidClaim);
    check(InspectNativeWorkDuty(ledger,parent,miner,std::array{a,b},spent,3).verdict==NativeDutyVerdict::InvalidClaim);
    check(!ConsumeNativeWorkDuty(*ready.proof,id(11),miner,id(30),spent)&&spent.empty());
    check(!ConsumeNativeWorkDuty(*ready.proof,parent_hash,alias,id(30),spent)&&spent.empty());
    check(ConsumeNativeWorkDuty(*ready.proof,parent_hash,miner,id(30),spent)&&spent.size()==2);
    check(ConsumeNativeWorkDuty(*ready.proof,parent_hash,miner,id(30),spent)&&spent.size()==2);
    check(!ConsumeNativeWorkDuty(*ready.proof,parent_hash,miner,id(31),spent)&&spent.size()==2);
    check(inspect({a,b}).verdict==NativeDutyVerdict::AlreadySpent);
    automatic=InspectAutomaticNativeWorkDuty(ledger,parent,miner,spent,0);
    check(automatic.checked.verdict==NativeDutyVerdict::Ready&&automatic.references.empty());
    // Competing branches use their canonical debit state; a rejected fork or
    // failed second debit never mutates the retained parent state.
    NativeDutySpent fork;fork.emplace(a,id(99));auto before=fork;
    check(!ConsumeNativeWorkDuty(*ready.proof,parent_hash,miner,id(32),fork)&&fork==before);
    spent.clear();second.jobs[0]->stage=NativeAuditJob::Failed;
    check(inspect({a,b}).verdict==NativeDutyVerdict::InvalidClaim);
    second.jobs[0]->stage=NativeAuditJob::Passed;first.owner=alias;
    check(inspect({a,b}).verdict==NativeDutyVerdict::InvalidClaim); // Same operator as owner.
    first.owner=owner;first.jobs[1]=NativeAuditJob{alias,0,2,100};
    check(inspect({a,b}).verdict==NativeDutyVerdict::InvalidClaim); // Two aliases cannot form independent slots.
    check(InspectNativeWorkDuty(ledger,parent,miner,std::array{a,b,a},spent,0).verdict==NativeDutyVerdict::InvalidContext);
    std::cout<<"{\"PASS\":true,\"checks\":"<<checks<<",\"native_ledger\":true,\"network_activated\":false}\n";
}
