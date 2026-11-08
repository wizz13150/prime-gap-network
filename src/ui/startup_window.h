#pragma once
#include <QApplication>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>
namespace pgn::ui {
// Counts real startup milestones, never elapsed time or blockchain sync.
class StartupWindow final:public QWidget {
 QLabel* detail_{};QProgressBar* progress_{};bool enabled_{};
public:
 explicit StartupWindow(bool minimized):QWidget(nullptr,Qt::SplashScreen),enabled_(!minimized){
  setObjectName("pgn-startup-window");setWindowTitle("Starting PGN Horizon");setWindowIcon(QApplication::windowIcon());setFixedSize(440,148);
  setStyleSheet("QWidget#pgn-startup-window{background:#061820;border:1px solid #347076;border-radius:12px;}QLabel{color:#e0f0ed;background:transparent;}QProgressBar{border:1px solid #294b55;border-radius:5px;background:#0a252e;color:#acd6d7;text-align:center;height:21px;}QProgressBar::chunk{background:#21766d;border-radius:4px;}");
  auto* outer=new QVBoxLayout(this);outer->setContentsMargins(22,18,22,18);outer->setSpacing(9);auto* line=new QHBoxLayout;
  auto* icon=new QLabel;icon->setPixmap(QApplication::windowIcon().pixmap(40,40));line->addWidget(icon);auto* title=new QLabel("Starting Horizon");title->setStyleSheet("font:600 18px 'Segoe UI';");line->addWidget(title,1);outer->addLayout(line);
  detail_=new QLabel;detail_->setStyleSheet("font:13px 'Segoe UI';color:#a7c8d1;");outer->addWidget(detail_);progress_=new QProgressBar;progress_->setRange(0,5);progress_->setFormat("Step %v of %m");outer->addWidget(progress_);
  if(auto* screen=QGuiApplication::primaryScreen())move(screen->availableGeometry().center()-rect().center());
 }
 void step(int completed,const QString& detail){if(!enabled_)return;detail_->setText(detail);progress_->setValue(completed);show();repaint();}
 void finish(){hide();}
};
}
