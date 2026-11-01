#include "mining/prime_gap_source_stream.h"
#include "booking/decimal_range.h"
#include "mining/source_decimal.h"
#include <QRegularExpression>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
quint64 number(const QString& text){bool ok=false;const auto n=text.toULongLong(&ok);need(ok&&source_decimal::canonical(text),"Native source integer malformed or overflowing");return n;}
QString coordinate(const QString& text){need(text.size()<=39&&source_decimal::canonical(text),"Native source coordinate malformed");return text;}
}
PrimeGapSourceStream::PrimeGapSourceStream(PrimeGapEngineJob job,PrimeGapSourceCounters base)
    :job_(std::move(job)),base_(base),last_(base){
    const auto checked=primeGapEngineJob(job_.start,job_.end,job_.descriptorK,job_.minimumGap);
    need(checked.length==job_.length&&checked.positions==job_.positions&&base.positions<job_.positions,"Native source baseline/job differs");
    slot_=quint64(job_.descriptorK)*1006632960ULL;
    remainder_=(job_.lowWord%30+(job_.highWord%30)*16)%30;
    need(job_.positions<=std::numeric_limits<quint64>::max()-remainder_,"Native descriptor span overflow");
    const auto span=job_.positions+remainder_;descriptors_=span/slot_+(span%slot_!=0);
    need(base.batches<descriptors_,"Native checkpoint descriptor baseline exceeds job");
    need(base.positions==(base.batches?base.batches*slot_-remainder_:0),"Native checkpoint prefix does not match selected descriptor K/origin");
}
void PrimeGapSourceStream::row(const QByteArray& raw){
    need(raw.size()<=32768&&!raw.contains('\0')&&!raw.contains('\r')&&!raw.contains('\n')&&QString::fromUtf8(raw).toUtf8()==raw,"Native source row encoding/bound invalid");
    const auto text=QString::fromUtf8(raw);if(text.isEmpty())return;
    if(summary_){need(text=="[phase0-gpu] --- end ---","Data after native source summary");summary_=false;complete_=true;return;}
    need(!complete_&&!stopped_,"Data after native source terminal footer");
    if(text.startsWith("[phase0-gpu] engine=")){
        static const QRegularExpression pattern("^\\[phase0-gpu\\] engine=GPU_OWNED_CONTINUOUS descriptors=([0-9]+) original_start=([0-9]+) length=([0-9]+) proof=EXACT_ENDPOINTS_GPU_INTERIOR coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING$");
        const auto m=pattern.match(text);need(m.hasMatch()&&!header_&&!records_&&!countersSeen_
            &&number(m.captured(1))==descriptors_&&m.captured(2)==job_.start&&m.captured(3)==job_.length,"Native source header differs from selected engine job");header_=true;return;
    }
    if(text.startsWith("[phase0-gpu] GAP")){
        static const QRegularExpression pattern("^\\[phase0-gpu\\] GAP merit=([0-9]+\\.[0-9]{4}) gap=([0-9]+) lower=([0-9]+) upper=([0-9]+) verified=1 proof=EXACT_ENDPOINTS_EULER_COMPOSITE record=off table=unknown(?: unix_ts=([1-9][0-9]*) time_basis=source_batch_publication| time_basis=source_batch_publication time_status=unavailable)?$");
        const auto m=pattern.match(text);need(header_&&m.hasMatch(),"Native source GAP/profile malformed");
        const auto width=number(m.captured(2));const auto lower=coordinate(m.captured(3)),upper=coordinate(m.captured(4));
        need(width>=job_.minimumGap&&width<=UINT32_MAX&&!source_decimal::less(lower,job_.start)&&source_decimal::less(lower,job_.end)
            &&source_decimal::less(lower,upper)&&source_decimal::difference(upper,lower)==QString::number(width),"Native source GAP bounds/threshold differ");
        if(minimumLower_.isEmpty()||source_decimal::less(lower,minimumLower_))minimumLower_=lower;
        if(maximumLower_.isEmpty()||source_decimal::less(maximumLower_,lower))maximumLower_=lower;
        if(largestUpper_.isEmpty()||!source_decimal::less(upper,largestUpper_)){secondUpper_=largestUpper_;largestUpper_=upper;}
        else if(secondUpper_.isEmpty()||source_decimal::less(secondUpper_,upper))secondUpper_=upper;
        need(records_<std::numeric_limits<quint64>::max(),"Native source record count overflow");++records_;++groupRecords_;return;
    }
    if(text.startsWith("[pgn-counters]")){
        static const QRegularExpression pattern("^\\[pgn-counters\\] time_ns=([0-9]+) positions=([0-9]+) gaps=([0-9]+) tests=([0-9]+) batches=([0-9]+) processed_gaps=([0-9]+) emitted_gaps=([0-9]+)$");
        const auto m=pattern.match(text);need(header_&&m.hasMatch(),"Native source counter/profile malformed");
        const PrimeGapSourceCounters next{number(m.captured(1)),number(m.captured(2)),number(m.captured(3)),number(m.captured(4)),number(m.captured(5))};
        need(next.positions>=last_.positions&&next.positions<=job_.positions&&next.tests>=last_.tests&&next.gaps>=last_.gaps&&next.batches>=last_.batches
            &&(!countersSeen_||next.time>=last_.time)&&number(m.captured(6))==next.gaps&&number(m.captured(7))==next.gaps
            &&records_<=std::numeric_limits<quint64>::max()-base_.gaps&&next.gaps==base_.gaps+records_,"Native source cumulative counters disagree with exact raw record delta");
        if(next.batches==last_.batches){need(!groupRecords_&&next.positions==last_.positions&&next.tests==last_.tests&&next.gaps==last_.gaps,"Native source progress counter changes uncommitted data");}
        else{
            need(last_.batches<std::numeric_limits<quint64>::max()&&next.batches==last_.batches+1&&next.batches<=descriptors_,"Native source descriptor counter skipped or regressed");
            const auto expected=next.batches==descriptors_?job_.positions:next.batches*slot_-remainder_;
            need(next.positions==expected&&next.positions>last_.positions,"Native source descriptor position differs from selected K/origin");
            const auto first=booking::addUnsigned(job_.start,QString::number(last_.positions)),end=booking::addUnsigned(job_.start,QString::number(next.positions));
            if(groupRecords_)need(!source_decimal::less(minimumLower_,first)&&source_decimal::less(maximumLower_,end)
                &&(secondUpper_.isEmpty()||source_decimal::less(secondUpper_,end)),"Native source GAP group ownership/crossing differs");
        }
        groupRecords_=0;minimumLower_.clear();maximumLower_.clear();largestUpper_.clear();secondUpper_.clear();last_=next;countersSeen_=true;return;
    }
    if(text.startsWith("[phase0-gpu] stopped")){
        static const QRegularExpression pattern("^\\[phase0-gpu\\] stopped positions=([0-9]+) checkpoint=(.+)$");
        const auto m=pattern.match(text);need(header_&&countersSeen_&&!groupRecords_&&m.hasMatch()&&number(m.captured(1))==last_.positions,"Native source stop footer differs");stopLabel_=m.captured(2);stopped_=true;return;
    }
    if(text.startsWith("[phase0-gpu] gaps reported=")||text=="[phase0-gpu] --- end ---"){
        static const QRegularExpression pattern("^\\[phase0-gpu\\] gaps reported=([0-9]+) verification_failures=0 records_new=0$");
        const auto m=pattern.match(text);need(header_&&countersSeen_&&!groupRecords_&&m.hasMatch()&&number(m.captured(1))==last_.gaps
            &&last_.positions==job_.positions&&last_.batches==descriptors_,"Native source completion footer/coverage differs");summary_=true;return;
    }
    need(!text.startsWith("[pgn-source-boundary"),"Unexpected foreign source boundary dialect");
}
void PrimeGapSourceStream::finish(bool complete,const QString& label)const{
    need(header_&&countersSeen_&&!groupRecords_&&!summary_,"Native source EOF has unpaid header/counter/group");
    need(complete?complete_:stopped_&&!label.isEmpty()&&label==stopLabel_,"Native source terminal scope/checkpoint differs");
}
}
