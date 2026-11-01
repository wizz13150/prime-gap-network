#include "mining/run_source_profile.h"
#include <QSet>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

namespace pgn::mining {
RunSourceProfileInspection inspectRunSourceProfile(const QJsonObject& manifest){
    RunSourceProfileInspection result;
    if(!manifest.contains("source_profile")){
        result.status=RunSourceProfileStatus::AbsentHistoricalUnattested;
        return result;
    }
    const auto value=manifest.value("source_profile");
    if(!value.isObject()){result.error="Present run source profile must be an object";return result;}
    const auto p=value.toObject();
    if(p.value("schema")=="pgn-run-source-profile-2"){
        // Content-addressed engine ABI mapping, independent of the installation
        // directory. Actual image reads/process binding still belong to the
        // existing launch and observation gates, never this pure decoder.
        const QSet<QString> fields{"schema","protocol","image_sha256","executable","source_manifest_sha256","checkpoint_schema","job"};
        QSet<QString> names;for(auto i=p.begin();i!=p.end();++i)names.insert(i.key());
        const auto image=p.value("image_sha256").toString();
        const QSet<QString> images{
            "093c8037a6a8bf2082e5ac8a3f85a31b6d57aa6fbcaa741cb99f1fae3845a91d",
            "83c1fc46b47d46348dfc7ddc865593dd7da040ffd033b091e090d5288c629c92"};
        const auto executable=p.value("executable").toString();
        const auto job=p.value("job").toObject();QSet<QString> jobNames;for(auto i=job.begin();i!=job.end();++i)jobNames.insert(i.key());
        const auto gap=job.value("minimum_gap").toString();bool gapOk=false;const auto minimum=gap.toUInt(&gapOk);
        if(names!=fields||p.value("protocol")!="PGN_NATIVE_GAP_RUNTIME_V1"||!images.contains(image)
           ||p.value("source_manifest_sha256")!="c4b2dec85a526227644bb315bb348a88d217d2b28d8a0511e065efde3c6bfbd0"
           ||p.value("checkpoint_schema")!="PGNS_GAP32_64B"||!QFileInfo(executable).isAbsolute()
           ||executable.size()>4096||executable.contains(QChar(0))||executable.contains('\r')||executable.contains('\n')
           ||jobNames!=QSet<QString>{"start","end","profile_k","minimum_gap"}
           ||!job.value("profile_k").isDouble()||job.value("profile_k").toDouble()!=job.value("profile_k").toInt()
           ||!gapOk||QString::number(minimum)!=gap){result.error="Adaptive engine profile identity/schema is invalid";return result;}
        try{
            const auto selected=primeGapEngineJob(job.value("start").toString(),job.value("end").toString(),job.value("profile_k").toInt(),minimum);
            if(!primeGapImageProfile(executable,selected.descriptorK)||manifest.value("start")!=selected.start||manifest.value("end")!=selected.end)
                throw std::invalid_argument("Adaptive engine job/profile differs from its actual run manifest");
            result.engineJob=selected;result.profile=p;result.status=RunSourceProfileStatus::KnownAdaptivePrimeGap;return result;
        }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());return result;}
    }
    const QSet<QString> expected{"schema","protocol","image_sha256","executable"};
    QSet<QString> actual;for(auto i=p.begin();i!=p.end();++i)actual.insert(i.key());
    if(actual!=expected){result.error="Run source profile keys are incomplete or unknown";return result;}
    for(const auto& key:expected)if(!p.value(key).isString()){
        result.error="Run source profile fields require exact strings";return result;
    }
    if(p.value("schema")!="pgn-run-source-profile-1"){
        result.error="Unsupported run source profile schema";return result;
    }
    // Explicit image/path/protocol triples, not a version-prefix allowlist.
    // ROOT555 qualifies 74A SOURCE_PRP publication, stop/resume and clipping;
    // it does not certify independent prime coverage or a production launch.
    if(p.value("protocol")=="pgn-source-legacy-a73"&&
       p.value("image_sha256")=="a73a6e383308560edc36ce351b09eb449fa099275361be74be89020b03864b43"&&
       p.value("executable")=="C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe"){
        result.status=RunSourceProfileStatus::KnownLegacyA73;
    }else if(p.value("protocol")=="pgn-source-boundary-v2"&&
             p.value("image_sha256")=="74a0fdd01ccf242fa3ca8714e4765c23022987c6785889cd8b1ecfa3aa27472b"&&
             p.value("executable")=="C:/PGN/native/20261015-source-boundary-v2/bin/pgn-continuous-gpu.exe"){
        result.status=RunSourceProfileStatus::KnownBoundaryV2;
    }else if(p.value("protocol")=="pgn-source-legacy-lazy-v1"&&
             p.value("image_sha256")=="a1597b9c4d2431c204bcc27ae69e9840c86cc5253e0fb63c09fa162266f55639"&&
             p.value("executable")=="C:/PGN/native/20261017-lazy-integer-v1/bin/pgn-continuous-gpu.exe"){
        // Exact arithmetic-only image: its RAW/checkpoint grammar remains
        // legacy. This metadata mapping grants neither launch nor coverage.
        result.status=RunSourceProfileStatus::KnownLazyV1;
    }else if(p.value("protocol")=="pgn-source-legacy-lazy-shared-v1"&&
             p.value("image_sha256")=="fa6f7e44d1187e1cc519fdc0454d36c8e886d1c7d1f3f4c8321adcaa8399aa81"&&
             p.value("executable")=="C:/PGN/native/20261017-lazy-shared-v1/bin/pgn-continuous-gpu.exe"){
        // Host shared/L1 preference only; exact legacy RAW/checkpoint grammar.
        result.status=RunSourceProfileStatus::KnownLazySharedV1;
    }else if(p.value("protocol")=="pgn-source-legacy-lazy-shared-periodic-v1"&&
             p.value("image_sha256")=="bfebd7f80dd7b0670a3ff51573c476ab0fd61daff7b59949ea4e60af8031bc53"&&
             p.value("executable")=="C:/PGN/native/20261017-lazy-shared-periodic-v1/bin/pgn-continuous-gpu.exe"){
        // Exact periodic pre-sieve; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownLazySharedPeriodicV1;
    }else if(p.value("protocol")=="pgn-source-legacy-lazy-shared-periodic-bank-v1"&&
             p.value("image_sha256")=="9a76e632fa1761b26b68b33d6d7eee15c1d7a0ff6e0d0d5772da9c6f0c13171e"&&
             p.value("executable")=="C:/PGN/native/20261017-lazy-shared-periodic-bank-v1/bin/pgn-continuous-gpu.exe"){
        // Exact fullchunk bank-phase pre-sieve; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownLazySharedPeriodicBankV1;
    }else if(p.value("protocol")=="pgn-source-legacy-radix70-v1"&&
             p.value("image_sha256")=="c6f209ab51936473611e7d34dde30e838793073f2d061d5dc8c0d4da69884684"&&
             p.value("executable")=="C:/PGN/native/20261017-radix70-v1/bin/pgn-continuous-gpu.exe"){
        // Exact mixed-radix70 arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownRadix70V1;
    }else if(p.value("protocol")=="pgn-source-legacy-radix70-float-prefix-v1"&&
             p.value("image_sha256")=="9afba37ae45903e9c279349041784cc0b8f1ebfce129bd10992f84870126644c"&&
             p.value("executable")=="C:/PGN/native/20261017-radix70-float-prefix-v1/bin/pgn-continuous-gpu.exe"){
        // Exact integer-corrected exponent-prefix arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownFloatPrefixV1;
    }else if(p.value("protocol")=="pgn-source-legacy-barrett70-v1"&&
             p.value("image_sha256")=="1a909ab796ac1e385dee6f1c09ab6ceb1a3a334b887e46cfbbf2d656726cb41a"&&
             p.value("executable")=="C:/PGN/native/20261017-barrett70-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownBarrett70V1;
    }else if(p.value("protocol")=="pgn-source-legacy-window2-recip5-v1"&&
             p.value("image_sha256")=="c5fbaf75de15631933c87419ec638ca53e3cb6b03668ad0a6b1f5b1818979566"&&
             p.value("executable")=="C:/PGN/native/20261017-window2-recip5-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownWindow2Recip5V1;
    }else if(p.value("protocol")=="pgn-source-legacy-barrett70-top2-v1"&&
             p.value("image_sha256")=="bbf75d45431606b39f2bb5e9e7c74f5c8ef1009b0cdfbbe3458e164adb78aff2"&&
             p.value("executable")=="C:/PGN/native/20261017-barrett70-top2-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownTop2V1;
    }else if(p.value("protocol")=="pgn-source-legacy-rolling-roots-v1"&&
             p.value("image_sha256")=="3e800bb02f0249c315b4d84eb346ef887660e023beb52ba57981ae4a26f8558b"&&
             p.value("executable")=="C:/PGN/native/20261017-rolling-roots-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownRollingRootsV1;
    }else if(p.value("protocol")=="pgn-source-legacy-rolling-roots-lanes2-v1"&&
             p.value("image_sha256")=="2876a22cb28805062d3f01423e56c918b60b6a4e09c14a888a7333c98d697488"&&
             p.value("executable")=="C:/PGN/native/20261017-rolling-roots-lanes2-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownRollingRootsLanes2V1;
    }else if(p.value("protocol")=="pgn-source-legacy-carry-chain-v1"&&
             p.value("image_sha256")=="65c7b566b76d6a90cd94c75b2d31bf8c516074f30cce8e5ea307363816f82b8d"&&
             p.value("executable")=="C:/PGN/native/20261018-carry-chain-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownCarryChainV1;
    }else if(p.value("protocol")=="pgn-source-legacy-fused-square-v1"&&
             p.value("image_sha256")=="12e1c0a2d43955f51a21aca6ff41ded562f44bdbda7b29a303ce3ba1c26fed59"&&
             p.value("executable")=="C:/PGN/native/20261018-fused-square-v1/bin/pgn-continuous-gpu.exe"){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownFusedSquareV1;
    }else if(p.value("protocol")=="pgn-source-legacy-fused-window3-ring-v1"&&
             ((p.value("image_sha256")=="42dea3f63af76512d2799248696d4f1f22fdd4d54aad803c2abe1367d29302a1"&&p.value("executable")=="C:/PGN/native/20261018-fused-window3-ring-v1/bin/pgn-continuous-gpu.exe")||
              (p.value("image_sha256")=="ca363cb76cbaf9a008402e0d47b80dc6dc846c331efa1bd42daf91de15dfc6b3"&&p.value("executable")=="C:/PGN/native/20261018-fused-window3-seed8r-v1/bin/pgn-continuous-gpu.exe")||
              (p.value("image_sha256")=="122abd3da35adfce429325f3ed0e16cca8b38b78bc144a11722a9ac342793952"&&p.value("executable")=="C:/PGN/native/20261019-frontier16-retry-v2/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="3121a85578aebc61648311216a468574745447a52fed7088285d2222c029d1e3"&&p.value("executable")=="C:/PGN/native/20261019-frontier16-event210-hybrid-v1/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="b0439859530229d2c3451690c3a0361a78fe570cdff14f131efb1b8a22253c43"&&p.value("executable")=="C:/PGN/native/20261019-frontier16-event210-composed-v1/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="85412118a5686090fa612c090c6c43dc9222231e4ac08a39bc594f4d4235a945"&&p.value("executable")=="C:/PGN/native/20261019-event210-smallword7-v1/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="843bcadbaa279a19e5cd0d6c54abd386ddef47b10b1f2e41a8da32c6390a0428"&&p.value("executable")=="C:/PGN/native/20261019-event-pair16-v1/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="cf3c215bf30140f89d04324377f64694d106852120634a47af6b08558a6294ed"&&p.value("executable")=="C:/PGN/native/20261019-prefix11-final-v1/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="ccd3d4fda96b8c26b74947b6fc8399d5502efbf55a4aefcabfc92659c6277673"&&p.value("executable")=="C:/PGN/native/20261020-stateful-u160-live-v1/bin/pgn-continuous-gpu.exe")||
               (p.value("image_sha256")=="962a98e4d6fd97390ce20ff99143c7bc1befd7a1093998d8b73f730d28525f67"&&p.value("executable")=="C:/PGN/native/20261021-live-composition-v22/bin/pgn-continuous-gpu.exe"))){
        // Exact lazy Barrett arithmetic; legacy RAW/checkpoint grammar is unchanged.
        result.status=RunSourceProfileStatus::KnownFusedWindow3RingV1;
    }else if(p.value("protocol")=="pgn-source-legacy-fused-high47-v1"&&
             p.value("image_sha256")=="da55d109d9e5bd366dc6a9d8e3ae29f7a5d831b4839ac17fd891adf0f8b23906"&&
             p.value("executable")=="C:/PGN/native/20261018-fused-high47-v1/bin/pgn-continuous-gpu.exe"){
        result.status=RunSourceProfileStatus::KnownFusedHigh47V1;
    }else{result.error="Unknown or contradictory run source image/executable/protocol";return result;}
    result.profile=p;return result;
}
bool runSourceProtocolAdmitted(const RunSourceProfileInspection& profile,const QString& protocol){
    if(profile.status==RunSourceProfileStatus::KnownAdaptivePrimeGap)return profile.engineJob.has_value()&&protocol=="PGN_NATIVE_GAP_RUNTIME_V1";
    if(profile.status==RunSourceProfileStatus::KnownBoundaryV2)return protocol=="pgn-source-boundary-v2";
    return (profile.status==RunSourceProfileStatus::KnownLegacyA73||
            profile.status==RunSourceProfileStatus::KnownLazyV1||
            profile.status==RunSourceProfileStatus::KnownLazySharedV1||
            profile.status==RunSourceProfileStatus::KnownLazySharedPeriodicV1||
            profile.status==RunSourceProfileStatus::KnownLazySharedPeriodicBankV1||
            profile.status==RunSourceProfileStatus::KnownRadix70V1||
            profile.status==RunSourceProfileStatus::KnownFloatPrefixV1||
            profile.status==RunSourceProfileStatus::KnownBarrett70V1||
            profile.status==RunSourceProfileStatus::KnownWindow2Recip5V1||
            profile.status==RunSourceProfileStatus::KnownTop2V1||
            profile.status==RunSourceProfileStatus::KnownRollingRootsV1||
            profile.status==RunSourceProfileStatus::KnownRollingRootsLanes2V1||
            profile.status==RunSourceProfileStatus::KnownCarryChainV1||
            profile.status==RunSourceProfileStatus::KnownFusedSquareV1||
            profile.status==RunSourceProfileStatus::KnownFusedHigh47V1||
            profile.status==RunSourceProfileStatus::KnownFusedWindow3RingV1||
            profile.status==RunSourceProfileStatus::AbsentHistoricalUnattested)&&protocol.isEmpty();
}
}
