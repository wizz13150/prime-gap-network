// MIT. Candidate WP 4.3 delayed chain seed, NOT an unbiased-beacon claim.
#pragma once
#include <pgn/ticket_request_confirmation.h>
#include <utility>
namespace pgn::core {
class VerifiedRequestRegistry;
class VerifiedNativeBlockScience;
class NativeRequestHistoryArchive;
struct RequestHistoryArchiveBudget;
struct TicketSeedCheck;
class ConfirmedTicketSeed {
    ConfirmedTicketRequest m_confirmation;uint256 m_source,m_seed;int32_t m_height;
    // A verified-science seed retains its exact native claim provenance.
    // The old header-derived candidate remains distinguishable, never promoted.
    uint256 m_material,m_policy,m_fact,m_work;bool m_scientific{false};
    ConfirmedTicketSeed(ConfirmedTicketRequest confirmation,uint256 source,uint256 seed,int32_t height)
        :m_confirmation(std::move(confirmation)),m_source(source),m_seed(seed),m_height(height){}
    friend TicketSeedCheck DeriveTicketSeed(const CCoinsView&,const CBlockIndex&,const ConfirmedTicketRequest&,uint32_t);
    friend TicketSeedCheck DeriveScientificTicketSeed(const CCoinsView&,const CBlockIndex&,
        const ConfirmedTicketRequest&,uint32_t,const VerifiedNativeBlockScience&);
    friend bool TicketSeedOnBranch(const CCoinsView&,const CBlockIndex&,const ConfirmedTicketSeed&);
public:
    const ConfirmedTicketRequest& Confirmation()const{return m_confirmation;}
    const uint256& SourceBlock()const{return m_source;}
    const uint256& Value()const{return m_seed;}
    int32_t Height()const{return m_height;}
    bool HasScientificSource()const{return m_scientific;}
};
enum class TicketSeedVerdict {DerivedAtParent,InvalidDelay,InvalidParent,WaitingForLaterBlock,InvalidSeedBlock};
struct TicketSeedCheck {TicketSeedVerdict verdict;std::optional<ConfirmedTicketSeed> seed;};
// Caller holds cs_main. Explicit positive delay is a proposal input, not an
// adopted constant. Native validated ancestor at confirmation height + delay,
// NOT a caller-supplied hash or current tip (which would reroll every block).
// Candidate domain-separated Hash(later block hash, request ID), per WP 71-77.
// This does NOT prove resistance to producer grinding/withholding, choose a
// range, establish a request registry, mint a ticket or activate consensus.
TicketSeedCheck DeriveTicketSeed(const CCoinsView&,const CBlockIndex&,
    const ConfirmedTicketRequest&,uint32_t delay);
bool TicketSeedOnBranch(const CCoinsView&,const CBlockIndex&,const ConfirmedTicketSeed&);
// Positive delay, actual native validated source ancestor AND its retained
// exact scientific fact/forward state are mandatory. No header-hash fallback.
// The sealed proof cannot authorize an unaccepted proposal, orphan or rewind.
// Does NOT solve bootstrap, paid-fact selection/withholding bias or activate
// this source in the still-unapproved historical range-allocation candidate.
TicketSeedCheck DeriveScientificTicketSeed(const CCoinsView&,const CBlockIndex&,
    const ConfirmedTicketRequest&,uint32_t delay,const VerifiedNativeBlockScience&);

enum class MaturedSeedVerdict {CompleteAtParent,NoDueHeight,InvalidDelay,InvalidParent,IncompleteRegistry,
    MissingHistory,BudgetExceeded,InvalidHistory,BlockDataUnavailable,StorageError};
struct MaturedSeedCheck;
class CompleteMaturedTicketSeeds {
    uint256 m_chain,m_parent;int32_t m_height;
    std::vector<ConfirmedTicketSeed> m_seeds;
    CompleteMaturedTicketSeeds(uint256 chain,uint256 parent,int32_t height,std::vector<ConfirmedTicketSeed> seeds)
        :m_chain(chain),m_parent(parent),m_height(height),m_seeds(std::move(seeds)){}
    friend MaturedSeedCheck DeriveMaturedTicketSeeds(const CCoinsView&,const CBlockIndex&,
        const VerifiedRequestRegistry&,const NativeRequestHistoryArchive&,uint32_t,
        RequestHistoryArchiveBudget,ConfirmedRequestScanBudget,const VerifiedNativeBlockScience*);
public:
    CompleteMaturedTicketSeeds(const CompleteMaturedTicketSeeds&)=default;
    CompleteMaturedTicketSeeds& operator=(const CompleteMaturedTicketSeeds&)=default;
    CompleteMaturedTicketSeeds(CompleteMaturedTicketSeeds&& other)noexcept
        :m_chain(std::move(other.m_chain)),m_parent(std::move(other.m_parent)),
         m_height(std::exchange(other.m_height,-1)),m_seeds(std::move(other.m_seeds)){}
    CompleteMaturedTicketSeeds& operator=(CompleteMaturedTicketSeeds&& other)noexcept{
        if(this!=&other){m_chain=std::move(other.m_chain);m_parent=std::move(other.m_parent);
            m_height=std::exchange(other.m_height,-1);m_seeds=std::move(other.m_seeds);}return *this;
    }
    const uint256& Chain()const{return m_chain;}
    const uint256& Parent()const{return m_parent;}
    int32_t Height()const{return m_height;}
    std::span<const ConfirmedTicketSeed> Seeds()const{return m_seeds;}
};
struct MaturedSeedCheck {MaturedSeedVerdict verdict;std::optional<CompleteMaturedTicketSeeds> batch;};
// Native cs_main held. Derives ALL requests due at THIS current native height,
// after complete globally unique request replay. No supplied request subset,
// source hash, reward identity, nonce or target. Canonical order is native
// uint256 serialized-ID ordering, independent of coinbase output order.
// This is NOT an allocated ticket, proof of unbiasedness, range authority,
// bootstrap, monetary policy or consensus activation.
MaturedSeedCheck DeriveMaturedTicketSeeds(const CCoinsView&,const CBlockIndex&,
    const VerifiedRequestRegistry&,const NativeRequestHistoryArchive&,uint32_t delay,
    RequestHistoryArchiveBudget,ConfirmedRequestScanBudget,const VerifiedNativeBlockScience* scientific_source=nullptr);
// scientific_source != nullptr selects ONLY retained exact science for EVERY
// due request; any missing/mismatched source holds the whole batch. nullptr is
// explicitly the older unapproved header-source experiment, not production.
bool MaturedTicketSeedsAtParent(const CCoinsView&,const CBlockIndex&,const CompleteMaturedTicketSeeds&);
}
