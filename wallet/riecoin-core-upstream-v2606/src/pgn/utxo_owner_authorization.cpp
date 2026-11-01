// MIT. Uses actual Core coins view and maintained native BIP-322 verifier.
#include <pgn/utxo_owner_authorization.h>
#include <pgn/script_work_proof.h>
#include <coins.h>
#include <hash.h>
#include <script/solver.h>
namespace pgn::core {
namespace {
bool ValidScriptBudget(ScriptWorkBudget b){return b.script_bytes&&b.signature_bytes&&b.script_bytes<=1024*1024&&b.signature_bytes<=1024*1024;}
}
std::optional<std::string> ScriptWorkChallenge(const ScriptWorkContext& c,const CScript& script,ScriptWorkBudget budget){
    if(!ValidScriptBudget(budget)||c.chain.IsNull()||c.work.IsNull()||c.parent.IsNull()||script.empty()||script.size()>budget.script_bytes)return {};
    HashWriter h{TaggedHash("PGN/UNAPPROVED/SCRIPT-WORK/1")};h<<c.chain<<c.work<<c.parent<<script;
    return "PGN/UNAPPROVED/SCRIPT-WORK/1:"+h.GetHash().GetHex();
}
ScriptWorkCheck VerifyScriptWork(const ScriptWorkContext& c,const CScript& script,std::span<const unsigned char> signature,ScriptWorkBudget budget){
    if(!ValidScriptBudget(budget))return {ScriptWorkVerdict::InvalidBudget,{},{}};
    if(script.size()>budget.script_bytes||signature.size()>budget.signature_bytes)return {ScriptWorkVerdict::BudgetExceeded,{},{}};
    const auto challenge=ScriptWorkChallenge(c,script,budget);if(!challenge)return {ScriptWorkVerdict::InvalidContext,{},{}};
    CTxDestination destination;if(!ExtractDestination(script,destination))return {ScriptWorkVerdict::UnsupportedScript,{},{}};
    std::vector<unsigned char> bounded(signature.begin(),signature.end());
    const auto result=MessageVerifyBIP322(destination,bounded,*challenge,MessageVerificationResult::ERR_INVALID);
    if(result!=MessageVerificationResult::OK)return {ScriptWorkVerdict::InvalidSignature,result,{}};
    return {ScriptWorkVerdict::Verified,result,VerifiedScriptWork{c,script}};
}
std::optional<std::string> UtxoOwnerChallenge(const UtxoOwnerContext& c){
    if(c.chain.IsNull()||c.work.IsNull()||c.expected_tip.IsNull()||c.owner_coin.IsNull())return {};
    HashWriter h{TaggedHash("PGN/UNAPPROVED/UTXO-OWNER/1")};h<<c.chain<<c.work<<c.expected_tip<<c.owner_coin;
    return "PGN/UNAPPROVED/UTXO-OWNER/1:"+h.GetHash().GetHex();
}
UtxoOwnerCheck VerifyUtxoOwner(const CCoinsView& view,const UtxoOwnerContext& c,std::span<const unsigned char> signature,size_t budget){
    // Explicit caller budget, not a newly chosen consensus default.
    if(!budget||budget>1024*1024)return {UtxoOwnerVerdict::InvalidBudget,{},{}};
    const auto message=UtxoOwnerChallenge(c);if(!message)return {UtxoOwnerVerdict::InvalidContext,{},{}};
    if(signature.size()>budget)return {UtxoOwnerVerdict::SignatureBudgetExceeded,{},{}};
    if(view.GetBestBlock()!=c.expected_tip)return {UtxoOwnerVerdict::WrongTip,{},{}};
    const auto coin=view.GetCoin(c.owner_coin);if(!coin||coin->IsSpent())return {UtxoOwnerVerdict::MissingCoin,{},{}};
    CTxDestination destination;if(!ExtractDestination(coin->out.scriptPubKey,destination))return {UtxoOwnerVerdict::UnsupportedScript,{},{}};
    std::vector<unsigned char> bounded(signature.begin(),signature.end());
    const auto result=MessageVerifyBIP322(destination,bounded,*message,MessageVerificationResult::ERR_INVALID);
    // Timelocked or inconclusive cannot become current unconditional authority.
    if(result!=MessageVerificationResult::OK)return {UtxoOwnerVerdict::InvalidSignature,result,{}};
    const auto after=view.GetCoin(c.owner_coin);
    if(view.GetBestBlock()!=c.expected_tip||!after||after->IsSpent()||after->out!=coin->out)return {UtxoOwnerVerdict::StateChanged,result,{}};
    return {UtxoOwnerVerdict::Authorized,result,UtxoOwnerAuthorization{c,coin->out.scriptPubKey}};
}
}
