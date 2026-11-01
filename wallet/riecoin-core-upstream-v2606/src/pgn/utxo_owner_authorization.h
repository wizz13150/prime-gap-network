// MIT. UNAPPROVED envelope candidate, no ticket/PoW/consensus activation.
#pragma once
#include <common/signmessage.h>
#include <optional>
#include <span>
class CCoinsView;
namespace pgn::core {
// Caller holds native chainstate read lock; obtains identities from its trusted
// registry. Script satisfaction is NOT exclusive key knowledge, eligibility,
// maturity/expiry, scientific validity, duplicate admission, reward or coverage.
struct UtxoOwnerContext {uint256 chain,work,expected_tip;COutPoint owner_coin;};
enum class UtxoOwnerVerdict {Authorized,InvalidContext,InvalidBudget,WrongTip,MissingCoin,UnsupportedScript,SignatureBudgetExceeded,InvalidSignature,StateChanged};
struct UtxoOwnerCheck;
UtxoOwnerCheck VerifyUtxoOwner(const CCoinsView&,const UtxoOwnerContext&,std::span<const unsigned char>,size_t signature_budget);
class UtxoOwnerAuthorization {
    UtxoOwnerContext m_context;CScript m_script;
    UtxoOwnerAuthorization(UtxoOwnerContext c,CScript s):m_context(std::move(c)),m_script(std::move(s)){}
    friend UtxoOwnerCheck VerifyUtxoOwner(const CCoinsView&,const UtxoOwnerContext&,std::span<const unsigned char>,size_t);
public:
    const UtxoOwnerContext& Context()const{return m_context;}
    const CScript& Script()const{return m_script;}
};
struct UtxoOwnerCheck {UtxoOwnerVerdict verdict;std::optional<MessageVerificationResult> native_result;std::optional<UtxoOwnerAuthorization> authorization;};
// Public challenge only: no key, search/device, address Params or IO.
std::optional<std::string> UtxoOwnerChallenge(const UtxoOwnerContext&);
}
