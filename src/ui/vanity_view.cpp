#include "app/horizon_user_paths.h"
// MIT. Genuine local Core key custody and explicitly selected public-only workers.
#include "ui/vanity_view.h"
#include "ui/vanity_runtime.h"
#include "mining/ssh_snapshot_transport.h"
#include "ui/panel_style.h"
#include <QDirIterator>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QMouseEvent>
#include <QKeyEvent>
#include "ui/vanity_batch.h"
#include "ui/vanity_import_count.h"
#include "ui/vanity_device_picker.h"
#include "wallet/vanity_result_store.h"
#include "wallet/core_runtime.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QPointer>
#include <bit>
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QButtonGroup>
#include <QDateTime>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QWidget>
#include <QFrame>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QProcess>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QSet>
#include <memory>
namespace pgn::ui {
namespace {
void paintVanityCheck(QPainter& painter,QRectF area,bool checked,bool enabled){
 painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(enabled?QColor("#4d9898"):QColor("#35535b"),1));painter.setBrush(checked?QColor("#247f70"):QColor("#08242d"));painter.drawRoundedRect(area,3,3);
 if(checked){QPainterPath tick;tick.moveTo(area.left()+3,area.center().y());tick.lineTo(area.left()+6,area.bottom()-3);tick.lineTo(area.right()-2,area.top()+3);painter.setPen(QPen(QColor("#effff7"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(tick);}painter.restore();
}
class VanityCheckDelegate final:public QStyledItemDelegate {
public:
 explicit VanityCheckDelegate(QObject* parent):QStyledItemDelegate(parent){}
 void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
  QStyleOptionViewItem background(option);initStyleOption(&background,index);background.features&=~QStyleOptionViewItem::HasCheckIndicator;background.text.clear();option.widget->style()->drawControl(QStyle::CE_ItemViewItem,&background,painter,option.widget);
  paintVanityCheck(*painter,QRectF(option.rect.center().x()-7,option.rect.center().y()-7,14,14),index.data(Qt::CheckStateRole).toInt()==Qt::Checked,option.state&QStyle::State_Enabled);
 }
 bool editorEvent(QEvent* event,QAbstractItemModel* model,const QStyleOptionViewItem& option,const QModelIndex& index)override{
  bool toggle=false;if(event->type()==QEvent::MouseButtonRelease){const auto* mouse=static_cast<QMouseEvent*>(event);toggle=mouse->button()==Qt::LeftButton&&option.rect.contains(mouse->position().toPoint());}
  else if(event->type()==QEvent::KeyPress)toggle=static_cast<QKeyEvent*>(event)->key()==Qt::Key_Space;
  if(!toggle||!(index.flags()&Qt::ItemIsEnabled)||!(index.flags()&Qt::ItemIsUserCheckable))return false;
  return model->setData(index,index.data(Qt::CheckStateRole).toInt()==Qt::Checked?Qt::Unchecked:Qt::Checked,Qt::CheckStateRole);
 }
};
class VanityView final:public QWidget {
 struct Device {QCheckBox* box;QString host,mask;};
 struct Worker {QProcess* process{};QString host,id;QByteArray pending;bool finished{};quint64 hashed{};qint64 elapsed{};double rate{};};
 QString root_,directory_,helper_,remote_,gpuLease_,vaultDirectory_;pgn::mining::SshObservationEndpoint sshEndpoint_;QJsonObject profile_,job_,winner_;QList<Device> devices_;QList<std::shared_ptr<Worker>> workers_;
 QLineEdit* prefix_{};QLineEdit* suffix_{};QLineEdit* address_{};QLabel* state_{};VanityDevicePicker* devicePicker_{};QPushButton* generate_{};QPushButton* cancel_{};QCheckBox* gpu_{};
 QButtonGroup* patternModes_{};QLabel* patternFeedback_{};QLabel* patternJoin_{};QTimer* patternValidation_{};quint64 patternRevision_{};bool patternReady_{};
 bool testnet_{};QPointer<pgn::wallet::CoreRuntime> runtime_;QPushButton *export_{},*import_{},*importFile_{},*importKey_{};bool keyOperation_{};
 std::shared_ptr<bool> callbacksAlive_{std::make_shared<bool>(true)};
 VanityBatch batch_;std::uint64_t item_{},preparingToken_{};QSpinBox* count_{};QButtonGroup* countChoices_{};QLabel* batchCount_{};QTableWidget* results_{};QPushButton* older_{};QPushButton* newer_{};
 QBoxLayout* composer_{};
 VanityGpuServices gpuServices_;bool closing_{};std::function<void()> drained_;
 std::unique_ptr<pgn::wallet::VanityResultStore> store_;QSet<QString> checked_,imported_;QString importedBinding_;int pageOffset_{};bool devicePrepared_{};
 QSet<QString> cancelling_;bool busy_{},cancelled_{},resume_{},finalizing_{},polling_{};int waitCount_{};
 using Done=std::function<void(QJsonObject)>;
public:
 explicit VanityView(QString root,pgn::wallet::CoreRuntime* runtime,QString vaultDirectory,bool testnet):root_(std::move(root)),vaultDirectory_(std::move(vaultDirectory)),testnet_(testnet),runtime_(runtime){
  setObjectName("pgn-vanity-generator");setStyleSheet(dataPanelStyle()+
   "QWidget#pgn-vanity-generator{color:#e0e8ed;}"
   "QFrame#pgn-vanity-pattern-card{background:#101c26;border:1px solid #496275;border-radius:12px;}"
   "QFrame#pgn-vanity-quantity-card{background:#0b1b22;border:1px solid #2a4b56;border-radius:12px;}"
   "QLabel[vanityRole=caption]{color:#9eb0bb;} QLabel[vanityRole=heading]{color:#f1f4f6;font-weight:700;}"
   "QLabel#pgn-vanity-network{color:#edc480;background:#29251c;border:1px solid #6c5737;border-radius:10px;padding:5px 10px;}"
   "QLabel#pgn-vanity-title{font-size:22px;font-weight:700;}"
   "QFrame#pgn-vanity-prefix-field{background:#07111b;border:1px solid #bc9656;border-radius:7px;}"
   "QFrame#pgn-vanity-fixed-prefix{background:#29332f;border:1px solid #527364;border-radius:5px;}"
   "QLabel#pgn-vanity-fixed-namespace{color:#c8eadb;font-size:14px;font-weight:700;padding:6px 9px;}"
   "QLabel#pgn-vanity-fixed-codec{color:#9cadb5;font-size:16px;padding:0 8px;border-left:1px solid #527364;}"
   "QLineEdit#pgn-vanity-suffix{background:#07111b;color:#fff3cf;border:1px solid #bc9656;border-radius:7px;min-height:26px;padding:10px;font-size:20px;}"
   "QLineEdit#pgn-vanity-prefix{background:#07111b;color:#fff3cf;border:0;border-radius:7px;min-height:26px;padding:10px;font-size:20px;font-weight:600;}"
   "QLineEdit#pgn-vanity-prefix:focus{border:2px solid #edc480;padding:9px;}"
   "QPushButton[vanityPreset=true]{border-radius:0;background:#122630;color:#c0d0d9;padding:4px;}"
   "QPushButton:checked{background:#245449;border-color:#79d9b7;color:#f0fff9;font-weight:700;}"
   "QPushButton#pgn-vanity-generate{background:#e8bc72;color:#18232b;border:1px solid #f4d49f;font-weight:700;padding:11px 22px;}"
   "QPushButton#pgn-vanity-generate:hover{background:#f4d49f;}"
   "QPushButton#pgn-vanity-generate:disabled{background:#25363e;color:#8c9ca5;border-color:#3c505a;}"
   "QPushButton#pgn-vanity-cancel{background:transparent;border-color:#425760;color:#b8c8cf;}"
   "QPushButton#pgn-vanity-cancel:disabled{color:#607480;border-color:#293d47;}"
   "QSpinBox#pgn-vanity-count:disabled{color:#637680;border-color:#2b3e48;background:#111c24;}"
   "QLabel#pgn-vanity-found-count{color:#91dfbb;font-weight:700;}"
   "QTableWidget#pgn-vanity-results{color:#e1eaf0;border-color:#334f60;alternate-background-color:#0c1e29;}"
   "QHeaderView::section{color:#acbecb;background:#122631;font-weight:600;}"
   "QCheckBox{background:transparent;padding:2px;color:#c5d4dd;}"
   "QCheckBox::indicator{width:14px;height:14px;border:1px solid #65828d;border-radius:3px;background:#07111b;}"
   "QCheckBox::indicator:checked{background:#70caa4;border-color:#b2f1d4;}"
   "QCheckBox::indicator:disabled{background:#16232a;border-color:#33444d;}");
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(20,12,20,12);layout->setSpacing(8);
  auto* heading=new QHBoxLayout;auto* title=new QLabel("Your address. Your signature.");title->setObjectName("pgn-vanity-title");auto titleFont=title->font();titleFont.setPointSizeF(qMax(18.0,titleFont.pointSizeF()*1.8));titleFont.setBold(true);title->setFont(titleFont);title->setProperty("vanityRole","heading");heading->addWidget(title);heading->addStretch();auto* networkBadge=new QLabel(testnet_?"Testnet":"Mainnet");networkBadge->setObjectName("pgn-vanity-network");heading->addWidget(networkBadge);layout->addLayout(heading);
  auto* intro=new QLabel("Find a personal address, then keep it or add it to your wallet.");intro->setProperty("vanityRole","caption");intro->setWordWrap(true);layout->addWidget(intro);
  composer_=new QBoxLayout(QBoxLayout::LeftToRight);composer_->setSpacing(12);layout->addLayout(composer_);
  auto* patternCard=new QFrame;patternCard->setObjectName("pgn-vanity-pattern-card");auto* pattern=new QVBoxLayout(patternCard);pattern->setContentsMargins(12,10,12,10);pattern->setSpacing(6);composer_->addWidget(patternCard,3);
  auto* patternHeading=new QHBoxLayout;auto* prefixLabel=new QLabel("Match");prefixLabel->setProperty("vanityRole","heading");patternHeading->addWidget(prefixLabel);patternHeading->addStretch();patternModes_=new QButtonGroup(this);patternModes_->setExclusive(true);int mode=0;for(const auto& name:{QString("Prefix"),QString("Suffix"),QString("Both")}){auto* option=new QPushButton(name);option->setObjectName("pgn-vanity-mode-"+name.toLower());option->setProperty("vanityPreset",true);option->setCheckable(true);option->setChecked(mode==0);option->setMinimumHeight(26);patternModes_->addButton(option,mode++);patternHeading->addWidget(option);}pattern->addLayout(patternHeading);
  auto* row=new QHBoxLayout;auto* prefixFrame=new QFrame;prefixFrame->setObjectName("pgn-vanity-prefix-field");auto* prefixInput=new QHBoxLayout(prefixFrame);prefixInput->setContentsMargins(5,4,4,4);prefixInput->setSpacing(0);auto* fixedGroup=new QFrame;fixedGroup->setObjectName("pgn-vanity-fixed-prefix");fixedGroup->setProperty("addressPrefix",fixedPrefix());auto* fixedLayout=new QHBoxLayout(fixedGroup);fixedLayout->setContentsMargins(0,0,0,0);fixedLayout->setSpacing(0);auto* fixed=new QLabel(testnet_?"tpgn":"PGN");fixed->setObjectName("pgn-vanity-fixed-namespace");fixed->setAccessibleName(testnet_?"Fixed testnet network prefix tpgn":"Fixed mainnet network prefix PGN");fixedLayout->addWidget(fixed);prefixInput->addWidget(fixedGroup);prefix_=new QLineEdit;prefix_->setObjectName("pgn-vanity-prefix");prefix_->setAccessibleName("Characters after the fixed network prefix");prefix_->setMaxLength(32);prefix_->setPlaceholderText("Prefix");{auto palette=prefix_->palette();palette.setColor(QPalette::PlaceholderText,QColor(158,176,187,140));prefix_->setPalette(palette);}prefixLabel->setBuddy(prefix_);prefixInput->addWidget(prefix_,1);row->addWidget(prefixFrame,1);patternJoin_=new QLabel("…");patternJoin_->setAlignment(Qt::AlignCenter);row->addWidget(patternJoin_);suffix_=new QLineEdit;suffix_->setObjectName("pgn-vanity-suffix");suffix_->setAccessibleName("Address ending");suffix_->setMaxLength(testnet_?38:32);suffix_->setPlaceholderText("Suffix");{auto palette=suffix_->palette();palette.setColor(QPalette::PlaceholderText,QColor(158,176,187,140));suffix_->setPalette(palette);}row->addWidget(suffix_,1);pattern->addLayout(row);
  auto* alphabet=new QLabel(testnet_?"Fixed tpgn · case sensitive Base58. Excludes 0, O, I and lowercase l.":"Fixed PGN · case sensitive Base58. Excludes 0, O, I and lowercase l.");alphabet->setProperty("vanityRole","caption");alphabet->setWordWrap(true);pattern->addWidget(alphabet);patternFeedback_=new QLabel;patternFeedback_->setObjectName("pgn-vanity-pattern-feedback");patternFeedback_->setTextFormat(Qt::PlainText);patternFeedback_->setWordWrap(true);pattern->addWidget(patternFeedback_);pattern->addStretch();
  auto* quantityCard=new QFrame;quantityCard->setObjectName("pgn-vanity-quantity-card");auto* quantityLayout=new QVBoxLayout(quantityCard);quantityLayout->setContentsMargins(12,10,12,10);quantityLayout->setSpacing(6);composer_->addWidget(quantityCard,2);auto* quantityLabel=new QLabel("How many addresses?");quantityLabel->setProperty("vanityRole","heading");quantityLayout->addWidget(quantityLabel);
  auto* quantity=new QHBoxLayout;quantity->setSpacing(0);countChoices_=new QButtonGroup(this);countChoices_->setExclusive(true);
  for(int n:{1,5,10,0}){auto* choice=new QPushButton(n?QString::number(n):QString("Continuous"));choice->setMinimumHeight(26);choice->setProperty("vanityPreset",true);choice->setCheckable(true);choice->setChecked(n==1);choice->setObjectName("pgn-vanity-count-"+QString::number(n));countChoices_->addButton(choice,n);quantity->addWidget(choice);}
  quantity->addSpacing(8);count_=new QSpinBox;count_->setRange(1,1000);count_->setValue(1);count_->setObjectName("pgn-vanity-count");count_->setAccessibleName("Number of addresses");count_->setToolTip("Custom quantity: 1–1,000 addresses");count_->setMinimumHeight(28);count_->setFixedWidth(90);count_->setStyleSheet("QSpinBox{background:#07111b;color:#f1f4f6;border:1px solid #456271;border-radius:6px;padding:4px;}QSpinBox:disabled{color:#637680;border-color:#2b3e48;background:#111c24;}QSpinBox QLineEdit{border:0;background:transparent;padding:0;}");quantity->addWidget(count_);quantityLayout->addLayout(quantity);
  connect(countChoices_,&QButtonGroup::idClicked,this,[this](int n){count_->setEnabled(n!=0&&!busy_);if(n){const QSignalBlocker block(count_);count_->setValue(n);}});
  connect(count_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int n){countChoices_->setExclusive(false);for(auto* button:countChoices_->buttons())button->setChecked(countChoices_->id(button)==n);countChoices_->setExclusive(true);});
  devicePicker_=new VanityDevicePicker;quantityLayout->addWidget(devicePicker_);gpu_=devicePicker_->addDevice("gpu-source","GPU · no qualified worker attached",VanityDevicePicker::Kind::Gpu,false,false);gpu_->setObjectName("pgn-vanity-gpu-wizz-3090");devicePicker_->selectionChanged=[this]{updateKeyActions();};
  auto* actions=new QHBoxLayout;generate_=new QPushButton("Find addresses");generate_->setObjectName("pgn-vanity-generate");generate_->setMinimumWidth(150);cancel_=new QPushButton("Stop search");cancel_->setObjectName("pgn-vanity-cancel");cancel_->setEnabled(false);actions->addWidget(generate_);actions->addWidget(cancel_);actions->addStretch();batchCount_=new QLabel("0 found");batchCount_->setObjectName("pgn-vanity-found-count");actions->addWidget(batchCount_);quantityLayout->addLayout(actions);
  state_=new QLabel("Loading actual CPU packages…");state_->setObjectName("pgn-vanity-status");state_->setWordWrap(true);layout->addWidget(state_);
  address_=new QLineEdit;address_->setReadOnly(true);address_->setObjectName("pgn-vanity-address");address_->setPlaceholderText("Generated address");layout->addWidget(address_);
  address_->hide();
  auto* paging=new QHBoxLayout;auto* historyTitle=new QLabel("Generated addresses");historyTitle->setProperty("vanityRole","heading");paging->addWidget(historyTitle);paging->addStretch();newer_=new QPushButton("Newer");older_=new QPushButton("Older");newer_->setEnabled(false);older_->setEnabled(false);paging->addWidget(newer_);paging->addWidget(older_);layout->addLayout(paging);
  auto* keyActions=new QHBoxLayout;export_=new QPushButton("Export selected…");export_->setObjectName("pgn-vanity-export-selected");import_=new QPushButton("I&mport selected into wallet");import_->setObjectName("pgn-vanity-import-selected");importFile_=new QPushButton("Import &file…");importKey_=new QPushButton("Import &private key…");keyActions->addWidget(export_);keyActions->addWidget(import_);keyActions->addStretch();keyActions->addWidget(importFile_);keyActions->addWidget(importKey_);
  connect(export_,&QPushButton::clicked,this,[this]{exportSelected();});connect(import_,&QPushButton::clicked,this,[this]{importSelected();});connect(importFile_,&QPushButton::clicked,this,[this]{importFile();});connect(importKey_,&QPushButton::clicked,this,[this]{importPrivateKey();});
  results_=new QTableWidget;results_->setObjectName("pgn-vanity-results");results_->setColumnCount(5);results_->setHorizontalHeaderLabels({"✓","Address","Pattern · Network","Created","Key custody"});results_->setEditTriggers(QAbstractItemView::NoEditTriggers);results_->setSelectionBehavior(QAbstractItemView::SelectRows);results_->setAlternatingRowColors(true);results_->verticalHeader()->hide();results_->verticalHeader()->setDefaultSectionSize(42);results_->setShowGrid(false);results_->setItemDelegateForColumn(0,new VanityCheckDelegate(results_));results_->horizontalHeaderItem(0)->setToolTip("Select or clear this page");results_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);results_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);results_->setTextElideMode(Qt::ElideMiddle);layout->addWidget(results_,1);layout->addLayout(keyActions);
  connect(results_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){if(item->column()!=0)return;const auto id=item->data(Qt::UserRole).toString();if(item->checkState()==Qt::Checked)checked_.insert(id);else checked_.remove(id);updateKeyActions();});
  connect(results_->horizontalHeader(),&QHeaderView::sectionClicked,this,[this](int column){if(column!=0)return;bool all=true;for(int row=0;row<results_->rowCount();++row)all&=results_->item(row,0)->checkState()==Qt::Checked;for(int row=0;row<results_->rowCount();++row)results_->item(row,0)->setCheckState(all?Qt::Unchecked:Qt::Checked);});
  connect(newer_,&QPushButton::clicked,this,[this]{pageOffset_=std::max(0,pageOffset_-1000);loadResults();});connect(older_,&QPushButton::clicked,this,[this]{pageOffset_+=1000;loadResults();});
  auto* scope=new QLabel("Private keys are protected on this PC. Results remain available after restarting Horizon.");scope->setWordWrap(true);scope->setStyleSheet("color:#87ada9");layout->addWidget(scope);
  if(QFileInfo::exists(vaultRoot()+"/index/addresses.sqlite")){ensureStore();loadResults();}

