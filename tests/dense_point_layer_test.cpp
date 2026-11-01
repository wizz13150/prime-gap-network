#include "ui/dense_point_layer.h"
#include <QElapsedTimer>
#include <iostream>
int main(){
 pgn::ui::DensePointLayer layer;pgn::ui::DenseView view{0,100,0,100,QSize(1000,400),1};
 const QRgb mint=qRgb(105,239,197);
 layer.rebuild(view,{{0,0,mint},{100,100,mint},{50,50,mint},{-1,50,mint}});
 if(layer.visibleCount()!=3||!layer.matches(view)||layer.image().pixelColor(0,399).alpha()!=255||layer.image().pixelColor(999,0).alpha()!=255)return 1;
 const auto onePass=layer.image();layer.begin(view);layer.append({{0,0,mint},{100,100,mint}});
 layer.append({{50,50,mint},{-1,50,mint}});layer.finish();
 if(layer.image()!=onePass||layer.visibleCount()!=3)return 3;
 layer.rebuild(view,{{0,0,mint,2},{100,100,mint,3},{50,50,mint,4},{-1,50,mint,100},{20,20,mint,0}});
 const auto compressed=layer.image();
 layer.rebuild(view,{{0,0,mint},{0,0,mint},{100,100,mint},{100,100,mint},{100,100,mint},
  {50,50,mint},{50,50,mint},{50,50,mint},{50,50,mint}});
 if(layer.image()!=compressed||layer.visibleCount()!=9)return 4;
 std::vector<pgn::ui::DensePoint> points;points.reserve(1000000);
 for(int i=0;i<1000000;++i)points.push_back({double(i%10000)/100,double((i*17)%10000)/100,mint});
 view.revision=2;QElapsedTimer timer;timer.start();layer.rebuild(view,points);
 if(layer.visibleCount()!=points.size()||!layer.matches(view))return 2;
 std::cout<<"PASS exact visible admission/bounds;1000000 source points rasterized in "<<timer.nsecsElapsed()/1000000.0<<" ms; no sampling or averages\n";
 return 0;
}
