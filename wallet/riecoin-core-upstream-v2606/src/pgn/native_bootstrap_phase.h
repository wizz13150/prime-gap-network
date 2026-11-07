// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <chain.h>
#include <coins.h>
#include <validation.h>
#include <deque>
#include <map>
#include <vector>

namespace pgn::core {
enum class NativeBootstrapPhaseVerdict {Bootstrap,Scientific,InvalidParent,HistoryHeld,BudgetExceeded};
// The same pinned continuation rule governs template creation and body acceptance.
inline bool NativeLedgerAllowsCoordination(NativeBootstrapPhaseVerdict phase,bool continuation){
    return phase==NativeBootstrapPhaseVerdict::Bootstrap||
        (phase==NativeBootstrapPhaseVerdict::Scientific&&continuation);
}
class NativeBootstrapPhaseCursor;
NativeBootstrapPhaseVerdict InspectNativeBootstrapPhaseSlice(
    const CCoinsView&,const CBlockIndex&,uint32_t,NativeBootstrapPhaseCursor&);

// A process-local, bounded witness index and continuation of verified ancestry.
// Only the inspector can advance it or install a completed phase witness.
// It is neither a serialized peer claim nor an accepted scientific result.
class NativeBootstrapPhaseCursor {
    static constexpr size_t MAX_WITNESSES=65536;
    uint256 m_chain,m_target,m_cursor;
    int32_t m_cursor_height{-1};
    std::map<uint256,NativeBootstrapPhaseVerdict> m_witnesses;
    std::deque<uint256> m_order;
    std::vector<uint256> m_prefix;
    bool m_pending{};
    friend NativeBootstrapPhaseVerdict InspectNativeBootstrapPhaseSlice(
        const CCoinsView&,const CBlockIndex&,uint32_t,NativeBootstrapPhaseCursor&);
public:
    void Reset(){*this=NativeBootstrapPhaseCursor{};}
    std::optional<int32_t> PendingHeight()const{return m_pending?std::optional{m_cursor_height}:std::nullopt;}
};

inline NativeBootstrapPhaseVerdict InspectNativeBootstrapPhaseSlice(
    const CCoinsView& view,const CBlockIndex& parent,uint32_t maximum,
    NativeBootstrapPhaseCursor& cursor)
{
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    if(!head||!head->Valid()||!parent.phashBlock||parent.nHeight<0||
       !parent.IsValid(BLOCK_VALID_SCRIPTS)||view.GetBestBlock()!=parent.GetBlockHash()||
       head->tip!=parent.GetBlockHash())return NativeBootstrapPhaseVerdict::InvalidParent;
    if(!maximum)return NativeBootstrapPhaseVerdict::BudgetExceeded;
    if(cursor.m_chain!=head->chain){cursor.Reset();cursor.m_chain=head->chain;}
    const CBlockIndex* index=&parent;bool resumed{};
    if(cursor.m_pending&&cursor.m_target==parent.GetBlockHash()){
        const auto* saved=parent.GetAncestor(cursor.m_cursor_height);
        if(saved&&saved->phashBlock&&saved->GetBlockHash()==cursor.m_cursor){index=saved;resumed=true;}
    }
    if(!resumed)cursor.m_prefix.clear();
    cursor.m_pending=false;
    const auto complete=[&](NativeBootstrapPhaseVerdict phase){
        // Only a completed proof can populate this index. Store near-parent
        // witnesses last so reverse database checks reuse them immediately.
        const auto remember=[&](const uint256& block){
            const auto found=cursor.m_witnesses.find(block);
            if(found!=cursor.m_witnesses.end())return found->second==phase;
            cursor.m_witnesses.emplace(block,phase);cursor.m_order.push_back(block);
            while(cursor.m_witnesses.size()>NativeBootstrapPhaseCursor::MAX_WITNESSES){
                cursor.m_witnesses.erase(cursor.m_order.front());cursor.m_order.pop_front();
            }
            return true;
        };
        try{
            for(auto it=cursor.m_prefix.rbegin();it!=cursor.m_prefix.rend();++it)
                if(!remember(*it)){cursor.Reset();return NativeBootstrapPhaseVerdict::HistoryHeld;}
            if(!remember(parent.GetBlockHash())){cursor.Reset();return NativeBootstrapPhaseVerdict::HistoryHeld;}
        }catch(...){cursor.Reset();throw;}
        cursor.m_prefix.clear();cursor.m_pending=false;return phase;
    };
    uint32_t checked{};
    while(index){
        if(!index->phashBlock||!index->IsValid(BLOCK_VALID_SCRIPTS))
            return NativeBootstrapPhaseVerdict::HistoryHeld;
        // A previous witness is useful only if it is actually reached on this
        // accepted branch. Equal heights, process state or peer hints do not suffice.
        const auto known=cursor.m_witnesses.find(index->GetBlockHash());
        if(known!=cursor.m_witnesses.end())return complete(known->second);
        if(index->nHeight==0)return complete(NativeBootstrapPhaseVerdict::Bootstrap);
        if(checked==maximum){
            cursor.m_target=parent.GetBlockHash();cursor.m_cursor=index->GetBlockHash();
            cursor.m_cursor_height=index->nHeight;cursor.m_pending=true;
            return NativeBootstrapPhaseVerdict::BudgetExceeded;
        }
        ++checked;
        if(!index->pprev||!index->pprev->phashBlock||
           index->pprev->nHeight!=index->nHeight-1)
            return NativeBootstrapPhaseVerdict::HistoryHeld;
        const auto step=view.GetPgnFactForward(index->GetBlockHash());
        if(!step||!step->Valid()||
           step->before!=std::optional{FactHead{head->chain,index->pprev->GetBlockHash()}}||
           step->after!=FactHead{head->chain,index->GetBlockHash()})
            return NativeBootstrapPhaseVerdict::HistoryHeld;
        if(cursor.m_prefix.size()<NativeBootstrapPhaseCursor::MAX_WITNESSES)
            cursor.m_prefix.push_back(index->GetBlockHash());
        if(!step->changes.empty())return complete(NativeBootstrapPhaseVerdict::Scientific);
        index=index->pprev;
    }
    return NativeBootstrapPhaseVerdict::HistoryHeld;
}

inline NativeBootstrapPhaseVerdict InspectNativeBootstrapPhase(
    const CCoinsView& view,const CBlockIndex& parent,uint32_t maximum)
{
    AssertLockHeld(cs_main);
    static NativeBootstrapPhaseCursor cursor;
    int64_t before=int64_t(parent.nHeight)+1;
    for(;;){
        const auto result=InspectNativeBootstrapPhaseSlice(view,parent,maximum,cursor);
        if(result!=NativeBootstrapPhaseVerdict::BudgetExceeded)return result;
        const auto pending=cursor.PendingHeight();
        if(!pending||*pending>=before)return result;
        before=*pending;
    }
}
}
