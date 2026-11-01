#undef NDEBUG
#include "wallet/peer_bootstrap.h"
#include "engine/local_runtime_client.h"
#include <QCoreApplication>
#include <cassert>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 const QString genesis(64,'a');
 const QJsonObject valid{{"schema","PGN_TESTNET_BOOTSTRAP_V1"},{"genesis",genesis},{"checkpoint",QJsonObject{{"height",1242},{"hash",QString(64,'b')}}},{"tls",QJsonObject{{"host","bootstrap.example.org"},{"port",8443}}}};
 assert(pgn::wallet::bootstrapDescriptorError(valid,genesis).isEmpty());
 assert(!pgn::wallet::bootstrapDescriptorError(valid,QString(64,'c')).isEmpty());
 for(const auto& port:QJsonArray{0,-1,65536,443.5,"8443",QJsonValue{}}){auto value=valid;auto tls=value["tls"].toObject();tls["port"]=port;value["tls"]=tls;assert(!pgn::wallet::bootstrapDescriptorError(value,genesis).isEmpty());}
 for(const auto& host:QStringList{"","https://example.org","127.0.0.1:443","host\nheader","../bootstrap"}){auto value=valid;auto tls=value["tls"].toObject();tls["host"]=host;value["tls"]=tls;assert(!pgn::wallet::bootstrapDescriptorError(value,genesis).isEmpty());}
 for(const auto& height:QJsonArray{0,-1,1.5,"1242",QJsonValue{}}){auto value=valid;auto checkpoint=value["checkpoint"].toObject();checkpoint["height"]=height;value["checkpoint"]=checkpoint;assert(!pgn::wallet::bootstrapDescriptorError(value,genesis).isEmpty());}
 const QJsonObject device{{"id","GPU-11111111-2222-3333-4444-555555555555"},{"name","GPU"},{"utilization_percent",40},{"power_watts",80},{"memory_used_bytes","1048576"},{"memory_total_bytes","8388608"}};
 QJsonObject hardware{{"schema","PGN_LOCAL_HARDWARE_V1"},{"available",true},{"devices",QJsonArray{device}}};
 const auto sample=pgn::engine::localHardwareSnapshot(hardware);assert(sample&&sample->devices.size()==1&&sample->devices[0].utilizationPercent==40&&!sample->devices[0].throttleTelemetryAuthoritative);
 hardware["available"]=false;assert(!pgn::engine::localHardwareSnapshot(hardware));hardware["available"]=true;hardware["devices"]=QJsonArray{device,device};assert(!pgn::engine::localHardwareSnapshot(hardware));
 return 0;
}
