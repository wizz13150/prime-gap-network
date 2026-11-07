#include "ui/bounded_catalog_export.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#include <stdexcept>
#include <cstdio>

using namespace pgn::ui;
static int checks{};
static void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
static QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Read test file");return f.readAll();}
static void write(const QString& path,const QByteArray& data){QFile f(path);require(f.open(QIODevice::WriteOnly),"Write test file");require(f.write(data)==data.size(),"Write all test bytes");}
static QByteArray digest(const QByteArray& data){return QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();}

int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  QTemporaryDir temp;require(temp.isValid(),"Temporary directory");const auto root=temp.path();
  const auto source=root+"/history.ndjson",destination=root+"/export.ndjson";
  for(const auto& small:QList<QByteArray>{QByteArray{},QByteArray("{\"n\":1}\n"),QByteArray(1024,'x')}){
   write(source,small);write(destination,"old destination");const auto result=exportBoundedCatalogue(source,destination,small.size(),1024);
   require(result.ok&&result.parts==1&&result.destination==destination,"Small atomic export");require(read(destination)==small,"Small export exact bytes");require(read(source)==small,"Source unchanged");
  }
  QByteArray complete;
  for(int n=0;n<127;++n)complete+="{\"id\":"+QByteArray::number(n)+",\"value\":\""+QByteArray(70,'a'+n%20)+"\"}\r\n";
  complete+="{\"id\":127}";write(source,complete);write(destination,"previous file retained");
  auto result=exportBoundedCatalogue(source,destination,complete.size(),1024);require(result.ok&&result.parts>1,"Large multipart export");
  require(read(destination)=="previous file retained","Large bundle never replaces prior file");
  const auto manifest=QJsonDocument::fromJson(read(result.destination+"/manifest.json")).object();
  require(manifest.value("schema")=="PGN_EXACT_CATALOGUE_EXPORT_V1","Manifest schema");
  require(manifest.value("bytes").toString().toLongLong()==complete.size(),"Manifest exact byte count");
  require(manifest.value("sha256").toString().toLatin1()==digest(complete),"Whole catalogue hash");
  QByteArray restored;const auto parts=manifest.value("parts").toArray();require(parts.size()==result.parts,"Part count");
  for(int n=0;n<parts.size();++n){
   const auto entry=parts[n].toObject();const auto data=read(result.destination+"/"+entry.value("file").toString());
   require(!data.isEmpty()&&data.size()<=1024,"Every part respects bound");
   require(data.size()==entry.value("bytes").toString().toLongLong(),"Part byte count");
   require(digest(data)==entry.value("sha256").toString().toLatin1(),"Part hash");
   require(n==parts.size()-1||data.endsWith('\n'),"Rows never split across parts");restored+=data;
  }
  require(restored==complete&&read(source)==complete,"Byte-exact reconstruction including CRLF and final unterminated line");
  require(!exportBoundedCatalogue(source,destination,complete.size(),1024).ok,"Existing bundle refused");
  const auto oldManifest=read(result.destination+"/manifest.json");
  require(!exportBoundedCatalogue(source,source,complete.size()).ok,"Self-overwrite refused");
  require(!exportBoundedCatalogue(source,root+"/wrong.ndjson",complete.size()+1,1024).ok,"Changed source size refused");
  require(!QFileInfo::exists(root+"/wrong.ndjson.parts"),"Failed export not published");
  require(!exportBoundedCatalogue(source,root+"/no-parent/out",complete.size(),1024).ok,"Missing destination parent refused");
  write(source,QByteArray(1025,'x')+'\n');
  require(!exportBoundedCatalogue(source,root+"/oversized.ndjson",1026,1024).ok,"Oversized row refused");
  require(!QFileInfo::exists(root+"/oversized.ndjson.parts"),"Oversized export not published");
  QByteArray manyParts;
  for(int n=0;n<1025;++n)manyParts+=QByteArray(1023,'a')+'\n';
  write(source,manyParts);write(root+"/too-many.ndjson","keep existing file");
  require(!exportBoundedCatalogue(source,root+"/too-many.ndjson",manyParts.size(),1024).ok,"1024-part budget enforced");
  require(read(source)==manyParts&&read(root+"/too-many.ndjson")=="keep existing file","Capacity failure conserves all original bytes");
  require(!QFileInfo::exists(root+"/too-many.ndjson.parts"),"Capacity failure never publishes incomplete bundle");
  const auto cancelled=std::make_shared<std::atomic_bool>(true);
  require(!exportBoundedCatalogue(source,root+"/cancelled.ndjson",manyParts.size(),1024,cancelled).ok,"Cancelled export refused before writing");
  require(!QFileInfo::exists(root+"/cancelled.ndjson")&&!QFileInfo::exists(root+"/cancelled.ndjson.parts"),"Cancelled export never published");
  require(QDir(root).entryList({".pgn-catalogue-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty(),"Failure staging removed");
  require(read(result.destination+"/manifest.json")==oldManifest,"Prior bundle unchanged after failures");
  for(qint64 budget:{qint64(0),qint64(1023),qint64(64*1024*1024)+1})require(!exportBoundedCatalogue(source,root+"/bad",1026,budget).ok,"Invalid part budget refused");
  std::printf("PASS %d bounded export checks; no user catalogue exported\n",checks);
  return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
