#include "mining/source_stop_checkpoint.h"
#include <QCoreApplication>
#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks=0;
using Words=std::array<quint64,7>; // low,high,length,positions,tests,gaps,batches
const Words standard{1000,10,10000,3000,9007199254740993ULL,0,4};
const QString run(32,'a');
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
QString decimal(const boost::multiprecision::cpp_int& value){return QString::fromStdString(value.convert_to<std::string>());}
QByteArray binary(const Words& words){
 QByteArray bytes("PGNCG001",8);for(const auto value:words)for(unsigned i=0;i<8;++i)bytes.append(char((value>>(8*i))&255));return bytes;
}
QByteArray counters(const Words& words,quint64 time=9007199254740995ULL){
 return "[pgn-counters] time_ns="+QByteArray::number(time)+" positions="+QByteArray::number(words[3])+" gaps="+QByteArray::number(words[5])+
  " tests="+QByteArray::number(words[4])+" batches="+QByteArray::number(words[6])+" processed_gaps="+QByteArray::number(words[5])+"\r\n";
}
SourceStopCheckpointEvidence fixture(const Words& words=standard,quint64 time=9007199254740995ULL){
 const boost::multiprecision::cpp_int start=(boost::multiprecision::cpp_int(words[1])<<64)+words[0];
 SourceStopCheckpointEvidence evidence;
 evidence.expectedRun=run;evidence.owned=false;evidence.checkpointBytes=binary(words);evidence.checkpointLabel="C:/PGN/runtime/current-job/checkpoint";
 evidence.manifest={{"schema","pgn-native-process-1"},{"run",run},{"start",decimal(start)},{"end",decimal(start+words[2])}};
 evidence.lastExit={{"run",run},{"exit_code",0}};
 evidence.sourceTail=counters(words,time)+"[phase0-gpu] stopped positions="+QByteArray::number(words[3])+" checkpoint="+evidence.checkpointLabel.toUtf8()+"\r\n";
 return evidence;
}
void rejected(const SourceStopCheckpointEvidence& evidence,const char* message){
 const auto result=validateSourceStopCheckpoint(evidence);
 require(result.status==SourceStopCheckpointStatus::Unavailable&&!result.resume&&!result.error.isEmpty(),message);
}
void checkReady(const SourceStopCheckpointEvidence& evidence,const Words& words,quint64 time){
 const auto result=validateSourceStopCheckpoint(evidence);
 require(result.status==SourceStopCheckpointStatus::Ready&&result.resume&&result.error.isEmpty(),"exact byte64 stop tuple ready");
 const auto& resume=*result.resume;const boost::multiprecision::cpp_int start=(boost::multiprecision::cpp_int(words[1])<<64)+words[0];
 require(resume.run==run&&resume.start==decimal(start)&&resume.end==decimal(start+words[2]),"full source identity/range matches independent cpp_int oracle");
 require(resume.length==QString::number(words[2])&&resume.positions==QString::number(words[3])&&resume.nextStart==decimal(start+words[3])&&
  resume.remaining==decimal(boost::multiprecision::cpp_int(words[2])-words[3]),"next/remaining exact, never double or overflowing signed arithmetic");
 require(resume.tests==QString::number(words[4])&&resume.gaps==QString::number(words[5])&&resume.batches==QString::number(words[6])&&resume.counterTimeNs==QString::number(time),"all UInt64 source counters remain exact strings");
 require(resume.checkpointLabel==evidence.checkpointLabel&&resume.checkpointSha256.size()==64,"exact caller label and byte fingerprint retained, not filesystem authority");
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  const auto good=fixture();require(good.checkpointBytes.size()==64,"actual checkpoint format is64 bytes");
  checkReady(good,standard,9007199254740995ULL);
  require(validateSourceStopCheckpoint(good).resume->nextStart=="184467440737095520160","explicit >2^53 next-start decimal witness");
  {
   const Words words{1000,1084202172,10000,3000,9007199254740993ULL,0,4};auto scout=fixture(words);
   scout.dialect=SourceCheckpointDialect::ScoutGapBound;scout.expectedGapMinimum=1332;
   scout.checkpointBytes.replace(0,4,"PGNS");for(unsigned i=0;i<4;++i)scout.checkpointBytes[4+i]=char((1332U>>(8*i))&255);
   scout.sourceTail.replace("processed_gaps=0","processed_gaps=0 emitted_gaps=0");
   checkReady(scout,words,9007199254740995ULL);const auto result=validateSourceStopCheckpoint(scout);require(result.resume->gapMinimum==1332,"Scout threshold retained after exact95bit decode");
   auto bad=scout;bad.expectedGapMinimum=1334;rejected(bad,"Different checkpoint threshold rejected");
   bad=scout;bad.expectedGapMinimum.reset();rejected(bad,"No inferred threshold authority from raw bytes");
   bad=scout;bad.sourceTail.replace("emitted_gaps=0","emitted_gaps=1");rejected(bad,"Incomplete emission cannot conserve Scout checkpoint");
   bad=scout;bad.dialect=SourceCheckpointDialect::LegacyHigh10;rejected(bad,"Scout bytes do not relabel a legacy context");
  }
  for(qsizetype size=0;size<64;++size){auto bad=good;bad.checkpointBytes.truncate(size);rejected(bad,"truncated byte layout rejected without partial value");}
  {auto bad=good;bad.checkpointBytes.append('\0');rejected(bad,"trailing byte rejected, no concatenated checkpoint accepted");}
  for(qsizetype byte=0;byte<64;++byte)for(unsigned bit=0;bit<8;++bit){auto bad=good;bad.checkpointBytes[byte]=char(uchar(bad.checkpointBytes[byte])^(1U<<bit));rejected(bad,"every single-bit corrupt byte refuses against unchanged source tuple");}
  for(int which=0;which<16;++which){auto bad=good;switch(which){
   case 0:bad.expectedRun.clear();break;
   case 1:bad.expectedRun=QString(32,'A');break;
   case 2:bad.manifest["run"]=QString(32,'b');break;
   case 3:bad.manifest["schema"]="other";break;
   case 4:bad.manifest["start"]="184467440737095517161";break;
   case 5:bad.manifest["end"]="184467440737095527161";break;
   case 6:bad.manifest["start"]=0;break;
   case 7:bad.owned.reset();break;
   case 8:bad.owned=true;break;
   case 9:bad.lastExit["run"]=QString(32,'b');break;
   case 10:bad.lastExit.remove("exit_code");break;
   case 11:bad.lastExit["exit_code"]=1;break;
   case 12:bad.lastExit["exit_code"]="0";break;
   case 13:bad.lastExit["exit_code"]=false;break;
   case 14:bad.lastExit["exit_code"]=0.5;break;
   case 15:bad.lastExit["exit_code"]=QJsonValue::Null;break;
  }rejected(bad,"run/range/ownership/exit absence and type failures are explicit");}
  for(const auto fields:{Words{0,10,100,0,0,0,0},Words{1000,11,10000,3000,1,0,1},Words{1000,10,0,0,0,0,0},
      Words{std::numeric_limits<quint64>::max(),10,1,0,0,0,0},Words{1000,10,10000,10000,1,0,1},Words{1000,10,10000,std::numeric_limits<quint64>::max(),1,0,1}})
   rejected(fixture(fields),"current producer profile/floor/window/fullcompletion domains rejected before addition");
  const auto maximum=std::numeric_limits<quint64>::max();
  for(const auto words:{Words{1000,10,10000,0,0,0,0},Words{1000,10,10000,9999,maximum,maximum,maximum},
      Words{0,10,maximum,maximum-1,maximum,0,1},Words{maximum-100,10,100,99,maximum,maximum,maximum}})
   checkReady(fixture(words,maximum),words,maximum);
  // Independent exact oracle across public synthetic UInt64 tuples, not GPU
  // execution or a claim that these workloads fit physical GPU resources.
  for(quint64 n=1;n<=64;++n){const Words words{1000+n*101,10,100000+n*7,n*119,n*9007199254740993ULL,n*5,n};checkReady(fixture(words,n),words,n);}
  for(int which=0;which<20;++which){auto bad=good;switch(which){
   case 0:bad.sourceTail.clear();break;
   case 1:bad.sourceTail.chop(1);break;
   case 2:bad.sourceTail=bad.sourceTail.left(bad.sourceTail.indexOf("[phase0-gpu]"));break;
   case 3:bad.sourceTail=bad.sourceTail.mid(bad.sourceTail.indexOf("[phase0-gpu]"));break;
   case 4:bad.sourceTail.replace("positions=3000","positions=3001");break;
   case 5:bad.sourceTail.replace("tests=9007199254740993","tests=9007199254740994");break;
   case 6:bad.sourceTail.replace("batches=4","batches=5");break;
   case 7:bad.sourceTail.replace("processed_gaps=0","processed_gaps=1");break;
   case 8:bad.sourceTail.replace("time_ns=9007199254740995","time_ns=18446744073709551616");break;
   case 9:bad.sourceTail.replace("positions=3000","positions=3e3");break;
   case 10:bad.sourceTail.replace("tests=9007199254740993","tests=09007199254740993");break;
   case 11:bad.sourceTail.replace(" processed_gaps=0","");break;
   case 12:bad.sourceTail+=bad.sourceTail.mid(bad.sourceTail.indexOf("[phase0-gpu]"));break;
   case 13:bad.sourceTail+="[phase0-gpu] --- end ---\n";break;
   case 14:bad.sourceTail+="[phase0-gpu] gaps reported=0 verification_failures=0 records_new=0\n";break;
   case 15:bad.sourceTail+=counters(standard);break;
   case 16:bad.sourceTail+="unclosed trailing bytes";break;
   case 17:bad.sourceTail.insert(1,'\0');break;
   case 18:bad.sourceTail=QByteArray(65537,'x');bad.sourceTail.back()='\n';break;
   case 19:bad.sourceTail.insert(1,char(0xff));break;
  }rejected(bad,"missing/corrupt/duplicate/full/fragmented/counter overflow evidence held");}
  for(const auto label:{QString{},QString("other-checkpoint"),QString("bad\nlabel"),QString(1,QChar(0))}){auto bad=good;bad.checkpointLabel=label;rejected(bad,"missing or conflicting label never authenticates an arbitrary blob");}
  {auto bad=good;auto earlier=standard;earlier[4]=std::numeric_limits<quint64>::max();bad.sourceTail=counters(earlier,1)+bad.sourceTail;rejected(bad,"wrapped/regressed cumulative tests held");}
  {auto bad=good;auto earlier=standard;earlier[3]++;bad.sourceTail=counters(earlier,1)+bad.sourceTail;rejected(bad,"regressed positions held");}
  {auto valid=good;valid.sourceTail="middle-of-a-prior-unrelated-row\n"+valid.sourceTail;checkReady(valid,standard,9007199254740995ULL);}
  {auto valid=good;valid.sourceTail.replace("\r\n","\n");checkReady(valid,standard,9007199254740995ULL);}
  std::cout<<"Source stop checkpoint: "<<checks<<" checks / 0 failures; PURE QtCore byte64/counters/cpp_int fixtures, NO files/process/GPU/network/UI/durability proof\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Source stop checkpoint failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
