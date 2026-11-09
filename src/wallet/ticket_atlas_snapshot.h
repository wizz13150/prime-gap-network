#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <boost/multiprecision/cpp_int.hpp>
#include <array>
#include <stdexcept>

namespace pgn::wallet {
using AtlasInteger=boost::multiprecision::cpp_int;
inline AtlasInteger atlasInteger(const QJsonValue& value){
    static const QRegularExpression decimal("^(0|[1-9][0-9]{0,159})$");
    if(!value.isString()||!decimal.match(value.toString()).hasMatch())throw std::invalid_argument("Exact atlas integer required");
    return AtlasInteger(value.toString().toStdString());
}
inline constexpr std::array<const char*,5> AtlasStates{"assigned","pending_audits","audits_assigned","audits_passed","audit_failed"};
inline QString validateTicketAtlas(const QJsonObject& view,const QString& genesis,const QString& start,const QString& end,int bins){
    try{
        static const QRegularExpression hash("^[0-9a-f]{64}$"),script("^[0-9a-f]+$");
        const auto count=[](const QJsonValue& value){const auto n=value.toInteger(-1);if(!value.isDouble()||n<0||n>2147483647||value.toDouble()!=double(n))throw std::invalid_argument("Invalid atlas counter");return n;};
        if(view.value("schema")!="PGN_TICKET_ATLAS_V1"||view.value("genesis")!=genesis||!hash.match(genesis).hasMatch()||genesis==QString(64,'0')||!hash.match(view.value("bestblockhash").toString()).hasMatch()||view.value("bestblockhash")==QString(64,'0')||!view.value("current_assignments_complete").isBool()||!view.value("current_assignments_complete").toBool())return "Atlas chain or completeness unavailable";
        count(view.value("height"));
        const AtlasInteger origin=atlasInteger(view.value("range_origin")),limit=atlasInteger(view.value("range_limit")),width=atlasInteger(view.value("ticket_width"));
        const AtlasInteger first=atlasInteger(view.value("start")),last=atlasInteger(view.value("end")),span=last-first;
        if(width<=0||origin>=limit||first<origin||last>limit||first>=last||bins<1||bins>512)return "Invalid atlas domain";
        if((start.isEmpty()&&(first!=origin||last!=limit))||(!start.isEmpty()&&(view.value("start")!=start||view.value("end")!=end)))return "Atlas viewport changed";
        const auto all=count(view.value("tracked_assignments")),visible=count(view.value("visible_assignments"));if(visible>all)return "Atlas count exceeds schedule";
        const auto rows=view.value("details").toArray(),blocks=view.value("bins").toArray();const int expected=span<bins?span.convert_to<int>():bins;
        if(!view.value("details").isArray()||!view.value("bins").isArray()||blocks.isEmpty()||blocks.size()>expected||rows.size()!=std::min<qint64>(visible,100)||!view.value("details_truncated").isBool()||view.value("details_truncated").toBool()!=(visible>100))return "Atlas display budget or detail count invalid";
        std::array<AtlasInteger,5> sums{};AtlasInteger previous=first;
        for(qsizetype i=0;i<blocks.size();++i){
            const auto block=blocks[i].toObject();const AtlasInteger a=atlasInteger(block.value("start")),b=atlasInteger(block.value("end"));
            // Linear and per-bit views share the same invariant: bounded,
            // adjacent exact integer cells covering the requested viewport.
            if(a!=previous||a>=b||b>last)return "Atlas bins do not partition viewport";
            previous=b;AtlasInteger used{};const auto stages=block.value("stages").toObject();
            if(stages.size()!=5)return "Atlas stage set invalid";
            for(size_t s=0;s<5;++s){const auto amount=stages.value(AtlasStates[s]).toObject();const AtlasInteger positions=atlasInteger(amount.value("positions"));const auto n=count(amount.value("tickets"));if(n>visible||(n==0)!=(positions==0)||positions<n)return "Atlas bin count invalid";used+=positions;sums[s]+=positions;}
            if(used>b-a)return "Overlapping atlas occupancy";
        }
        if(previous!=last)return "Atlas bins do not cover viewport";
        const auto totals=view.value("totals").toObject();if(totals.size()!=5)return "Atlas total stages invalid";qint64 counted{};
        for(size_t s=0;s<5;++s){const auto amount=totals.value(AtlasStates[s]).toObject();const auto n=count(amount.value("tickets"));counted+=n;if(n>visible||atlasInteger(amount.value("positions"))!=sums[s])return "Atlas totals mismatch";}
        if(counted!=visible)return "Atlas visible count mismatch";
        QSet<QString> ids;AtlasInteger prior{};bool havePrior=false;
        for(const auto& entry:rows){const auto row=entry.toObject();const auto id=row.value("request").toString();const AtlasInteger a=atlasInteger(row.value("start")),b=atlasInteger(row.value("end")),number=atlasInteger(row.value("ticket_number"));const auto state=row.value("stage").toString();
            bool known=false;for(const auto* name:AtlasStates)known|=state==name;
            if(!hash.match(id).hasMatch()||id==QString(64,'0')||ids.contains(id)||!known)return "Atlas detail identity or stage invalid";
            const auto owner=row.value("miner_script").toString();
            if(owner.size()<2||owner.size()>8192||(owner.size()&1)||!script.match(owner).hasMatch())return "Atlas signing script invalid";
            if(a<origin||b>limit||a>=b||a>=last||b<=first||(havePrior&&a<prior))return "Atlas detail interval invalid";
            if(number!=(a-origin)/width+1)return "Atlas ticket number invalid";
            ids.insert(id);prior=b;havePrior=true;
        }
        if(QJsonDocument(view).toJson(QJsonDocument::Compact).size()>524288)return "Atlas byte budget exceeded";
        return {};
    }catch(const std::exception&){return "Atlas exact geometry or counter invalid";}
}
}
