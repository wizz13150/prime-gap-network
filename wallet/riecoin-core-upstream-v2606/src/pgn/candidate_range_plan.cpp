#include <pgn/candidate_range_plan.h>
#include <hash.h>
#include <kernel/cs_main.h>
#include <algorithm>
#include <iterator>
#include <limits>
namespace pgn::core {
CandidateRangeCheck PlanCandidateRanges(const CCoinsView& view,const CBlockIndex& parent,
    const CompleteMaturedTicketSeeds& due,SparseRangePool& pool,RangeDrawBudget budget){
    AssertLockHeld(cs_main);
    if(!MaturedTicketSeedsAtParent(view,parent,due))return {CandidateRangeVerdict::InvalidParent,{}};
    if(!budget.maximum_attempts||!budget.maximum_draw_bytes||!budget.maximum_hashes)return {CandidateRangeVerdict::InvalidBudget,{}};
    if(pool.FreeCount()<due.Seeds().size())return {CandidateRangeVerdict::PoolExhausted,{}};
    struct Restore {
        SparseRangePool& pool;std::vector<Integer> inserted;
        ~Restore(){for(const auto& slot:inserted)pool.Release(slot);}
    } restore{pool,{}};
    restore.inserted.reserve(due.Seeds().size());
    std::vector<CandidateRangeAssignment> assignments;assignments.reserve(due.Seeds().size());
    uint64_t hashes{};
    for(const auto& seed:due.Seeds()){
        const Integer count=pool.SelectionCount();Integer rank{};uint32_t attempt{};bool accepted=count==1;
        if(!accepted){
            const auto bits=boost::multiprecision::msb(Integer{count-1})+size_t{1};
            const size_t bytes=(bits+7)/8;
            if(bytes>budget.maximum_draw_bytes)return {CandidateRangeVerdict::BudgetExceeded,{},hashes};
            const size_t chunks=(bytes+31)/32;
            if(chunks>std::numeric_limits<uint32_t>::max())return {CandidateRangeVerdict::BudgetExceeded,{},hashes};
            std::vector<unsigned char> encodedCount;export_bits(count,std::back_inserter(encodedCount),8,true);
            // Hash potentially large domain metadata ONCE. Each draw chunk
            // copies the maintained incremental SHA state: no quadratic
            // rehashing of the full domain encoding for every output chunk.
            HashWriter base{TaggedHash("PGN/UNAPPROVED/RANGE-DRAW/1")};base<<seed.Value()<<encodedCount;
            std::vector<unsigned char> draw(bytes);
            for(;attempt<budget.maximum_attempts;++attempt){
                if(chunks>budget.maximum_hashes-hashes)return {CandidateRangeVerdict::BudgetExceeded,{},hashes};
                for(uint32_t chunk=0;chunk<chunks;++chunk){
                    auto hash=base;hash<<attempt<<chunk;const auto value=hash.GetHash();++hashes;
                    const size_t position=size_t{chunk}*32,n=std::min(size_t{32},bytes-position);
                    std::copy_n(value.begin(),n,draw.begin()+position);
                }
                draw.front()&=static_cast<unsigned char>(255u>>((8-bits%8)%8));
                import_bits(rank,draw.begin(),draw.end(),8,true);
                if(rank<count){accepted=true;break;}
            }
            if(!accepted)return {CandidateRangeVerdict::BudgetExceeded,{},hashes};
        }
        const auto selected=pool.SelectFree(rank);
        if(!selected)return {CandidateRangeVerdict::PoolChanged,{},hashes};
        // Every possibly allocating result/undo copy precedes the tree insert.
        assignments.push_back({seed.Confirmation().Request().Id(),seed.Value(),count,
            selected->slot,selected->start,selected->end,attempt});
        restore.inserted.push_back(selected->slot);
        if(!pool.Occupy(selected->slot)){restore.inserted.pop_back();return {CandidateRangeVerdict::PoolChanged,{},hashes};}
    }
    return {CandidateRangeVerdict::Planned,std::move(assignments),hashes};
}
}
