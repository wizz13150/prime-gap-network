#include "app/horizon_startup.h"
#include <QCoreApplication>
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);unsigned checks{};
 const auto check=[&](bool value){++checks;if(!value){std::cerr<<"FAIL startup contract "<<checks<<'\n';std::exit(1);}};
 const QStringList bare{"Horizon.exe"};
 const auto absent=pgn::app::resolveHorizonStartup(bare,std::nullopt);check(absent.testnet&&!absent.developerLegacy&&absent.profileState=="setup-required"&&!absent.diagnostic.isEmpty());
 for(const QByteArray bytes:{QByteArray{},QByteArray{"{"},QByteArray{"[]"},QByteArray{"{}"},QByteArray(32769,'x'),QByteArray{"{\"schema\":\"wrong\",\"deployment\":{\"chain\":\"test\"}}"}}){
  const auto value=pgn::app::resolveHorizonStartup(bare,bytes);check(value.testnet&&!value.developerLegacy&&value.profileState=="invalid-core-template"&&!value.diagnostic.isEmpty());
 }
 for(const QByteArray chain:{QByteArray{"test"},QByteArray{"main"}}){
  const auto value=pgn::app::resolveHorizonStartup(bare,QByteArray{"{\"schema\":\"PGN_CORE_BUNDLE_V1\",\"deployment\":{\"chain\":\""}+chain+"\"}}");
  check(value.testnet&&!value.developerLegacy);check(chain=="test"?value.diagnostic.isEmpty():!value.diagnostic.isEmpty());
 }
 const auto explicitTest=pgn::app::resolveHorizonStartup({"Horizon.exe","--testnet"},std::nullopt);check(explicitTest.testnet&&!explicitTest.developerLegacy);
 const auto oldViewer=pgn::app::resolveHorizonStartup({"Horizon.exe","--resident-viewer"},std::nullopt);check(oldViewer.testnet&&!oldViewer.developerLegacy);
 const auto legacy=pgn::app::resolveHorizonStartup({"Horizon.exe","--developer-legacy-prototype"},std::nullopt);check(!legacy.testnet&&legacy.developerLegacy);
 const auto conflict=pgn::app::resolveHorizonStartup({"Horizon.exe","--developer-legacy-prototype","--testnet"},std::nullopt);check(conflict.testnet&&!conflict.developerLegacy);
 QTemporaryDir temporary;check(temporary.isValid());const auto root=temporary.path();
 const auto write=[](const QString& path,const QByteArray& data){QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())std::abort();};
 const QByteArray test="{\"schema\":\"PGN_CORE_BUNDLE_V1\",\"deployment\":{\"product\":\"PGN Core\",\"chain\":\"test\"}}";
 write(root+"/Core/core-template.json",test);QDir().mkpath(root+"/bin");QDir().mkpath(root+"/build-release");
 const QStringList layoutBare{"Horizon"};
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root)==root);
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/bin")==root);
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/build-release")==root+"/build-release");
 const QString explicitRoot=root+"/explicit-product-root";
 check(pgn::app::resolveHorizonApplicationRoot({"Horizon",explicitRoot,"--testnet"},root+"/bin")==explicitRoot);
 write(root+"/bin/Core/core-template.json","{");
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/bin")==root+"/bin");
 QFile::remove(root+"/bin/Core/core-template.json");
 write(root+"/Core/core-template.json","{\"schema\":\"PGN_CORE_BUNDLE_V1\",\"deployment\":{\"product\":\"PGN Core\",\"chain\":\"main\"}}");
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/bin")==root);
 write(root+"/Core/core-template.json","{\"deployment\":{\"chain\":\"test\"}}");
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/bin")==root+"/bin");
 write(root+"/Core/core-template.json",QByteArray(32769,'x'));
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/bin")==root+"/bin");
 QFile::remove(root+"/Core/core-template.json");
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root)==root);
 check(pgn::app::resolveHorizonApplicationRoot(layoutBare,root+"/bin")==root+"/bin");
 std::cout<<"PASS "<<checks<<" startup/profile+code-layout checks; original15 cases retained; no backend/database discovery\n";
}
