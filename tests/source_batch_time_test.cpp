#include "../tools/source_batch_time.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int checks=0;
void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
std::string read(const char* path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("source fixture unavailable");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
void replaceOnce(std::string& text,const std::string& from,const std::string& to){
    const auto at=text.find(from);require(at!=std::string::npos&&text.find(from,at+from.size())==std::string::npos,"exactly one allowed source edit");
    text.replace(at,from.size(),to);
}
}
int main(int argc,char** argv){
    try{
        require(argc==3,"explicit current and conserved preimage CU paths required");
        constexpr std::uint64_t epoch=116444736000000000ULL,unit=10000000ULL;
        for(const auto ticks:{std::uint64_t(0),epoch-1,epoch,epoch+1,epoch+unit-1,
                              std::uint64_t(0x8000000000000000ULL),std::numeric_limits<std::uint64_t>::max()}){
            unsigned reads=0;
            const auto stamp=pgn::source::captureBatchPublicationTime([&]{++reads;return ticks;});
            require(reads==1,"invalid batch still reads clock only once");
            require(std::string(stamp.fields.data())==" time_basis=source_batch_publication time_status=unavailable","invalid clock has explicit absence, no fake0");
            char line[400];const auto size=pgn::source::formatSourceGap(line,sizeof(line),1.9,6,"23","29",stamp);
            require(size>0&&size<int(sizeof(line)),"invalid clock GAP fits buffer");
            require(std::string(line)=="[phase0-gpu] GAP merit=1.9000 gap=6 lower=23 upper=29 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown time_basis=source_batch_publication time_status=unavailable\n","invalid clock retains complete genuine GAP fields");
            require(std::string(line).find("unix_ts=")==std::string::npos,"invalid clock publishes no timestamp");
        }
        for(const auto seconds:{std::uint64_t(1),std::uint64_t(1791982800),std::uint64_t(1791982790)}){
            unsigned reads=0;
            const auto stamp=pgn::source::captureBatchPublicationTime([&]{++reads;return epoch+seconds*unit+9999999;});
            require(reads==1,"one valid clock read per batch");
            const auto expected=" unix_ts="+std::to_string(seconds)+" time_basis=source_batch_publication";
            require(std::string(stamp.fields.data())==expected,"integer seconds floor; positive rollback stays raw, no clamp");
            for(unsigned row=0;row<5000;++row){
                char line[400];const auto size=pgn::source::formatSourceGap(line,sizeof(line),1.9,6,"23","29",stamp);
                require(size>0&&size<int(sizeof(line))&&std::string(line).find(expected+"\n")!=std::string::npos,"all rows use SAME precomputed batch suffix");
            }
            require(reads==1,"formatting5000 rows did not reread clock");
        }
        const auto maxStamp=pgn::source::publicationTimeFromFileTime(0x7fffffffffffffffULL);
        require(std::string(maxStamp.fields.data()).find("unix_ts=")!=std::string::npos,"last representable Windows FILETIME accepted");
        char maximum[400];const auto size=pgn::source::formatSourceGap(maximum,sizeof(maximum),4294967295.125,
            std::numeric_limits<unsigned>::max(),"340282366920938463463374607427473244160","340282366920938463463374607431768211455",maxStamp);
        require(size>0&&size<int(sizeof(maximum))&&maximum[size-1]=='\n',"actual400B buffer holds widest128bit endpoints/uint32gap/timestamp");
        require(std::string(maximum).find("gap=4294967295 lower=340282366920938463463374607427473244160 upper=340282366920938463463374607431768211455")!=std::string::npos,"old argument widths and endpoints preserved");
        // This is a SOURCE boundary proof, not a CUDA compile/run. Undo exactly
        // three permitted edits and compare EVERY byte with the frozen preimage.
        auto current=read(argv[1]);const auto before=read(argv[2]);
        replaceOnce(current,"#include \"source_batch_time.h\"\n","");
        replaceOnce(current,"   const auto publication=pgn::source::captureBatchPublicationTime();\n","");
        replaceOnce(current,
            "    char line[400];pgn::source::formatSourceGap(line,sizeof(line),merit,g.gap,p.c_str(),q.c_str(),publication);\n",
            "    char line[400];std::snprintf(line,sizeof(line),\"[phase0-gpu] GAP merit=%.4f gap=%u lower=%s upper=%s verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown\\n\",merit,g.gap,p.c_str(),q.c_str());\n");
        require(current==before,"ALL remaining CU bytes unchanged: CUDA APIs/kernel/count/monotonic/checkpoint");
        const auto source=read(argv[1]);
        const auto acquired=source.find("MemoryBarrier();const auto* rows=h_results+");
        const auto capture=source.find("const auto publication=pgn::source::captureBatchPublicationTime();",acquired);
        const auto rowLoop=source.find("for(unsigned row=1;row<=count;++row)",acquired);
        require(acquired!=std::string::npos&&capture>acquired&&capture<rowLoop,"capture follows batch acquisition and precedes row loop");
        std::cout<<"SOURCE_BATCH_TIME "<<checks<<" checks / 0 failures; injected clock once/batch, invalid absence preserves GAP, raw rollback, exact3-edit CU source conservation; HOST ONLY, no CUDA compile/run\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"SOURCE_BATCH_TIME failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
