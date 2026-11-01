#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include "mining/prime_gap_engine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);unsigned checks=0;auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Engine booking checkpoint check "+std::to_string(checks));};
    try{
        QTemporaryDir temp;check(temp.isValid());const auto file=temp.filePath("owned-fixture.sqlite");
        pgn::booking::BookingStore store(file);check(store.valid());
        for(int bits=67;bits<=95;++bits){
            QString start="1";for(int i=0;i<bits-1;++i)start=pgn::booking::addUnsigned(start,start);start=pgn::booking::addUnsigned(start,"100000");
            const auto end=pgn::booking::addUnsigned(start,"1000000000000");const auto job=pgn::mining::primeGapEngineJob(start,end,64);
            pgn::mining::SourceCheckpointResume resume;resume.run=QString("%1").arg(bits,32,16,QChar('0'));
            resume.start=start;resume.end=end;resume.length=job.length;resume.positions="1000000";resume.nextStart=pgn::booking::addUnsigned(start,resume.positions);
            resume.remaining=pgn::booking::subtractUnsigned(job.length,resume.positions);resume.tests="9007199254740993";resume.gaps="2";resume.batches="1";resume.counterTimeNs="9007199254740995";
            resume.checkpointLabel="C:/fixture/current-job/checkpoint";resume.dialect=pgn::mining::SourceCheckpointDialect::ScoutGapBound;resume.gapMinimum=job.minimumGap;
            QByteArray bytes="PGNS";for(unsigned i=0;i<4;++i)bytes.append(char((job.minimumGap>>(8*i))&255));
            for(quint64 word:{job.lowWord,job.highWord,job.positions,quint64(1000000),quint64(9007199254740993ULL),quint64(2),quint64(1)})for(unsigned i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));
            resume.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
            const QString device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
            check(store.saveResumeCheckpoint(device,resume,bytes).isEmpty());check(store.saveResumeCheckpoint(device,resume,bytes).isEmpty());
            const auto saved=store.savedResumeCheckpoint(device,resume.run);check(saved&&saved->checkpointBytes==bytes&&saved->resume.dialect==resume.dialect&&saved->resume.gapMinimum==job.minimumGap&&saved->resume.tests==resume.tests);
            auto bad=resume;bad.gapMinimum=job.minimumGap+2;check(!store.saveResumeCheckpoint(device,bad,bytes).isEmpty());
            auto changed=bytes;const auto changedGap=job.minimumGap+2;for(unsigned i=0;i<4;++i)changed[4+i]=char((changedGap>>(8*i))&255);
            bad.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(changed,QCryptographicHash::Sha256).toHex());
            check(!store.saveResumeCheckpoint(device,bad,changed).isEmpty());
        }
        std::cout<<"Existing PGN booking checkpoint PASS "<<checks<<" checks/all29 engine widths; own temporary SQLite only\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
