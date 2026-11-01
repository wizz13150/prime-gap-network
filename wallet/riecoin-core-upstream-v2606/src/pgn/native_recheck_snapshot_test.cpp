#undef NDEBUG
#include <pgn/native_db_recheck.h>
#include <cassert>
using namespace pgn::core;
namespace {
uint256 Id(char value){return *uint256::FromHex(std::string(64,value));}
struct Base:CCoinsViewBacked {
    mutable unsigned money_reads{},fact_reads{},claim_reads{};
    unsigned writes{};
    FactCommit forward{FactHead{Id('1'),Id('2')},FactHead{Id('1'),Id('3')},{}};
    Base():CCoinsViewBacked(&CoinsViewEmpty::Get()){}
    uint256 GetBestBlock()const override{return Id('3');}
    std::optional<IssuanceCommit> GetPgnIssuanceForward(const uint256&)const override{++money_reads;return {};}
    std::optional<FactCommit> GetPgnFactForward(const uint256& key)const override{
        ++fact_reads;return key==Id('3')?std::optional{forward}:std::nullopt;
    }
    std::optional<FactClaim> GetPgnFact(const uint256&)const override{++claim_reads;return FactClaim{Id('4'),Id('3')};}
    void BatchWrite(CoinsViewCacheCursor&,const uint256&)override{++writes;}
};
}
int main(){
    LOCK(cs_main);Base base;assert(base.forward.Valid());NativeRecheckReadSnapshot snapshot{base,1U<<20};
    assert(!snapshot.GetPgnIssuanceForward(Id('3')));assert(!snapshot.GetPgnIssuanceForward(Id('3')));assert(base.money_reads==1);
    assert(snapshot.GetPgnFactForward(Id('3'))==base.forward);assert(snapshot.GetPgnFactForward(Id('3'))==base.forward);assert(base.fact_reads==1);
    assert(!snapshot.GetPgnFactForward(Id('5')));assert(!snapshot.GetPgnFactForward(Id('5')));assert(base.fact_reads==2);
    assert(snapshot.GetPgnFact(Id('6'))==FactClaim(Id('4'),Id('3')));
    assert(snapshot.GetPgnFact(Id('6'))==FactClaim(Id('4'),Id('3')));assert(base.claim_reads==1);
    assert(snapshot.MemoryUsage()>0&&snapshot.MemoryUsage()<=1U<<20);
    CCoinsViewCache child{&snapshot};child.SetBestBlock(Id('3'));
    bool rejected{};try{child.Flush();}catch(const std::logic_error&){rejected=true;}
    assert(rejected&&base.writes==0);
    snapshot.Relinquish();assert(snapshot.MemoryUsage()==0);
    assert(snapshot.GetPgnFactForward(Id('3'))==base.forward);assert(snapshot.GetPgnFactForward(Id('3'))==base.forward);
    assert(base.fact_reads==4&&snapshot.MemoryUsage()==0&&base.writes==0);
    return 0;
}
