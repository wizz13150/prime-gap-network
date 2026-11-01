#include "ui/record_submission_view.h"
#include "science/record_submissions.h"
#include "science/record_reference_refresh.h"
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QSettings>
#include <QDateTime>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <cstdint>
#include <QSaveFile>
#include <QDir>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

namespace pgn::ui {
namespace {
class RecordSubmissionView final:public QWidget {
    QString root_,referenceFile_;QPlainTextEdit* text_;QLabel* status_;QPushButton *copy_,*refresh_;QToolButton* expand_;
    QFutureWatcher<science::RecordSubmissions> work_;QTimer timer_;
    std::shared_ptr<std::atomic_bool> cancelled_=std::make_shared<std::atomic_bool>(false);
    bool again_{},ticketMode_{},ticketDataKnown_{},manualBusy_{},published_{},eligibilityPublished_{};
    QJsonArray lastEligibility_;QVector<science::SubmissionRecord> pairs_;QByteArray pairsHash_,candidateHash_;
    QString referenceNotice_;std::function<void(const QJsonArray&)> eligibility_;
    static constexpr qint64 CandidateByteLimit=1024*1024;
    static constexpr int CandidateCountLimit=4096;
    QString candidatePath()const{return root_+"/data/testnet/record-candidates.json";}
    bool mergeCandidates(const QJsonArray& incoming,bool save,bool* changed=nullptr){
        QMap<quint32,science::SubmissionRecord> best;
        for(const auto& pair:pairs_)best.insert(pair.gap,pair);
        static const QRegularExpression integer("^[1-9][0-9]{0,28}$");
        for(const auto& value:incoming){const auto pair=value.toObject();const auto gap=pair.value("gap").toInteger();const auto lower=pair.value("lower_prime").toString(),upper=pair.value("upper_prime").toString();
            if(gap<=0||gap>UINT32_MAX||!integer.match(lower).hasMatch()||!integer.match(upper).hasMatch())return false;
            const auto key=quint32(gap);const auto found=best.constFind(key);if(found==best.cend()||lower.size()<found->lower.size()||(lower.size()==found->lower.size()&&lower<found->lower))best.insert(key,{key,lower,upper,{}});
        }
        if(best.size()>CandidateCountLimit)return false;
        QJsonArray records;for(const auto& pair:best)records.append(QJsonObject{{"gap",qint64(pair.gap)},{"lower_prime",pair.lower},{"upper_prime",pair.upper}});
        const auto bytes=QJsonDocument(QJsonObject{{"schema","PGN_RECORD_CANDIDATES_V1"},{"source_pairs",records}}).toJson(QJsonDocument::Compact);if(bytes.size()>CandidateByteLimit)return false;
        const auto logicalHash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);const bool different=logicalHash!=candidateHash_;if(changed)*changed=different;
        if(save&&different){QDir().mkpath(QFileInfo(candidatePath()).absolutePath());QSaveFile file(candidatePath());if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return false;}
        candidateHash_=logicalHash;pairs_=best.values().toVector();setProperty("pgn_record_candidate_bytes",bytes.size());setProperty("pgn_record_candidate_limit_bytes",CandidateByteLimit);return true;
    }
    void loadCandidates(){
        QFile file(candidatePath());if(!file.exists())return;
        if(!file.open(QIODevice::ReadOnly)||file.size()>CandidateByteLimit){status_->setText("Saved record candidates exceed their allowed size");return;}
        const auto doc=QJsonDocument::fromJson(file.readAll()).object();const auto values=doc.value("source_pairs").toArray();if(doc.value("schema")!="PGN_RECORD_CANDIDATES_V1"||values.size()>CandidateCountLimit||!mergeCandidates(values,false)){status_->setText("Saved record candidates are invalid");return;}
        ticketDataKnown_=true;
    }
    void setStatus(const QString& text){
        if(status_->text()==text)return;
        status_->setText(text);setProperty("pgn_record_status_text",text);setProperty("pgn_record_status_revision",property("pgn_record_status_revision").toLongLong()+1);
    }
    void refresh(bool manual=false){
        if(manual){manualBusy_=true;refresh_->setEnabled(false);setProperty("pgn_record_check_busy",true);}
        if(ticketMode_&&!ticketDataKnown_){setStatus("Waiting for ticket record data");copy_->setEnabled(false);manualBusy_=false;refresh_->setEnabled(true);setProperty("pgn_record_check_busy",false);return;}
        if(work_.isRunning()){again_=true;return;}
        if(!published_)setStatus(ticketMode_?"Checking ticket candidates…":"Checking the archive against the record reference…");
        const auto root=root_,referenceFile=referenceFile_;const auto cancelled=cancelled_;
        if(ticketMode_){const auto pairs=pairs_;work_.setFuture(QtConcurrent::run([referenceFile,pairs,cancelled]{return science::collectRecordSubmissionsFromPairs(pairs,referenceFile,cancelled.get());}));return;}
        work_.setFuture(QtConcurrent::run([root,referenceFile,cancelled]{return science::collectRecordSubmissions(root+"/data/runtime/scientific-history.sqlite",referenceFile,cancelled.get());}));
    }
public:
    void observeEligibility(std::function<void(const QJsonArray&)> observer){eligibility_=std::move(observer);}
    RecordSubmissionView(QString root,QWidget* parent,bool ticketMode):QWidget(parent),root_(std::move(root)),ticketMode_(ticketMode){
        setObjectName("pgn-record-submissions");setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
        auto* box=new QVBoxLayout(this);box->setContentsMargins(10,5,10,5);box->setSpacing(4);
        auto* row=new QHBoxLayout;expand_=new QToolButton;expand_->setObjectName("pgn-expand-record-submissions");expand_->setText("RECORDS TO SUBMIT");expand_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);expand_->setCheckable(true);expand_->setCursor(Qt::PointingHandCursor);expand_->setStyleSheet("font-weight:600;color:#69efc5");row->addWidget(expand_);
        status_=new QLabel("Record reference pending");status_->setObjectName("pgn-record-submission-status");status_->setWordWrap(true);row->addWidget(status_,1);
        refresh_=new QPushButton("Check records");copy_=new QPushButton("Copy all");copy_->setEnabled(false);
        refresh_->setObjectName("pgn-check-record-submissions");
        copy_->setObjectName("pgn-copy-record-submissions");
        row->addWidget(refresh_);row->addWidget(copy_);auto* cloudygo=new QPushButton("Open Cloudygo");cloudygo->setObjectName("pgn-open-cloudygo");cloudygo->setToolTip("Open the prime-gap record submission site in your browser");connect(cloudygo,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://primegaps.cloudygo.com/"));});row->addWidget(cloudygo);box->addLayout(row);
        text_=new QPlainTextEdit;text_->setObjectName("pgn-admissible-record-text");text_->setReadOnly(true);text_->setLineWrapMode(QPlainTextEdit::NoWrap);text_->setPlaceholderText("Verified improvements will appear here as: GAP  MERIT  LOWER_PRIME");text_->setFixedHeight(96);box->addWidget(text_);
        text_->setToolTip("Only independently verified consecutive-prime gaps which improve the dated reference are offered for copying. Paste into the prime-gap record submission form; acceptance is decided by its maintainers.");
        const auto setExpanded=[this](bool expanded){text_->setVisible(expanded);expand_->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);expand_->setToolTip(expanded?"Collapse records":"Expand records");setProperty("pgn_record_submissions_expanded",expanded);QSettings().setValue("records/expanded",expanded);updateGeometry();};
        const bool expanded=QSettings().value("records/expanded",false).toBool();expand_->setChecked(expanded);setExpanded(expanded);
        connect(expand_,&QToolButton::toggled,this,setExpanded);
        referenceFile_=root_+"/data/testnet/record-reference.json";QDir().mkpath(QFileInfo(referenceFile_).absolutePath());
        if(!QFile::exists(referenceFile_)){QFile bundled(root_+"/data/reference/prime-gap-records.json");if(bundled.open(QIODevice::ReadOnly)&&bundled.size()<=4*1024*1024){QSaveFile copy(referenceFile_);const auto bytes=bundled.readAll();if(copy.open(QIODevice::WriteOnly)&&copy.write(bytes)==bytes.size())copy.commit();}}
        auto* reference=new science::RecordReferenceRefresh(referenceFile_,this);
        reference->onChanged=[this](bool ok,const QString& message){referenceNotice_=ok?QString{}:message;setProperty("pgn_record_reference_update_error",referenceNotice_);if(ok)setProperty("pgn_record_reference_checked_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));refresh();};
        refresh_->setToolTip("Check the official prime-gap list now. Automatic reference updates run hourly; a failed download retains the last valid reference.");
        connect(refresh_,&QPushButton::clicked,this,[this,reference]{refresh(true);reference->refresh(true);});
        connect(copy_,&QPushButton::clicked,this,[this]{if(!text_->toPlainText().isEmpty())QApplication::clipboard()->setText(text_->toPlainText());});
        connect(&work_,&QFutureWatcher<science::RecordSubmissions>::finished,this,[this]{
            const auto result=work_.result();published_=true;
            if(manualBusy_){manualBusy_=false;refresh_->setEnabled(true);setProperty("pgn_record_check_busy",false);}
            if(!result.error.isEmpty()){setStatus("Record check unavailable · "+result.error);setProperty("pgn_record_submission_error",result.error);copy_->setEnabled(false);if(again_){again_=false;QTimer::singleShot(0,this,[this]{refresh();});}return;}
            QJsonArray eligible;for(const auto& record:result.records)eligible.append(QJsonObject{{"gap",qint64(record.gap)},{"lower_prime",record.lower}});if(!eligibilityPublished_||lastEligibility_!=eligible){eligibilityPublished_=true;lastEligibility_=eligible;if(eligibility_)eligibility_(eligible);}
            if(text_->toPlainText()!=result.text)text_->setPlainText(result.text);copy_->setEnabled(!result.text.isEmpty());
            QString caption=result.records.isEmpty()&&!result.pending?QString("No admissible records · reference %1").arg(result.referenceUtc.left(10)):QString("%1 verified candidate(s) · reference %2%3").arg(result.records.size()).arg(result.referenceUtc.left(10),result.pending?QString(" · %1 awaiting verification").arg(result.pending):QString{});
            QStringList sizes;for(const auto& line:result.text.split('\n',Qt::SkipEmptyParts))sizes.append(line.section(' ',0,0));
            expand_->setText(sizes.isEmpty()?QStringLiteral("RECORDS TO SUBMIT"):QString("%1 RECORDS · %2").arg(result.records.size()).arg(sizes.join(", ")));
            status_->setToolTip(result.referenceUrl+"\nCompared against the dated first-known-occurrence list. Gold merit alone is not record eligibility.");
            if(!referenceNotice_.isEmpty()){caption+=" · saved reference";status_->setToolTip(status_->toolTip()+"\n"+referenceNotice_);}
            setStatus(caption);
            setProperty("pgn_record_submission_count",result.records.size());setProperty("pgn_record_submission_reference",result.referenceUtc);setProperty("pgn_record_submission_error",QString{});
            if(again_){again_=false;QTimer::singleShot(0,this,[this]{refresh();});}
        });
        if(ticketMode_)loadCandidates();
        timer_.setInterval(30000);connect(&timer_,&QTimer::timeout,this,[this]{refresh();});timer_.start();QTimer::singleShot(0,this,[this]{refresh();});
    }
    void ticketData(const QJsonObject& packet){
        if(!ticketMode_||packet.value("schema")!="PGN_HORIZON_MINING_VIEW_V1")return;
        const auto data=packet.value("record_pairs");if(!data.isArray()){setStatus("Waiting for ticket record data");copy_->setEnabled(false);return;}
        const auto array=data.toArray();if(array.size()>100){setStatus("Ticket record data exceeds its admitted size");copy_->setEnabled(false);return;}
        const auto fingerprint=QCryptographicHash::hash(QJsonDocument(array).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);if(ticketDataKnown_&&pairsHash_==fingerprint)return;
        bool changed=false;if(!mergeCandidates(array,true,&changed)){setStatus("Record candidate storage unavailable · 1 MiB limit · source ticket preserved");copy_->setEnabled(false);return;}pairsHash_=fingerprint;ticketDataKnown_=true;if(changed||!published_)refresh();
    }
    ~RecordSubmissionView()override{cancelled_->store(true);}
};
}
QWidget* createRecordSubmissionView(const QString& root,QWidget* parent,bool ticketMode){return new RecordSubmissionView(root,parent,ticketMode);}
void bindRecordEligibility(QWidget* widget,std::function<void(const QJsonArray&)> observer){if(auto* view=dynamic_cast<RecordSubmissionView*>(widget))view->observeEligibility(std::move(observer));}
void presentRecordTicketData(QWidget* widget,const QJsonObject& packet){if(auto* view=dynamic_cast<RecordSubmissionView*>(widget))view->ticketData(packet);}
}
