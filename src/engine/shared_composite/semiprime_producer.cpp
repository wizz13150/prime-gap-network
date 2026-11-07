// Complete scoped residual producer; no GPU, allocation, report or network authority.
#include "semiprime_tail.h"
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace pgn_psp_tail;
using U128=unsigned __int128;
static_assert(std::endian::native==std::endian::little,"Witness wire format requires explicit little-endian encoding");
namespace {
template<class Character> U128 parse(const Character* text) {
    std::basic_string<Character> s{text};require(!s.empty()&&s.size()<=29&&(s.size()==1||s[0]!='0'),"Canonical decimal input required");
    U128 n=0;
    for(auto c:s){require(c>='0'&&c<='9',"Unsigned decimal input required");n=n*10+unsigned(c-'0');}
    return n;
}
std::string decimal(U128 n) {
    std::string s;do{s.push_back(char('0'+n%10));n/=10;}while(n);
    std::reverse(s.begin(),s.end());return s;
}
Number wide(U128 n){return{uint64_t(n),uint64_t(n>>64)};}
uint64_t square_root(U128 n) {
    uint64_t lo=0,hi=MaximumPrimeEnd-1;
    while(lo<hi){const auto mid=lo+(hi-lo+1)/2;if(U128(mid)*mid<=n)lo=mid;else hi=mid-1;}
    require(lo<MaximumPrimeEnd-1,"Factor domain exceeded");return lo;
}
struct Output {
    U128 begin,end;
    std::ofstream file;
    uint64_t witnesses{};
};
}

