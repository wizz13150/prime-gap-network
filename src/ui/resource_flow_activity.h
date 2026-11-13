#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <algorithm>
#include <cmath>
namespace pgn::ui {
// Resource activity is separate from ticket results: concurrent CPU work must
// not replace the GPU's gaps, progress, reports or audit evidence.
class ResourceFlowActivity {
public:
 struct State {bool moving{};double activity{};qint64 observed{};};
 void observe(const QString& device,const QJsonObject& status,bool connected,qint64 now){
  if(!device.startsWith("CPU/")||device.size()>128||(!rows_.contains(device)&&rows_.size()>=8))return;
  const bool resident=status.value("resident_result_handoff")==true&&status.value("resident_workers").toInt()>0&&status.value("resource_dispatch_threads").toInt()>0;
  const auto value=status.value(resident?"resource_positions_per_second":"positions_per_second");const double rate=value.toDouble();
  const bool active=connected&&status.value("enabled").toBool()&&status.value("compute_started").toBool()
   &&!status.value("budget_refused").toBool()&&status.value("error").toString().isEmpty()
   &&value.isDouble()&&std::isfinite(rate)&&rate>0;
  rows_[device]={active,active?std::clamp(std::log1p(rate/1e6)/std::log1p(1e6),.1,1.):0.,now};
 }
 bool expire(qint64 now){bool changed=false;for(auto& row:rows_)if(row.moving&&now-row.observed>3000){row.moving=false;changed=true;}return changed;}
 bool moving()const{for(const auto& row:rows_)if(row.moving)return true;return false;}
 const QMap<QString,State>& rows()const{return rows_;}
private:QMap<QString,State> rows_;
};
}
