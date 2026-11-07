#include "ui/mining_results_view.h"
#include <QApplication>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QScrollBar>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void settle(QWidget* view){for(int i=0;i<8;++i)QApplication::processEvents();QImage image(view->size(),QImage::Format_ARGB32);view->render(&image);}
QJsonObject packet(const QString& lowerBase){
    QJsonArray best,recent;
    const QStringList merits{"30.0","29.4","28.6","27.7","26.8","25.2"};
    for(int i=0;i<200;++i){
        QString lower=lowerBase;lower.replace(lower.size()-5,5,QString::number(10000+i*2));
        best.append(QJsonObject{{"gap",2000-i*2},{"lower_prime",lower},{"upper_prime",lower},{"merit",merits[std::min(i,5)]}});
    }
    recent.append(best.last());for(int i=0;i<199;++i)recent.append(best[i]);
    QJsonObject statistics{{"count_total",200},{"statistics_target",29.0},{"mixed_targets",false},{"at_or_above_target",2},{"threshold_counts",QJsonObject{{"954",200}}},{"merit_band_counts",QJsonObject{{"26",1},{"27",1},{"28",1},{"29",1},{"30",1}}}};
    const QJsonObject scope{{"statistics",statistics},{"best",best},{"recent",recent}};
    return {{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"genesis",QString(64,'a')},{"ticket_id",QString(64,'b')},{"request_id",QString(64,'c')},
            {"title","Public rendering fixture, not scientific results"},{"start",lowerBase},{"end",lowerBase},{"bits",lowerBase.size()>25?95:68},{"minimum_gap",954},{"target_merit",29.0},
            {"scopes",QJsonObject{{"ticket",scope}}}};
}
}
int main(int argc,char** argv){
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-QA");QCoreApplication::setApplicationName("results-layout");QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;if(!temp.isValid())return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    try{
        std::unique_ptr<QWidget> view(pgn::ui::createMiningResultsView(temp.path()));view->show();
        auto* best=view->findChild<QTableWidget*>("pgn-results-best");auto* recent=view->findChild<QTableWidget*>("pgn-results-recent");need(best&&recent,"missing real result tables");
        int least=1000,most=0;
        for(const auto& size:{QSize(800,280),QSize(1060,300),QSize(1060,600),QSize(1380,900),QSize(1920,900),QSize(800,280)}){
            for(const auto& lower:{QString("160696461657079905863"),QString("19807040628566084398385988493")}){
                view->resize(size);auto data=packet(lower);pgn::ui::presentMiningTicketData(view.get(),data);settle(view.get());
                const int count=view->property("pgn_results_fitted_rows").toInt();need(count>=3&&count<=100,"unbounded fitted result count");least=std::min(least,count);most=std::max(most,count);
                need(best->rowCount()==count&&recent->rowCount()==count,"unequal visible result counts");
                for(auto* table:{best,recent}){
                    int width=0,height=0;for(int c=0;c<3;++c)width+=table->columnWidth(c);for(int r=0;r<count;++r)height+=table->rowHeight(r);
                    if(width>table->viewport()->width())need(table->horizontalScrollBar()->isVisible()&&table->horizontalScrollBar()->maximum()>0,"wide values inaccessible in compact window");
                    else need(std::abs(width-table->viewport()->width())<=1,"columns leave unused horizontal space");
                    need(height==table->viewport()->height(),"rows leave blank space or clip below viewport");
                    const auto first=table->visualItemRect(table->item(0,0)),last=table->visualItemRect(table->item(count-1,2));
                    need(first.top()>=0&&last.bottom()<table->viewport()->height(),"first/final row sliced");
                    for(int row=0;row<count;++row)for(int col=0;col<3;++col){auto* item=table->item(row,col);need(item!=nullptr,"partial displayed result");need(item->textAlignment()==Qt::AlignCenter,"result alignment differs");need(item->font().pixelSize()>=11,"result font became unreadable");if(QFontMetrics(item->font()).horizontalAdvance(item->text())+12>table->columnWidth(col))throw std::runtime_error(QString("elided %1 size%2x%3 row%4 col%5 font%6 measured%7 column%8 text%9").arg(table->objectName()).arg(size.width()).arg(size.height()).arg(row).arg(col).arg(item->font().pixelSize()).arg(QFontMetrics(item->font()).horizontalAdvance(item->text())).arg(table->columnWidth(col)).arg(item->text()).toStdString());++checks;}
                    table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->maximum());QApplication::processEvents();
                    const auto endpoint=table->visualItemRect(table->item(count-1,2));
                    if(endpoint.left()<0||endpoint.right()>=table->viewport()->width())throw std::runtime_error(QString("complete endpoint unreachable: size%1x%2 view%3 column%4 rect%5..%6 scroll%7/%8").arg(size.width()).arg(size.height()).arg(table->viewport()->width()).arg(table->columnWidth(2)).arg(endpoint.left()).arg(endpoint.right()).arg(table->horizontalScrollBar()->value()).arg(table->horizontalScrollBar()->maximum()).toStdString());++checks;
                    table->horizontalScrollBar()->setValue(0);
                }
                need(recent->item(0,0)->foreground().color()==QColor("#e7f9f1"),"hidden Best candidate colored in Recent");
                for(int row=0;row<5;++row)need(recent->item(row+1,0)->foreground().color()==best->item(row,0)->foreground().color(),"visible Best candidate lost its Recent color");
                const QStringList colors{"#ffd700","#ffd700","#c0c0c0","#cd7f32","#ff6474"};
                for(int row=0;row<colors.size();++row)need(best->item(row,0)->foreground().color()==QColor(colors[row]),"Best tier color is incorrect");
                const QStringList discoveries{"Gold discovery","Gold discovery","Silver discovery","Bronze discovery","Rust discovery"};
                for(int row=0;row<discoveries.size();++row){
                    for(auto* item:{best->item(row,0),recent->item(row+1,0)}){
                        need(!item->icon().isNull(),"missing Best/Recent discovery rosette");
                        need(item->data(Qt::AccessibleDescriptionRole).toString()==discoveries[row],"rosette claims the wrong discovery tier");
                        need(item->icon().pixmap(QSize(16,20),QIcon::Selected).toImage()==item->icon().pixmap(QSize(16,20),QIcon::Normal).toImage(),"selection recolored the discovery tier");
                        need(item->toolTip().contains("not a credited payment"),"discovery rosette is confused with wallet credit");
                    }
                }
                need(recent->item(0,0)->icon().isNull(),"ordinary hidden-Best result inferred a rosette");
                const auto visible=view->property("pgn_visible_best").toJsonObject().value("rows").toArray();need(visible.size()==count,"visible Best contract differs from drawn rows");
                bool foundLegend=false;
                for(auto* label:view->findChildren<QLabel*>())if(label->text().contains("Gold ≈")){
                    foundLegend=true;const auto bottom=label->mapTo(view.get(),QPoint(0,label->height())).y();need(bottom<=best->mapTo(view.get(),QPoint(0,0)).y(),"legend consumes a footer row below Best");
                }
                need(foundLegend,"missing inline Best legend");
                auto* selected=recent->item(2,0);const auto key=selected->data(Qt::UserRole);recent->setCurrentCell(2,0);recent->selectRow(2);
                auto scope=data.value("scopes").toObject(),ticket=scope.value("ticket").toObject(),stats=ticket.value("statistics").toObject();stats.insert("count_total",201);ticket.insert("statistics",stats);scope.insert("ticket",ticket);data.insert("scopes",scope);
                pgn::ui::presentMiningTicketData(view.get(),data);settle(view.get());need(recent->currentItem()&&recent->currentItem()->data(Qt::UserRole)==key,"stream refresh lost selection identity");
                if(argc>1)view->grab().save(QString::fromLocal8Bit(argv[1])+QString("/results-%1x%2-%3.png").arg(size.width()).arg(size.height()).arg(lower.size()));
            }
        }
        need(most>least,"window enlargement did not add usable rows");
        std::cout<<"PASS "<<checks<<" real result-widget checks: twelve size/domain cases, full columns and rows, color membership/tiers, inline legend, selection preserved; no RPC/GPU/mining\n";
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
