#include "mining/source_history.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName("pgn-import-source-history");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native lossless source-journal importer. Writes only the explicitly selected SQLite database; no GPU, wallet, network or Node execution. The journal must not be rewritten concurrently. Reopening verifies all committed prefix bytes (linear I/O, bounded memory). Nonzero legacy cursors without native digests are held for an explicit audit, never silently adopted.");
    parser.addHelpOption();
    parser.addOption({"database","Explicit destination SQLite file (created if absent).","path"});
    parser.addOption({"journal","Conserved <run>.stdout journal, opened read-only.","path"});
    parser.addOption({"run","Exact 32-character lowercase native run identity.","id"});
    parser.process(app);
    const auto database=parser.value("database"),journal=parser.value("journal"),run=parser.value("run");
    if(database.isEmpty()||journal.isEmpty()||!QRegularExpression("^[0-9a-f]{32}$").match(run).hasMatch()||!parser.positionalArguments().isEmpty()||
       QFileInfo(journal).fileName()!=run+".stdout"||!QFileInfo(journal).isFile()||
       QFileInfo(database).absoluteFilePath()==QFileInfo(journal).absoluteFilePath()||
       (QFileInfo(database).exists()&&QFileInfo(database).canonicalFilePath()==QFileInfo(journal).canonicalFilePath())){
        std::cerr<<"Explicit database, existing <run>.stdout journal and matching run required; nothing imported.\n";return 2;
    }
    try{
        pgn::mining::SourceHistory store(database);
        const auto result=store.importJournal(run,journal);
        const QJsonObject receipt{{"schema","pgn-native-source-import-1"},{"run",run},
            {"offset",QString::number(result.offset)},{"lines",QString::number(result.lines)},
            {"admitted",QString::number(result.admitted)},{"inserted",QString::number(result.inserted)},
            {"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"}};
        std::cout<<QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Source import held: "<<error.what()<<". Committed cursor retained; original journal unchanged.\n";return 1;}
}