  patternValidation_=new QTimer(this);patternValidation_->setSingleShot(true);patternValidation_->setInterval(140);connect(patternValidation_,&QTimer::timeout,this,[this]{validatePattern();});connect(prefix_,&QLineEdit::textChanged,this,[this]{patternInputChanged();});connect(suffix_,&QLineEdit::textChanged,this,[this]{patternInputChanged();});connect(patternModes_,&QButtonGroup::idClicked,this,[this](int){patternInputChanged();});patternInputChanged();
  connect(generate_,&QPushButton::clicked,this,[this]{begin();});connect(cancel_,&QPushButton::clicked,this,[this]{batch_.stop();cancelled_=true;state_->setText("Cancelling selected workers safely…");stopWorkers();if(workers_.isEmpty()&&!polling_&&!preparingToken_)finish();});
  auto* cancellationClock=new QTimer(this);cancellationClock->setInterval(1500);connect(cancellationClock,&QTimer::timeout,this,[this]{if(busy_&&(cancelled_||!winner_.isEmpty()))stopWorkers();});cancellationClock->start();
  QTimer::singleShot(0,this,[this,accepted=callbacksAlive_]{if(*accepted){inventory();updateKeyActions();}});auto* keyClock=new QTimer(this);keyClock->setInterval(1000);connect(keyClock,&QTimer::timeout,this,[this]{updateKeyActions();});keyClock->start();
 }
 ~VanityView()override{
  *callbacksAlive_=false;closing_=true;
  for(auto* timer:findChildren<QTimer*>())timer->stop();
  for(auto* child:findChildren<QObject*>())QObject::disconnect(child,nullptr,this,nullptr);
  for(const auto& worker:workers_)if(!worker->finished&&worker->host=="local"&&!directory_.isEmpty()){QFile cancel(directory_+"/cpu.cancel");if(cancel.open(QIODevice::WriteOnly))cancel.write("stop\n");}
  for(auto* process:findChildren<QProcess*>())if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(1500);}
 }
 void setGpuServices(const VanityGpuServices& services){gpuServices_=services;auto label=services.label;label.remove("NVIDIA GeForce ");gpu_->setText(label+" · GPU");gpu_->setToolTip(services.label);gpu_->setChecked(true);gpu_->setEnabled(!busy_);devicePicker_->refresh();}
 bool drainForClose(std::function<void()> done){
  if(!busy_)return false;closing_=true;resume_=false;drained_=std::move(done);batch_.stop();cancelled_=true;state_->setText("Closing · stopping the selected vanity workers…");stopWorkers();if(workers_.isEmpty()&&!polling_&&!preparingToken_)finish();return true;
 }
