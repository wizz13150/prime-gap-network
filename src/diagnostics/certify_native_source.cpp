// Finite diagnostic entry point for the SAME in-process library, not a daemon.
#include "verification/source_proof_page.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <limits>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCommandLineParser args;args.setApplicationDescription("Finite local certificate page for already supplied native source pairs; NO mining/network/coverage credit");args.addHelpOption();
    args.addOptions({{QStringLiteral("source"),"Existing READONLY native history SQLite", "path"},{QStringLiteral("journal"),"Separate append-only local certificate journal","path"},
        {QStringLiteral("run"),"Exact declared native source run","hex"},{QStringLiteral("limit"),"Finite page size,1..64","count","4"},
        {QStringLiteral("max-interior"),"Explicit LOCAL per-pair work budget, NOT consensus","count","10000"},
        {QStringLiteral("inspect-gap-id"),"READONLY exact certificate retrieval and independent reverification; creates nothing","id"}});args.process(app);
    bool sizeOkay=false,budgetOkay=false;const auto count=args.value("limit").toInt(&sizeOkay);const auto budget=args.value("max-interior").toULongLong(&budgetOkay);
    if(!args.isSet("source")||!args.isSet("journal")||!args.isSet("run")||!sizeOkay||!budgetOkay||budget>std::numeric_limits<quint64>::max()-2)return 2;
    const pgn::verification::SourceProofPolicy policy{count,{{128,32,32},budget},{budget+2,1048576,32,1}};
    QElapsedTimer elapsed;elapsed.start();
    if(args.isSet("inspect-gap-id")){
        bool idOkay=false;const auto id=args.value("inspect-gap-id").toLongLong(&idOkay);if(!idOkay||id<=0)return 2;
        const auto r=pgn::verification::readCertifiedNativeSourceProof(args.value("source"),args.value("journal"),args.value("run"),id,policy);
        const bool verified=r.evidence.has_value()&&r.error.isEmpty()&&!r.cancelled;
        const QJsonObject out{{"run",r.run},{"gap_id",QString::number(r.gapId)},{"declared_identity_sha256",r.identitySha256},{"local_policy_sha256",r.policySha256},
            {"canonical_sha256",r.canonicalSha256},{"canonical_bytes",r.canonical.size()},{"independently_reverified",verified},{"read_only",true},
            {"network_credit",false},{"range_coverage",false},{"error",r.error},{"elapsed_ms",elapsed.elapsed()}};
        const auto bytes=QJsonDocument(out).toJson(QJsonDocument::Compact);QFile console;
        if(!console.open(stdout,QIODevice::WriteOnly)||console.write(bytes)!=bytes.size()||console.write("\n")!=1)return 3;
        return verified?0:1;
    }
    const auto r=pgn::verification::certifyNativeSourcePage(args.value("source"),args.value("journal"),args.value("run"),policy);
    const QJsonObject out{{"run",r.run},{"declared_identity_sha256",r.identitySha256},{"local_policy_sha256",r.policySha256},
        {"after_gap_id",QString::number(r.after)},{"source_watermark",QString::number(r.through)},{"processed",r.processed},{"verified",r.verified},
        {"rejected",r.rejected},{"total",QString::number(r.total)},{"verified_total",QString::number(r.verifiedTotal)},{"rejected_total",QString::number(r.rejectedTotal)},
        {"more",r.more},{"cancelled",r.cancelled},{"error",r.error},{"elapsed_ms",elapsed.elapsed()},{"source_modified",false},{"mining",false},{"network_credit",false},{"range_coverage",false}};
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 3;
    const auto bytes=QJsonDocument(out).toJson(QJsonDocument::Compact)+"\n";if(output.write(bytes)!=bytes.size())return 3;
    return r.error.isEmpty()&&!r.cancelled?0:1;
}
