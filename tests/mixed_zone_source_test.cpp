#include "ui/prototype_community_atlas.h"
#include "ui/zone_source_coverage.h"
#include <QApplication>
#include <cmath>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);PGNCommunityAtlas atlas;atlas.resize(1040,520);
 const auto row=[](QString id,QString a,QString b,QString owner,QString status){return QJsonObject{{"id",id},{"axis_start",a},{"axis_end",b},{"start",a+QString(15,'0')},{"end",b+QString(15,'0')},{"owner",owner},{"status",status},{"_device_color","#46c8ee"}};};
 QJsonArray zones;for(int i=0;i<1000;++i){auto z=row(QString::number(i),QString::number(100000+i*100),QString::number(100100+i*100),{},"DECLARED_OPEN");z["label"]=QString("Zone %1").arg(i+1);zones.append(z);}
 const auto zone=zones.last().toObject();const auto open=row("open","100000","200000",{},"DECLARED_OPEN");
 const auto done=row("done","199981","200000","Wizz","LOCAL_SOURCE_FINISHED");
 const auto coverage=pgn::ui::zoneSourceCoverage(zone,{done,done});if(coverage.complete||coverage.covered!="19000000000000000"||std::abs(coverage.fraction-.19)>1e-9)return 1;
 auto tiny=done;tiny["start"]=zone["start"];tiny["end"]=pgn::booking::subtractUnsigned(zone["end"].toString(),"1");
 const auto nearFull=pgn::ui::zoneSourceCoverage(zone,{tiny});if(nearFull.complete||nearFull.covered!=pgn::booking::subtractUnsigned(pgn::booking::subtractUnsigned(zone["end"].toString(),zone["start"].toString()),"1"))return 2;
 const auto full=row("full","199900","200000","Wizz","LOCAL_SOURCE_FINISHED");if(!pgn::ui::zoneSourceCoverage(zone,{full,done}).complete)return 3;
 auto active=done;active["status"]="LOCAL_ACTIVE";if(pgn::ui::zoneSourceCoverage(zone,{active}).covered!="0")return 4;
 atlas.setData({{"researcher","Wizz"},{"zones",zones},{"runs",QJsonArray{open,done}}});atlas.show();app.processEvents();auto image=atlas.grab().toImage();
 const auto rect=atlas.jobRect("done");if(rect.width()!=2||atlas.property("pgn_atlas_mixed_source_zones").toInt()!=1)return 5;
 const auto x=int(std::floor(rect.center().x()));const auto top=image.pixelColor(x,int(rect.top()+3)),bottom=image.pixelColor(x,int(rect.bottom()-3));
 if(top!=QColor("#46c8ee")||bottom!=QColor("#69efc5"))return 6;
 if(argc>1&&!image.save(QString::fromLocal8Bit(argv[1])))return 7;
 atlas.setData({{"researcher","Wizz"},{"zones",zones},{"runs",QJsonArray{open,full}}});app.processEvents();atlas.grab();if(atlas.property("pgn_atlas_mixed_source_zones").toInt()!=0||atlas.jobRect("full")!=rect)return 8;
 atlas.setData({{"researcher","Wizz"},{"zones",zones},{"runs",QJsonArray{open,tiny}}});app.processEvents();image=atlas.grab().toImage();if(atlas.property("pgn_atlas_mixed_source_zones").toInt()!=1||image.pixelColor(x,int(rect.bottom()-1))!=QColor("#69efc5"))return 9;
 atlas.focusRange(190000,200000);app.processEvents();atlas.grab();if(atlas.property("pgn_atlas_mixed_source_zones").toInt()!=1||atlas.jobRect("done").width()<=2)return 10;
 std::cout<<"PASS exact completed-interval union/dedup, rounded-near-full remains partial, active not completed, two-color overview at unchanged zone width, zoom retains partial state\n";return 0;
}
