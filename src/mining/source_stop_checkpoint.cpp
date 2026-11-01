#include "mining/source_stop_checkpoint.h"
#include "booking/decimal_range.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pgn::mining {
namespace {
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
quint64 word(const QByteArray& bytes,qsizetype offset){
 quint64 value=0;for(unsigned i=0;i<8;++i)value|=quint64(uchar(bytes[offset+i]))<<(8*i);return value;
}
quint64 unsignedToken(const QString& text){
 if(text.isEmpty()||(text.size()>1&&text[0]=='0'))fail("Noncanonical source counter");
 quint64 value=0;for(const auto c:text){
  if(c<'0'||c>'9')fail("Source counter is not ASCII unsigned decimal");const auto digit=unsigned(c.unicode()-'0');
  if(value>(std::numeric_limits<quint64>::max()-digit)/10)fail("Source counter exceeds UInt64");value=value*10+digit;
 }return value;
}
struct Counters {quint64 time,positions,gaps,tests,batches;};
}
SourceStopCheckpointResult validateSourceStopCheckpoint(const SourceStopCheckpointEvidence& evidence){
 try{
  constexpr qsizetype TailLimit=65536;
  static const QRegularExpression runPattern("^[0-9a-f]{32}$");
  if(!runPattern.match(evidence.expectedRun).hasMatch()||evidence.manifest.value("schema")!="pgn-native-process-1"||
     evidence.manifest.value("run")!=evidence.expectedRun)fail("Expected source manifest run/schema mismatch");
  if(!evidence.owned.has_value()||*evidence.owned)fail("Stopped ownership observation is absent or still owned");
  const auto exitCode=evidence.lastExit.value("exit_code");
  if(evidence.lastExit.value("run")!=evidence.expectedRun||!exitCode.isDouble()||exitCode.toInteger(-1)!=0)
   fail("Exact source run successful exit is absent");
  const bool scout=evidence.dialect==SourceCheckpointDialect::ScoutGapBound;
  if(evidence.checkpointBytes.size()!=64)fail("Checkpoint requires exact64byte layout");
  if(scout){
   if(evidence.checkpointBytes.left(4)!="PGNS"||!evidence.expectedGapMinimum||*evidence.expectedGapMinimum<954||*evidence.expectedGapMinimum>2147483647U)fail("Explicit supported Scout threshold and PGNS checkpoint required");
   quint32 gap=0;for(unsigned i=0;i<4;++i)gap|=quint32(uchar(evidence.checkpointBytes[4+i]))<<(8*i);
   if(gap!=*evidence.expectedGapMinimum)fail("Scout checkpoint threshold differs from the admitted job");
  }else if(evidence.checkpointBytes.left(8)!="PGNCG001")fail("Checkpoint requires legacy PGNCG001 layout");
  const auto low=word(evidence.checkpointBytes,8),high=word(evidence.checkpointBytes,16),length=word(evidence.checkpointBytes,24);
  const auto positions=word(evidence.checkpointBytes,32),tests=word(evidence.checkpointBytes,40),gaps=word(evidence.checkpointBytes,48),batches=word(evidence.checkpointBytes,56);
  // Match the CURRENT actual CUDA host reader/start constraints, not a new
  // financial/network policy or a claim that other engine profiles are ready.
  if((scout?(high<4||high>2147483647ULL):high!=10)||!length||length>std::numeric_limits<quint64>::max()-low)fail("Checkpoint outside declared source domain/single-low64 profile");
  if(positions>=length)fail("Completed or out-of-range checkpoint is not resumable");
  const auto resumeLow=low+positions;const auto remainder=(resumeLow%30+(high%30)*16)%30;
  if(resumeLow<remainder)fail("Source resume floor crosses its low64 window");
  QString start=QString::number(high);for(int i=0;i<64;++i)start=pgn::booking::addUnsigned(start,start);
  start=pgn::booking::addUnsigned(start,QString::number(low));
  const auto end=pgn::booking::addUnsigned(start,QString::number(length));
  if(!evidence.manifest.value("start").isString()||!evidence.manifest.value("end").isString()||
     evidence.manifest.value("start").toString()!=start||evidence.manifest.value("end").toString()!=end)
   fail("Checkpoint range differs from source manifest");
  if(evidence.checkpointLabel.isEmpty()||evidence.checkpointLabel.contains(QChar(0))||evidence.checkpointLabel.contains('\r')||evidence.checkpointLabel.contains('\n'))
   fail("Explicit checkpoint label is absent/ambiguous");
  const auto& tail=evidence.sourceTail;
  if(tail.isEmpty()||tail.size()>TailLimit||!tail.endsWith('\n')||tail.contains('\0')||QString::fromUtf8(tail).toUtf8()!=tail)
   fail("Bounded source tail is absent/invalid/fragmented");
  static const QRegularExpression counter("^\\[pgn-counters\\] time_ns=([0-9]+) positions=([0-9]+) gaps=([0-9]+) tests=([0-9]+) batches=([0-9]+) processed_gaps=([0-9]+)$");
  static const QRegularExpression scoutCounter("^\\[pgn-counters\\] time_ns=([0-9]+) positions=([0-9]+) gaps=([0-9]+) tests=([0-9]+) batches=([0-9]+) processed_gaps=([0-9]+) emitted_gaps=([0-9]+)$");
  static const QRegularExpression stopped("^\\[phase0-gpu\\] stopped positions=([0-9]+) checkpoint=(.+)$");
  std::optional<Counters> last;qsizetype lastCounter=-1,stopAt=-1,lastNonempty=-1;int stops=0;
  for(qsizetype at=0;at<tail.size();){
   const auto newline=tail.indexOf('\n',at);auto line=tail.mid(at,newline-at);if(line.endsWith('\r'))line.chop(1);
   const auto lineAt=at;at=newline+1;if(line.isEmpty())continue;lastNonempty=lineAt;
   if(line=="[phase0-gpu] --- end ---"||line.startsWith("[phase0-gpu] gaps reported="))fail("Full completion is not a stop checkpoint receipt");
   if(line.startsWith("[pgn-counters] ")){
    const auto match=(scout?scoutCounter:counter).match(QString::fromUtf8(line));if(!match.hasMatch())fail("Source counter row is malformed/fragmented");
    const Counters current{unsignedToken(match.captured(1)),unsignedToken(match.captured(2)),unsignedToken(match.captured(3)),unsignedToken(match.captured(4)),unsignedToken(match.captured(5))};
    if(unsignedToken(match.captured(6))!=current.gaps||(scout&&unsignedToken(match.captured(7))!=current.gaps))fail("Processed/emitted gaps differ from committed source gaps");
    if(last&&(current.time<last->time||current.positions<last->positions||current.gaps<last->gaps||current.tests<last->tests||current.batches<last->batches))
     fail("Cumulative source counters regressed or wrapped");
    last=current;lastCounter=lineAt;
   }else if(line.startsWith("[phase0-gpu] stopped ")){
    const auto match=stopped.match(QString::fromUtf8(line));
    if(!match.hasMatch()||unsignedToken(match.captured(1))!=positions||match.captured(2)!=evidence.checkpointLabel)
     fail("Stopped footer position/checkpoint label mismatch");
    ++stops;stopAt=lineAt;
   }
  }
  if(stops!=1||stopAt!=lastNonempty||!last||lastCounter>=stopAt)fail("One final stopped footer and preceding real counter required");
  if(last->positions!=positions||last->tests!=tests||last->gaps!=gaps||last->batches!=batches)fail("Checkpoint and final source counter tuple differ");
  SourceCheckpointResume resume;
  resume.run=evidence.expectedRun;resume.start=start;resume.end=end;resume.length=QString::number(length);resume.positions=QString::number(positions);
  resume.nextStart=pgn::booking::addUnsigned(start,resume.positions);resume.remaining=QString::number(length-positions);
  resume.tests=QString::number(tests);resume.gaps=QString::number(gaps);resume.batches=QString::number(batches);resume.counterTimeNs=QString::number(last->time);
  resume.dialect=evidence.dialect;resume.gapMinimum=scout?evidence.expectedGapMinimum:std::nullopt;
  resume.checkpointLabel=evidence.checkpointLabel;resume.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(evidence.checkpointBytes,QCryptographicHash::Sha256).toHex());
  return {SourceStopCheckpointStatus::Ready,{},std::move(resume)};
 }catch(const std::exception& e){return {SourceStopCheckpointStatus::Unavailable,QString::fromUtf8(e.what()).left(256),{}};}
}
}
