#pragma once
#include "mining_layout.h"
#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <functional>

namespace pgn::ui {
// React to work-area/DPI/monitor changes, not just the first show. No scaling,
// scrolling or mining control: retain native logical-pixel content/layout.
class WindowScreenFit final : public QObject {
    QWidget& window_;
    std::function<int()> chrome_;
    std::function<QRect()> area_;
    bool queued_{}, fitting_{};
    QPointer<QWindow> boundWindow_;
    void bindScreen(QScreen* screen) {
        if(screen)connect(screen,&QScreen::availableGeometryChanged,this,[this]{requestFit();});
    }
    void bindWindow() {
        auto* native=window_.windowHandle();
        if(native&&native!=boundWindow_){
            boundWindow_=native;
            connect(native,&QWindow::screenChanged,this,[this](QScreen*){requestFit();});
        }
    }
    bool eventFilter(QObject* object,QEvent* event) override {
        if(object==&window_&&!fitting_&&
           (event->type()==QEvent::Show||event->type()==QEvent::Resize||event->type()==QEvent::Move||
            event->type()==QEvent::WindowStateChange||event->type()==QEvent::WinIdChange)){
            bindWindow();requestFit();
        }
        return QObject::eventFilter(object,event);
    }
public:
    WindowScreenFit(QWidget& window,std::function<int()> chrome,std::function<QRect()> area={})
        :QObject(&window),window_(window),chrome_(std::move(chrome)),area_(std::move(area)) {
        window_.installEventFilter(this);
        for(auto* screen:QGuiApplication::screens())bindScreen(screen);
        connect(qApp,&QGuiApplication::screenAdded,this,[this](QScreen* screen){bindScreen(screen);requestFit();});
        connect(qApp,&QGuiApplication::screenRemoved,this,[this](QScreen*){requestFit();});
        bindWindow();requestFit();
    }
    void requestFit() {
        if(queued_||fitting_)return;
        queued_=true;
        QTimer::singleShot(0,this,[this]{queued_=false;fitNow();});
    }
    void fitNow() {
        if(fitting_||!window_.isVisible()||window_.isMinimized()||window_.isMaximized()||window_.isFullScreen())return;
        const QRect available=area_?area_():(window_.screen()?window_.screen()->availableGeometry():QRect{});
        if(!available.isValid())return;
        fitting_=true;
        const auto frameExtra=window_.frameGeometry().size()-window_.size();
        const auto safe=available.width()>16&&available.height()>16?available.adjusted(8,8,-8,-8):available;
        const int maxWidth=std::max(1,safe.width()-std::max(0,frameExtra.width()));
        const int maxHeight=std::max(1,safe.height()-std::max(0,frameExtra.height()));
        window_.setMinimumWidth(std::min(window_.minimumWidth(),maxWidth));
        window_.setMinimumHeight(MiningLayout::minimumWindowHeight(chrome_?chrome_():0,maxHeight));
        window_.resize(std::min(window_.width(),maxWidth),std::min(window_.height(),maxHeight));
        const auto frame=window_.frameGeometry();
        const QPoint target(std::clamp(frame.left(),safe.left(),std::max(safe.left(),safe.right()-frame.width()+1)),
                            std::clamp(frame.top(),safe.top(),std::max(safe.top(),safe.bottom()-frame.height()+1)));
        // Move by the measured native-frame delta, not an assumption that pos()
        // and frameGeometry().topLeft() share an origin on every platform.
        if(target!=frame.topLeft())window_.move(window_.pos()+target-frame.topLeft());
        fitting_=false;
    }
};
}
