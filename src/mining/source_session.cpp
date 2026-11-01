#include "mining/source_session.h"
#include <QRegularExpression>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
[[noreturn]] void fail(const char* text){throw std::runtime_error(text);}
void cancellation(const std::function<bool()>& cancelled){if(cancelled&&cancelled())fail("Native source attachment/admission cancelled; replay required");}
}
QJsonObject SourceConservation::json()const{
    const auto number=[](const std::optional<qint64>& value){return value?QJsonValue(*value):QJsonValue(QJsonValue::Null);};
    return {{"schema","pgn-source-conservation-1"},{"scope","LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY"},
        {"run",run.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(run)},
        {"source",run.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue("native/"+run+".stdout")},{"required_end",number(requiredEnd)},
        {"committed_offset",number(committedOffset)},{"mirror_size",number(mirrorSize)},
        {"transport_offset",number(transportOffset)},{"ready",ready},{"error",error}};
}
SourceConservation SourceSession::conservation(const QString& run,qint64 next,qint64 pending)const{
    SourceConservation result;
    try{
        if(run.size()!=32||!QRegularExpression("^[0-9a-f]{32}$").match(run).hasMatch())fail("Conservation source run invalid");
        result.run=run;
        if(next<0||pending<0||next>std::numeric_limits<qint64>::max()-pending)fail("Conservation observed end is absent/invalid/overflow");
        result.requiredEnd=next+pending;
        if(run_!=run||!handleEpoch_||integrityVersion_<0)fail("Source epoch has no already verified durable session; no read-side attachment");
        result.transportOffset=expected_;
        // inspect does not write/create data. If eviction requires a new
        // handle, its NEW epoch MUST be rejected, never adopted by this view.
        const auto mirror=mirror_.inspect(run);
        if(!mirror)fail("Conserved source mirror absent; not zero");result.mirrorSize=mirror->size;
        if(mirror->handle_epoch!=handleEpoch_)fail("Conserved source handle incarnation changed; consuming reattachment required");
        const auto cursor=history_.cursorRecord(run);
        if(!cursor)fail("Conserved source cursor absent; not an invented zero");result.committedOffset=cursor->offset;
        if(cursor->dataVersion!=integrityVersion_)fail("Source integrity DB changed since prefix verification; fresh attachment required");
        if(cursor->offset!=committed_)fail("Source stored cursor differs from this session's admitted epoch");
        if(pending!=0||!fragment_.isEmpty())fail("Source contains an incomplete/unreceived record; not conserved final frontier");
        if(expected_!=committed_||cursor->offset!=*result.requiredEnd||mirror->size!=*result.requiredEnd||expected_!=*result.requiredEnd)
            fail("Source committed/mirror/transport frontiers do not all equal observed end");
        result.ready=true;return result;
    }catch(const std::exception& error){result.error=QString::fromUtf8(error.what()).left(400);return result;}
}
void SourceSession::attach(const QString& run,const SourceMirrorState& state,const std::function<bool()>& cancelled){
    cancellation(cancelled);
    // Reopening an existing file does not prove its last writer flushed it.
    // Establish durability through the SAME held handle before recovering any
    // complete-but-unindexed tail into SQLite. This is once per incarnation.
    mirror_.append(run,state.size,{},state.handle_epoch);
    // Verify the complete old prefix once under this exact held incarnation.
    // New packets then preserve/flush bytes before SQLite admission. Eviction
    // changes handle_epoch and forces verification again, not a blind path read.
    const auto prefix=history_.verifySourcePrefix(run,state.size,[&](qint64 at,qsizetype size){cancellation(cancelled);return mirror_.read(run,at,size,state.handle_epoch);});
    cancellation(cancelled);
    committed_=prefix.offset;integrityVersion_=prefix.dataVersion;
    qint64 at=committed_;QByteArray pending;
    while(at<state.size){
        cancellation(cancelled);
        const auto requested=std::min<qint64>(65536,state.size-at);const auto bytes=mirror_.read(run,at,requested,state.handle_epoch);
        if(bytes.isEmpty()||bytes.size()>requested)fail("Conserved source tail read failed");
        at+=bytes.size();pending+=bytes;
        const auto newline=pending.lastIndexOf('\n');
        if(newline>=0){cancellation(cancelled);const auto result=history_.append(run,committed_,pending.left(newline+1),integrityVersion_);committed_=result.offset;pending.remove(0,newline+1);}
        if(pending.size()>32768)fail("Incomplete conserved source record exceeds bound");
    }
    // Replay an incomplete tail from its LAST COMMITTED byte, so the mirror
    // compares its overlap with the producer. Never silently skip that tail.
    run_=run;handleEpoch_=state.handle_epoch;expected_=committed_;fragment_.clear();
}
SourceChunk SourceSession::consume(const QString& run,qint64 offset,qint64 next,const QByteArray& bytes,const std::function<bool()>& cancelled){
    if(offset<0||bytes.size()>SourceMirror::BatchLimit||offset>std::numeric_limits<qint64>::max()-bytes.size()||next!=offset+bytes.size())fail("Native source packet offset/length invalid");
    // Refuse BEFORE this session is invalidated or any tail/offset is touched.
    // The transport retains/replays this unconsumed chunk after lease release.
    if(mirror_.lineageFrozen())fail("Source ingestion frozen for lineage; replay this chunk after release");
    cancellation(cancelled);
    try{
        auto state=mirror_.inspect(run);
        if(!state){if(history_.cursor(run)!=0)fail("Conserved source prefix missing; no empty substitute created");if(offset!=0)return {true,0,0,{}};mirror_.append(run,0,{});state=mirror_.inspect(run);if(!state)fail("Native source mirror did not open");}
        if(run_!=run||handleEpoch_!=state->handle_epoch)attach(run,*state,cancelled);
        if(offset!=expected_)return {true,expected_,committed_,{}};
        cancellation(cancelled);mirror_.append(run,offset,bytes,handleEpoch_); // actual durable boundary FIRST
        const auto combined=fragment_+bytes;const auto newline=combined.lastIndexOf('\n');
        SourceChunk result;
        if(newline>=0){
            const auto complete=combined.left(newline+1);
            cancellation(cancelled);
            committed_=history_.append(run,committed_,complete,integrityVersion_).offset;
            qsizetype start=0;
            while(start<complete.size()){
                const auto end=complete.indexOf('\n',start);auto line=complete.mid(start,end-start);if(line.endsWith('\r'))line.chop(1);
                if(result.displayLines.size()==512)result.displayLines.pop_front();
                result.displayLines.push_back(std::move(line));start=end+1;
            }
            fragment_=combined.mid(newline+1);
        }else fragment_=combined;
        if(fragment_.size()>32768)fail("Incomplete native source record exceeds bound");
        expected_=next;result.nextOffset=expected_;result.committedOffset=committed_;return result;
    }catch(...){run_.clear();handleEpoch_=0;integrityVersion_=-1;fragment_.clear();throw;}
}
}
