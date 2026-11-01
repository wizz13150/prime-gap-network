// MIT. Candidate public script-satisfaction proof, NOT ticket eligibility.
#pragma once
#include <common/signmessage.h>
#include <optional>
#include <span>
namespace pgn::core {
struct ScriptWorkContext {uint256 chain,work,parent;};
struct ScriptWorkBudget {size_t script_bytes,signature_bytes;};
enum class ScriptWorkVerdict {Verified,InvalidContext,InvalidBudget,BudgetExceeded,UnsupportedScript,InvalidSignature};
struct ScriptWorkCheck;
class VerifiedScriptWork {
    ScriptWorkContext m_context;CScript m_script;
    VerifiedScriptWork(ScriptWorkContext c,CScript s):m_context(std::move(c)),m_script(std::move(s)){}
    friend ScriptWorkCheck VerifyScriptWork(const ScriptWorkContext&,const CScript&,std::span<const unsigned char>,ScriptWorkBudget);
public:
    const ScriptWorkContext& Context()const{return m_context;}
    const CScript& Script()const{return m_script;}
};
struct ScriptWorkCheck {ScriptWorkVerdict verdict;std::optional<MessageVerificationResult> native_result;std::optional<VerifiedScriptWork> proof;};
// Native BIP322, no address encoding/Params, funded UTXO, balance or key search.
// Chain/work/parent/script are all challenge-bound. This proves ONLY current
// script satisfaction, not exclusive key knowledge, chain state freshness,
// first-ticket availability, reservation authority, Sybil resistance, valid
// science, bootstrap rules, emission, block work or consensus approval.
std::optional<std::string> ScriptWorkChallenge(const ScriptWorkContext&,const CScript&,ScriptWorkBudget);
ScriptWorkCheck VerifyScriptWork(const ScriptWorkContext&,const CScript&,std::span<const unsigned char>,ScriptWorkBudget);
}
