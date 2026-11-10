#pragma once
#include "ui/component_versions_dialog.h"
#include "update/component_update_service.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QFileInfo>
#include <QPointer>
#include <QToolButton>
#include <QStyle>
#include <QTimer>
#include <QDateTime>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <stdexcept>

namespace pgn::ui {
// A compact view of the SAME verified local inventory used by Components.
// Unknown remote metadata is deliberately never rendered as "up to date".
class ComponentStatusStrip final:public QWidget {
public:
 explicit ComponentStatusStrip(ComponentVersionsContext context,QWidget* parent=nullptr)
  :QWidget(parent),context_(std::move(context)){
  setObjectName("pgn-component-status-strip");
  auto* layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);
  for(const auto& id:QStringList{"ui","core","gpu","cpu"}){
   auto* button=new QToolButton(this);button->setObjectName("pgn-component-status-"+id);
   button->setText(id.toUpper()+" …");button->setCursor(Qt::PointingHandCursor);
   button->setToolTip("Checking installed component files");buttons_.insert(id,button);layout->addWidget(button);
   connect(button,&QToolButton::clicked,this,[this]{openComponents();});
  }
  updates_=new QToolButton(this);updates_->setObjectName("pgn-component-update-status");updates_->setText("Updates · not checked");updates_->setCursor(Qt::PointingHandCursor);layout->addWidget(updates_);
  connect(updates_,&QToolButton::clicked,this,[this]{openComponents();});
  setStyleSheet("QToolButton{font-family:'Segoe UI';font-size:12px;color:#a6c1c8;background:#0c2028;border:1px solid #20414d;border-radius:5px;padding:4px 7px;}QToolButton:hover{color:#efffff;border-color:#46cbb5;background:#12323a;}QToolButton:focus{border-color:#f1c572;}QToolButton[state=current]{color:#9de1d1;}QToolButton[state=unidentified],QToolButton[state=update],QToolButton[state=stale],QToolButton[state=unknown],QToolButton[state=ahead]{color:#edc17c;}");
  profileIdentity_=componentProfileIdentity(context_);refreshLocal();
  if(!context_.coreProfilePath.isEmpty()){
   auto* profileClock=new QTimer(this);profileClock->setInterval(1000);profileClock->setTimerType(Qt::CoarseTimer);
   connect(profileClock,&QTimer::timeout,this,[this]{
    const auto identity=componentProfileIdentity(context_);if(identity==profileIdentity_)return;
    profileIdentity_=identity;++profileRevision_;context_=refreshComponentVersionsContext(context_);
    updateRequest_={};refreshLocal();if(dialog_)dialog_->refreshLocal();
   });profileClock->start();
  }
  // A running client must discover a later release without reopening the menu.
  // Signed metadata only: optional installation is never triggered by this clock.
  if(context_.fetchOnOpen){
   auto* refresh=new QTimer(this);refresh->setObjectName("pgn-component-update-refresh");
   refresh->setTimerType(Qt::CoarseTimer);refresh->setInterval(10*60*1000);
   connect(refresh,&QTimer::timeout,this,[this]{if(!dialog_)checkSignedUpdates();});
   refresh->start();
  }
 }
 void refreshLocal(){
  if(localBusy_){localAgain_=true;return;}
  localBusy_=true;context_=refreshComponentVersionsContext(context_);const auto revision=profileRevision_;
  auto* watcher=new QFutureWatcher<QJsonObject>(this);
  connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[this,watcher,revision]{
   localBusy_=false;const auto value=watcher->result();watcher->deleteLater();
   if(revision==profileRevision_)presentLocal(value);
   if(localAgain_||revision!=profileRevision_){localAgain_=false;refreshLocal();return;}
   if(context_.fetchOnOpen)checkSignedUpdates();
  });
  watcher->setFuture(QtConcurrent::run([context=context_]{return inspectLocalComponentVersions(context);}));
 }
 void presentLocal(const QJsonObject& snapshot){
  setProperty("pgn_component_local_snapshot",snapshot);
  for(const auto& entry:snapshot.value("components").toArray()){
   const auto row=entry.toObject();auto* button=buttons_.value(row.value("id").toString());if(!button)continue;
   const bool verified=row.value("state")=="matched_local_manifest";const bool absent=row.value("code_root").toString().isEmpty();
   button->setText(row.value("id").toString().toUpper()+" "+(verified?row.value("version").toString():absent?QString("—"):QString("?")));
   button->setProperty("state",verified?"verified":absent?"absent":"unidentified");button->style()->unpolish(button);button->style()->polish(button);
   button->setToolTip((verified?QString("Installed version verified. "):absent?QString("Component is not configured locally. "):QString("Installed version is unverified. "))+row.value("reason").toString()+"\nClick to manage components.");
  }
  if(remoteFresh_)presentPublished(property("pgn_component_authenticated_catalog").toJsonObject());
 }
 void presentPublished(const QJsonObject& catalog){
  setProperty("pgn_component_release_catalog",catalog);
  if(catalog.value("schema")=="PGN_AUTHENTICATED_COMPONENT_VIEW_V1"&&catalog.value("publisher_authenticated").toBool()){
   remoteFresh_=true;
   setProperty("pgn_component_authenticated_catalog",catalog);setProperty("pgn_component_last_checked_utc",catalog.value("checked_at_utc").toString());QMap<QString,QString> available;
   for(const auto& value:catalog.value("components").toArray()){const auto row=value.toObject();available.insert(row.value("id").toString(),row.value("version").toString());}
   int changes=0,unknown=0;const auto local=property("pgn_component_local_snapshot").toJsonObject().value("components").toArray();
   for(const auto& id:buttons_.keys()){
    QJsonObject row;for(const auto& item:local)if(item.toObject().value("id")==id)row=item.toObject();
    const auto compare=row.value("state")=="matched_local_manifest"?pgn::update::componentVersionComparison(row.value("version").toString(),available.value(id)):std::optional<int>{};
    auto* button=buttons_[id];
    if(row.value("state")=="matched_local_manifest"){button->setText(id.toUpper()+" "+row.value("version").toString());button->setProperty("state","verified");button->setToolTip("Installed version verified. "+row.value("reason").toString()+"\nClick to manage components.");button->style()->unpolish(button);button->style()->polish(button);}
    if(!compare||*compare>0){
     ++unknown;
     if(row.value("state")=="matched_local_manifest"){
      button->setProperty("state",compare?"ahead":"unknown");button->setText(button->text()+" ?");
      button->setToolTip(compare?"Installed version is newer than the signed published version. No downgrade is proposed.":"Installed version verified; no comparable signed published version is available.");
      button->style()->unpolish(button);button->style()->polish(button);
     }
     continue;
    }
    if(*compare<0){++changes;button->setProperty("state","update");button->setText(button->text()+" ↑");button->setToolTip("Installed version verified. "+row.value("reason").toString()+"\nAvailable verified version: "+available.value(id)+"\nClick to manage components.");button->style()->unpolish(button);button->style()->polish(button);}
    else{button->setProperty("state","current");button->setText(button->text()+" ✓");button->setToolTip("Installed files verified; this version matches the signed published version.\nClick to manage components.");button->style()->unpolish(button);button->style()->polish(button);}
   }
   updates_->setText(changes?QString("Updates · %1 available").arg(changes):unknown?QString("Updates · partially verified"):QString("Updates · up to date"));
   updates_->setToolTip("Component availability checked against signed publisher metadata. Updates remain optional.");return;
  }
  if(property("pgn_component_authenticated_catalog").isValid())return;
  const bool available=catalog.value("component_versions_available").toBool();
  updates_->setText(available?"Updates · review":"Updates · versions unavailable");
  updates_->setToolTip(available?"Review available component versions before choosing an update.":"The repository response contains package versions, but no verified component version catalogue. Current/update status cannot be inferred.");
 }
 void presentUpdateChecking(){
  remoteFresh_=false;presentLocal(property("pgn_component_local_snapshot").toJsonObject());
  for(auto* button:buttons_)if(button->property("state")=="verified"){
   button->setText(button->text()+" …");button->setProperty("state","checking");
   button->setToolTip("Installed files verified. Checking signed published versions; current/update status is pending.");
   button->style()->unpolish(button);button->style()->polish(button);
  }
  updates_->setText("Updates · checking");
 }
 void presentUpdateFailure(const QString& error){
  remoteFresh_=false;presentLocal(property("pgn_component_local_snapshot").toJsonObject());
  for(auto* button:buttons_)if(button->property("state")=="verified"){
   button->setText(button->text()+" ?");button->setProperty("state","stale");
   button->setToolTip("Installed files verified. The latest signed update check failed; current/update status is unconfirmed.\n"+error);
   button->style()->unpolish(button);button->style()->polish(button);
  }
  const auto checked=property("pgn_component_last_checked_utc").toString();updates_->setText(error.contains("GitHub update service")?"Updates · GitHub unavailable":error.contains("Internet connection")?"Updates · connection failed":"Updates · check failed");updates_->setToolTip(error+(checked.isEmpty()?QString("\nNo successful version check yet."):QString("\nLast successful check: ")+checked));
 }
 void checkSignedUpdates(){
  try{
   const auto root=QFileInfo(context_.manifestPath).absolutePath();auto deployment=pgn::update::componentUpdateDeployment(root,context_.testnet);
   if(!context_.genesisHash.isEmpty()&&context_.genesisHash!=deployment.context.value("genesis_hash").toString())throw std::runtime_error("Update deployment differs from the active Core epoch");
   updateRequest_=pgn::update::componentUpdateRequest(deployment,property("pgn_component_local_snapshot").toJsonObject(),root,context_.mutableDataRoot,"check",{"ui","core","gpu","cpu"});
   if(!updateService_){
    updateService_=new pgn::update::ComponentUpdateService(std::move(deployment),this);
    updateService_->onResult=[this](const QJsonObject& value){presentPublished(value);};
    updateService_->onError=[this](const QString& error){presentUpdateFailure(error);};
   }
   if(updateService_->start(updateRequest_))presentUpdateChecking();
  }catch(const std::exception& error){presentUpdateFailure(QString::fromUtf8(error.what()));}
 }
 void openComponents(){
  if(dialog_){dialog_->show();dialog_->raise();dialog_->activateWindow();return;}
  dialog_=new ComponentVersionsDialog(context_,this);dialog_->setAttribute(Qt::WA_DeleteOnClose);
  const QPointer<ComponentStatusStrip> guard(this);
  dialog_->onLocalSnapshot=[guard](const QJsonObject& value){if(guard)guard->presentLocal(value);};
  dialog_->onPublishedCatalog=[guard](const QJsonObject& value){if(guard)guard->presentPublished(value);};
  dialog_->onPublishedFailure=[guard](const QString& error){if(guard)guard->presentUpdateFailure(error);};
  dialog_->show();
 }
private:
 ComponentVersionsContext context_;QMap<QString,QToolButton*> buttons_;QToolButton* updates_{};QPointer<ComponentVersionsDialog> dialog_;
 pgn::update::ComponentUpdateService* updateService_{};QJsonObject updateRequest_;bool remoteFresh_{},localBusy_{},localAgain_{};QByteArray profileIdentity_;quint64 profileRevision_{};
};
}
