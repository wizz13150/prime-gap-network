#include "app/horizon_user_paths.h"
#include "ui/memory_benchmark_view.h"
#include "ui/position_formats.h"
#include "telemetry.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHash>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDir>
#include <QPointer>
#include <QTimer>
#include <QUuid>

namespace pgn::ui { namespace {
QString writeReceipt(const QString& path,const QJsonObject& receipt) {
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return file.errorString();
    const auto bytes=QJsonDocument(receipt).toJson();if(file.write(bytes)!=bytes.size()||!file.commit())return file.errorString();return {};
}
class MemoryView final:public QWidget {
public:
    MemoryView(QString root,QWidget* parent):QWidget(parent),root_(std::move(root)) {
        setObjectName("pgn-benchmark-memory-workspace");auto* box=new QVBoxLayout(this);
        auto* notice=new QLabel("Short Native probes use the configured grid (default: 4000–9250 MHz, 10 points); no probe below 4000 MHz. Requested and applied frequencies remain distinct: rounding to 5001 MHz does not validate a 4000 MHz target. Only distinct finalist groups receive long A-B-B-A confirmations. Probes are never validated Native graph points. Core stays AUTO; original memory settings are restored before release.");
        notice->setWordWrap(true);box->addWidget(notice);
        devices_=new QComboBox;devices_->setObjectName("pgn-benchmark-memory-device");devices_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);devices_->setMinimumContentsLength(24);box->addWidget(devices_);
        connect(devices_,&QComboBox::currentIndexChanged,this,[this]{updateTarget();});
        auto* range=new QHBoxLayout;box->addLayout(range);
        minimum_=new QSpinBox;maximum_=new QSpinBox;count_=new QSpinBox;
        minimum_->setObjectName("pgn-benchmark-memory-minimum");maximum_->setObjectName("pgn-benchmark-memory-maximum");count_->setObjectName("pgn-benchmark-memory-count");
        minimum_->setRange(4000,9250);maximum_->setRange(4000,9250);count_->setRange(2,12);
        minimum_->setValue(4000);maximum_->setValue(9250);count_->setValue(10);
        range->addWidget(new QLabel("Minimum MHz"));range->addWidget(minimum_);range->addWidget(new QLabel("Maximum MHz"));range->addWidget(maximum_);range->addWidget(new QLabel("Points"));range->addWidget(count_);range->addStretch();
        auto* actions=new QHBoxLayout;box->addLayout(actions);
        start_=new QPushButton("Search memory requests · core AUTO");start_->setObjectName("pgn-benchmark-memory-start");actions->addWidget(start_);
        cancel_=new QPushButton("Cancel and restore original settings");cancel_->setObjectName("pgn-benchmark-memory-cancel");cancel_->setEnabled(false);actions->addWidget(cancel_);actions->addStretch();
        status_=new QLabel;status_->setWordWrap(true);status_->setObjectName("pgn-benchmark-memory-status");box->addWidget(status_);
        groupSummary_=new QLabel("No completed applied-frequency groups yet");groupSummary_->setObjectName("pgn-benchmark-memory-group-summary");groupSummary_->setWordWrap(true);box->addWidget(groupSummary_);
        groups_=new QTableWidget(0,4,this);groups_->setObjectName("pgn-benchmark-memory-groups");groups_->setMaximumHeight(140);
        groups_->setHorizontalHeaderLabels({"Applied MHz","Equivalent requests MHz","Median Native positions/s","Group qualification"});groups_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        groups_->verticalHeader()->hide();groups_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);groups_->horizontalHeader()->setStretchLastSection(true);box->addWidget(groups_);
        results_=new QTableWidget(0,8,this);results_->setObjectName("pgn-benchmark-memory-results");
        results_->setHorizontalHeaderLabels({"Point","Policy / requested MHz","Actual memory MHz","Actual core MHz","Native positions/s","Source seconds","Observed UTC","Clock limits"});
        results_->setEditTriggers(QAbstractItemView::NoEditTriggers);results_->verticalHeader()->hide();
        results_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);results_->horizontalHeader()->setStretchLastSection(true);box->addWidget(results_,1);
        timer_=new QTimer(this);timer_->setSingleShot(true);
        connect(timer_,&QTimer::timeout,this,[this]{
            if(state_==Restoring) {error_="Original settings restoration is unconfirmed; the lease owner must finish recovery";publish(false);status_->setText(error_);return;}
            finish("Controlled Native measurement timed out; restoring the captured original settings");
        });
        connect(start_,&QPushButton::clicked,this,[this]{begin();});
        connect(cancel_,&QPushButton::clicked,this,[this]{finish("Cancelled by operator");});
        unavailable();
    }
    ~MemoryView() override {
        // The backend, not a QWidget callback, owns crash/disconnect recovery.
        if(executor_&&!session_.isEmpty()&&state_!=Idle) {
            error_="Benchmark view closed during the session";search_.cancel(error_);publish(false);
            const auto path=receiptPath();auto receipt=property("pgn_benchmark_memory_receipt").toJsonObject();
            executor_->cancelAndRestore(session_,[path,receipt](bool restored,QString detail) mutable {
                receipt.insert("restore_confirmed",restored);receipt.insert("restore_detail",detail);receipt.insert("session_state",restored?"CLOSED_RESTORED":"CLOSED_RESTORE_UNCONFIRMED");
                writeReceipt(path,receipt);
            });
        }
    }
    void source(const FrontierTelemetrySnapshot& value) {
        const bool fresh=value.authoritative&&!value.stale&&value.observedUtc.isValid()&&
            value.observedUtc.msecsTo(QDateTime::currentDateTimeUtc())>=0&&value.observedUtc.msecsTo(QDateTime::currentDateTimeUtc())<=5000&&
            value.raw.value("pgn_process_running").toBool();
        QString image=value.artifactSha256;
        if(image.isEmpty())image=value.raw.value("pgn_native_manifest").toObject().value("source_profile").toObject().value("image_sha256").toString();
        if(value.gpuUuid.isEmpty())return;
        if(!sources_.contains(value.gpuUuid)&&sources_.size()>=64)return;
        sources_.insert(value.gpuUuid,{fresh?MemoryBenchmarkTarget{value.gpuUuid,value.runId,image}:MemoryBenchmarkTarget{},value.observedUtc});
        if(fresh&&devices_->findData(value.gpuUuid)<0)devices_->addItem(value.gpuName+" · "+value.gpuUuid,value.gpuUuid);
        updateTarget();
        // Source-window observation can lag a controlled executor's own jobs;
        // the executor must independently recheck target/lease at every write.
    }
    void bind(std::shared_ptr<MemoryBenchmarkExecutor> executor) {
        if(state_!=Idle)return;executor_=std::move(executor);
        setProperty("pgn_benchmark_memory_available",bool(executor_));
        if(executor_)status_->setText("Controlled Native executor connected. Acquisition will recheck the GPU, exact workload, regular memory requests, automatic core and exclusive lease.");else unavailable();
    }
