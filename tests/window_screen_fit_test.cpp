#include "ui/render/window_screen_fit.h"
#include <QApplication>
#include <QMainWindow>
#include <cstdio>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    int checks=0,failures=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}};
    // Native move/resize acknowledgements and the coalesced zero timer are
    // asynchronous; settle queued events before asserting the fixed point.
    const auto settle=[] {for(int i=0;i<4;++i)QApplication::processEvents();};
    QMainWindow window;
    QRect workArea{0,0,1920,1080};
    int chrome=174;
    window.resize(1380,970);window.move(269,7);window.show();
    pgn::ui::WindowScreenFit fit(window,[&]{return chrome;},[&]{return workArea;});
    QApplication::processEvents();fit.fitNow();
    check(workArea.contains(window.frameGeometry()),"initial native frame fits");
    workArea={0,0,1536,816};fit.requestFit();QApplication::processEvents();
    check(workArea.contains(window.frameGeometry()),"smaller laptop work area shrinks and repositions complete frame");
    check(window.minimumHeight()<=workArea.height(),"old larger minimum cannot lock window outside new screen");
    const auto stable=window.frameGeometry();fit.fitNow();fit.fitNow();
    check(window.frameGeometry()==stable,"repeat fit is stable, no resize loop or animation");
    window.setMinimumSize(0,0);window.resize(900,650);settle();
    for(const QPoint position:{QPoint{-250,70},QPoint{1400,70},QPoint{120,-180},QPoint{120,650}}){
        window.move(position);const auto requested=window.pos();settle();
        // The offscreen Qt plugin adjusts negative native-frame origins by its
        // two-pixel decoration. A policy snap to the work area is far larger.
        const auto decoration=window.frameGeometry().size()-window.size();
        const auto delta=window.pos()-requested;
        check(std::abs(delta.x())<=decoration.width()&&std::abs(delta.y())<=decoration.height()&&
              !workArea.contains(window.frameGeometry()),"ordinary user move may cross each screen edge without being clamped");
    }
    window.resize(920,660);const auto userGeometry=window.geometry();settle();
    check(window.geometry()==userGeometry,"ordinary resize does not reposition a partially offscreen window");
    for(const QRect area:{QRect{0,0,1280,720},QRect{-1600,20,1600,900},QRect{1920,0,1024,768},QRect{0,0,800,480},QRect{0,0,640,360}}){
        workArea=area;window.setMinimumSize(1500,1200);window.resize(1700,1300);window.move(area.topLeft()-QPoint(100,100));
        fit.fitNow();settle();
        check(area.contains(window.frameGeometry()),"large old minimum and offscreen origin repaired at changed work area");
        check(window.minimumHeight()<=window.height(),"readable-height preference never exceeds actual screen budget");
        fit.fitNow();settle();const auto once=window.frameGeometry();fit.fitNow();settle();
        if(window.frameGeometry()!=once){const auto twice=window.frameGeometry();std::fprintf(stderr,"UNSTABLE area=%d,%d %dx%d once=%d,%d %dx%d twice=%d,%d %dx%d min=%dx%d\n",area.x(),area.y(),area.width(),area.height(),once.x(),once.y(),once.width(),once.height(),twice.x(),twice.y(),twice.width(),twice.height(),window.minimumWidth(),window.minimumHeight());}
        check(window.frameGeometry()==once,"all work-area layouts reach fixed point");
    }
    workArea={0,0,1536,816};window.setMinimumSize(0,0);window.resize(900,650);window.move(10,10);fit.fitNow();
    check(window.size()==QSize(900,650),"already fitting user size preserved, not forced to largest size");
    window.hide();const auto hidden=window.geometry();workArea={0,0,640,360};fit.fitNow();
    check(window.geometry()==hidden&&!window.isVisible(),"hidden window never reopened or resized by monitor event");
    window.show();QApplication::processEvents();fit.fitNow();
    check(workArea.contains(window.frameGeometry()),"show after monitor change fits before further interaction");
    window.showMinimized();QApplication::processEvents();fit.fitNow();
    check(window.isMinimized(),"minimized window is not restored by fitter");
    std::printf("WINDOW_SCREEN_FIT %d checks / %d failures; native logical frame only, no mining/network/control\n",checks,failures);
    return failures?1:0;
}