// Windows must preserve the UTF-16 path supplied by CreateProcessW. Narrow
// argv depends on the machine's ANSI code page and can corrupt a user's folder.
#ifdef _WIN32
int wmain(int argc,wchar_t** argv)try {
#else
int main(int argc,char** argv)try {
#endif
    if(argc==2&&std::filesystem::path(argv[1])=="--describe"){
        std::cout<<"{\"schema\":\"PGN_SCOPED_SEMIPRIME_PRODUCER_V1\",\"GPU\":false,\"maximum_scopes\":64,\"maximum_positions\":\"1000000000000000\",\"cutoff_maximum\":10000000,\"condition\":\"Residual has no primefactor<=cutoff and number<cutoff^3\",\"claims_prime_end_sentinels\":false,\"claims_full95_coverage\":false}\n";return 0;
    }
    require(argc>=5&&argc<=131&&(argc-3)%2==0,"Usage: cutoff fresh-output-directory begin end [begin end ...], up to64 disjoint intervals");
    const auto cutoff128=parse(argv[1]);require(cutoff128>=2&&cutoff128<=10000000,"Cutoff outside qualified domain");
    const auto cutoff=uint64_t(cutoff128);const size_t count=size_t(argc-3)/2;
    std::vector<Scope> scopes;scopes.reserve(count);U128 total=0,prior=0;
    for(size_t i=0;i<count;++i){
        const auto a=parse(argv[3+2*i]),b=parse(argv[4+2*i]);
        scopes.emplace_back(cutoff,wide(a),wide(b));
        require(!i||a>=prior,"Input scopes must be sorted and disjoint");
        require(b-a<=1000000000000000ULL-total,"Batch positions exceed1e15");
        prior=b;total+=b-a;
    }
    const auto last=square_root(parse(argv[argc-1])-1)+1;
    const std::filesystem::path output{argv[2]};
    require(!std::filesystem::exists(output)&&std::filesystem::create_directory(output),"Fresh output directory required; never overwrite partial or completed evidence");
    std::vector<Output> outputs;outputs.reserve(count);
    for(size_t i=0;i<count;++i){
        const auto a=parse(argv[3+2*i]),b=parse(argv[4+2*i]);
        outputs.push_back(Output{a,b,std::ofstream(output/("scope-"+std::to_string(i)+".factors"),std::ios::binary|std::ios::out)});
        require(bool(outputs.back().file),"Cannot create scoped witness output");
    }
    uint64_t rows=0,candidates=0,passes=0,peak=0,subdivisions=0,emitted=0;
    const auto began=std::chrono::steady_clock::now();
    for(uint64_t begin=cutoff+1;begin<last;){
        const auto end=begin+std::min<uint64_t>(16000000,last-begin);
        const auto stats=stream_certified(begin,end,1048576,[&](CertifiedPrimeOrder certified){
            const auto row=certified.value();const U128 step=U128(row.prime)*row.order;
            for(size_t i=0;i<count;++i){
                auto& target=outputs[i];
                const auto emit=[&](CompositeWitness w){
                    const U128 n=(U128(w.n.high)<<64)|w.n.low;
                    require(target.begin<=n&&n<target.end&&U128(w.factor)*w.cofactor==n,"Scoped product or ownership mismatch");
                    require(emitted<262144,"Batch witness budget reached; no complete receipt");
                    const uint64_t values[]{uint64_t(n-target.begin),w.factor,w.cofactor};
                    static_assert(sizeof(values)==24);
                    target.file.write(reinterpret_cast<const char*>(values),sizeof(values));
                    require(bool(target.file),"Witness write failed");++target.witnesses;++emitted;
                };
                // Split the exact half-open interval for rare low-order rows.
                // No candidate suffix can disappear behind a work budget.
                for(U128 a=target.begin;a<target.end;){
                    const U128 stop=std::min(target.end,a+step*999999);
                    const auto part=visit_row(Scope{cutoff,wide(a),wide(stop)},certified,emit,1000000);
                    candidates+=part.odd_progression_events;passes+=part.cofactor_passes;
                    if(a!=target.begin||stop!=target.end)++subdivisions;
                    a=stop;
                }
            }
        });
        rows+=stats.rows;peak=std::max(peak,stats.peak_array_bytes);
        for(auto& item:outputs){item.file.flush();require(bool(item.file),"Witness flush failed");}
        std::cout<<"{\"factor_end_exclusive\":"<<end<<",\"target_factor_end_exclusive\":"<<last<<",\"rows\":"<<rows<<",\"cofactor_passes\":"<<passes<<"}\n"<<std::flush;
        begin=end;
    }
    for(auto& item:outputs){item.file.close();require(bool(item.file),"Witness close failed");}
    const auto temporary=output/"COMPLETE.json.tmp";
    std::ofstream receipt(temporary);require(bool(receipt),"Cannot open final receipt");receipt.precision(12);
    receipt<<"{\"schema\":\"PGN_SCOPED_SEMIPRIME_PRODUCER_V1\",\"wire_format\":\"offset_factor_cofactor_u64le\",\"complete_factor_scan\":true,\"cutoff\":"<<cutoff
        <<",\"factor_begin\":"<<cutoff+1<<",\"factor_end_exclusive\":"<<last<<",\"factor_rows\":"<<rows
        <<",\"generated_candidates\":"<<candidates<<",\"cofactor_passes\":"<<passes<<",\"main_array_peak_bytes\":"<<peak
        <<",\"subdivided_intervals\":"<<subdivisions<<",\"positions\":\""<<decimal(total)<<"\",\"GPU_used\":false,\"scopes\":[";
    for(size_t i=0;i<count;++i){if(i)receipt<<',';const auto& item=outputs[i];
        receipt<<"{\"begin\":\""<<decimal(item.begin)<<"\",\"end_exclusive\":\""<<decimal(item.end)
            <<"\",\"witnesses\":"<<item.witnesses<<",\"file\":\"scope-"<<i<<".factors\"}";
    }
    receipt<<"],\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()
        <<",\"prime_ended_coverage_proved\":false,\"full95_coverage\":false,\"production_admitted\":false}\n";
    receipt.flush();require(bool(receipt),"Final receipt flush failed");receipt.close();require(bool(receipt),"Final receipt close failed");
    // Consumers require a successful owned process receipt as well as COMPLETE.
    // Failed/terminated executions leave partial files, never this authority.
    std::filesystem::rename(temporary,output/"COMPLETE.json");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
