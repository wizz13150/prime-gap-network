#include "mining/prime_gap_source_stream.h"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=2)return 2;unsigned checks=0;
    auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Source stream check "+std::to_string(checks));};
    using namespace pgn::mining;
    try{
        QFile file(QString::fromLocal8Bit(argv[1]));check(file.open(QIODevice::ReadOnly)&&file.size()<65536);
        const auto raw=file.readAll();const auto job=primeGapEngineJob("20000000000000000000000000000","20000000000000002000000000000",64);
        auto consume=[&](const QByteArray& source,const PrimeGapEngineJob& j){PrimeGapSourceStream parser(j);for(auto line:source.split('\n')){if(line.endsWith('\r'))line.chop(1);parser.row(line);}parser.finish(true);return parser;};
        const auto good=consume(raw,job);check(good.newRecords()==8&&good.counters().gaps==8&&good.counters().tests==5198622089ULL&&good.counters().positions==2000000000000ULL&&good.counters().batches==32);
        auto rejected=[&](const QByteArray& source,const PrimeGapEngineJob& j){bool no=false;try{consume(source,j);}catch(const std::exception&){no=true;}check(no);};
        for(const auto pair:{qMakePair(QByteArray("EXACT_ENDPOINTS_GPU_INTERIOR"),QByteArray("GPU_MR12_GAP")),qMakePair(QByteArray("EXACT_ENDPOINTS_EULER_COMPOSITE"),QByteArray("GPU_MR12_EULER_COMPOSITE")),qMakePair(QByteArray("descriptors=32"),QByteArray("descriptors=31")),qMakePair(QByteArray("emitted_gaps=3"),QByteArray("emitted_gaps=2"))}){
            auto changed=raw;changed.replace(pair.first,pair.second);check(changed!=raw);rejected(changed,job);
        }
        auto changed=raw;const auto a=changed.indexOf("[pgn-counters]");const auto b=changed.indexOf('\n',a);changed.remove(a,b-a+1);rejected(changed,job);
        changed=raw;changed.replace("[phase0-gpu] --- end ---","[phase0-gpu] --- truncated ---");rejected(changed,job);
        rejected(raw+"[pgn-counters] malformed\n",job);
        auto wrong=primeGapEngineJob(job.start,job.end,8);rejected(raw,wrong);
        wrong=primeGapEngineJob(job.start,job.end,64,1400);rejected(raw,wrong);
        std::cout<<"Native67..95 source stream PASS "<<checks<<" checks; actual95 stream32descriptors/8gaps exact\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
