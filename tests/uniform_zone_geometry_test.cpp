#include <QApplication>
#include <cmath>
#include <iostream>
#include "ui/prototype_community_atlas.h"
int main(int argc,char** argv){
 QApplication app(argc,argv);PGNCommunityAtlas atlas;atlas.resize(1040,520);
 auto row=[](QString id,QString a,QString b,QString owner,QString status){return QJsonObject{{"id",id},{"axis_start",a},{"axis_end",b},{"start",a+QString(15,'0')},{"end",b+QString(15,'0')},{"owner",owner},{"status",status}};};
 QJsonArray zones;for(int i=0;i<1000;++i){auto zone=row(QString::number(i),QString::number(100000+i*100),QString::number(100100+i*100),{},"DECLARED_OPEN");zone["label"]=QString("Zone %1").arg(i+1);zones.append(zone);}
 const auto other=row("other-unit","199800","199900","Other","DECLARED_RESERVED");
 const auto local=row("own-unit","199982","199983","Wizz","LOCAL_ACTIVE");
 atlas.setData({{"researcher","Wizz"},{"zones",zones},{"runs",QJsonArray{row("open","100000","200000",{},"DECLARED_OPEN"),other,local}}});
 atlas.show();app.processEvents();atlas.grab();
 auto equal=[&]{const auto a=atlas.jobRect("other-unit"),b=atlas.jobRect("own-unit");return a.isValid()&&b.isValid()&&std::abs(a.width()-b.width())<1e-6&&std::abs(a.height()-b.height())<1e-6;};
 if(!equal()||std::abs(atlas.jobRect("own-unit").width()-2.)>1e-6)return 1;
 atlas.focusRange(190000,200000);app.processEvents();atlas.grab();if(!equal()||atlas.jobRect("own-unit").width()<=2.)return 2;
 const auto before=atlas.jobRect("own-unit");atlas.setProgress("own-unit",.5,"2026-10-12T03:00:00Z");app.processEvents();atlas.grab();if(atlas.jobRect("own-unit")!=before)return 3;
 atlas.clickReset();app.processEvents();atlas.grab();if(!equal()||std::abs(atlas.jobRect("own-unit").width()-2.)>1e-6)return 4;
 std::cout<<"PASS identical projected zone widths/heights for local and other owners, overview/detail/reset, live progress unchanged geometry\n";
}
