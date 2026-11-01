#include "ui/mining_results_view.h"
#include "ui/panel_style.h"
#include "ui/mining_session_summary.h"
#include "science/mining_results.h"
#include "science/mining_scope_archive.h"
#include "booking/range_identity.h"
#include "mining/native_scientific_context.h"
#include "telemetry.h"
#include <QComboBox>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QToolTip>
#include <QPushButton>
#include <QEvent>
#include <QTimer>
#include <QJsonArray>
#include <QFrame>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <cmath>
#include <memory>

namespace pgn::ui {
namespace {
struct ResultsTurn {QString key;science::MiningResults data;QStringList runs;};
class MiningResultsView final:public QWidget {
    QString root_,run_,request_,genesis_,start_,end_,revision_,activeKey_,wantedKey_,selectionKey_;
    QStringList sessionRequests_,sessionRuns_;quint32 minimum_{};std::optional<double> target_;
    MiningSessionSummary* sessionSummary_;QLabel *heading_,*detail_,*state_,*legend_;
    QString scopeKey()const{return QStringLiteral("ticket");}
    QString scopeLabel()const{return QStringLiteral("Selected ticket");}
    QTableWidget *best_,*recent_,*gapStatistics_,*meritStatistics_;QWidget *bestHeader_{},*recentHeader_{};
    QSet<QString> eligibleRecords_;QHash<QString,double> rowTargets_;bool mixedTargets_{};
    science::MiningScopeArchive sessionArchive_,localArchive_;
    int fittedRows_{10};bool fittingQueued_{};
    bool direct_{};QJsonObject directPacket_;QMap<quint32,qint64> cumulative_;
    QFutureWatcher<ResultsTurn> work_;std::shared_ptr<std::atomic_bool> cancel_;
    science::MiningResults latest_;
    void fitStatistics(){
        int height=0;
        for(auto* table:{gapStatistics_,meritStatistics_}){
            table->verticalHeader()->setMinimumSectionSize(1);table->verticalHeader()->setDefaultSectionSize(23);
            height=std::max(height,table->horizontalHeader()->height()+table->rowCount()*23+4);
            table->setMinimumHeight(0);const int h=table->viewport()->height(),rowHeight=std::max(1,std::min(25,h/std::max(1,table->rowCount())));QFont font=table->font();font.setPixelSize(std::max(6,std::min(12,rowHeight-2)));table->setFont(font);for(int i=0;i<table->rowCount();++i){table->setRowHeight(i,rowHeight);for(int c=0;c<table->columnCount();++c)if(table->item(i,c))table->item(i,c)->setFont(font);}
        }
        setProperty("pgn_statistics_internal_scroll",false);
        setProperty("pgn_statistics_content_height",height);
    }
    QColor color(double merit,std::optional<double> rowTarget={})const{
        const auto target=rowTarget?rowTarget:target_;
        if(!target)return QColor("#69cf8b");
        if(merit>=*target)return QColor("#ffd700");
        if(merit>=*target-1)return QColor("#c0c0c0");
        if(merit>=*target-2)return QColor("#cd7f32");
        if(merit>=*target-3)return QColor("#ff6474");
        return QColor("#69cf8b");
    }
    QTableWidget* table(const QStringList& labels,const char* name){
        auto* t=new QTableWidget;t->setObjectName(name);t->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);t->setMinimumHeight(0);t->setColumnCount(labels.size());t->setHorizontalHeaderLabels(labels);t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setAlternatingRowColors(true);t->verticalHeader()->hide();t->setShowGrid(false);t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);t->horizontalHeader()->setStretchLastSection(true);t->verticalHeader()->setDefaultSectionSize(26);return t;
    }
    void fill(QTableWidget* table,const QVector<science::MiningResult>& records){
        table->setUpdatesEnabled(false);table->clearContents();table->setRowCount(fittedRows_);
        QSet<QString> visibleBest;for(int i=0;i<std::min(fittedRows_,int(latest_.best.size()));++i){const auto& row=latest_.best[i];visibleBest.insert(row.lower+":"+QString::number(row.gap));}
        for(int i=0;i<std::min(fittedRows_,int(records.size()));++i){const auto& row=records[i];const bool eligible=eligibleRecords_.contains(row.lower+":"+QString::number(row.gap));const QStringList values{(eligible?QString("★ "):QString{})+QString::number(row.gap),QString::number(row.merit.toDouble(),'f',4),row.lower};
            for(int c=0;c<values.size();++c){auto* item=new QTableWidgetItem(values[c]);item->setForeground(table==recent_&&!visibleBest.contains(row.lower+":"+QString::number(row.gap))?QColor("#e7f9f1"):color(row.merit.toDouble(),rowTargets_.contains(row.lower+":"+QString::number(row.gap))?std::optional<double>(rowTargets_.value(row.lower+":"+QString::number(row.gap))):std::nullopt));item->setFont(QFont("Consolas",10));item->setTextAlignment(Qt::AlignCenter);item->setToolTip((eligible?QString("Verified improvement against the current prime-gap list reference.\n"):QString{})+QString("Exact pair: [%1, %2]\nSource observation; network validation is separate.").arg(row.lower,row.upper));table->setItem(i,c,item);}}
        table->setUpdatesEnabled(true);
        table->setMinimumHeight(0);table->verticalHeader()->setMinimumSectionSize(1);const int width=table->viewport()->width();table->setColumnWidth(0,width*16/100);table->setColumnWidth(1,width*24/100);table->setColumnWidth(2,width-width*16/100-width*24/100);const int available=table->viewport()->height();for(int i=0;i<fittedRows_;++i){const int rowHeight=available/fittedRows_+(i<available%fittedRows_?1:0);table->setRowHeight(i,rowHeight);for(int c=0;c<table->columnCount();++c)if(auto* item=table->item(i,c)){QFont font("Consolas");font.setPixelSize(std::max(8,std::min(13,rowHeight-3)));if(c==2){const int measured=QFontMetrics(font).horizontalAdvance(item->text()),limit=std::max(1,table->columnWidth(c)-12);if(measured>limit)font.setPixelSize(std::max(8,font.pixelSize()*limit/measured));}item->setFont(font);}}
    }
    void present(const ResultsTurn& turn){
        latest_=turn.data;
        if(bestHeader_&&recentHeader_){const int required=std::max(16,legend_->heightForWidth(std::max(1,legend_->width())));if(bestHeader_->height()!=required){bestHeader_->setFixedHeight(required);recentHeader_->setFixedHeight(required);}}
        if(!latest_.error.isEmpty()){state_->show();state_->setText("Results unavailable · "+latest_.error);state_->setStyleSheet("color:#ffad4a");setProperty("pgn_results_error",latest_.error);return;}
        state_->hide();fill(best_,latest_.best);fill(recent_,latest_.recent);gapStatistics_->setRowCount(0);meritStatistics_->setRowCount(0);
        const auto stat=[](QTableWidget* table,const QString& title,qint64 n){const auto row=table->rowCount();table->insertRow(row);auto* label=new QTableWidgetItem(title);auto* value=new QTableWidgetItem(QString::number(n));value->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);value->setFont(QFont("Consolas",10));if(!n){label->setForeground(QColor("#70918f"));value->setForeground(QColor("#70918f"));}table->setItem(row,0,label);table->setItem(row,1,value);};
        stat(gapStatistics_,"Larger gaps",latest_.points);
        if(direct_){
            for(auto it=cumulative_.cbegin();it!=cumulative_.cend();++it){stat(gapStatistics_,QString("≥ %1").arg(it.key()),it.value());bool ok=false;const double end=end_.toDouble(&ok);if(target_&&ok&&end>1){const auto shade=color(double(it.key())/std::log(end));const int row=gapStatistics_->rowCount()-1;for(int c=0;c<2;++c)gapStatistics_->item(row,c)->setForeground(shade);gapStatistics_->item(row,0)->setToolTip("Cumulative count: all these gaps meet at least the colored tier across this ticket interval. Higher award tiers are included.");}}
        }else if(!latest_.gapSizes.isEmpty()){
            const auto base=minimum_?minimum_:latest_.gapSizes.firstKey();
            QVector<quint32> thresholds{base};for(quint64 n=(quint64(base)/100+1)*100;n<=std::max(quint32(1500),latest_.gapSizes.lastKey())&&thresholds.size()<16;n+=100)thresholds.append(quint32(n));
            for(const auto threshold:thresholds){qint64 n=0;for(auto it=latest_.gapSizes.lowerBound(threshold);it!=latest_.gapSizes.cend();++it)n+=it.value();stat(gapStatistics_,QString("≥ %1").arg(threshold),n);}
        }
        if(!latest_.meritBands.isEmpty()){
            const int first=latest_.meritBands.firstKey();const int last=target_?int(std::ceil(*target_))-1:latest_.meritBands.lastKey();
            for(int band=first;band<=last&&band-first<64;++band){
                const bool final=target_&&double(band+1)>*target_;
                const auto label=QString("%1–%2").arg(band==first?QStringLiteral("min"):QString::number(band),final?QStringLiteral("target"):QString::number(band+1));
                stat(meritStatistics_,mixedTargets_?QString("Below target · %1").arg(label):label,latest_.meritBands.value(band));
                if(target_){const double tier=band>=*target_-1?*target_-1:band>=*target_-2?*target_-2:band>=*target_-3?*target_-3:-1;
                    if(tier>=0){const auto row=meritStatistics_->rowCount()-1;for(int c=0;c<2;++c)meritStatistics_->item(row,c)->setForeground(color(band));meritStatistics_->item(row,0)->setToolTip("Color indicates the minimum award tier guaranteed by the band lower bound; higher tiers may also occur inside the band.");}}
            }
        }
        if(target_||mixedTargets_){stat(meritStatistics_,mixedTargets_?"Reached issued target":"≥ target",latest_.atOrAboveTarget);for(int c=0;c<2;++c)meritStatistics_->item(meritStatistics_->rowCount()-1,c)->setForeground(QColor("#ffd700"));meritStatistics_->item(meritStatistics_->rowCount()-1,0)->setToolTip(target_?QString("Exact target: merit ≥ %1").arg(*target_,0,'g',17):QString("Each result is compared with its original ticket target"));}
        fitStatistics();
        const auto sourceScope=scopeKey()=="history"?QStringLiteral("local archive"):QString("%1 source run(s)").arg(turn.runs.size());
        state_->setStyleSheet("color:#9ec7c1");state_->setText(QString("%1 · %2 retained gaps · %3 · verification separate").arg(scopeLabel()).arg(latest_.points).arg(sourceScope));
        setProperty("pgn_results_error",QString{});setProperty("pgn_results_points",latest_.points);setProperty("pgn_results_runs",turn.runs.size());setProperty("pgn_results_best_rows",latest_.best.size());setProperty("pgn_results_recent_rows",latest_.recent.size());setProperty("pgn_results_scope",scopeKey());
    }
    void refresh(){
        if(direct_){presentDirect();return;}
        const auto scope=scopeKey();auto requests=scope=="session"?sessionRequests_:QStringList{request_};requests.removeAll(QString{});
        auto known=scope=="session"?sessionRuns_:QStringList{run_};known.removeAll(QString{});
        const bool whole=scope=="history";
        const auto key=scope+"/"+requests.join(',')+"/"+known.join(',')+"/"+revision_+"/"+QString::number(fittedRows_)+"/"+(target_?QString::number(*target_,'g',17):QString{});
        const auto selection=scope+"/"+requests.join(',')+"/"+known.join(',')+"/"+(target_?QString::number(*target_,'g',17):QString{});
        if(selectionKey_!=selection){selectionKey_=selection;best_->setRowCount(0);recent_->setRowCount(0);gapStatistics_->setRowCount(0);meritStatistics_->setRowCount(0);setProperty("pgn_results_points",QVariant{});}
        wantedKey_=key;
        if(work_.isRunning()){if(activeKey_!=key&&cancel_)cancel_->store(true);return;}
        if(activeKey_==key)return;activeKey_=key;cancel_=std::make_shared<std::atomic_bool>(false);
        state_->setText("Loading "+scopeLabel().toLower()+"…");setProperty("pgn_results_loading",true);
        const auto root=root_,genesis=genesis_;const int limit=fittedRows_;const auto target=target_;const auto cancel=cancel_;
        work_.setFuture(QtConcurrent::run([root,genesis,known,requests,key,whole,limit,target,cancel]{
            ResultsTurn turn;turn.key=key;
            try{turn.runs=known;if(!whole&&!requests.isEmpty())turn.runs+=science::ticketSourceRuns(root+"/data/runtime/local-bookings.sqlite",requests,genesis);turn.runs.removeDuplicates();
                turn.data=science::readMiningResults(root+"/data/runtime/scientific-history.sqlite",turn.runs,whole,limit,target,cancel.get());}
            catch(const std::exception& e){turn.data.error=QString::fromUtf8(e.what());}return turn;
        }));
    }
