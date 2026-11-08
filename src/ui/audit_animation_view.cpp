#include "ui/audit_animation_view.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#ifdef Q_OS_WIN
#include "ui/audit3d/audit_native_surface.h"
#endif
namespace pgn::ui{
struct AuditAnimationView::Impl{
 Reader reader;Projection projection;QTimer timer;QLabel* status{};QPushButton* previewButton{};qint64 height{};QString hash;QJsonArray watched,displayed;QJsonObject last;
 bool busy{},reset{true},started{},ready{},preview{};quint64 generation{};
#ifdef Q_OS_WIN
 pgn::audit3d::Surface* surface{};
#endif
};
AuditAnimationView::AuditAnimationView(Reader reader,Projection projection,QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>()){
 setObjectName("pgn-network-audit-animation");setMinimumHeight(220);setMaximumHeight(310);impl_->reader=std::move(reader);impl_->projection=std::move(projection);
 auto* box=new QVBoxLayout(this);box->setContentsMargins(0,0,0,0);box->setSpacing(0);auto* line=new QHBoxLayout;line->setSpacing(10);
 auto* title=new QLabel("NETWORK AUDITS");title->setStyleSheet("color:#bce3e7;font:600 11px 'Segoe UI';");line->addWidget(title);
 impl_->status=new QLabel("Connecting to current audit activity…");impl_->status->setStyleSheet("color:#93b8c4;font:11px 'Segoe UI';");line->addWidget(impl_->status,1);
 auto* previewButton=new QPushButton("Preview animation");impl_->previewButton=previewButton;previewButton->setEnabled(false);previewButton->setObjectName("pgn-preview-audit-animation");previewButton->setMaximumHeight(25);line->addWidget(previewButton);box->addLayout(line);
 connect(previewButton,&QPushButton::clicked,this,[this]{preview();});
#ifdef Q_OS_WIN
 impl_->surface=new pgn::audit3d::Surface(this);box->addWidget(impl_->surface,1);
 impl_->surface->onWatched=[this](const QJsonArray& ids){if(!impl_->preview)impl_->displayed=ids;};
 impl_->surface->onReady=[this]{impl_->ready=true;impl_->previewButton->setEnabled(true);refresh();};
 impl_->surface->onError=[this](const QString& error){impl_->previewButton->setEnabled(false);impl_->status->setText(error);setProperty("pgn_audit_animation_error",error);};
#else
 impl_->status->setText("The articulated audit renderer is currently available on Windows");previewButton->setEnabled(false);box->addStretch();
#endif
 impl_->timer.setInterval(2000);connect(&impl_->timer,&QTimer::timeout,this,[this]{refresh();});impl_->timer.start();
}
AuditAnimationView::~AuditAnimationView(){
#ifdef Q_OS_WIN
 impl_->surface->onReady={};impl_->surface->onError={};impl_->surface->onWatched={};delete impl_->surface;impl_->surface=nullptr;
#endif
}
void AuditAnimationView::showEvent(QShowEvent* event){QWidget::showEvent(event);impl_->reset=true;impl_->preview=false;
#ifdef Q_OS_WIN
 if(!impl_->started){impl_->started=true;const auto root=QCoreApplication::applicationDirPath();impl_->surface->start(QDir(root).filePath("assets/audit-animation.rcc"),QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("audit3d-v1"),QDir(root).filePath("WebView2Loader.dll"),"network-animation.html");}
#endif
 QTimer::singleShot(0,this,[this]{refresh();});
}
void AuditAnimationView::hideEvent(QHideEvent* event){++impl_->generation;impl_->reset=true;impl_->preview=false;impl_->displayed={};
#ifdef Q_OS_WIN
 impl_->surface->suspend();
#endif
 QWidget::hideEvent(event);}
void AuditAnimationView::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);QTimer::singleShot(0,this,[this]{reproject();});}
void AuditAnimationView::reproject(){
#ifdef Q_OS_WIN
 if(impl_->ready&&isVisible()&&!impl_->preview&&impl_->projection&&!impl_->last.isEmpty())impl_->surface->project(impl_->projection(impl_->last.value("audits").toArray(),impl_->surface));
#endif
}
void AuditAnimationView::preview(){
#ifdef Q_OS_WIN
 if(!impl_->ready)return;++impl_->generation;impl_->preview=true;impl_->status->setText("Animation preview · simulated activity");impl_->surface->preview(impl_->projection?impl_->projection({},impl_->surface):QJsonObject{});
 const auto generation=impl_->generation;QTimer::singleShot(35000,this,[this,generation]{if(generation!=impl_->generation||!isVisible())return;impl_->preview=false;impl_->reset=true;refresh();});
#endif
}
void AuditAnimationView::refresh(){
#ifdef Q_OS_WIN
 if(!isVisible()||!impl_->ready||impl_->busy||impl_->preview||!impl_->reader)return;
 impl_->busy=true;const auto generation=impl_->generation;const bool reset=impl_->reset;const QPointer<AuditAnimationView> guard(this);
 impl_->reader(reset?0:impl_->height,reset?QString{}:impl_->hash,reset?QJsonArray{}:impl_->watched,[guard,generation,reset](QJsonObject view,QString error){
  if(!guard)return;auto& d=*guard->impl_;d.busy=false;if(generation!=d.generation||!guard->isVisible()||d.preview)return;
  if(!error.isEmpty()){d.status->setText("Audit activity unavailable · "+error);guard->setProperty("pgn_audit_animation_error",error);return;}
  d.reset=false;d.last=view;const auto state=view.value("state").toObject();d.height=state.value("height").toInteger();d.hash=state.value("bestblockhash").toString();
  d.watched=d.displayed;QSet<QString> ids;for(const auto& id:d.displayed)ids.insert(id.toString());for(const auto& row:view.value("audits").toArray()){const auto report=row.toObject();bool active=false;for(const auto& j:report.value("jobs").toArray()){const auto job=j.toObject();active|=job.value("state")=="committed"||(job.value("state")=="assigned"&&!job.value("expired").toBool());}if(active&&d.watched.size()<32&&!ids.contains(report.value("request_id").toString())){d.watched.append(report.value("request_id"));ids.insert(report.value("request_id").toString());}}
  const auto totals=view.value("totals").toObject();const auto jobs=totals.value("active_jobs").toInteger();d.status->setText(jobs?QString("%1 active audits across the network%2").arg(jobs).arg(view.value("truncated").toBool()?" · bounded visual sample":""):"No audits currently being processed");
  guard->setProperty("pgn_audit_animation_global_jobs",jobs);guard->setProperty("pgn_audit_animation_height",d.height);guard->setProperty("pgn_audit_animation_error",QString{});
  const auto projected=d.projection?d.projection(view.value("audits").toArray(),d.surface):QJsonObject{};d.surface->present({{"view",view},{"reset",reset},{"age_ms",0},{"projection",projected}});
 });
#endif
}
}
