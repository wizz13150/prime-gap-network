#include "mining/prime_gap_engine.h"
#include "mining/run_source_profile.h"
#include "mining/source_stop_checkpoint_packet.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);unsigned checks=0;
    auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Engine binding check "+std::to_string(checks));};
    using namespace pgn::mining;
    try{
        const auto job=primeGapEngineJob("20000000000000000000000000000","20000000000000000000000010000",64);
        QJsonObject p{{"schema","pgn-run-source-profile-2"},{"protocol","PGN_NATIVE_GAP_RUNTIME_V1"},
            {"image_sha256","093c8037a6a8bf2082e5ac8a3f85a31b6d57aa6fbcaa741cb99f1fae3845a91d"},
            {"source_manifest_sha256","c4b2dec85a526227644bb315bb348a88d217d2b28d8a0511e065efde3c6bfbd0"},
            {"executable","C:/installed/engines/worker-k64.exe"},{"checkpoint_schema","PGNS_GAP32_64B"},
            {"job",QJsonObject{{"start",job.start},{"end",job.end},{"profile_k",64},{"minimum_gap","1332"}}}};
        QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",QString(32,'a')},
            {"uuid","GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac"},{"start",job.start},{"end",job.end},{"source_profile",p}};
        auto parsed=inspectRunSourceProfile(manifest);check(parsed.status==RunSourceProfileStatus::KnownAdaptivePrimeGap&&parsed.engineJob.has_value());
        check(runSourceProtocolAdmitted(parsed,"PGN_NATIVE_GAP_RUNTIME_V1"));check(!runSourceProtocolAdmitted(parsed,{}));
        for(const auto key:{"schema","protocol","image_sha256","source_manifest_sha256","executable","checkpoint_schema","job"}){
            auto poisoned=p;poisoned.insert(key,"wrong");auto m=manifest;m.insert("source_profile",poisoned);check(inspectRunSourceProfile(m).status==RunSourceProfileStatus::Invalid);
        }
        {auto m=manifest;m.insert("start","20000000000000000000000000001");check(inspectRunSourceProfile(m).status==RunSourceProfileStatus::Invalid);}
        {auto poisoned=p;auto j=p.value("job").toObject();j.insert("profile_k",8);poisoned.insert("job",j);auto m=manifest;m.insert("source_profile",poisoned);check(inspectRunSourceProfile(m).status==RunSourceProfileStatus::Invalid);}
        const auto label=QString("C:/runtime/checkpoint");QByteArray bytes="PGNS";
        for(unsigned i=0;i<4;++i)bytes.append(char((job.minimumGap>>(8*i))&255));
        for(quint64 word:{job.lowWord,job.highWord,job.positions,quint64(3000),quint64(9007199254740993ULL),quint64(0),quint64(4)})
            for(unsigned i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));
        const QString tail="[pgn-counters] time_ns=9007199254740995 positions=3000 gaps=0 tests=9007199254740993 batches=4 processed_gaps=0 emitted_gaps=0\n[phase0-gpu] stopped positions=3000 checkpoint="+label+"\n";
        QJsonObject snapshot{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"manifest",manifest},
            {"last_exit",QJsonObject{{"run",QString(32,'a')},{"exit_code",0}}},
            {"checkpoint_evidence",QJsonObject{{"schema","pgn-stop-checkpoint-evidence-1"},{"status","captured"},{"error",""},{"run",QString(32,'a')},
                {"checkpoint_label",label},{"checkpoint_base64",QString::fromLatin1(bytes.toBase64())},{"source_tail_utf8",tail}}}};
        auto decoded=decodeSourceStopCheckpointPacket(snapshot,QString(32,'a'),manifest.value("uuid").toString(),label,job);check(decoded.evidence.has_value());
        check(decoded.evidence->expectedGapMinimum==1332);const auto result=validateSourceStopCheckpoint(*decoded.evidence);
        check(result.resume&&result.resume->positions=="3000"&&result.resume->tests=="9007199254740993");
        check(!decodeSourceStopCheckpointPacket(snapshot,QString(32,'a'),manifest.value("uuid").toString(),label).evidence);
        auto wrong=job;wrong.minimumGap=1334;check(!decodeSourceStopCheckpointPacket(snapshot,QString(32,'a'),manifest.value("uuid").toString(),label,wrong).evidence);
        std::cout<<"Existing PGN engine profile/checkpoint packet PASS "<<checks<<" checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
