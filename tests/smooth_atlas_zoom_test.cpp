#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QWheelEvent>
#include <iostream>
#include "ui/prototype_community_atlas.h"

int main(int argc,char** argv){
 QApplication app(argc,argv);PGNCommunityAtlas atlas;atlas.resize(1040,520);
 QJsonArray zones;for(int i=0;i<1000;++i){const auto a=QString::number(100000+i*100),b=QString::number(100100+i*100);
  zones.append(QJsonObject{{"start",a+"000000000000000"},{"end",b+"000000000000000"},{"axis_start",a},{"axis_end",b},{"zone",i+1},{"status","DECLARED_OPEN"}});
 }
 QJsonArray runs;for(const auto& zone:zones)runs.append(zone);for(int i=985;i<1000;++i){auto own=zones[i].toObject();own["owner"]="Wizz";own["status"]="LOCAL_COMPLETED";own["id"]=QString::number(i);runs.append(own);}
 atlas.setData({{"researcher","Wizz"},{"zones",zones},{"runs",runs}});atlas.show();app.processEvents();atlas.grab();
 if(atlas.property("pgn_atlas_owner_rows").toInt()!=15||atlas.property("pgn_atlas_owner_overlap_checks").toInt()!=15000)return 8;
 if(atlas.property("pgn_atlas_owner_exact_checks").toInt()>45)return 9;
 const QPointF point(145+(1040-178)*.42,180);const double anchor=.42,pivot=atlas.axisStart()+(atlas.axisEnd()-atlas.axisStart())*anchor;
 auto wheel=[&](int angle,int pixel=0){QWheelEvent e(point,atlas.mapToGlobal(point.toPoint()),QPoint(0,pixel),QPoint(0,angle),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&atlas,&e);};
 wheel(120);if(atlas.axisStart()!=100000||atlas.axisEnd()!=200000||!atlas.zoomAnimating())return 1;
 int frames=0;double previous=100000;long long worst=0;QElapsedTimer elapsed;elapsed.start();
 while(atlas.zoomAnimating()&&elapsed.elapsed()<1600){app.processEvents();const double span=atlas.axisEnd()-atlas.axisStart();
  if(span>previous+1e-6)return 2;if(span<previous-1e-6){++frames;previous=span;}
  if(std::abs(atlas.axisStart()+span*anchor-pivot)>1e-6)return 3;
  worst=std::max(worst,atlas.property("pgn_atlas_render_ns").toLongLong());QThread::msleep(2);
 }
 if(atlas.zoomAnimating()||frames<8||std::abs(previous/100000-.7)>.0001)return 4;
 const double before=previous;wheel(0,7);while(atlas.zoomAnimating()){app.processEvents();QThread::msleep(2);}if(atlas.axisEnd()-atlas.axisStart()>=before)return 5;
 const double beforeBurst=atlas.axisEnd()-atlas.axisStart();wheel(120);wheel(120);wheel(120);
 elapsed.restart();while(atlas.zoomAnimating()&&elapsed.elapsed()<1600){app.processEvents();QThread::msleep(2);}
 if(std::abs((atlas.axisEnd()-atlas.axisStart())/beforeBurst-std::pow(.7,3))>.0001)return 6;
 wheel(-120);atlas.clickReset();elapsed.restart();while(elapsed.elapsed()<180){app.processEvents();QThread::msleep(2);}
 if(atlas.zoomAnimating()||atlas.axisStart()!=100000||atlas.axisEnd()!=200000)return 7;
 std::cout<<"PASS continuous_cursor_anchored_zoom frames="<<frames<<" worst_render_ns="<<worst<<" pixel_input burst_accumulation reset_cancels_motion\n";
}
