// Copyright (c) 2026 The PGN developers. MIT.
#include <pgn/ticket_atlas_projection.h>
#include <iostream>
#include <random>
using namespace pgn::core;
int main(){
    size_t checks{};auto need=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Atlas oracle mismatch");};
    try{
        std::mt19937_64 random{0xa71a5};
        for(unsigned trial=0;trial<1024;++trial){
            const size_t span=1+random()%521,requested=1+random()%512;
            const Integer origin=(Integer(1)<<95)-1000+trial;
            TicketAtlasProjection view(origin,origin+span,requested);
            std::vector<unsigned> oracle(span,5);size_t position{},visible{};
            while(position<span){
                position+=random()%5;if(position>=span)break;
                const size_t end=std::min(span,position+1+random()%19),stage=random()%5;
                need(view.Add(origin+position,origin+end,AtlasStage(stage)));++visible;
                for(size_t i=position;i<end;++i)oracle[i]=stage;
                position=end;
            }
            need(!view.Add(origin+span+1,origin+span+3,AtlasStage::Assigned));
            need(view.AllAssignments()==visible+1);
            std::array<Integer,5> sums{};Integer previous=origin;
            for(const auto& bin:view.Bins()){
                need(bin.start==previous&&bin.end>bin.start);previous=bin.end;
                for(size_t s=0;s<5;++s){
                    size_t expected{};
                    for(Integer x=bin.start;x<bin.end;++x)expected+=oracle[(x-origin).convert_to<size_t>()]==s;
                    need(bin.stages[s].positions==expected);sums[s]+=bin.stages[s].positions;
                }
            }
            need(previous==origin+span);
            for(size_t s=0;s<5;++s)need(sums[s]==view.Totals()[s].positions);
        }
        const Integer largeOrigin("150000000000000000000"),ticketWidth("200000000000000");
        TicketAtlasProjection population(largeOrigin,largeOrigin+ticketWidth*10000,512);
        for(unsigned i=0;i<10000;++i)population.Add(largeOrigin+ticketWidth*i,largeOrigin+ticketWidth*(i+1),AtlasStage(i%5));
        need(population.AllAssignments()==10000&&population.Bins().size()==512);
        for(const auto& bin:population.Bins()){Integer sum{};for(const auto& state:bin.stages)sum+=state.positions;need(sum==bin.end-bin.start);}
        for(const auto& state:population.Totals())need(state.tickets==2000&&state.positions==ticketWidth*2000);
        TicketAtlasProjection corner(0,101,100);corner.Add(33,34,AtlasStage::Passed);
        need(corner.Bins()[33].stages[3].positions==1&&corner.Bins()[32].stages[3].positions==0);
        TicketAtlasProjection huge(Integer("150000000000000000000"),(Integer(1)<<95),512);
        const Integer end=(Integer(1)<<95);huge.Add(end-1,end,AtlasStage::Assigned);
        need(huge.Bins().back().stages[0].positions==1);
        bool rejected=false;try{huge.Add(end-1,end,AtlasStage::Assigned);}catch(const std::invalid_argument&){rejected=true;}need(rejected);
        for(int n:{0,513}){rejected=false;try{TicketAtlasProjection bad(0,100,n);}catch(const std::invalid_argument&){rejected=true;}need(rejected);}
        std::cout<<"PASS "<<checks<<" atlas integer checks;1024 independent per-position grids,10000ticket/512binpopulation,95bitorigin,indivisible bins,clipping,overlap and budgets\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
