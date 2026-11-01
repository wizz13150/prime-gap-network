#include "mining/native_observation_session.h"
#include "mining/source_decimal.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QMetaType>
#include <QJsonDocument>
#include <stdexcept>
#include <limits>

namespace pgn::mining {
namespace {
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
}
NativeObservationSession::NativeObservationSession(SourceHistory& history,SourceMirror& mirror,QString device)
    :history_(history),mirror_(mirror),source_(history,mirror),gate_(device),device_(std::move(device)){}
SourceConservation NativeObservationSession::conservation(const SshObservationFrame& frame,const QString& selected)const{
    SourceConservation held;
    try{
        if(frame.packet.value("schema")!="pgn-runtime-snapshot-1"||!frame.packet.value("manifest").isObject())fail("Conservation snapshot has no exact source epoch");
        if(selected.size()!=32)fail("Conservation selected run invalid");
        const auto identity=nativeSourceObservationIdentity(frame.packet.value("manifest").toObject(),selected,device_);
        held.run=selected;
        if(run_!=selected||identity_!=identity)fail("Conservation source identity is not this already admitted epoch");
        if(!frame.pendingBytes)fail("Conservation pending_bytes is unknown, not zero");
        // Compare with the exact typed values already validated from lexemes
        // by decodeSshSnapshot. Do not parse/default integers from inventory.
        if(frame.packet.value("pending_bytes")!=QJsonValue(*frame.pendingBytes)||frame.packet.value("next_offset")!=QJsonValue(frame.nextOffset))fail("Conservation typed frame and packet disagree");
        return source_.conservation(selected,frame.nextOffset,*frame.pendingBytes);
    }catch(const std::exception& error){held.error=QString::fromUtf8(error.what()).left(400);return held;}
}
NativeObservation NativeObservationSession::consume(const SshObservationFrame& frame,const QString& selected,qint64 now,bool enabled,bool paused,const std::function<bool()>& cancelled){
    if(cancelled&&cancelled())fail("Native SOURCE observation cancelled before admission; replay required");
    if(frame.packet.value("schema")!="pgn-runtime-snapshot-1")fail("Native observation snapshot schema mismatch");
    const auto manifestValue=frame.packet.value("manifest");
    if(manifestValue.isNull()){
        if(frame.offset!=0||frame.nextOffset!=0||!frame.chunk.isEmpty())fail("Cold native observation contains source bytes");
        // A cold observation is not a completion and never erases an old run.
        return {};
    }
    if(!manifestValue.isObject())fail("Native observation manifest missing");
    const auto identity=nativeSourceObservationIdentity(manifestValue.toObject(),selected,device_);
    if(run_==selected&&identity_!=identity)fail("Native observation run identity changed");
    const auto mirrorState=mirror_.inspect(selected);
    history_.bindSourceIdentity(selected,QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)),!mirrorState||mirrorState->size==0);
    const auto admitted=source_.consume(selected,frame.offset,frame.nextOffset,frame.chunk,cancelled);
    if(run_!=selected){gate_.reset();run_=selected;identity_=identity;}
    NativeObservation result;
    result.state=admitted.needsReplay?NativeObservationState::ReplayRequired:NativeObservationState::Source;
    result.run=selected;result.nextOffset=admitted.nextOffset;result.committedOffset=admitted.committedOffset;result.displayLines=admitted.displayLines;
    if(!admitted.needsReplay)
        result.advance=gate_.claim(frame.packet,selected,now,enabled,paused,admitted.nextOffset!=admitted.committedOffset);
    return result;
}
}
