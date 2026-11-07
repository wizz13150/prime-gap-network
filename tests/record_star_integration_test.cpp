// Controlled reference only: these fixtures are not claims of new public records.
#include "science/record_submissions.h"
#include <prime_certificate_builder.h>
#include <certified_gap_builder.h>
#include <certified_gap_encoding.h>
#include <QCryptographicHash>
#include <QPixmap>
#include "ui/record_submission_view.h"
#include "ui/mining_results_view.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc<2||argc>3)return 2;int checks=0;
 const auto need=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
 try{
  QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;need(root.isValid(),"temporary root");
  QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.filePath("settings"));QSettings::setDefaultFormat(QSettings::IniFormat);
  const auto file=root.filePath("data/testnet/record-reference.json");QDir().mkpath(QFileInfo(file).absolutePath());
  QJsonObject starts{{"1","2"},{"2","3"},{"4","7"},{"6","39614081257132168796771975167"}};
  QJsonObject reference{{"schema","prime-gap-record-reference-1"},{"supported_bits",QJsonArray{67,95}},
   {"retrieved_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
   {"source_url","https://example.invalid/controlled-record-test-reference"},{"source_blob_sha1",QString(40,'a')},
   {"minimum_potential_gap_in_domain",6},{"known_starts",starts},{"known_beyond_domain",QJsonArray{}}};
  const auto save=[&]{reference.insert("known_starts",starts);QSaveFile f(file);need(f.open(QIODevice::WriteOnly),"reference open");const auto bytes=QJsonDocument(reference).toJson();need(f.write(bytes)==bytes.size()&&f.commit(),"reference save");need(pgn::science::readRecordReference(file).valid(),"controlled reference valid");};save();
  const pgn::science::SubmissionRecord brian{956,"160696461657079905863","160696461657079906819",{}};
  const auto first=pgn::science::collectRecordSubmissionsFromPairs({brian},file);need(first.error.isEmpty()&&first.records.size()==1,"68-bit exact certificate positive, including interior Euler-Jacobi pseudoprime");
  QFile input(QString::fromLocal8Bit(argv[1]));need(input.open(QIODevice::ReadOnly)&&input.size()<16384,"95-bit candidates input");
  const auto candidates=QJsonDocument::fromJson(input.readAll()).array();pgn::science::SubmissionRecord high;bool found=false;int tried=0;
  for(const auto& value:candidates){const auto p=value.toObject();if(p.value("lower")!="19807040628566084398385988493")continue;pgn::science::SubmissionRecord row{quint32(p.value("gap").toInt()),p.value("lower").toString(),p.value("upper").toString(),{}};++tried;
   const auto r=pgn::science::collectRecordSubmissionsFromPairs({row},file);need(r.error.isEmpty(),"95-bit verifier remains defined");if(r.records.size()==1){high=r.records.first();found=true;break;}}
  need(found,"95-bit supplied exact certificate positive is mandatory, never a pending-only pass");
  using namespace pgn::core;
  const Integer hp(high.lower.toStdString()),hg(high.gap);need(boost::multiprecision::msb(hp)==94,"fixture has exactly95 bits");
  const CertifiedGapLimits proofLimits{{128,32,32},10000};
  const PrimeBuildBudget endpointBudget{proofLimits.endpoint_limits,20000,64};
  const auto pc=buildSuppliedPrimeCertificate(hp,endpointBudget),qc=buildSuppliedPrimeCertificate(hp+hg,endpointBudget);
  need(pc.evidence&&qc.evidence,"actual native endpoint certificate production");
  const auto proof=buildCertifiedGapFromPrimeCertificates(hp,hg,*pc.evidence,*qc.evidence,proofLimits);
  need(proof.evidence&&proof.interior_positions_built==39,"all39 interior positions have individual composite witnesses");
  need(verifyCertifiedGap(*proof.evidence,proofLimits).verdict==CertifiedGapVerdict::VerifiedConsecutiveGap,"independent exact consumer verifies full95-bit gap");
  auto corrupt=*proof.evidence;corrupt.interior.pop_back();need(verifyCertifiedGap(corrupt,proofLimits).verdict!=CertifiedGapVerdict::VerifiedConsecutiveGap,"omitted interior witness rejected");
  corrupt=*proof.evidence;std::swap(corrupt.lower_certificate,corrupt.upper_certificate);need(verifyCertifiedGap(corrupt,proofLimits).verdict!=CertifiedGapVerdict::VerifiedConsecutiveGap,"swapped endpoint certificates rejected");
  corrupt=*proof.evidence;corrupt.interior[1]={CompositeWitnessKind::Divisor,2};need(verifyCertifiedGap(corrupt,proofLimits).verdict!=CertifiedGapVerdict::VerifiedConsecutiveGap,"false divisor rejected");
  const CertifiedGapEncodingLimits encoding{{10002,1048576,32,1},proofLimits};
  const auto bytes=encodeCertifiedGap(*proof.evidence,encoding);need(verifyCertifiedGap(decodeCertifiedGap(bytes,encoding),proofLimits).verdict==CertifiedGapVerdict::VerifiedConsecutiveGap,"canonical serialized proof independently revalidates");
  const QByteArray portable(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size()));const auto proofSha=QCryptographicHash::hash(portable,QCryptographicHash::Sha256).toHex();
  if(argc==3){QDir().mkpath(QString::fromLocal8Bit(argv[2]));QSaveFile output(QDir(QString::fromLocal8Bit(argv[2])).filePath("95-bit-gap40.proof"));need(output.open(QIODevice::WriteOnly)&&output.write(portable)==portable.size()&&output.commit(),"public proof saved");}
  // The first known pair was unresolved by the bounded producer. Saturating
  // the list must remain bounded and must not turn unpaid work into stars.
  const auto hard=candidates.first().toObject();const pgn::science::SubmissionRecord unpaid{quint32(hard.value("gap").toInt()),hard.value("lower").toString(),hard.value("upper").toString(),{}};
  QVector<pgn::science::SubmissionRecord> pressure(1000,unpaid);QElapsedTimer pressureClock;pressureClock.start();const auto held=pgn::science::collectRecordSubmissionsFromPairs(pressure,file);
  need(held.error.isEmpty()&&!held.cancelled&&held.records.isEmpty()&&held.pending==1000,"budget pressure remains pending without fabricated positives");need(pressureClock.elapsed()<2500,"larger-domain aggregate background work bounded");

  const pgn::science::SubmissionRecord falseGap{958,brian.lower,"160696461657079906821",{}};
  auto all=pgn::science::collectRecordSubmissionsFromPairs({brian,high,falseGap},file);need(all.error.isEmpty()&&all.records.size()==(found?2:1)&&all.pending==(found?1:2),"interior prime and unpaid certificate prevent false promotion");
  std::atomic_bool stop{true};const auto cancelled=pgn::science::collectRecordSubmissionsFromPairs({brian,high},file,&stop);need(cancelled.cancelled&&cancelled.records.isEmpty(),"cancellation cannot release partial positives");
  auto malformed=brian;malformed.upper="160696461657079906818";need(!pgn::science::collectRecordSubmissionsFromPairs({malformed},file).error.isEmpty(),"inconsistent pair rejected");
  QJsonArray rows,pairs;for(const auto& r:{brian,high,falseGap}){rows.append(QJsonObject{{"gap",int(r.gap)},{"lower_prime",r.lower},{"upper_prime",r.upper},{"merit",QString::number(r.gap/std::log(r.lower.toDouble()),'f',6)}});pairs.append(QJsonObject{{"gap",int(r.gap)},{"lower_prime",r.lower},{"upper_prime",r.upper}});}
  QJsonObject packet{{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"ticket_id",QString(64,'1')},{"request_id",QString(64,'2')},{"genesis",QString(64,'3')},
   {"start",brian.lower},{"end","39614081257132168796771975167"},{"bits",95},{"minimum_gap",6},{"target_merit",29},
   {"record_pairs",pairs},{"scopes",QJsonObject{{"ticket",QJsonObject{{"statistics",QJsonObject{{"count_total",3},{"statistics_target",29}}},{"best",rows},{"recent",rows}}}}}};
  std::unique_ptr<QWidget> mining(pgn::ui::createMiningResultsView(root.path()));mining->resize(1280,760);mining->show();pgn::ui::presentMiningTicketData(mining.get(),packet);app.processEvents();
  const auto starred=[&](const char* tableName,const pgn::science::SubmissionRecord& row){auto* table=mining->findChild<QTableWidget*>(tableName);need(table!=nullptr,"real result table");for(int n=0;n<table->rowCount();++n)if(auto* item=table->item(n,0))if(item->data(Qt::UserRole).toString()==row.lower+":"+QString::number(row.gap)){
    const bool star=item->text().startsWith(QString::fromUtf8("★ "));
    need(star==item->data(Qt::AccessibleDescriptionRole).toString().contains("Reference improvement",Qt::CaseInsensitive),"record rosette and certificate-gated star disagree");
    if(star)need(!item->icon().isNull(),"verified record rosette missing");return star;
   }throw std::runtime_error("fixture row missing");};
  need(!starred("pgn-results-best",brian)&&!starred("pgn-results-best",high),"no star before exact eligibility delivery");
  int callbacks=0;QJsonArray last;
  const auto observer=[&](const QJsonArray& eligible){++callbacks;last=eligible;pgn::ui::presentMiningRecordEligibility(mining.get(),eligible);};
  std::unique_ptr<QWidget> record(pgn::ui::createRecordSubmissionView(root.path(),nullptr,true));pgn::ui::bindRecordEligibility(record.get(),observer);pgn::ui::presentRecordTicketData(record.get(),packet);
  const auto wait=[&](auto done){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<12000){app.processEvents();QThread::msleep(5);}need(done(),"asynchronous eligibility delivery");};wait([&]{return callbacks>0&&last.size()==(found?2:1);});
  for(const int width:{1060,1280,1920}){mining->resize(width,760);app.processEvents();for(const auto* name:{"pgn-results-best","pgn-results-recent"}){need(starred(name,brian)&&starred(name,high)==found,"stars exactly follow verified eligibility in both tables at all sizes");need(!starred(name,falseGap),"false gap never starred");}if(argc==3)need(mining->grab().save(QDir(QString::fromLocal8Bit(argv[2])).filePath(QString("verified-stars-%1.png").arg(width))),"actual widgets render saved");}
  const int delivered=callbacks;pgn::ui::presentRecordTicketData(record.get(),packet);app.processEvents();need(callbacks==delivered,"identical source packet does not restart or blink eligibility");
  // A later reference now includes these pairs: cold reload must retract stars.
  record.reset();starts.insert(QString::number(brian.gap),brian.lower);starts.insert(QString::number(high.gap),high.lower);save();
  const auto superseded=pgn::science::collectRecordSubmissionsFromPairs({brian,high},file);need(superseded.error.isEmpty()&&superseded.records.isEmpty(),"published/equal reference no longer eligible");
  const int before=callbacks;record.reset(pgn::ui::createRecordSubmissionView(root.path(),nullptr,true));pgn::ui::bindRecordEligibility(record.get(),observer);wait([&]{return callbacks>before&&last.isEmpty();});
  for(const auto* name:{"pgn-results-best","pgn-results-recent"})need(!starred(name,brian)&&!starred(name,high),"stale stars removed after reference reload");
  record.reset();mining.reset();std::cout<<QJsonDocument(QJsonObject{{"PASS",true},{"checks",checks},{"positive_bits",found?QJsonArray{68,95}:QJsonArray{68}},{"native_95_candidates_tried",tried},{"positive_95_certified",found},{"proof_sha256",QString::fromLatin1(proofSha)},{"proof_bytes",portable.size()},{"pressure_candidates",1000},{"fixture_95_lower",high.lower},{"fixture_95_upper",high.upper},{"fixture_95_gap",int(high.gap)},{"scope","Controlled reference; actual certificate gate and asynchronous record-to-Best/Recent delivery.Positive95-bit Pocklington endpoint certificates and all39 interior witnesses independently verified; unpaid candidates remain pending. No public record, network acceptance or exhaustive search claim"}}).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
