#include <pgn/rpc_public_attribution.h>
#include "ui/public_report_name.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
int main(){int checks=0;const auto need=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Projection check "+std::to_string(checks));};
 const auto hash=[](std::string_view raw){return QCryptographicHash::hash(QByteArray(raw.data(),qsizetype(raw.size())),QCryptographicHash::Sha256).toHex().toStdString();};
 const auto project=[&](const std::string& raw){return pgn::core::DisclosedPublicAttribution(raw,hash(raw),hash);};
 const std::string raw=R"({"schema":"PGN_NETWORK_RANGE_REPORT_V1","discoverer":{"schema":"PGN_PUBLIC_ATTRIBUTION_V1","name":"Alice"},"computed":{"counts":["123"]},"commitment_nonce":"not projected"})";
 const auto label=project(raw);need(label&&label->operator[]("name").get_str()=="Alice");need(label->getKeys().size()==2);need(!label->exists("computed")&&!label->exists("commitment_nonce"));
 need(!pgn::core::DisclosedPublicAttribution(raw,std::string(64,'0'),hash));need(!project(""));need(!project(std::string(65537,' ')));need(!project("{}"));
 for(const auto& bad:{std::string("<script>"),std::string("bad\nname"),std::string(193,'A')}){auto object=QJsonDocument::fromJson(QByteArray::fromStdString(raw)).object();auto meta=object.value("discoverer").toObject();meta.insert("name",QString::fromStdString(bad));object.insert("discoverer",meta);need(!project(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString()));}
 const auto id=QString(64,'a'),root=QString::fromStdString(hash(raw));QJsonObject meta{{"schema","PGN_PUBLIC_ATTRIBUTION_V1"},{"name","Alice"}};
 QJsonObject snapshot{{"leases",QJsonArray{QJsonObject{{"request",id},{"report_root",root},{"miner_script","owner-kept"}}}},
    {"reports",QJsonArray{QJsonObject{{"request_id",id},{"report_root",root}}}},
    {"audits",QJsonArray{QJsonObject{{"request_id",id},{"public_attribution",meta}}}}};
 need(pgn::ui::publicReportName(snapshot)==QString("Alice"));
 auto changed=snapshot;changed.insert("reports",QJsonArray{QJsonObject{{"request_id",id},{"report_root",QString(64,'b')}}});need(!pgn::ui::publicReportName(changed));
 changed=snapshot;changed.insert("audits",QJsonArray{QJsonObject{{"request_id",QString(64,'b')},{"public_attribution",meta}}});need(!pgn::ui::publicReportName(changed));
 changed=snapshot;auto rows=changed.value("audits").toArray();rows.append(rows.first());changed.insert("audits",rows);need(!pgn::ui::publicReportName(changed));
 for(const auto& name:{QString{},QString(" Alice "),QString("---"),QString("bad<script>"),QString(QChar(0x202e))+"hidden",QString(49,'A')}){changed=snapshot;auto invalid=meta;invalid.insert("name",name);changed.insert("audits",QJsonArray{QJsonObject{{"request_id",id},{"public_attribution",invalid}}});need(!pgn::ui::publicReportName(changed));}
 changed=snapshot;auto unicode=meta;unicode.insert("name",QString::fromUtf8("Émile-31"));changed.insert("audits",QJsonArray{QJsonObject{{"request_id",id},{"public_attribution",unicode}}});need(pgn::ui::publicReportName(changed)==QString::fromUtf8("Émile-31"));
 changed=snapshot;changed.insert("audits",QJsonArray{QJsonObject{{"request_id",id}}});need(!pgn::ui::publicReportName(changed));
 std::cout<<"PASS "<<checks<<" native root-bound projection + strict UI identity/display checks\n";}
