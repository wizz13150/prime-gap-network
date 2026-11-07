#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QIcon>
#include <QPainter>
#include <QTableWidgetItem>
#include <QTreeWidgetItem>
#include <QLabel>

namespace pgn::ui {
struct AuditStatusPresentation {QString text,tooltip;QColor color;};

inline AuditStatusPresentation auditStatusPresentation(const QString& state){
    if(state.isEmpty()||state=="awaiting")return {"Awaiting auditor","This audit remains pending until an eligible independent auditor is assigned.",QColor("#ffbd69")};
    if(state=="assigned")return {"Assigned","An auditor holds this assignment. Assignment alone does not prove that computation is running.",QColor("#62cfff")};
    if(state=="committed")return {"Answer committed","The auditor committed its answer; reveal and comparison are still required.",QColor("#b58cff")};
    if(state=="passed")return {"Counts match","The revealed sampling counts match. Exact gap proof and monetary acceptance remain separate checks.",QColor("#69efc5")};
    if(state=="failed")return {"Mismatch","The revealed sampling counts disagree. This report must not be presented as validated.",QColor("#ff718b")};
    return {"Unknown state","Unrecognized audit state: "+state,QColor("#99aeb8")};
}

inline AuditStatusPresentation auditReportPresentation(const QJsonObject& report){
    bool assigned=false,committed=false,unknown=false;
    for(const auto& entry:report.value("jobs").toArray()){
        const auto state=entry.toObject().value("state").toString();
        if(state=="failed")return auditStatusPresentation("failed");
        assigned|=state=="assigned";committed|=state=="committed";
        unknown|=state!="assigned"&&state!="committed"&&state!="passed";
    }
    if(unknown)return auditStatusPresentation("unknown");
    if(report.value("audited")==true){auto result=auditStatusPresentation("passed");result.text="Both audits passed";return result;}
    if(committed)return auditStatusPresentation("committed");
    if(assigned)return auditStatusPresentation("assigned");
    auto result=auditStatusPresentation("awaiting");result.text="Awaiting independent audits";return result;
}

inline QIcon auditStatusIcon(const QColor& color){
    QPixmap image(12,12);image.fill(Qt::transparent);QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawEllipse(QRectF(2,2,8,8));return QIcon(image);
}
inline void presentAuditStatus(QTableWidgetItem* item,const AuditStatusPresentation& status){
    item->setText(status.text);item->setForeground(status.color);item->setIcon(auditStatusIcon(status.color));item->setToolTip(status.tooltip);
}
inline void presentAuditStatus(QTreeWidgetItem* item,const AuditStatusPresentation& status){
    item->setText(1,status.text);item->setForeground(1,status.color);item->setIcon(1,auditStatusIcon(status.color));item->setToolTip(1,status.tooltip);
}
inline QLabel* auditStatusLegend(QWidget* parent){
    auto* label=new QLabel(parent);label->setObjectName("pgn-audit-status-legend");label->setWordWrap(true);
    label->setText("<span style='color:#ffbd69'>● Awaiting</span> &nbsp; "
                   "<span style='color:#62cfff'>● Assigned</span> &nbsp; "
                   "<span style='color:#b58cff'>● Answer committed</span> &nbsp; "
                   "<span style='color:#69efc5'>● Passed</span> &nbsp; "
                   "<span style='color:#ff718b'>● Mismatch</span>");
    label->setToolTip("Colors describe confirmed audit states, not inferred live worker activity. Pending audit reports do not expire.");return label;
}
}