private:
    enum State {Idle,Acquiring,Measuring,Restoring};
    void rangeEnabled(bool enabled){minimum_->setEnabled(enabled);maximum_->setEnabled(enabled);count_->setEnabled(enabled);}
    void updateTarget() {
        const auto observed=sources_.value(devices_->currentData().toString());
        const auto age=observed.utc.msecsTo(QDateTime::currentDateTimeUtc());
        target_=observed.utc.isValid()&&age>=0&&age<=5000?observed.target:MemoryBenchmarkTarget{};
    }
    void unavailable() {
        status_->setText("Memory search unavailable: no controlled Native fixed-work executor or exclusive memory-clock lease is connected. No hardware setting was changed.");
        setProperty("pgn_benchmark_memory_available",false);
    }
    void begin() {
        if(state_!=Idle)return;
        updateTarget();
        target_.minimumMemoryMhz=minimum_->value();target_.maximumMemoryMhz=maximum_->value();target_.pointCount=count_->value();
        target_.requestedMemoryMhz=mining::regularMemoryRequests(target_.minimumMemoryMhz,target_.maximumMemoryMhz,target_.pointCount);
        if(target_.requestedMemoryMhz.isEmpty()){status_->setText("Invalid memory min/max/count; no search started");return;}
        if(!executor_) {unavailable();setProperty("pgn_benchmark_memory_error",status_->text());return;}
        if(target_.gpuUuid.isEmpty()||target_.sourceRun.isEmpty()) {
            status_->setText("Memory search unavailable: fresh authoritative GPU/run identity is required. The executor must acquire the actual image identity. No setting was changed.");
            setProperty("pgn_benchmark_memory_error",status_->text());return;
        }
        session_=QUuid::createUuid().toString(QUuid::WithoutBraces);sessionTarget_=target_;search_={};error_.clear();saveError_.clear();results_->setRowCount(0);groups_->setRowCount(0);groupSummary_->setText("Waiting for completed applied-frequency groups");
        setProperty("pgn_benchmark_memory_error",QVariant());setProperty("pgn_benchmark_memory_receipt",QVariant());
        state_=Acquiring;start_->setEnabled(false);devices_->setEnabled(false);rangeEnabled(false);cancel_->setEnabled(true);timer_->start(15000);setProperty("pgn_benchmark_memory_running",true);
        // Durable bounded intent BEFORE any acquisition/probe can mutate clocks.
        publish(false);
        if(!saveError_.isEmpty()) {timer_->stop();state_=Idle;start_->setEnabled(true);devices_->setEnabled(true);rangeEnabled(true);cancel_->setEnabled(false);setProperty("pgn_benchmark_memory_running",false);status_->setText("Search not started: cannot save session intent: "+saveError_);return;}
        status_->setText("Acquiring exclusive memory lease and exact Native workload identity…");
        const auto session=session_;const auto target=sessionTarget_;QPointer<MemoryView> guard(this);
        executor_->acquire(session,target,[guard,session,target](mining::MemoryBenchmarkCapabilities caps,QString error){
            if(!guard||guard->session_!=session||guard->state_!=Acquiring)return;
            if(!error.isEmpty()){guard->finish(error);return;}
            if(caps.identity.gpuUuid!=target.gpuUuid||(!target.imageSha256.isEmpty()&&caps.identity.imageSha256!=target.imageSha256)) {guard->finish("Acquired GPU/image differs from the requested actual source");return;}
            if(caps.requestedMemoryMhz!=target.requestedMemoryMhz){guard->finish("Acquired memory requests differ from the requested regular series");return;}
            if(caps.minimumRequestedMhz!=target.minimumMemoryMhz||caps.maximumRequestedMhz!=target.maximumMemoryMhz||caps.requestedPointCount!=target.pointCount){guard->finish("Acquired min/max/count differ from the requested range");return;}
            if(!guard->search_.begin(caps)){guard->finish(guard->search_.error());return;}
            guard->setProperty("pgn_benchmark_memory_available",true);guard->state_=Measuring;guard->step();
        });
    }
    void step() {
        const auto point=search_.next();if(!point){finish({});return;}
        timer_->start(90000);status_->setText(QString("%1 · point %2 · %3 · fixed work %4 positions + warmup %5").arg(point->phase==mining::MemoryBenchmarkPhase::Probe?"SHORT NATIVE PROBE":"NATIVE A-B-B-A CONFIRMATION").arg(point->sequence+1).arg(point->control?"original-policy control":QString::number(point->memoryMhz)+" MHz").arg(positionFormats(QString::number(point->measuredPositions)).inlineText()).arg(positionFormats(QString::number(point->warmupPositions)).inlineText()));
        const auto session=session_;const int sequence=point->sequence;QPointer<MemoryView> guard(this);
        executor_->measure(session,*point,[guard,session,sequence](mining::MemoryBenchmarkSample sample,QString error){
            if(!guard||guard->session_!=session||guard->state_!=Measuring)return;
            const auto pending=guard->search_.next();if(!pending||pending->sequence!=sequence)return;
            if(!error.isEmpty()){guard->finish(error);return;}
            if(!guard->search_.accept(sample)){guard->finish(guard->search_.error());return;}
            const auto json=sample.json(*pending);const int row=guard->results_->rowCount();guard->results_->insertRow(row);
            const bool probe=pending->phase==mining::MemoryBenchmarkPhase::Probe;
            const bool rejected=probe&&!guard->search_.receipt().value("probes").toArray().last().toObject().value("accepted").toBool();
            const QString rateKey=probe?"probe_native_positions_per_second":"native_positions_per_second";
            const QString rateText=json.contains(rateKey)?QString::number(json.value(rateKey).toDouble(),'g',10)+(probe?" · probe only":""):QStringLiteral("No Native measurement");
            const bool rounded=!json.value("requested_target_achieved").toBool();
            const QStringList cells{(probe?(rejected?"Rejected probe ":rounded?"Rounded diagnostic ":"Probe "):"Confirmation ")+QString::number(sequence+1),pending->control?"Original policy ("+QString::number(pending->memoryMhz)+")":QString::number(pending->memoryMhz),
                QString::number(sample.memoryMinMhz)+"–"+QString::number(sample.memoryMaxMhz),QString::number(sample.coreMinMhz)+"–"+QString::number(sample.coreMaxMhz),
                rateText,
                sample.nativeAccountingVerified?QString::number(double(sample.sourceEndNs-sample.sourceBeginNs)/1e9,'f',3):QStringLiteral("Unavailable"),sample.observedUtc.toUTC().toString(Qt::ISODateWithMs),
                sample.clockLimitReasons.isEmpty()?QStringLiteral("Unavailable"):sample.clockLimitReasons};
            for(int i=0;i<cells.size();++i)guard->results_->setItem(row,i,new QTableWidgetItem(cells[i]));
            guard->refreshGroups();
            guard->publish(false);
            if(!guard->saveError_.isEmpty()){guard->finish("Cannot checkpoint benchmark evidence: "+guard->saveError_);return;}
            // Queue the next point; synchronous test ports cannot recurse through
            // a whole session or starve cancel/close processing.
            QTimer::singleShot(0,guard.data(),[guard,session]{if(guard&&guard->session_==session&&guard->state_==Measuring)guard->step();});
        });
    }
    void refreshGroups() {
        const auto groups=search_.receipt().value("physical_groups").toArray();groups_->setRowCount(groups.size());
        int requests=0;
        for(int row=0;row<groups.size();++row) {
            const auto group=groups[row].toObject();QStringList labels;for(const auto request:group.value("requested_memory_mhz").toArray())labels.append(QString::number(request.toInt()));requests+=labels.size();
            const auto rate=group.value("native_positions_per_second_median");
            const QStringList cells{QString::number(group.value("actual_memory_mhz").toInt()),labels.join(", "),rate.isNull()?"Awaiting confirmation":QString::number(rate.toDouble(),'g',10),group.value("qualification_state").toString()};
            for(int column=0;column<cells.size();++column)groups_->setItem(row,column,new QTableWidgetItem(cells[column]));
        }
        const auto receipt=search_.receipt();groupSummary_->setText(QString("%1 probes completed · %2/%3 requested targets achieved · %4 eligible physical groups. Equivalent requests share one group; rejected/out-of-range clocks stay diagnostic.").arg(receipt.value("completed_probe_count").toInt()).arg(receipt.value("requested_targets_achieved").toInt()).arg(receipt.value("requested_point_count").toInt()).arg(groups.size()));
    }
    void finish(const QString& error) {
        if(state_==Idle||state_==Restoring)return;
        if(!error.isEmpty()){error_=error;search_.cancel(error);}
        state_=Restoring;timer_->start(15000);cancel_->setEnabled(false);status_->setText("Restoring captured original memory policy and releasing this session's lease…");
        const auto session=session_;QPointer<MemoryView> guard(this);
        executor_->cancelAndRestore(session,[guard,session](bool restored,QString detail){
            if(!guard||guard->session_!=session||guard->state_!=Restoring)return;
            guard->timer_->stop();if(!restored){guard->error_="Original settings restoration unconfirmed: "+detail;guard->publish(false);guard->status_->setText(guard->error_);return;}
            guard->publish(true);guard->state_=Idle;guard->start_->setEnabled(true);guard->devices_->setEnabled(true);guard->rangeEnabled(true);guard->setProperty("pgn_benchmark_memory_running",false);
            if(!guard->saveError_.isEmpty())guard->status_->setText("Original settings restored. Evidence save failed: "+guard->saveError_);
            else if(!guard->error_.isEmpty())guard->status_->setText(guard->error_+" · original settings restored; partial evidence saved locally");
            else if(guard->search_.receipt().value("best_observed_memory_mhz").isNull())guard->status_->setText("Search complete · no stable distinct winner · original settings restored · local evidence saved");
            else guard->status_->setText(QString("Search complete · best observed actual memory %1 MHz · core AUTO measured · original settings restored · local evidence saved; no global optimum or memory-only causality claimed").arg(guard->search_.bestMemoryMhz()));
        });
    }
    void publish(bool restored) {
        auto receipt=search_.receipt();receipt.insert("session_id",session_);receipt.insert("observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        receipt.insert("restore_confirmed",restored);receipt.insert("session_error",error_);
        QJsonArray requests;for(int mhz:sessionTarget_.requestedMemoryMhz)requests.append(mhz);
        receipt.insert("requested_target",QJsonObject{{"gpu_uuid",sessionTarget_.gpuUuid},{"source_run",sessionTarget_.sourceRun},{"expected_image_sha256",sessionTarget_.imageSha256},{"requested_memory_mhz",requests},
            {"minimum_memory_mhz",sessionTarget_.minimumMemoryMhz},{"maximum_memory_mhz",sessionTarget_.maximumMemoryMhz},{"point_count",sessionTarget_.pointCount},{"core_policy","AUTO"}});
        receipt.insert("session_state",restored?"RESTORED":state_==Acquiring?"ACQUIRING":state_==Measuring?"MEASURING":"RESTORE_PENDING");
        setProperty("pgn_benchmark_memory_receipt",receipt);setProperty("pgn_benchmark_memory_error",error_);
        const QString directory=pgn::app::mutableDataRoot(root_)+"/runtime/benchmarks/memory";
        if(!QDir().mkpath(directory)){saveError_="Cannot create benchmark evidence directory";return;}
        saveError_=writeReceipt(receiptPath(),receipt);
    }
    QString receiptPath() const {return pgn::app::mutableDataRoot(root_)+"/runtime/benchmarks/memory/"+session_+".json";}
    QString root_,session_,error_,saveError_;State state_{Idle};MemoryBenchmarkTarget target_,sessionTarget_;
    struct ObservedTarget {MemoryBenchmarkTarget target;QDateTime utc;};QHash<QString,ObservedTarget> sources_;QComboBox* devices_{};
    QSpinBox *minimum_{},*maximum_{},*count_{};
    mining::MemoryBenchmarkSearch search_;std::shared_ptr<MemoryBenchmarkExecutor> executor_;
    QPushButton *start_{},*cancel_{};QLabel *status_{},*groupSummary_{};QTableWidget *results_{},*groups_{};QTimer* timer_{};
};
}
QWidget* createMemoryBenchmarkView(const QString& root,QWidget* parent){return new MemoryView(root,parent);}
void presentMemoryBenchmarkSource(QWidget* widget,const FrontierTelemetrySnapshot& source){if(auto* view=dynamic_cast<MemoryView*>(widget))view->source(source);}
void bindMemoryBenchmarkExecutor(QWidget* widget,std::shared_ptr<MemoryBenchmarkExecutor> executor){
    if(!widget)return;auto* view=dynamic_cast<MemoryView*>(widget);
    if(!view)view=dynamic_cast<MemoryView*>(widget->findChild<QWidget*>("pgn-benchmark-memory-workspace"));
    if(view)view->bind(std::move(executor));
}
}
