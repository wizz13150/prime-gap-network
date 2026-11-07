#pragma once
// Private full-worker seam. Every imported composite is rechecked natively.
// A valid page is not a claim of exhaustive pseudoprime coverage.
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace pgn_composite_page {
struct Pair { uint64_t low, high; };
inline bool product(Pair p,Pair q,Pair n) {
    if((p.high==0&&p.low<2)||(q.high==0&&q.low<2))return false;
    const uint32_t a[4]={uint32_t(p.low),uint32_t(p.low>>32),uint32_t(p.high),uint32_t(p.high>>32)};
    const uint32_t b[4]={uint32_t(q.low),uint32_t(q.low>>32),uint32_t(q.high),uint32_t(q.high>>32)};
    uint32_t result[8]{};
    for(unsigned i=0;i<4;++i){
        uint64_t carry=0;
        for(unsigned j=0;j<4;++j){const uint64_t t=uint64_t(a[i])*b[j]+result[i+j]+carry;result[i+j]=uint32_t(t);carry=t>>32;}
        result[i+4]=uint32_t(carry);
    }
    return result[0]==uint32_t(n.low)&&result[1]==uint32_t(n.low>>32)&&result[2]==uint32_t(n.high)&&result[3]==uint32_t(n.high>>32)&&!(result[4]|result[5]|result[6]|result[7]);
}
struct Page { uint64_t low=0,high=0,length=0;std::vector<uint32_t> offsets,slots; };
inline Page load(const std::string& path,uint64_t requestLow,uint64_t requestHigh,uint64_t requestLength){
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Composite page cannot open");
    const auto bytes=file.tellg();
    if(bytes<40||bytes>40+65536*40)throw std::runtime_error("Composite page byte envelope");
    file.seekg(0);std::array<char,8> magic{};file.read(magic.data(),8);
    if(magic!=std::array<char,8>{'P','G','N','C','M','P','0','1'})throw std::runtime_error("Composite page schema");
    auto word=[&](){uint64_t v=0;for(unsigned i=0;i<8;++i){char c=0;if(!file.get(c))throw std::runtime_error("Truncated composite page");v|=uint64_t(static_cast<unsigned char>(c))<<(8*i);}return v;};
    Page page;page.low=word();page.high=word();page.length=word();const uint64_t count=word();
    if(page.low!=requestLow||page.high!=requestHigh||page.length!=requestLength||!page.length||page.length>1000000000000ull||page.high>0x7fffffffull||count>65536||uint64_t(bytes)!=40+40*count||((page.low%30)+(page.high%30)*16)%30)throw std::runtime_error("Composite page request identity/domain");
    constexpr uint64_t Span=30ull<<25,Tile=1ull<<19;
    const size_t owners=size_t((page.length+Span-1)/Span)*512;
    page.offsets.assign(owners+1,0);page.slots.reserve(size_t(count));
    const int classes[30]={-1,0,-1,-1,-1,-1,-1,1,-1,-1,-1,2,-1,3,-1,-1,-1,4,-1,5,-1,-1,-1,6,-1,-1,-1,-1,-1,7};
    uint64_t previous=0;
    for(uint64_t i=0;i<count;++i){
        const uint64_t offset=word();const Pair p{word(),word()},q{word(),word()};
        const Pair n{page.low+offset,page.high+uint64_t(page.low+offset<page.low)};
        const int residue=classes[(n.low%30+(n.high%30)*16)%30];
        if(offset>=page.length||(i&&offset<=previous)||residue<0||!product(p,q,n))throw std::runtime_error("False/unordered composite witness");
        previous=offset;const uint64_t local=offset%Span;
        const uint64_t slot=local/30*8+unsigned(residue),owner=offset/Span*512+slot/Tile;
        if(owner>=owners)throw std::runtime_error("Composite page owner escaped");
        ++page.offsets[size_t(owner)+1];page.slots.push_back(uint32_t(slot%Tile));
    }
    for(size_t i=1;i<page.offsets.size();++i)page.offsets[i]+=page.offsets[i-1];
    if(page.offsets.back()!=count)throw std::runtime_error("Composite page lost an event");
    return page;
}
}
