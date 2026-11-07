#pragma once
#include <QCheckBox>
#include <QMenu>
#include <QScrollArea>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QStringList>
#include <algorithm>
#include <functional>
#include <vector>

namespace pgn::ui {
// A bounded popup keeps the work surface stable as hardware inventory grows.
// It selects admitted devices only; it does not start or qualify an executor.
class VanityDevicePicker final : public QToolButton {
public:
 enum class Kind { Cpu, Gpu };
 explicit VanityDevicePicker(QWidget* parent=nullptr):QToolButton(parent){
  setObjectName("pgn-vanity-devices");setAccessibleName("Search devices");setPopupMode(QToolButton::InstantPopup);
  setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
  setStyleSheet("QToolButton{background:#122631;color:#dfebed;border:1px solid #456571;border-radius:6px;padding:6px;text-align:left;}QToolButton:hover{border-color:#8ed9c0;}QToolButton:disabled{color:#647984;border-color:#293d47;}QToolButton::menu-indicator{image:none;}");
  popup_=new QMenu(this);popup_->setStyleSheet("QMenu{background:#101c26;color:#dfebed;border:1px solid #456571;}");
  viewport_=new QScrollArea;viewport_->setWidgetResizable(true);viewport_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);viewport_->setFrameShape(QFrame::NoFrame);
  auto* content=new QWidget;content->setStyleSheet("QWidget{background:#101c26;color:#dfebed;}QCheckBox{padding:5px;background:transparent;}QCheckBox::indicator{width:14px;height:14px;border:1px solid #65828d;border-radius:3px;background:#07111b;}QCheckBox::indicator:checked{background:#70caa4;border-color:#b2f1d4;}QCheckBox:disabled{color:#627782;}");
  rows_=new QVBoxLayout(content);rows_->setContentsMargins(8,6,8,6);rows_->setSpacing(2);rows_->setAlignment(Qt::AlignTop);viewport_->setWidget(content);
  auto* action=new QWidgetAction(popup_);action->setDefaultWidget(viewport_);popup_->addAction(action);setMenu(popup_);
  connect(popup_,&QMenu::aboutToShow,this,[this]{
   const int available=window()?window()->width()-48:420;viewport_->setFixedWidth(std::clamp(available,260,480));
   const int screenHeight=screen()?screen()->availableGeometry().height():600;
   viewport_->setFixedHeight(std::clamp(rows_->sizeHint().height()+4,54,std::max(54,std::min(320,screenHeight-80))));
  });
  refresh();
 }
 QCheckBox* addDevice(const QString& id,const QString& label,Kind kind,bool enabled=true,bool selected=false){
  for(auto& item:items_)if(item.id==id){item.box->setText(label);item.box->setEnabled(enabled);refresh();return item.box;}
  auto* box=new QCheckBox(label);box->setEnabled(enabled);box->setChecked(selected);rows_->addWidget(box);items_.push_back({id,kind,box});
  connect(box,&QCheckBox::toggled,this,[this]{refresh();if(selectionChanged)selectionChanged();});refresh();return box;
 }
 int deviceCount()const{return int(items_.size());}
 int selectedCount()const{int count=0;for(const auto& item:items_)count+=item.box->isChecked();return count;}
 int popupViewportHeight()const{return viewport_->height();}
 std::function<void()> selectionChanged;
 void refresh(){
  int cpus=0,gpus=0;QStringList names;for(const auto& item:items_)if(item.box->isChecked()){(item.kind==Kind::Gpu?gpus:cpus)++;names.append(item.box->text());}
  QStringList counts;if(gpus)counts<<QString::number(gpus)+(gpus==1?" GPU":" GPUs");if(cpus)counts<<QString::number(cpus)+(cpus==1?" CPU":" CPUs");
  setText("Search devices · "+(counts.isEmpty()?QString("Select…"):counts.join(" + "))+" ▾");setToolTip(names.isEmpty()?QString("Choose one or more devices for address search."):names.join("\n"));
 }
private:
 struct Item{QString id;Kind kind;QCheckBox* box;};
 std::vector<Item> items_;QMenu* popup_{};QScrollArea* viewport_{};QVBoxLayout* rows_{};
};
}
