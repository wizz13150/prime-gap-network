#undef NDEBUG
#include <pgn/native_report_audit.h>
#include <cassert>
using namespace pgn::core;
int main(){
 const auto genesis=*uint256::FromHex("0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d");
 const auto rules=ConfiguredNativeAuditOperatorRules(genesis);
 assert(rules&&rules->chain==genesis&&rules->activation_height==16064);
 assert(!ConfiguredNativeAuditOperatorRules(uint256{}));
 auto different=genesis;different.begin()[0]^=1;
 assert(!ConfiguredNativeAuditOperatorRules(different));
 NativeAuditLedger ledger;ledger.audit_chain=genesis;ledger.audit_operator_rules=rules;
 assert(!NativeAuditRecheckAt(ledger,-1));
 assert(!NativeAuditRecheckAt(ledger,0));
 assert(!NativeAuditRecheckAt(ledger,16062));
 assert(NativeAuditRecheckAt(ledger,16063));
 assert(NativeAuditRecheckAt(ledger,16064));
 ledger.audit_chain=different;assert(!NativeAuditRecheckAt(ledger,16064));
 return 0;
}
