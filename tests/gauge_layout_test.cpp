#include "ui/render/gauge_layout.h"
#include <cmath>
#include <iostream>
int main(){
 for(double width:{270.,400.,440.,504.,544.})for(int count=1;count<=9;++count){
  double previousRight=-1e20;
  for(int i=0;i<count;++i){
   const auto g=FrontierGaugeGeometryForCard(10,width,i,count,70,22);
   const auto left=g.centerX-g.radius-3.5,right=g.centerX+g.radius+3.5;
   if(!std::isfinite(g.radius)||g.radius<=0||g.radius>22||left<previousRight+4.99||left<10||right>10+width)return 1;
   if(g.ringTop!=70-g.radius||g.ringBottom!=70+g.radius||g.labelTop!=g.ringBottom+4||g.needleInnerRadius!=g.radius+1)return 2;
   previousRight=right;
  }
 }
 const auto original=FrontierGaugeGeometryForCard(10,544,0,9,70,22);
 if(original.radius!=22||original.centerTextGuardRadius!=20)return 3;
 if(FrontierGaugeGeometryForCard(0,100,0,0,0,22).radius!=0||FrontierGaugeGeometryForCard(0,100,-1,1,0,22).radius!=0)return 4;
 std::cout<<"PASS original Horizon radius where it fits; nine metrics/strokes/hitboxes remain separated; invalid geometry fails closed\n";
}
