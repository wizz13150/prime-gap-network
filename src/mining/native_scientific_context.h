#pragma once
#include "booking/decimal_range.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <stdexcept>

namespace pgn::mining {
// Public search provenance only. This validates no signature, prime proof,
// reservation authority or whole-ticket coverage. The node owns admission.
inline bool nativeScientificSearchOrigin(const QJsonObject& manifest){
    if(manifest.value("source_kind")!="NATIVE_PGN_SCIENCE")return false;
    const auto need=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto fields=[&](const QJsonObject& object,QStringList expected){auto actual=object.keys();actual.sort();expected.sort();need(actual==expected,"Native search context fields differ");};
    const auto hex=[&](const QJsonValue& value,int maximum,bool digest=false){
        need(value.isString(),"Native search digest/script must be text");const auto s=value.toString();
        need(!s.isEmpty()&&s.size()<=maximum&&s.size()%2==0&&QRegularExpression("^[0-9a-f]+$").match(s).capturedLength()==s.size()&&(!digest||s.size()==64),"Native search digest/script is noncanonical");
    };
    const auto decimal=[&](const QJsonValue& value){need(value.isString(),"Native search number must be exact decimal text");const auto s=value.toString();need(s.size()<=160&&s!="0"&&booking::canonicalUnsigned(s)==s,"Native search number is noncanonical");return s;};
    need(manifest.value("queued_intent").isNull(),"Native unqueued search cannot replace a community dispatch identity");
    const auto context=manifest.value("native_scientific_context").toObject();const auto schema=context.value("schema").toString();const bool slice=schema=="pgn-native-scientific-source-context-2";
    need(slice||schema=="pgn-native-scientific-source-context-1","Unknown native search context schema");
    QStringList wanted{"schema","authority","rpc_work","rpc_work_sha256"};if(slice)wanted.append("search_slice");fields(context,wanted);
    need(context.value("authority")=="SEARCH_CONTEXT_NOT_NATIVE_ACCEPTANCE_OR_RANGE_CERTIFICATE","Native search provenance cannot claim acceptance");hex(context.value("rpc_work_sha256"),64,true);
    const auto work=context.value("rpc_work").toObject();
    fields(work,{"schema","genesis","parent","height","request_id","ticket","start","length","segments","report_root","work_id","challenge","miner_script","reward_script","target_numerator","target_denominator","science_policy_script","accepted"});
    need(work.value("schema")=="PGN_NATIVE_SCIENTIFIC_WORK1"&&work.value("accepted").isBool()&&!work.value("accepted").toBool(),"Native search work is not an untrusted public work DTO");
    for(const auto* name:{"genesis","parent","request_id","ticket","report_root","work_id"})hex(work.value(name),64,true);
    for(const auto* name:{"miner_script","reward_script","science_policy_script"})hex(work.value(name),8192);
    for(const auto* name:{"target_numerator","target_denominator"})decimal(work.value(name));
    for(const auto pair:{qMakePair("height",2147483647LL),qMakePair("segments",4096LL)}){const auto v=work.value(pair.first);const auto n=v.toInteger(-1);need(v.isDouble()&&n>0&&n<=pair.second&&v.toDouble()==double(n),"Native work height/segments is not an exact bounded integer");}
    const auto challenge=work.value("challenge").toString();need(work.value("challenge").isString()&&!challenge.isEmpty()&&challenge.size()<=4096&&!challenge.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")),"Native work challenge is invalid");
    const auto origin=decimal(work.value("start")),limit=booking::addUnsigned(origin,decimal(work.value("length")));
    const auto first=decimal(manifest.value("start")),last=decimal(manifest.value("end"));
    need(booking::compareUnsigned(origin,first)<=0&&booking::compareUnsigned(first,last)<0&&booking::compareUnsigned(last,limit)<=0,"Native source execution leaves its declared parent ticket");
    if(slice){const auto bounds=context.value("search_slice").toObject();fields(bounds,{"schema","work_id","start","end"});need(bounds.value("schema")=="pgn-native-search-slice-1"&&bounds.value("work_id")==work.value("work_id")&&bounds.value("start")==first&&bounds.value("end")==last,"Native physical slice identity/range differs");}
    else need(first==origin&&last==limit,"Native whole-work source bounds differ");
    const auto profile=manifest.value("source_profile").toObject();const auto job=profile.value("job").toObject();
    need(profile.value("schema")=="pgn-run-source-profile-2"&&profile.value("protocol")=="PGN_NATIVE_GAP_RUNTIME_V1"&&job.value("start")==first&&job.value("end")==last,"Native search execution profile/range differs");
    need(!manifest.contains("resume_previous_run")&&!manifest.contains("checkpoint_input_sha256"),"Native unqueued resume requires an explicit supported lineage contract");
    return true;
}
}
