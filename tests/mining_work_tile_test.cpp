#include "ui/mining_work_tile.h"
#include <QImage>
#include <iostream>
#include <limits>

int main(){
    int checks=0,failures=0;
    const auto check=[&](bool pass,const char* why){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<why<<'\n';}};
    using pgn::ui::observedWorkFraction;
    for(const auto invalid:{-1.,1.001,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})check(!observedWorkFraction(invalid),"invalid observation stays absent");
    check(!observedWorkFraction({}),"missing stays absent");
    pgn::ui::WorkProgressTransition motion;
    motion.observe("A",.2,0);check(motion.value(0)==.2,"first observation immediate");
    motion.observe("A",.8,1000);
    double previous=.2;
    for(int ms=1000;ms<=1250;++ms){const double at=*motion.value(ms);check(at>=previous&&at>=.2&&at<=.8,"motion monotone bounded by source cursor");previous=at;}
    check(motion.value(1250)==.8&&motion.value(9000)==.8,"no extrapolated progress");
    motion.observe("A",.8,1100);check(std::abs(*motion.value(1125)-.5)<1e-12,"identical repaint does not restart transition");
    motion.observe("A",.3,1200);check(motion.value(1200)==.3,"source regression resets immediately");
    motion.observe("B",.9,1300);check(motion.value(1300)==.9,"new job not blended with previous job");
    motion.observe("B",{},1400);check(!motion.value(1450),"missing observation not animated as zero");
    motion.observe("B",.7,1500);check(motion.value(1500)==.7,"new known value after missing immediate");
    motion.observe("B",.9,100);check(motion.value(100)==.9,"clock rollback resets immediate");
    for(int update=0;update<100;++update){motion.observe("stream",update/100.,update*25);const auto at=motion.value(update*25+24);check(at&&*at>=0&&*at<=update/100.,"rapid updates stay behind known work");}
    using pgn::ui::workTextureSpeed;
    check(!workTextureSpeed({})&&!workTextureSpeed(-1.)&&!workTextureSpeed(std::numeric_limits<double>::quiet_NaN()),"texture rejects missing/invalid throughput");
    check(workTextureSpeed(0.)==0.&&*workTextureSpeed(1e9)<*workTextureSpeed(1e12),"texture responds to real rate including zero");
    pgn::ui::WorkTextureMotion texture;
    check(!texture.advance("A",{},true,0),"no invented activity before first known rate");
    check(texture.advance("A",1e9,true,100)==0.,"first known source does not backdate motion");
    const auto moved=texture.advance("A",1e9,true,225);check(moved&&*moved>0,"known live throughput moves texture");
    check(texture.advance("A",{},true,350)==moved&&texture.advance("A",{},true,500)==moved,"missing source freezes without blinking");
    check(texture.advance("A",1e12,false,750)==moved,"stopped source cannot animate");
    check(texture.advance("A",0.,true,900)==moved&&texture.advance("A",0.,true,1100)==moved,"known zero is static");
    check(!texture.advance("B",{},true,1200),"other job cannot inherit texture activity");
    texture.advance("B",1e9,true,1300);
    const auto beforeGap=*texture.advance("B",1e9,true,1400);
    const auto afterGap=*texture.advance("B",1e9,true,100000);
    check(std::abs((afterGap-beforeGap)-*workTextureSpeed(1e9)*.25)<1e-10,"hidden presentation caps advance, no large catchup");
    check(texture.advance("B",1e9,true,1)==0.,"texture monotonic clock rollback reset");
    for(int i=0;i<=1000;++i){const double f=i/1000.;check(observedWorkFraction(f)==f,"exact observation unchanged");}
    const auto image=[&](std::optional<double> value,int width=202,int height=42){
        QImage pixels(width+20,height+20,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor("#010b10"));
        QPainter painter(&pixels);pgn::ui::drawMiningWorkTile(painter,QRectF(10,10,width,height),QColor("#a88df0"),value,true);painter.end();return pixels;
    };
    const auto open=image(0),closed=image(1),unknown=image({});
    check(open==unknown,"unknown does not invent completed work");
    check(closed.pixelColor(100,20)!=open.pixelColor(100,20)&&closed.pixelColor(100,40)!=open.pixelColor(100,40),"full height changes not just bottom strip");
    for(const auto fraction:{.01,.1,.25,.414,.5,.8,.99}){
        const auto current=image(fraction);const auto edge=11+200*fraction;
        for(int x=16;x<205;++x){
            if(x<edge-3)check(current.pixelColor(x,30)==closed.pixelColor(x,30),"completed left region blue");
            if(x>edge+3)check(current.pixelColor(x,30)==open.pixelColor(x,30),"remaining right region cyan");
        }
        check(current.pixelColor(0,0)==open.pixelColor(0,0),"paint remains inside tile");
    }
    check(image(std::numeric_limits<double>::quiet_NaN())==open,"NaN never paints processed work");
    check(image(.414)!=image(.415),"subpixel cursor motion does not snap to integer pixels");
    for(int width:{4,9,30,202,2000})for(int height:{8,12,42,100}){
        const auto pixels=image(.5,width,height);check(!pixels.isNull()&&pixels.pixelColor(0,0)==QColor("#010b10"),"tiny and wide clipping");
    }
    std::cout<<"mining work tile: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
