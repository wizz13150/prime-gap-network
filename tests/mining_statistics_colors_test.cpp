#include "ui/mining_results_view.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QScrollBar>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {
int checks=0,sequence=0;
const QStringList medals{"#ff6474","#cd7f32","#c0c0c0","#ffd700"};
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
bool isMedal(const QColor& c){return medals.contains(c.name());}
QJsonObject packet(std::optional<double> target,const QString& end,bool mixed=false){
    const double t=target.value_or(29.0),logEnd=std::log(end.toDouble());
    QJsonObject thresholds,bands;
    for(int i=0;i<5;++i){const auto gap=2*quint32(std::ceil((t-4+i+.25)*logEnd/2));thresholds.insert(QString::number(gap),5-i);}
    const int first=int(std::floor(t))-5,last=int(std::ceil(t))-1;
    for(int band=first;band<=last;++band)bands.insert(QString::number(band),band==last-1?0:2);
    QJsonObject stats{{"count_total",12},{"threshold_counts",thresholds},{"merit_band_counts",bands},{"at_or_above_target",0},{"mixed_targets",mixed}};
    stats.insert("statistics_target",target?QJsonValue(*target):QJsonValue(QJsonValue::Null));
    const QJsonObject scope{{"statistics",stats},{"best",QJsonArray{}},{"recent",QJsonArray{}}};
    return {{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"ticket_id",QString::number(++sequence).rightJustified(64,'0')},
            {"request_id",QString::number(sequence).rightJustified(64,'0')},{"genesis",QString(64,'a')},
            {"title","Temporary statistics QA fixture"},{"start","150000000000000000000"},{"end",end},
            {"bits",68},{"minimum_gap",954},{"target_merit",target?QJsonValue(*target):QJsonValue(QJsonValue::Null)},
            {"scopes",QJsonObject{{"ticket",scope}}}};
}
void verify(QWidget* view,std::optional<double> target,const QString& end,bool mixed){
    const auto data=packet(target,end,mixed);pgn::ui::presentMiningTicketData(view,data);for(int i=0;i<3;++i)QApplication::processEvents();
    auto* gaps=view->findChild<QTableWidget*>("pgn-results-gap-statistics");
    auto* bands=view->findChild<QTableWidget*>("pgn-results-merit-statistics");
    require(gaps&&bands,"missing real statistics tables");require(gaps->rowCount()==6,"cumulative thresholds missing");
    for(int row=1;row<gaps->rowCount();++row){
        require(gaps->item(row,0)&&gaps->item(row,1),"partial gap row");
        require(gaps->item(row,0)->foreground()==gaps->item(row,1)->foreground(),"gap label/count colors differ");
        const auto color=gaps->item(row,0)->foreground().color();
        if(target&&row>=2)require(color==QColor(medals[row-2]),"cumulative tier color incorrect");
        else require(!isMedal(color),"unknown/ordinary cumulative threshold inferred a medal");
    }
    const double t=target.value_or(29.0);const int first=int(std::floor(t))-5,last=int(std::ceil(t))-1;
    require(bands->rowCount()==last-first+1+int(target.has_value()||mixed),"histogram bands lost");
    for(int row=0;row<=last-first;++row){
        auto* label=bands->item(row,0);auto* count=bands->item(row,1);
        require(label&&count,"partial merit row");require(label->foreground()==count->foreground(),"merit label/count colors differ");
        const int lower=first+row;const auto color=label->foreground().color();
        if(target&&lower>=t-1)require(color==QColor("#c0c0c0"),"silver band incorrect");
        else if(target&&lower>=t-2)require(color==QColor("#cd7f32"),"bronze band incorrect");
        else if(target&&lower>=t-3)require(color==QColor("#ff6474"),"rust band incorrect");
        else require(!isMedal(color),"partial/unknown band overclaims an award");
    }
    if(target||mixed){const int row=bands->rowCount()-1;
        require(bands->item(row,0)->foreground().color()==QColor("#ffd700"),"gold summary label missing");
        require(bands->item(row,1)->foreground().color()==QColor("#ffd700"),"gold zero/count missing");
        require(bands->item(row,0)->text()==(mixed?"Reached issued target":"≥ target"),"mixed targets falsely share a fixed cutoff");}
    for(auto* table:{gaps,bands}){
        int rows=0;for(int row=0;row<table->rowCount();++row)rows+=table->rowHeight(row);
        for(int row=0;row<table->rowCount();++row){
            require(table->item(row,0)->font().pixelSize()>=11,"statistics text became unreadably small");
            require(table->rowHeight(row)>=18,"statistics row clips its text");
        }
        if(rows>table->viewport()->height()){
            require(table->verticalScrollBar()->isVisible()&&table->verticalScrollBar()->maximum()>0,"overflowing statistics are inaccessible");
            for(int top=0;top<table->rowCount();++top){
                table->scrollToItem(table->item(top,0),QAbstractItemView::PositionAtTop);QApplication::processEvents();
                for(int row=0;row<table->rowCount();++row){const auto rect=table->visualItemRect(table->item(row,0));
                    if(table->viewport()->rect().intersects(rect))require(rect.top()>=0&&rect.bottom()<table->viewport()->height(),"compact statistics slice a visible row");}
            }
            table->scrollToBottom();QApplication::processEvents();
            const auto last=table->visualItemRect(table->item(table->rowCount()-1,1));
            require(last.top()>=0&&last.bottom()<table->viewport()->height(),"final statistics row cannot be fully read");
            table->scrollToTop();
        }
        require(table->columnWidth(0)+table->columnWidth(1)<=table->viewport()->width()+1,"statistics columns overflow");
    }
}
}
int main(int argc,char** argv){
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-QA");QCoreApplication::setApplicationName("statistics-colors");QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;if(!temp.isValid())return 2;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    try{std::unique_ptr<QWidget> view(pgn::ui::createMiningResultsView(temp.path()));view->show();
        for(const auto size:{QSize(800,280),QSize(1060,300),QSize(1060,600),QSize(1280,800),QSize(1920,900)}){
            view->resize(size);QApplication::processEvents();
            for(const auto target:{29.0,28.8,32.125}){
                verify(view.get(),target,"160696461657079906819",false);
                verify(view.get(),target,"39614081257132168796771975168",false);
            }
            verify(view.get(),std::nullopt,"160696461657079906819",false);
            verify(view.get(),std::nullopt,"160696461657079906819",true);
            if(argc>1){verify(view.get(),29.0,"160696461657079906819",false);view->grab().save(QString::fromLocal8Bit(argv[1])+QString("/statistics-%1x%2.png").arg(size.width()).arg(size.height()));}
        }
        std::cout<<"PASS "<<checks<<" real-widget statistics color/readability checks: both columns, all four award tiers, fractional and changed targets, zeros, unknown/mixed targets, 68/95-bit endpoints, five sizes, reachable compact overflow; no mining/GPU/network\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}
}