protected:
 void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);if(composer_){const bool compact=width()<800;composer_->setDirection(compact?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);composer_->setStretch(0,compact?0:3);composer_->setStretch(1,compact?0:2);}QTimer::singleShot(0,this,[this]{arrangeResults();});}
private:
 QString fixedPrefix()const{return testnet_?QString("tpgn"):QString("PGN");}
 QString patternPrefix()const{return fixedPrefix()+(patternModes_->checkedId()==1?QString{}:prefix_->text());}
 QString patternSuffix()const{return patternModes_->checkedId()==0?QString{}:suffix_->text();}
 void patternInputChanged(){
  ++patternRevision_;patternReady_=false;setProperty("pgn_vanity_pattern_ready",false);patternValidation_->stop();prefix_->setVisible(patternModes_->checkedId()!=1);suffix_->setVisible(patternModes_->checkedId()!=0);patternJoin_->setVisible(patternModes_->checkedId()!=0);updateKeyActions();
  const QString alphabet=QString("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");const auto editable=(patternModes_->checkedId()==1?QString{}:prefix_->text())+patternSuffix();
  for(const auto c:editable)if(!alphabet.contains(c)){patternFeedback_->setStyleSheet("color:#ef9b99");patternFeedback_->setText("Use Base58 letters and digits only: no 0, O, I, l, spaces or symbols.");setProperty("pgn_vanity_validation_pending",false);return;}
  patternFeedback_->setStyleSheet("color:#9eb0bb");patternFeedback_->setText("Checking pattern…");setProperty("pgn_vanity_validation_pending",true);patternValidation_->start();
 }
 void validatePattern(){
  const auto revision=patternRevision_;if(!load()){setProperty("pgn_vanity_validation_pending",false);patternFeedback_->setText("Generator unavailable; no search will start.");return;}
  const auto prefix=patternPrefix(),suffix=patternSuffix();local({"--plan",prefix,network(),suffix},[this,revision,prefix,suffix](const QJsonObject& reply){if(revision!=patternRevision_)return;patternReady_=reply.value("status")=="pattern_ready"&&reply.value("prefix")==prefix&&reply.value("suffix")==suffix&&reply.value("pattern_modes").toArray().contains("both");setProperty("pgn_vanity_pattern_ready",patternReady_);setProperty("pgn_vanity_validation_pending",false);patternFeedback_->setStyleSheet(patternReady_?"color:#91dfbb":"color:#ef9b99");patternFeedback_->setText(patternReady_?((prefix==fixedPrefix()&&suffix.isEmpty())?QString("Ready · leave matching text empty for a random address."):QString("Ready · %1 prefix characters · %2 ending characters").arg(prefix.size()-fixedPrefix().size()).arg(suffix.size())):reply.value("error").toString("This generator cannot search the requested pattern."));updateKeyActions();});
 }
 void busy(bool b){busy_=b;updateKeyActions();count_->setEnabled(!b&&countChoices_->checkedId()!=0);for(auto* button:countChoices_->buttons())button->setEnabled(!b);generate_->setEnabled(!b&&!keyOperation_);prefix_->setEnabled(!b);suffix_->setEnabled(!b);for(auto* mode:patternModes_->buttons())mode->setEnabled(!b);gpu_->setEnabled(!b&&bool(gpuServices_.pause));for(const auto& d:devices_)d.box->setEnabled(!b);cancel_->setEnabled(b);setProperty("pgn_vanity_busy",b);setProperty("pgn_vanity_resume_pending",resume_);}
 bool load(){
  try {
   profile_=loadVanityRuntime(root_,pgn::app::mutableDataRoot(root_),QCoreApplication::applicationDirPath());
   helper_=profile_.value("executable").toString();remote_=profile_.value("remote_executable").toString();
   if(!remote_.isEmpty()){
    const auto endpoint=gpuServices_.remoteEndpoint.isEmpty()?profile_.value("remote_endpoint").toObject():gpuServices_.remoteEndpoint;
    sshEndpoint_.executable=gpuServices_.sshProgram.isEmpty()?ssh():gpuServices_.sshProgram;
    sshEndpoint_.keyFile=endpoint.value("key_file").toString();sshEndpoint_.host=endpoint.value("host").toString();sshEndpoint_.user=endpoint.value("user").toString();
    const int port=endpoint.value("port").toInt();if(port<1||port>65535)throw std::runtime_error("Remote generator port is invalid.");
    sshEndpoint_.port=quint16(port);sshEndpoint_.hostKeyAlias=endpoint.value("host_key_alias").toString();
    (void)pgn::mining::makeSshConnectionCommand(sshEndpoint_,remote_+" --cpu-inventory");
   }
   return true;
  }catch(const std::exception& error){state_->setText(QString::fromUtf8(error.what()));return false;}
 }
 QProcess* execute(const QString& program,const QStringList& args,const QByteArray& input,Done done,int timeout=20000){
  auto* p=new QProcess(this);
#ifdef Q_OS_WIN
  p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
  auto timer=new QTimer(p);timer->setSingleShot(true);p->setProgram(program);p->setArguments(args);p->setProperty("pgn_done",false);
  connect(timer,&QTimer::timeout,p,[p]{p->setProperty("pgn_timeout",true);p->kill();});
  connect(p,&QProcess::started,p,[p,input]{if(!input.isEmpty())p->write(input+"\n");p->closeWriteChannel();});
  auto complete=[this,p,timer,done=std::move(done)]{if(p->property("pgn_done").toBool())return;p->setProperty("pgn_done",true);timer->stop();const auto bytes=p->readAllStandardOutput();auto o=bytes.size()<=32768?QJsonDocument::fromJson(bytes.trimmed()).object():QJsonObject{};if(o.isEmpty())o={{"status","error"},{"error",p->property("pgn_timeout").toBool()?"Device operation timed out.":"Device returned no valid bounded result."}};p->deleteLater();done(o);};
  connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[complete](int,QProcess::ExitStatus){complete();});connect(p,&QProcess::errorOccurred,this,[p,complete](auto){if(p->state()==QProcess::NotRunning)complete();});p->start();timer->start(timeout);return p;
 }
 void local(const QStringList& a,Done done,const QByteArray& input={}){execute(helper_,a,input,std::move(done));}
 QStringList sshArgs(const QString& command)const{return pgn::mining::makeSshConnectionCommand(sshEndpoint_,command).arguments;}
 QString ssh()const{return qEnvironmentVariable("SystemRoot","C:/Windows")+"/System32/OpenSSH/ssh.exe";}
 void remote(const QString& cmd,Done done){execute(sshEndpoint_.executable,sshArgs(cmd),{},std::move(done));}
 void inventory(){if(!load())return;local({"--cpu-inventory"},[this](const auto& o){addCpus("local",QSysInfo::machineHostName(),o);state_->setText("Choose the processors and the number of addresses to find.");recoverEarlierResults();});if(!remote_.isEmpty())remote(remote_+" --cpu-inventory",[this](const auto& o){addCpus("remote","Remote",o);});}
 void addCpus(const QString& host,const QString& label,const QJsonObject& o){for(const auto& value:o.value("packages").toArray()){const auto v=value.toObject();const auto mask=v.value("mask").toString();if(!QRegularExpression("^[0-9a-f]{1,16}$").match(mask).hasMatch())continue;auto* box=devicePicker_->addDevice("cpu-"+host+"-"+v.value("id").toString(),QString("%1 · CPU %2 · %3 threads").arg(label).arg(v.value("id").toString().toInt()+1).arg(v.value("logical").toInt()),VanityDevicePicker::Kind::Cpu,!busy_,false);box->setObjectName("pgn-vanity-cpu-"+host+"-"+v.value("id").toString());box->setEnabled(!busy_);if(std::none_of(devices_.begin(),devices_.end(),[box](const auto& existing){return existing.box==box;}))devices_.append({box,host,mask});}devicePicker_->refresh();setProperty("pgn_vanity_cpu_packages",devices_.size());}

 void arrangeResults(){if(!results_)return;const int available=std::max(0,results_->viewport()->width()-32);results_->setColumnWidth(0,32);results_->setColumnWidth(2,available*19/100);results_->setColumnWidth(3,available*17/100);results_->setColumnWidth(4,available*14/100);}
 using KeyDone=std::function<void(QJsonArray,QString)>;
 void updateKeyActions(){
  if(!export_||!results_)return;setProperty("pgn_vanity_key_operation",keyOperation_);const bool ready=runtime_&&runtime_->walletReady();devicePicker_->setEnabled(!busy_&&!keyOperation_);
  const auto binding=ready?runtime_->walletBinding():QString{};
  if(binding!=importedBinding_){importedBinding_=binding;imported_.clear();}
  export_->setText(QString("Export selected (%1)…").arg(checked_.size()));export_->setEnabled(!keyOperation_&&!checked_.isEmpty());import_->setEnabled(!keyOperation_&&!checked_.isEmpty()&&ready);
  importFile_->setEnabled(!keyOperation_&&ready);importKey_->setEnabled(!keyOperation_&&ready);generate_->setEnabled(!busy_&&!keyOperation_&&patternReady_&&devicePicker_->selectedCount()>0);
 }
 QJsonArray selectedRecords(){
  QJsonArray selected;if(!store_)return selected;const auto count=store_->count();if(count<0||count>10000){state_->setText("Address index count is unavailable or exceeds its safety bound.");return selected;}
  for(int offset=0;offset<count;offset+=1000){const auto rows=store_->page(offset);if(!store_->pageError().isEmpty()){state_->setText(store_->pageError());return {};}for(const auto& value:rows)if(checked_.contains(value.toObject().value("session_id").toString()))selected.append(value);}
  return selected;
 }
 void collectKeys(QJsonArray records,int index,QJsonArray packets,KeyDone done){
  if(index>=records.size()){done(std::move(packets),{});return;}
  const auto record=records[index].toObject();const auto id=record.value("session_id").toString();
  if(QUuid(id).isNull()||QUuid(id).toString(QUuid::WithoutBraces)!=id){done({},"Stored key session identity is invalid.");return;}
  state_->setText(QString("Reading selected protected keys · %1 / %2").arg(index+1).arg(records.size()));
  local({"--export-key",vaultRoot()+"/jobs/"+id},[this,records,index,record,packets=std::move(packets),done=std::move(done)](const QJsonObject& response)mutable{
   if(response.value("schema")!="PGN_PRIVATE_KEY_V1"||response.value("address")!=record.value("address")||response.value("network").toString()!=record.value("network").toString("main")){done({},response.value("error").toString("Protected key does not match its saved address."));return;}
   auto packet=response;packet.insert("session_id",record.value("session_id"));packet.insert("created",record.value("created"));packet.insert("prefix",record.value("prefix"));packet.insert("suffix",record.value("suffix").toString());packets.append(packet);collectKeys(records,index+1,std::move(packets),std::move(done));
  });
 }
 void endKeyOperation(const QString& message){keyOperation_=false;state_->setText(message);updateKeyActions();}
 void exportSelected(){
  if(keyOperation_||!load())return;const auto records=selectedRecords();if(records.isEmpty())return;
  const auto path=QFileDialog::getSaveFileName(this,"Export private keys",QDir::homePath()+"/PGN-private-keys.pgnkeys.json","PGN private key file (*.pgnkeys.json)");if(path.isEmpty())return;
  keyOperation_=true;updateKeyActions();collectKeys(records,0,{},[this,path](QJsonArray packets,QString error){
   if(!error.isEmpty()){endKeyOperation(error);return;}
   QJsonObject file{{"schema","PGN_PRIVATE_KEYS_FILE_V1"},{"network","PGN"},{"genesis",runtime_&&runtime_->walletReady()&&!packets.isEmpty()&&packets.first().toObject().value("network")==network()?runtime_->networkGenesis():QString{}},{"created",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"keys",packets}};
   const auto bytes=QJsonDocument(file).toJson(QJsonDocument::Indented);if(bytes.size()>8*1024*1024){endKeyOperation("Private key export exceeds8MiB; export a smaller selection.");return;}
   QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||!output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)||output.write(bytes)!=bytes.size()||!output.commit()){endKeyOperation("Private key file could not be saved. Protected originals are retained.");return;}
   endKeyOperation(QString("%1 private keys exported. This file can spend their funds; keep it protected.").arg(packets.size()));
  });
 }
 void importSelected(){
  if(keyOperation_||!runtime_||!runtime_->walletReady()||!load())return;const auto records=selectedRecords();if(records.isEmpty())return;
  for(const auto& record:records)if(record.toObject().value("network").toString("main")!=network()){state_->setText("Select addresses from the same network as the open wallet.");return;}
  const auto binding=runtime_->walletBinding();keyOperation_=true;updateKeyActions();collectKeys(records,0,{},[this,binding](QJsonArray packets,QString error){if(!error.isEmpty()){endKeyOperation(error);return;}importPackets(std::move(packets),0,0,binding,false);});
 }
 void importPackets(QJsonArray packets,int offset,int succeeded,const QString& binding,bool history,QSet<QString> succeededAddresses={}){
  if(!runtime_||!runtime_->walletReady()||runtime_->walletBinding()!=binding){endKeyOperation("Wallet changed. Review the selected wallet before retrying the import.");return;}
  if(offset>=packets.size()){loadResults();endKeyOperation((succeeded==1?QString("1 address imported into the wallet. Make a new wallet backup."):QString("%1 addresses imported into the wallet. Make a new wallet backup.").arg(succeeded)));return;}
  const int count=std::min(1000,int(packets.size())-offset);QJsonArray descriptors;for(int i=0;i<count;++i){const auto packet=packets[offset+i].toObject();if(packet.value("network").toString()!=network()){endKeyOperation("Private keys are presented for another PGN network; no further imports sent.");return;}descriptors.append(packet.value("descriptor"));}
  state_->setText(QString("Importing into the wallet · %1 / %2").arg(offset).arg(packets.size()));
  const QPointer<VanityView> alive(this);const auto accepted=callbacksAlive_;runtime_->importPrivateDescriptors(descriptors,history,[this,alive,accepted,packets,offset,count,succeeded,binding,history,succeededAddresses=std::move(succeededAddresses)](QJsonValue value,QString error)mutable{
   if(!*accepted||!alive)return;
   if(!error.isEmpty()){loadResults();endKeyOperation(error);return;}
   const auto results=value.toArray();QString firstFailure;
   for(int i=0;i<count;++i){const auto result=results[i].toObject();if(result.value("success").toBool()){const auto address=packets[offset+i].toObject().value("address").toString();if(rememberVanityImportedAddress(succeededAddresses,address))++succeeded;const auto id=packets[offset+i].toObject().value("session_id").toString();if(!id.isEmpty())imported_.insert(id);}else if(firstFailure.isEmpty())firstFailure=result.value("error").toObject().value("message").toString("Core refused an imported address.");}
   if(!firstFailure.isEmpty()){loadResults();endKeyOperation(QString("%1 imported; remaining keys retained. %2").arg(succeeded).arg(firstFailure));return;}
   importPackets(packets,offset+count,succeeded,binding,history,std::move(succeededAddresses));
  });
 }
 void inspectImportedKeys(QJsonArray keys,int index,QJsonArray packets,const QString& binding){
  if(index>=keys.size()){importPackets(std::move(packets),0,0,binding,true);return;}
  const auto key=keys[index].toObject().value("private_key").toString();const auto expectedAddress=keys[index].toObject().value("address").toString();const auto sourceNetwork=keys[index].toObject().value("network").toString(expectedAddress.startsWith("PGN")?QString("main"):network());
  if(key.size()!=67){endKeyOperation("A private key in this file has an invalid format.");return;}
  state_->setText(QString("Checking private keys · %1 / %2").arg(index+1).arg(keys.size()));
  local({"--inspect-private-key"},[this,keys,index,packets=std::move(packets),binding](const QJsonObject& response)mutable{
   if(response.value("schema")!="PGN_PRIVATE_KEY_V1"){endKeyOperation(response.value("error").toString("Private key verification failed."));return;}
   const auto expected=keys[index].toObject().value("address").toString();if(!expected.isEmpty()&&expected!=response.value("address").toString()){endKeyOperation("An address in this file does not match its key in the selected PGN network.");return;}
   auto packet=response;if(store_){const auto id=store_->sessionFor(response.value("address").toString(),response.value("network").toString());if(!id.isEmpty())packet.insert("session_id",id);}packets.append(packet);inspectImportedKeys(keys,index+1,std::move(packets),binding);
  },QJsonDocument(QJsonObject{{"private_key",key},{"network",sourceNetwork}}).toJson(QJsonDocument::Compact));
 }
 void importFile(){
  if(keyOperation_||!runtime_||!runtime_->walletReady()||!load())return;
  const auto path=QFileDialog::getOpenFileName(this,"Import PGN private keys",QDir::homePath(),"PGN private key file (*.pgnkeys.json *.json)");if(path.isEmpty())return;
  QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024){state_->setText("Private key file is unavailable or larger than8MiB.");return;}
  const auto data=QJsonDocument::fromJson(file.readAll()).object();const auto keys=data.value("keys").toArray();const auto genesis=data.value("genesis").toString();
  if(data.value("schema")!="PGN_PRIVATE_KEYS_FILE_V1"||data.value("network")!="PGN"||keys.isEmpty()||keys.size()>10000){state_->setText("Unsupported or empty PGN private key file.");return;}
  if(!genesis.isEmpty()&&genesis!=runtime_->networkGenesis()){state_->setText("This file belongs to another PGN network; no key was imported.");return;}
  const auto binding=runtime_->walletBinding();keyOperation_=true;updateKeyActions();inspectImportedKeys(keys,0,{},binding);
 }
 void importPrivateKey(){
  if(keyOperation_||!runtime_||!runtime_->walletReady()||!load())return;bool okay=false;
  auto secret=QInputDialog::getText(this,"Import PGN private key","Native PGN private key",QLineEdit::Password,{},&okay);if(!okay||secret.isEmpty())return;
  const auto binding=runtime_->walletBinding();keyOperation_=true;updateKeyActions();inspectImportedKeys({QJsonObject{{"private_key",secret}}},0,{},binding);secret.fill(QChar(0));
 }
 void recoverEarlierResults(){
  const QDir earlier(pgn::app::mutableDataRoot(root_)+"/runtime/vanity/jobs");if(!earlier.exists())return;
  QStringList sessions;QDirIterator entries(earlier.absolutePath(),QDir::Dirs|QDir::NoDotAndDotDot);int examined=0;while(entries.hasNext()&&examined++<10000&&sessions.size()<1000){entries.next();const auto name=entries.fileName();if(!QUuid(name).isNull()&&QFileInfo::exists(entries.filePath()+"/key.dpapi"))sessions.append(name);}sessions.sort();
  if(sessions.isEmpty()||!ensureStore())return;
  keyOperation_=true;updateKeyActions();recoverEarlierSession(sessions,0);
 }
 void recoverEarlierSession(QStringList sessions,int index){
  if(index>=sessions.size()){loadResults();endKeyOperation("Earlier protected addresses are available.");return;}
  const auto source=pgn::app::mutableDataRoot(root_)+"/runtime/vanity/jobs/"+sessions[index];
  local({"--inspect-session",source},[this,sessions,index,source](const QJsonObject& response){
   if(response.value("schema")!="PGN_PROTECTED_SESSION_V1"){setProperty("pgn_vanity_recovery_error",response.value("error"));endKeyOperation("Earlier protected session could not be checked. Original files retained.");return;}
   const auto destination=vaultRoot()+"/jobs/"+sessions[index];if(!QDir().mkpath(destination)){endKeyOperation("Recovery directory unavailable. Original keys retained.");return;}
   qint64 bytes=0;
   for(const auto* name:{"seed.dpapi","key.dpapi","public.json","address.json"}){
    QFile input(source+"/"+name);if(!input.open(QIODevice::ReadOnly)||input.size()>16384){endKeyOperation("Protected recovery input unavailable or oversized.");return;}const auto content=input.readAll();bytes+=content.size();
    QFile existing(destination+"/"+name);if(existing.exists()){if(!existing.open(QIODevice::ReadOnly)||existing.size()>16384||existing.readAll()!=content){endKeyOperation("Recovery would overwrite a different session; both originals retained.");return;}continue;}
    QSaveFile output(destination+"/"+name);if(!output.open(QIODevice::WriteOnly)||!output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)||output.write(content)!=content.size()||!output.commit()){endKeyOperation("Protected recovery copy failed; originals retained.");return;}
   }
   const auto error=store_->append(response,bytes);if(!error.isEmpty()){endKeyOperation(error);return;}
   recoverEarlierSession(sessions,index+1);
  });
 }
 QString network()const{return testnet_?QString("test"):QString("main");}
 QString vaultRoot()const{return vaultDirectory_.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/vanity":vaultDirectory_;}
 bool ensureStore(){if(!store_)store_=std::make_unique<pgn::wallet::VanityResultStore>(vaultRoot()+"/index");if(!store_->error().isEmpty()){state_->setText(store_->error());return false;}return true;}
 void loadResults(){
  updateKeyActions();if(!store_)return;const auto rows=store_->page(pageOffset_);if(!store_->pageError().isEmpty()){state_->setText(store_->pageError());return;}
  const QSignalBlocker block(results_);results_->setRowCount(rows.size());
  for(int row=0;row<rows.size();++row){const auto record=rows[row].toObject();auto* check=new QTableWidgetItem;check->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);check->setData(Qt::UserRole,record.value("session_id").toString());check->setData(Qt::UserRole+1,record);check->setCheckState(checked_.contains(record.value("session_id").toString())?Qt::Checked:Qt::Unchecked);results_->setItem(row,0,check);
   const QStringList values{record.value("address").toString(),record.value("prefix").toString()+(record.value("suffix").toString().isEmpty()?QString{}:QString("…")+record.value("suffix").toString())+"\n"+(record.value("network").toString("main")=="test"?QString("Testnet"):QString("Mainnet")),QDateTime::fromString(record.value("created").toString(),Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm"),imported_.contains(record.value("session_id").toString())?QString("Imported into wallet"):QString("Protected locally")};for(int column=0;column<values.size();++column){auto* cell=new QTableWidgetItem(values[column]);cell->setTextAlignment(Qt::AlignCenter);cell->setToolTip(values[column]);if(column==0)cell->setToolTip(record.value("network").toString("main")=="test"?"PGN testnet":"PGN mainnet");results_->setItem(row,column+1,cell);}}
  newer_->setEnabled(pageOffset_>0);older_->setEnabled(store_->count()>pageOffset_+rows.size());setProperty("pgn_vanity_saved_results",store_->count());arrangeResults();updateKeyActions();
 }
 void begin(){
  if(busy_||keyOperation_||!patternReady_||!load())return;bool selected=gpu_->isChecked();for(const auto& device:devices_)selected|=device.box->isChecked();if(!selected){state_->setText("Select at least one CPU or GPU.");return;}
  if(!ensureStore()||!batch_.start(countChoices_->checkedId()==0?0:count_->value()))return;
  batchCount_->setText(batch_.target()?QString("0 / %1 found").arg(batch_.target()):QString("0 found"));
  cancelled_=false;resume_=false;devicePrepared_=false;busy(true);prepareItem();
 }
 void prepareItem(){
  item_=batch_.beginItem();if(!item_){restoreMining();return;}const auto token=item_;preparingToken_=token;
  finalizing_=false;workers_.clear();winner_={};directory_.clear();address_->clear();state_->setText("Preparing a protected local key session…");
  local({"--prepare",vaultRoot()+"/jobs",patternPrefix(),network(),patternSuffix()},[this,token](const auto& response){
   if(preparingToken_==token)preparingToken_=0;if(token!=item_)return;
   if(response.value("status")!="prepared"){batch_.fail(token);state_->setText(response.value("error").toString());restoreMining();return;}
   directory_=response.value("directory").toString();job_=response.value("job").toObject();if(cancelled_){finish();return;}
   if(!response.value("local_match").toObject().isEmpty()){winner_=response.value("local_match").toObject();finish();return;}
   if(gpu_->isChecked()&&profile_.value(gpuServices_.host=="local"?"local_gpu_module":"remote_gpu_module").toString().isEmpty()){batch_.fail(token);state_->setText("This GPU has no qualified vanity runtime configured.");restoreMining();return;}
   if(gpu_->isChecked()&&!devicePrepared_)checkMining();else launchWorkers();
  });
 }
 bool saveResult(const QJsonObject& response){
  const auto id=QFileInfo(directory_).fileName();if(response.value("network").toString()!=network()||response.value("session_id").toString()!=id||!QFileInfo(directory_+"/key.dpapi").isFile()||QFileInfo(directory_+"/key.dpapi").size()>8192){state_->setText("Protected key receipt is missing or inconsistent; generation paused.");return false;}QJsonObject record{{"network",network()},{"session_id",id},{"address",response.value("address")},{"prefix",job_.value("prefix")},{"suffix",job_.value("suffix").toString()},{"created",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  qint64 bytes=0;for(const auto* file:{"seed.dpapi","key.dpapi","public.json","address.json","cpu.cancel"})bytes+=QFileInfo(directory_+"/"+file).size();
  const auto error=store_->append(record,bytes);if(!error.isEmpty()){state_->setText(error);return false;}checked_.insert(id);pageOffset_=0;loadResults();return true;
 }
 void endBatch(){batch_.finish();const auto count=QString::number(batch_.found());batchCount_->setText(count+(batch_.target()?QString(" / %1 found").arg(batch_.target()):QString(" found")));busy(false);if(closing_&&drained_){auto done=std::move(drained_);QTimer::singleShot(0,this,std::move(done));}}
 void checkMining(){
  if(!gpuServices_.state||!gpuServices_.pause||!gpuServices_.release){batch_.fail(item_);state_->setText("GPU ownership controller is unavailable. No GPU worker started.");endBatch();return;}
  const auto state=gpuServices_.state();if(state!="on"&&state!="off"){batch_.fail(item_);state_->setText("GPU control is settling. Retry after its current transition.");endBatch();return;}
  const bool wasRunning=state=="on";
  if(wasRunning&&QMessageBox::question(this,"GPU is mining","Pause this GPU's mining while generating the selected addresses, then resume its saved ticket?",QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes){batch_.fail(item_);state_->setText("Generation cancelled; mining continues.");endBatch();return;}
  gpuLease_=QUuid::createUuid().toString(QUuid::WithoutBraces);resume_=wasRunning;polling_=true;persist("pause_requested");const QPointer<VanityView> alive(this);
  gpuServices_.pause(gpuLease_,[this,alive,lease=gpuLease_,token=item_,accepted=callbacksAlive_](QString error){
   if(!*accepted||!alive||gpuLease_!=lease||item_!=token||!polling_)return;polling_=false;
   if(!error.isEmpty()){resume_=false;gpuLease_.clear();batch_.fail(item_);state_->setText(error);endBatch();return;}
   devicePrepared_=true;persist("paused_checkpoint_retained");if(cancelled_)finish();else launchWorkers();
  });
 }
 void persist(const QString& state){QSaveFile f(vaultRoot()+"/broker.json");QDir().mkpath(vaultRoot());if(f.open(QIODevice::WriteOnly)){QJsonArray ids;for(const auto& w:workers_)if(w->host=="remote")ids.append(w->id);f.write(QJsonDocument(QJsonObject{{"schema","pgn-vanity-broker-2"},{"state",state},{"resume_mining",resume_},{"lease_token",gpuLease_},{"directory",directory_},{"ui_pid",QString::number(QCoreApplication::applicationPid())},{"remote_workers",ids}}).toJson(QJsonDocument::Compact));f.commit();}}
 void launchWorkers(){devicePrepared_=true;if(cancelled_){finish();return;}QStringList localMasks,remoteMasks;for(const auto& d:devices_)if(d.box->isChecked())(d.host=="local"?localMasks:remoteMasks).append(d.mask);
  if(!localMasks.isEmpty())launch("local",localMasks.join(','),0,false);
  if(!remoteMasks.isEmpty())launch("remote",remoteMasks.join(','),quint64(1)<<56,false);
  if(gpu_->isChecked())launch(gpuServices_.host.isEmpty()?QString("remote"):gpuServices_.host,{},quint64(2)<<56,true);
  persist("searching");state_->setText("Initializing the selected devices · keys stay local.");if(workers_.isEmpty())finish();
 }
 void launch(const QString& host,const QString& masks,quint64 base,bool gpu){
  auto w=std::make_shared<Worker>();w->host=host;w->id=QUuid::createUuid().toString(QUuid::WithoutBraces);auto job=job_;job["id"]=w->id;job["step_base"]=QString::number(base);job["remote_worker"]=host=="remote";job["cancel_path"]=directory_+"/cpu.cancel";quint64 selectedMask{};for(const auto& mask:masks.split(','))selectedMask|=mask.toULongLong(nullptr,16);job["cpu_thread_limit"]=std::max(1,std::min(2,int(std::popcount(selectedMask))));if(gpu)job["gpu_lease_file"]=profile_.value(host=="local"?"local_gpu_lease_file":"remote_gpu_lease_file").toString();auto* p=new QProcess(this);
#ifdef Q_OS_WIN
  p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
  w->process=p;workers_.append(w);
  const auto args=gpu?QStringList{"--worker",profile_.value(host=="local"?"local_gpu_module":"remote_gpu_module").toString(),gpuServices_.device,"0"}:QStringList{"--cpu-worker",masks,"0"};
  p->setProgram(host=="local"?helper_:sshEndpoint_.executable);p->setArguments(host=="local"?args:sshArgs(remote_+" "+args.join(' ')));
  connect(p,&QProcess::started,p,[p,job]{p->write(QJsonDocument(job).toJson(QJsonDocument::Compact)+"\n");p->closeWriteChannel();});
  connect(p,&QProcess::readyReadStandardOutput,this,[this,w,token=item_]{if(token!=item_||w->finished)return;w->pending+=w->process->readAllStandardOutput();if(w->pending.size()>32768){cancelled_=true;state_->setText("Worker exceeded its response bound; cancelling safely.");stopWorkers();return;}while(w->pending.contains('\n')){const auto n=w->pending.indexOf('\n');const auto o=QJsonDocument::fromJson(w->pending.left(n)).object();w->pending.remove(0,n+1);if(o.value("status")=="initializing"&&winner_.isEmpty()&&!cancelled_){state_->setText("Initializing selected processor…");}else if(o.value("status")=="match"&&winner_.isEmpty()&&!cancelled_){winner_=o;state_->setText("Match found · stopping the remaining workers before exact verification…");stopWorkers();}else if(o.value("status")=="searching"&&winner_.isEmpty()&&!cancelled_){const auto n=o.value("hashed").toString().toULongLong();const auto ms=qint64(o.value("elapsed_ms").toDouble());if(ms>w->elapsed)w->rate=double(n-w->hashed)*1000./double(ms-w->elapsed);w->hashed=n;w->elapsed=ms;quint64 total{};double rate{};for(const auto& x:workers_){total+=x->hashed;if(!x->finished)rate+=x->rate;}state_->setText(QString("%1 Mkey/s · %2 candidates · %3 s · searching until found or cancelled").arg(rate/1e6,0,'f',2).arg(total).arg(ms/1000.,0,'f',1));setProperty("pgn_vanity_rate_keys_s",rate);setProperty("pgn_vanity_candidates",QString::number(total));}else if(o.value("status")=="error")state_->setText(o.value("error").toString());}});
  auto completed=[this,w]{if(w->finished)return;w->finished=true;w->process->deleteLater();for(const auto& other:workers_)if(!other->finished)return;finish();};
  connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[completed](int,QProcess::ExitStatus){completed();});connect(p,&QProcess::errorOccurred,this,[p,completed](auto){if(p->state()==QProcess::NotRunning)completed();});p->start();
 }
 void stopWorkers(){if(directory_.isEmpty())return;QFile f(directory_+"/cpu.cancel");if(f.open(QIODevice::WriteOnly))f.write("stop\n");for(const auto& w:workers_)if(w->host=="remote"&&!w->finished&&!cancelling_.contains(w->id)){cancelling_.insert(w->id);remote(remote_+" --cancel-worker "+w->id,[this,id=w->id](const auto&){cancelling_.remove(id);});}}
 void finish(){if(finalizing_)return;for(const auto& w:workers_)if(!w->finished)return;finalizing_=true;checkRemoteFinished(0);}
 void checkRemoteFinished(int index){if(index>=workers_.size()){completeResult();return;}const auto w=workers_[index];if(w->host!="remote"){checkRemoteFinished(index+1);return;}remote(remote_+" --cancel-worker "+w->id,[this,w,index](const auto&){remote(remote_+" --worker-state "+w->id,[this,index](const auto& o){if(o.value("status")=="finished"||o.value("status")=="absent"){checkRemoteFinished(index+1);return;}state_->setText("Waiting for selected worker shutdown before mining resumes…");QTimer::singleShot(1200,this,[this,index]{checkRemoteFinished(index);});});});}
 void completeResult(){
  const auto token=item_;
  if(!winner_.isEmpty()&&!cancelled_){local({"--complete",directory_},[this,token](const auto& response){
   if(token!=item_)return;
   if(response.value("status")=="generated"){
    address_->setText(response.value("address").toString());setProperty("pgn_vanity_generated",true);
    const auto completion=batch_.accept(token);if(completion==VanityBatch::Completion::Ignored)return;
    const bool saved=saveResult(response);batchCount_->setText(QString::number(batch_.found())+(batch_.target()?QString(" / %1 found").arg(batch_.target()):QString(" found")));
    if(!saved)batch_.stop();else state_->setText("Address verified and protected locally.");
    if(saved&&completion==VanityBatch::Completion::Next){QTimer::singleShot(0,this,[this,accepted=callbacksAlive_]{if(*accepted)prepareItem();});return;}
   }else{batch_.fail(token);state_->setText(response.value("error").toString());}
   restoreMining();
  },QJsonDocument(winner_).toJson(QJsonDocument::Compact));}
  else{batch_.fail(token);state_->setText(cancelled_?"Search cancelled. Completed addresses are retained.":"Workers stopped before a verified match. Protected sessions retained.");restoreMining();}
 }
 void restoreMining(){
  if(gpuLease_.isEmpty()){persist("finished");endBatch();return;}
  persist("resume_pending");state_->setText(state_->text()+" Releasing the GPU…");const auto lease=gpuLease_;const QPointer<VanityView> alive(this);
  if(!gpuServices_.release){state_->setText("Vanity workers stopped. GPU ownership controller unavailable; mining remains paused.");gpuLease_.clear();endBatch();return;}
  gpuServices_.release(lease,resume_&&!closing_,[this,alive,lease,accepted=callbacksAlive_](QString error){
   if(!*accepted||!alive||gpuLease_!=lease)return;gpuLease_.clear();resume_=false;persist(error.isEmpty()?"finished_gpu_released":"released_resume_attention");
   state_->setText(state_->text()+(error.isEmpty()?QString(" GPU released."):QString(" ")+error));endBatch();
  });
 }

};
}
QWidget* createVanityView(const QString& root,pgn::wallet::CoreRuntime* runtime,const QString& vaultDirectory,bool testnet){return new VanityView(root,runtime,vaultDirectory,testnet);}
void bindVanityGpu(QWidget* wallet,const VanityGpuServices& services){if(wallet)if(auto* view=dynamic_cast<VanityView*>(wallet->findChild<QWidget*>("pgn-vanity-generator")))view->setGpuServices(services);}
bool drainVanityForClose(QWidget* wallet,std::function<void()> done){if(wallet)if(auto* view=dynamic_cast<VanityView*>(wallet->findChild<QWidget*>("pgn-vanity-generator")))return view->drainForClose(std::move(done));return false;}

}
