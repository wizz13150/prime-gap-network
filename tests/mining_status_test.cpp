#include "mining_status_button.h"
#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);
 MiningStatusButton button("PGN\nPRIME GAP SEARCH");button.resize(260,38);
 button.setStyleSheet("background:#02131a;color:#69efc5;");button.setCheckable(true);
 const QVariantList states{MiningCardState("qa-device-a","GPU A",true),MiningCardState("qa-device-b","GPU B",false)};
 button.present(states);button.show();app.processEvents();
 if(!button.findChildren<QTimer*>().isEmpty()){std::cerr<<"FAIL status projection retains a polling timer\n";return 1;}
 if(button.property("miningCardStates").toList()!=states||!button.toolTip().contains("GPU A: MINING")||!button.toolTip().contains("GPU B: INACTIVE")){std::cerr<<"FAIL source order/state/accessibility projection\n";return 2;}
 auto paint=[&]{QImage image(button.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);button.render(&image);return image;};
 const auto first=paint();
 if(first.pixelColor(234,19)!=QColor("#69efc5")||first.pixelColor(246,19)!=QColor("#ef6b72")){std::cerr<<"FAIL original dot geometry/colours changed\n";return 3;}
 button.present(states);const auto unchanged=paint();
 if(first!=unchanged){std::cerr<<"FAIL repeated identical event changed painting\n";return 4;}
 QEventLoop loop;QTimer::singleShot(650,&loop,&QEventLoop::quit);loop.exec();
 if(button.property("miningCardStates").toList()!=states||paint()!=first){std::cerr<<"FAIL missing event invented OFF/stale transition\n";return 5;}
 auto stopped=states;stopped[0]=MiningCardState("qa-device-a","GPU A",false);button.present(stopped);
 if(paint().pixelColor(234,19)!=QColor("#ef6b72")||!button.accessibleDescription().contains("GPU A: INACTIVE")){std::cerr<<"FAIL explicit OFF source event\n";return 6;}
 int clicks=0;QObject::connect(&button,&QPushButton::clicked,&app,[&]{++clicks;});button.click();
 if(clicks!=1||!button.isChecked()){std::cerr<<"FAIL primary-tab interaction disabled\n";return 7;}
 if(paint().pixelColor(2,19)!=QColor("#ffad4a")){std::cerr<<"FAIL checked primary workspace edge missing\n";return 9;}
 button.present(states);if(paint().pixelColor(2,19)!=QColor("#ffad4a"))return 10;
 button.setChecked(false);if(paint().pixelColor(2,19)==QColor("#ffad4a"))return 11;
 button.present({});if(!button.property("miningCardStates").toList().isEmpty()){std::cerr<<"FAIL explicit empty inventory retained obsolete dots\n";return 8;}
 std::cout<<"PASS event-only state, original dot geometry/colours, identical frame, absence preservation, explicit OFF/removal and primary interaction\n";
 return 0;
}
