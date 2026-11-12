#pragma once
#include "ui/gap_flow_model.h"
#include "ui/mining_cycle_progress.h"
#include "ui/resource_flow_activity.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QMouseEvent>
#include <QComboBox>
#include <functional>
namespace pgn::ui {
class GapFlowOverlay;
class MiningTicketProgress final:public QWidget {
public:
 explicit MiningTicketProgress(QWidget* parent=nullptr);
 ~MiningTicketProgress()override;
 void present(const QJsonObject&);
 QString workSource()const{return workSource_;}
 bool selectWorkSource(const QString&);
 std::function<void(const QJsonObject&)> onWorkSelected;
 void presentResourceActivity(const QString&,const QJsonObject&,bool connected);
 void bindSourceCanvas(QWidget*);
 void bindResultsView(QWidget*);
 using TicketLookup=std::function<void(const QString&,std::function<void(QJsonObject,QString)>)>;
 void bindTicketLookup(TicketLookup);
 void setRecordEligibility(const QJsonArray&);
protected:
 void paintEvent(QPaintEvent*)override;
 void mouseMoveEvent(QMouseEvent*)override;
 void mousePressEvent(QMouseEvent*)override;
 void leaveEvent(QEvent*)override;
private:
 friend class GapFlowOverlay;
 QJsonObject frame_;GapFlowModel flow_;QTimer timer_;QElapsedTimer clock_;
 struct WorkView {QJsonObject frame;GapFlowModel flow;MiningCycleHistory cycle;};
 QMap<QString,WorkView> workViews_;QString workSource_{"GPU"};QComboBox* workSelector_{};
 void applyFrame(const QJsonObject&);
 ResourceFlowActivity resourceActivity_;
 MiningCycleHistory cycleHistory_;
 QPointer<QWidget> canvas_,results_;QPointer<GapFlowOverlay> overlay_;
 struct Hit {QString pair;QPointF point;};
 QVector<Hit> hits_;QString hovered_,pulsed_;QPointF pointer_;qint64 pulsedAt_{};
 struct Landing {QString ticket,request;bool checking{},confirmed{};qint64 nextCheck{};QString block;
     bool complete{},audited{};qint64 born{},approvedAt{-1};quint64 query{};};
 QVector<Landing> landings_;TicketLookup lookup_;
 quint64 lookupGeneration_{},activeLookup_{};qint64 nextLookupAt_{};bool lookupBusy_{};
 bool workMarksActive(qint64)const;
 void pollLandings();
 QRectF bar()const;
 void ensureOverlay();
};
}