public:
    explicit MiningResultsView(QString root,QWidget* parent):QWidget(parent),root_(std::move(root)),localArchive_(root_+"/data/testnet/mining-scope-archive.json"){
        setObjectName("pgn-mining-results");setStyleSheet(dataPanelStyle()+"QGroupBox{border:1px solid #1c424b;border-radius:9px;margin-top:12px;}QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 5px;color:#b5e5df;font-weight:600;}");auto* box=new QVBoxLayout(this);box->setContentsMargins(12,4,12,4);box->setSpacing(6);
        auto* top=new QHBoxLayout;heading_=new QLabel("CURRENT TICKET · waiting for source context");heading_->setStyleSheet("font-weight:600;color:#69efc5");heading_->setWordWrap(true);top->addWidget(heading_,1);box->addLayout(top);

        detail_=new QLabel("Ticket parameters will remain fixed across pauses, restarts and internal chunks.");detail_->setWordWrap(true);box->addWidget(detail_);
        auto* panels=new QSplitter(Qt::Horizontal);panels->setChildrenCollapsible(false);panels->setObjectName("pgn-results-three-columns");
        legend_=new QLabel("Merit colors appear when the ticket target is available.",this);legend_->setWordWrap(true);legend_->setStyleSheet("font-size:9px;");
        const auto panel=[&](const QString& label,QTableWidget* t){auto* group=new QGroupBox;group->setStyleSheet("QGroupBox{margin-top:0;}");auto* layout=new QVBoxLayout(group);layout->setContentsMargins(6,4,6,6);layout->setSpacing(4);auto* header=new QWidget;auto* row=new QHBoxLayout(header);row->setContentsMargins(0,0,0,0);row->setSpacing(8);auto* title=new QLabel(label);title->setStyleSheet("color:#b5e5df;font-weight:600;font-size:10px;");row->addWidget(title,0,Qt::AlignTop);if(t==best_){bestHeader_=header;row->addWidget(legend_,1);}else{recentHeader_=header;row->addStretch();}layout->addWidget(header);layout->addWidget(t,1);group->setMinimumWidth(170);group->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);group->setMinimumHeight(0);panels->addWidget(group);};
        best_=table({"Gap","Merit","Lower prime"},"pgn-results-best");recent_=table({"Gap","Merit","Lower prime"},"pgn-results-recent");gapStatistics_=table({"Gap size","Count"},"pgn-results-gap-statistics");meritStatistics_=table({"Merit band","Count"},"pgn-results-merit-statistics");
        for(auto* table:{gapStatistics_,meritStatistics_}){table->setStyleSheet("QTableWidget::item{padding:0px 4px;}");table->viewport()->installEventFilter(this);table->horizontalHeader()->setMinimumSectionSize(24);table->horizontalHeader()->setStretchLastSection(false);table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);}
        for(auto* table:{best_,recent_}){table->viewport()->installEventFilter(this);table->verticalHeader()->setMinimumSectionSize(1);table->horizontalHeader()->setMinimumSectionSize(24);table->horizontalHeader()->setStretchLastSection(false);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);}
        for(auto* table:{best_,recent_,gapStatistics_,meritStatistics_}){table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);}
        panel("BEST · largest gaps",best_);panel("RECENT · latest gaps",recent_);
        auto* statistics=new QGroupBox("STATISTICS · selected scope");statistics->setObjectName("pgn-results-statistics");auto* statLayout=new QHBoxLayout(statistics);statLayout->setContentsMargins(6,12,6,6);statLayout->setSpacing(8);auto* distributions=new QWidget;distributions->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);distributions->setMinimumHeight(0);auto* column=new QVBoxLayout(distributions);column->setContentsMargins(0,0,0,0);column->setSpacing(4);
        const auto section=[&](const QString& title,QTableWidget* table){auto* header=new QHBoxLayout;auto* name=new QLabel(title);name->setStyleSheet("color:#69d9d0;font-size:10px;font-weight:600;");header->addWidget(name);header->addStretch();auto* count=new QLabel("COUNT");count->setStyleSheet("color:#93bab6;font-size:9px;");header->addWidget(count);column->addLayout(header);table->horizontalHeader()->hide();column->addWidget(table,table==gapStatistics_?8:10);};
        section("GAP SIZE · cumulative",gapStatistics_);auto* horizontal=new QFrame;horizontal->setFrameShape(QFrame::HLine);horizontal->setStyleSheet("color:#2b5961;");column->addWidget(horizontal);section("MERIT · separate bands",meritStatistics_);statLayout->addWidget(distributions,1);auto* divider=new QFrame;divider->setFrameShape(QFrame::VLine);divider->setStyleSheet("color:#27515b");statLayout->addWidget(divider);sessionSummary_=new MiningSessionSummary;statLayout->addWidget(sessionSummary_,1);statistics->setTitle("STATISTICS · ticket + session");statistics->setMinimumWidth(330);statistics->setMinimumHeight(0);statistics->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);panels->addWidget(statistics);panels->setSizes({420,420,380});box->addWidget(panels,1);
        state_=new QLabel("Awaiting ticket results");state_->setWordWrap(true);box->addWidget(state_);state_->hide();
        connect(&work_,&QFutureWatcher<ResultsTurn>::finished,this,[this]{const auto result=work_.result();setProperty("pgn_results_loading",false);if(!direct_&&result.key==wantedKey_&&!result.data.cancelled)present(result);if(result.key!=wantedKey_||result.data.cancelled){activeKey_.clear();refresh();}});
    }
    ~MiningResultsView()override{if(cancel_)cancel_->store(true);}
    bool eventFilter(QObject* watched,QEvent* event)override{
        if((watched==best_->viewport()||watched==recent_->viewport())&&event->type()==QEvent::Resize&&!fittingQueued_){fittingQueued_=true;QTimer::singleShot(0,this,[this]{fittingQueued_=false;const int count=std::clamp(std::min(best_->viewport()->height(),recent_->viewport()->height())/26,10,100);const bool changed=count!=fittedRows_;fittedRows_=count;setProperty("pgn_results_fitted_rows",count);if(changed)refresh();else{fill(best_,latest_.best);fill(recent_,latest_.recent);}});}

        if((watched==gapStatistics_->viewport()||watched==meritStatistics_->viewport())&&event->type()==QEvent::Resize)QTimer::singleShot(0,this,[this]{fitStatistics();});
        return QWidget::eventFilter(watched,event);
    }
    void presentDirect(){
        const auto scope=scopeKey();const auto data=directPacket_.value("scopes").toObject().value(scope).toObject();
        sessionSummary_->present(directPacket_.value("scopes").toObject().value("session").toObject(),directPacket_.value("scopes").toObject().value("history").toObject(),directPacket_);
        if(data.isEmpty()){best_->setRowCount(0);recent_->setRowCount(0);gapStatistics_->setRowCount(0);meritStatistics_->setRowCount(0);setProperty("pgn_results_points",QVariant{});state_->setText("Awaiting "+scopeLabel().toLower()+" data");return;}
        ResultsTurn turn;turn.runs={directPacket_.value("ticket_id").toString()};const auto stats=data.value("statistics").toObject();
        mixedTargets_=stats.value("mixed_targets").toBool();const auto exactTarget=stats.value("statistics_target");target_=exactTarget.isDouble()?std::optional<double>(exactTarget.toDouble()):std::nullopt;rowTargets_.clear();
        for(const auto& key:{QString("best"),QString("recent")})for(const auto& value:data.value(key).toArray()){const auto row=value.toObject();if(row.value("target_merit").isDouble())rowTargets_.insert(row.value("lower_prime").toString()+":"+QString::number(row.value("gap").toInt()),row.value("target_merit").toDouble());}
        turn.data.points=stats.value("count_total").toVariant().toLongLong();turn.data.atOrAboveTarget=stats.value("at_or_above_target").toVariant().toLongLong();
        cumulative_.clear();const auto thresholds=stats.value("threshold_counts").toObject();for(auto it=thresholds.begin();it!=thresholds.end();++it)cumulative_.insert(it.key().toUInt(),it.value().toVariant().toLongLong());
        const auto bands=stats.value("merit_band_counts").toObject();for(auto it=bands.begin();it!=bands.end();++it)turn.data.meritBands.insert(it.key().toInt(),it.value().toVariant().toLongLong());
        const auto parse=[this](const QJsonArray& array){QVector<science::MiningResult> records;const int count=std::min(fittedRows_,int(array.size()));for(int i=0;i<count;++i){const auto value=array[i].toObject();const auto lower=value.value("lower_prime").toString();const auto gap=value.value("gap").toInt();const auto merit=value.value("merit").toString();if(gap<=0||lower.isEmpty()||merit.isEmpty())continue;records.append({i,quint32(gap),lower,value.value("upper_prime").toString(),merit});}return records;};
        turn.data.best=parse(data.value("best").toArray());turn.data.recent=parse(data.value("recent").toArray());present(turn);
        state_->setText(QString("%1 · %2 larger gaps · %3").arg(scopeLabel()).arg(turn.data.points).arg(scope=="ticket"?directPacket_.value("validation_state").toString("Awaiting independent audits"):QString("%1 observed tickets · local calculations, audits separate").arg(data.value("ticket_count").toString())));
        if(scope=="history"&&!localArchive_.error().isEmpty())state_->setText(state_->text()+" · "+localArchive_.error());
        if(scope!="ticket")legend_->setText(mixedTargets_?"Mixed issued targets · row colors use each original ticket target; histogram bands count results below their issued target.":QString("Original ticket target: %1 · local calculations; independent audits are separate").arg(target_?QString::number(*target_,'f',1):QString("unavailable")));
    }
    void productivity(const QJsonArray& devices,const QString& width){sessionSummary_->presentProductivity(devices,width);}
    void recordEligibility(const QJsonArray& pairs){QSet<QString> keys;for(const auto& value:pairs){const auto pair=value.toObject();keys.insert(pair.value("lower_prime").toString()+":"+QString::number(pair.value("gap").toInt()));}if(keys==eligibleRecords_)return;eligibleRecords_=std::move(keys);fill(best_,latest_.best);fill(recent_,latest_.recent);}
    void ticketData(const QJsonObject& packet){
        if(packet.value("schema")!="PGN_HORIZON_MINING_VIEW_V1")return;heading_->hide();detail_->hide();direct_=true;directPacket_=packet;if(cancel_)cancel_->store(true);
        sessionArchive_.observe(packet);localArchive_.observe(packet);auto scopes=directPacket_.value("scopes").toObject();scopes.insert("session",sessionArchive_.scope());scopes.insert("history",localArchive_.scope());directPacket_.insert("scopes",scopes);
        request_=packet.value("request_id").toString();start_=packet.value("start").toString();end_=packet.value("end").toString();minimum_=packet.value("minimum_gap").toInt();
        const auto target=packet.value("target_merit").toDouble();target_=target>0&&std::isfinite(target)?std::optional<double>{target}:std::nullopt;
        heading_->setText(packet.value("title").toString("Network ticket"));heading_->setToolTip("Request identity: "+request_);
        detail_->setText(QString("%1 bits · %2 positions · %3").arg(packet.value("bits").toInt()).arg(packet.value("positions_label").toString(),packet.value("phase").toString()));
        if(target_)legend_->setText(QString("<span style='color:#ffd700'>● Gold ≈ %1</span> &nbsp; <span style='color:#c0c0c0'>● Silver ≈ %2</span> &nbsp; <span style='color:#cd7f32'>● Bronze ≈ %3</span> &nbsp; <span style='color:#ff6474'>● Near miss ≈ %4</span>").arg(*target_,0,'f',1).arg(*target_-1,0,'f',1).arg(*target_-2,0,'f',1).arg(*target_-3,0,'f',1));
        if(target_)legend_->setToolTip(QString("Exact ticket thresholds: Gold ≥ %1; Silver ≥ %2; Bronze ≥ %3; Near miss ≥ %4").arg(*target_,0,'g',17).arg(*target_-1,0,'g',17).arg(*target_-2,0,'g',17).arg(*target_-3,0,'g',17));
        refresh();
    }
    void source(const FrontierTelemetrySnapshot& lane){
      if(direct_)return;
      try{
        if(lane.runId.isEmpty())return;const auto manifest=lane.raw.value("pgn_native_manifest").toObject();run_=lane.runId;
        if(!sessionRuns_.contains(run_))sessionRuns_.append(run_);
        const auto work=manifest.value("native_scientific_context").toObject().value("rpc_work").toObject();
        request_=work.value("request_id").toString();genesis_=work.value("genesis").toString();
        if(!request_.isEmpty()&&!sessionRequests_.contains(request_))sessionRequests_.append(request_);
        start_=work.value("start").toString(lane.domainStart);const auto length=work.value("length").toString();
        end_=length.isEmpty()?lane.domainEnd:booking::addUnsigned(start_,length);
        bool ok=false;minimum_=lane.raw.value("pgn_native_min_gap").toString().toUInt(&ok);if(!ok)minimum_=0;
        const auto numerator=work.value("target_numerator").toString().toDouble(&ok);bool denominatorOk=false;const auto denominator=work.value("target_denominator").toString().toDouble(&denominatorOk);
        target_=(ok&&denominatorOk&&numerator>0&&denominator>0&&std::isfinite(numerator/denominator))?std::optional<double>(numerator/denominator):std::nullopt;
        heading_->setText("TICKET · "+booking::rangeDisplayTitle(start_,end_));heading_->setToolTip(request_.isEmpty()?QStringLiteral("Local source range; no network ticket has been established."):"Request identity: "+request_);
        detail_->setText(QString("%1 bits · min_gap %2 · target merit ≈ %3 · [%4, %5)").arg(lane.numberBits?QString::number(*lane.numberBits):QStringLiteral("—")).arg(minimum_?QString::number(minimum_):QStringLiteral("—")).arg(target_?QString::number(*target_,'f',0):QStringLiteral("—"),start_,end_));
        legend_->setText(target_?QString("<span style='color:#ffd700'>● Gold ≈ %1</span> &nbsp; <span style='color:#c0c0c0'>● Silver ≈ %2</span> &nbsp; <span style='color:#cd7f32'>● Bronze ≈ %3</span> &nbsp; <span style='color:#ff6474'>● Near miss ≈ %4</span> · ticket merit tiers").arg(*target_,0,'f',0).arg(*target_-1,0,'f',0).arg(*target_-2,0,'f',0).arg(*target_-3,0,'f',0):QStringLiteral("Target unavailable · no medal is inferred"));
        const auto exact=target_?QString("Exact thresholds: Gold ≥ %1; Silver ≥ %2; Bronze ≥ %3; Near miss ≥ %4. Display labels are rounded; classification uses these unrounded values. Source observations are not network acceptance.").arg(*target_,0,'g',17).arg(*target_-1,0,'g',17).arg(*target_-2,0,'g',17).arg(*target_-3,0,'g',17):QStringLiteral("Exact target unavailable");legend_->setToolTip(exact);detail_->setToolTip(exact);
        refresh();
      }catch(const std::exception& e){state_->setText("Source context unavailable · "+QString::fromUtf8(e.what()));}
    }
    void index(const QJsonObject& packet){if(packet.value("complete")!=true)return;revision_=QString::number(packet.value("all").toObject().value("length").toObject().value("revision").toInteger());refresh();}
};
}
QWidget* createMiningResultsView(const QString& root,QWidget* parent){return new MiningResultsView(root,parent);}
void presentMiningRecordEligibility(QWidget* widget,const QJsonArray& pairs){if(auto* view=dynamic_cast<MiningResultsView*>(widget))view->recordEligibility(pairs);}
void presentMiningResultsSource(QWidget* widget,const FrontierTelemetrySnapshot& lane){if(auto* view=dynamic_cast<MiningResultsView*>(widget))view->source(lane);}
void presentMiningTicketData(QWidget* widget,const QJsonObject& packet){if(auto* view=dynamic_cast<MiningResultsView*>(widget))view->ticketData(packet);}
void presentMiningProductivity(QWidget* widget,const QJsonArray& devices,const QString& width){if(auto* view=dynamic_cast<MiningResultsView*>(widget))view->productivity(devices,width);}
void presentMiningResultsIndex(QWidget* widget,const QJsonObject& packet){if(auto* view=dynamic_cast<MiningResultsView*>(widget))view->index(packet);}
}
