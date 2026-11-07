#include "app/horizon_user_paths.h"
#include "ui/archive_browser.h"
#include "ui/bounded_catalog_export.h"
#include "ui/native_archive_view.h"
#include "ui/panel_style.h"
#include "ui/mining_canvas.h"
#include "telemetry.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QSaveFile>
#include <QMessageBox>
#include <QTabWidget>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <memory>
namespace pgn::ui {
static QWidget* createImportedArchiveBrowser(const QString& root,QWidget* canvas){
 auto* page=new QWidget;auto* layout=new QVBoxLayout(page);layout->setContentsMargins(20,18,20,18);
 page->setStyleSheet(dataPanelStyle());
 auto* title=new QLabel("SCIENTIFIC ARCHIVE · EXACT ENDPOINTS / SOURCE PROVENANCE");layout->addWidget(title);
 auto* notice=new QLabel("Source observations are not independent certificates or certified interval coverage. Graph data is loaded explicitly by page; the complete conserved catalogue remains accessible here.");notice->setWordWrap(true);notice->setStyleSheet("color:#ffad4a");layout->addWidget(notice);
 const QString dir=pgn::app::mutableDataRoot(root)+"/import-20261011";QFile index(dir+"/history-pages.json");
 QJsonObject meta;if(index.open(QIODevice::ReadOnly)&&index.size()<=262144)meta=QJsonDocument::fromJson(index.readAll()).object();
 const auto offsets=meta.value("offsets").toArray();const int rows=meta.value("rows").toInt();
 auto* controls=new QHBoxLayout;auto* previous=new QPushButton("Previous");auto* next=new QPushButton("Next");auto* number=new QSpinBox;number->setRange(1,qMax(1,int(offsets.size())));
 auto* plot=new QPushButton("Add this page to Mining graph");auto* provenance=new QPushButton("Open conserved sources and proofs");
 controls->addWidget(previous);controls->addWidget(number);controls->addWidget(next);controls->addWidget(plot);controls->addWidget(provenance);layout->addLayout(controls);
 auto* count=new QLabel;count->setWordWrap(true);layout->addWidget(count);
 auto* table=new QTableWidget;table->setColumnCount(5);table->setHorizontalHeaderLabels({"Left endpoint p","Right endpoint q","Gap q − p","Merit (≈)","Proof / coverage status"});
 table->horizontalHeaderItem(3)->setToolTip("Rounded display of gap / ln(lower prime); exact integer endpoints and gap sizes are retained.");
 table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table->horizontalHeader()->setStretchLastSection(true);table->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(table,1);
 auto records=std::make_shared<QList<QJsonObject>>();
 auto load=[=](int pageNumber){
  records->clear();table->setRowCount(0);const int i=pageNumber-1;
  QFile file(dir+"/history.ndjson");
  bool ok=false;const qint64 offset=i>=0&&i<offsets.size()?offsets[i].toString().toLongLong(&ok):-1;
  if(!ok||!file.open(QIODevice::ReadOnly)||file.size()!=meta.value("bytes").toInteger()||!file.seek(offset)){count->setText("Archive index unavailable or source identity changed; no inferred data");return;}
  table->setUpdatesEnabled(false);
  for(int n=0;n<500&&!file.atEnd();++n){auto o=QJsonDocument::fromJson(file.readLine(16384)).object();if(o.isEmpty())break;records->append(o);table->insertRow(n);
   const QStringList values{o.value("p").toString(),o.value("q").toString(),o.value("gap").toString(),QString::number(o.value("merit_visual").toDouble(),'g',12),"SOURCE OBSERVATION · independent proof not promoted · coverage unknown"};
   for(int c=0;c<values.size();++c)table->setItem(n,c,new QTableWidgetItem(values[c]));}
  table->setUpdatesEnabled(true);count->setText(QStringLiteral("%1 conserved pairs · page %2/%3 · exact rows %4–%5 · no downsampling").arg(rows).arg(pageNumber).arg(offsets.size()).arg(i*500+1).arg(i*500+records->size()));
 };
 QObject::connect(number,qOverload<int>(&QSpinBox::valueChanged),page,load);
 QObject::connect(previous,&QPushButton::clicked,page,[=]{number->setValue(number->value()-1);});
 QObject::connect(next,&QPushButton::clicked,page,[=]{number->setValue(number->value()+1);});
 QObject::connect(plot,&QPushButton::clicked,page,[=]{beginHistoryBatch(canvas);for(const auto& o:*records){FrontierGapEvent e;e.runId="historical-source";e.domainId="pgn-imported-history";e.endpointLeft=o.value("p").toString();e.endpointRight=o.value("q").toString();e.gap=o.value("gap").toString();e.merit=QString::number(o.value("merit_visual").toDouble(),'g',17);e.candidateId=e.endpointLeft+":"+e.endpointRight;e.proofState="prp_pending";presentGap(canvas,e);}endHistoryBatch(canvas);});
 QObject::connect(provenance,&QPushButton::clicked,page,[=]{QDesktopServices::openUrl(QUrl::fromLocalFile(dir));});
 auto* actions=new QHBoxLayout;
 for(const QString& action:QStringList{"Independent certificates","Export gaps","Export coverage","Compare record references"}){
  auto* button=new QPushButton(action);actions->addWidget(button);
  QObject::connect(button,&QPushButton::clicked,page,[=]{
   if(action=="Independent certificates"){QDesktopServices::openUrl(QUrl::fromLocalFile(dir+"/archive/evidence"));return;}
   if(action=="Export gaps"){
    const QString destination=QFileDialog::getSaveFileName(page,"Export complete source catalogue",{},"Newline-delimited JSON (*.ndjson)");if(destination.isEmpty())return;
    button->setEnabled(false);count->setText("Exporting the complete catalogue · files are limited to 64 MiB each…");
    auto* watcher=new QFutureWatcher<CatalogueExportResult>(page);
    const auto cancellation=std::make_shared<std::atomic_bool>(false);
    const auto closeConnection=QObject::connect(page,&QObject::destroyed,[cancellation]{cancellation->store(true);});
    QObject::connect(watcher,&QFutureWatcher<CatalogueExportResult>::finished,page,[=]{
     const auto result=watcher->result();QObject::disconnect(closeConnection);watcher->deleteLater();button->setEnabled(true);
     if(!result.ok){count->setText("Export failed · conserved source unchanged");QMessageBox::warning(page,action,result.error);return;}
     count->setText(QString("Complete export saved · %1 file(s) · %2").arg(result.parts).arg(result.destination));
    });
    const auto source=dir+"/history.ndjson";const auto expected=meta.value("bytes").toInteger(-1);
    watcher->setFuture(QtConcurrent::run([source,destination,expected,cancellation]{return exportBoundedCatalogue(source,destination,expected,64*1024*1024,cancellation);}));return;
   }
   if(action=="Export coverage"){
    const QString destination=QFileDialog::getSaveFileName(page,"Export explicit coverage status",{},"JSON (*.json)");if(destination.isEmpty())return;
    const QJsonObject status{{"schema","pgn-imported-coverage-status-1"},{"source_pairs",rows},{"coverage_status","UNKNOWN"},{"complete_interval",false},{"independent_proof_promoted",false},{"reason","An endpoint catalogue is not a certificate that an entire interval was searched"}};
    QSaveFile output(destination);if(!output.open(QIODevice::WriteOnly)||output.write(QJsonDocument(status).toJson())<0||!output.commit())QMessageBox::warning(page,action,"Coverage export not committed");return;
   }
   QMessageBox::information(page,action,"Record-reference comparison is not connected yet. No scientific or mining state was changed.");
  });
 }
 layout->addLayout(actions);
 load(1);return page;
}
QWidget* createArchiveBrowser(const QString& root,QWidget* canvas){
 auto* tabs=new QTabWidget;
 tabs->addTab(createNativeArchiveView(root,tabs),"Native GPU observations");
 tabs->addTab(createImportedArchiveBrowser(root,canvas),"Conserved prototype sources");
 return tabs;
}
}
