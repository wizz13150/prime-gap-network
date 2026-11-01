#pragma once
#include "mining/source_counter_grammar.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace pgn::mining {
// Presentation coalescing may discard a frame whose successor carries no new
// counters. Keep the latest *consecutive producer pair*, never a relay average.
// The durable SOURCE archive is independent and retains the complete raw input.
// Only presentation lines may change: identity, clock, provenance and ownership
// remain the successor's actual observations, so old counters cannot look fresh.
inline bool carryRawCounterPair(const QJsonObject& previous,QJsonObject& next,
                               qsizetype publicationLimit){
 const auto identity=next.value("manifest").toObject();
 if(identity.isEmpty()||identity.value("run").toString().isEmpty()||
    previous.value("manifest").toObject()!=identity)return false;
  const auto& pattern=sourceDisplayCounterGrammar();
 QMap<quint64,QString> samples;
 const auto collect=[&](const QJsonArray& lines){
  qsizetype found=0;quint64 last=0;
  for(const auto& value:lines){
   if(!value.isString())continue;
   const auto text=value.toString();const auto match=pattern.match(text);
   if(!match.hasMatch())continue;
   bool ok=false;const auto clock=match.captured(1).toULongLong(&ok);
   if(!ok)throw std::runtime_error("SOURCE display counter clock exceeds uint64");
   const auto existing=samples.constFind(clock);
   if(existing!=samples.cend()&&*existing!=text)
    throw std::runtime_error("Conflicting SOURCE display counters share a clock");
   samples.insert(clock,text);++found;last=std::max(last,clock);
  }
  return std::pair<qsizetype,quint64>{found,last};
 };
 const auto old=collect(previous.value("lines").toArray());
 if(!old.first)return false;
 const auto current=collect(next.value("lines").toArray());
 if(current.first>=2&&current.second>=old.second)return false;
 bool clockOk=false;const auto observedClock=next.value("source_clock_ns").toString().toULongLong(&clockOk);
 if(!clockOk||samples.isEmpty()||samples.lastKey()>observedClock)return false;
 while(samples.size()>2)samples.erase(samples.begin());
 QJsonArray view;
 for(const auto& value:next.value("lines").toArray()){
  if(value.isString()&&pattern.match(value.toString()).hasMatch())continue;
  view.append(value);
 }
 const auto nonCounters=view.size();
 for(auto it=samples.cbegin();it!=samples.cend();++it)view.append(it.value());
 bool trimmed=false;
 while(view.size()>512&&view.size()>samples.size()){view.removeFirst();trimmed=true;}
 next["lines"]=view;next["native_counter_pair_carried"]=true;
 while(QJsonDocument(next).toJson(QJsonDocument::Compact).size()>publicationLimit&&view.size()>samples.size()){
  view.removeFirst();next["lines"]=view;trimmed=true;
 }
 if(QJsonDocument(next).toJson(QJsonDocument::Compact).size()>publicationLimit)
  throw std::runtime_error("SOURCE carried counter pair exceeds publication bound");
 if(trimmed||view.size()<nonCounters+samples.size())next["native_display_truncated"]=true;
 return true;
}
}
