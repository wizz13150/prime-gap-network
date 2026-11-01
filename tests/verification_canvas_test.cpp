#include "ui/mining_canvas.h"
#include "ui/historical_proof_presentation.h"
#include "verification/local_validator_pins.h"
#include <QApplication>
#include <QImage>
#include <iostream>
#include <memory>
int main(int argc,char** argv){
    QApplication app(argc,argv);std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());
    canvas->resize(1400,1000);
    const auto networkBefore=canvas->property("pgn_chain_connected");
    pgn::verification::LatestReceipt result;
    pgn::verification::JournalReceipt receipt;
    receipt.id=7;receipt.sourceId=6;receipt.lower="11";receipt.upper="13";receipt.verifiedGapSize=2;
    receipt.validatorSha256=pgn::verification::localArchiveValidatorPins().first();
    receipt.verdict=pgn::verification::GapVerdict::VerifiedWithinBound;receipt.elapsedNs=0;
    receipt.originalCheckedUtc=QDateTime::fromString("2026-10-11T12:00:00.000Z",Qt::ISODateWithMs);
    result.receipt=receipt;pgn::ui::presentVerificationReceipt(canvas.get(),result);
    const auto first=canvas->property("pgn_independent_verification_receipt").value<QJsonObject>();
    if(first.value("receipt_id").toString()!="7"||first.value("elapsed_ns").toString()!="0"||
       first.value("original_checked_utc").toString()!="2026-10-11T12:00:00.000Z"||
       first.value("network_credit").toBool(true)||first.value("range_coverage").toBool(true))return 1;
    const auto exact=pgn::ui::historicalProofPresentation(first,{});
    if(!exact.available||!exact.rail.contains("ARCHIVE EXACT GAP")||
       !exact.footer.contains("ARCHIVE EXACT")||!exact.footer.contains("LIVE PROOF PENDING")||
       !exact.footer.contains("NO NETWORK CREDIT")||!exact.tooltip.contains("2026-10-11T12:00:00.000Z"))return 6;
    pgn::verification::LatestReceipt unavailable;unavailable.error="journal unavailable";
    pgn::ui::presentVerificationReceipt(canvas.get(),unavailable);
    if(canvas->property("pgn_independent_verification_receipt").value<QJsonObject>()!=first||
       canvas->property("pgn_independent_verification_error").toString().isEmpty())return 2;
    const auto retained=pgn::ui::historicalProofPresentation(first,unavailable.error);
    if(!retained.rail.contains("RETAINED")||!retained.footer.contains("READ ERROR")||
       !retained.tooltip.contains("journal unavailable"))return 7;
    auto rejected=first;rejected.insert("verdict",int(pgn::verification::GapVerdict::InteriorPrime));
    const auto notExact=pgn::ui::historicalProofPresentation(rejected,{});
    if(notExact.rail.contains("ARCHIVE EXACT")||!notExact.footer.contains("NOT VERIFIED"))return 8;
    if(pgn::ui::historicalProofPresentation({},{}).available||
       !pgn::ui::historicalProofPresentation({},"read failure").footer.contains("READ ERROR"))return 9;
    pgn::ui::presentVerificationReceipt(canvas.get(),result);
    if(!canvas->property("pgn_independent_verification_error").toString().isEmpty())return 3;
    pgn::ui::presentVerificationReceipt(canvas.get(),{});
    if(canvas->property("pgn_independent_verification_receipt").value<QJsonObject>()!=first||
       canvas->property("pgn_independent_verification_error").toString().isEmpty())return 4;
    if(canvas->property("pgn_chain_connected")!=networkBefore)return 5;
    QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);canvas->render(&image);
    const auto liveState=canvas->property("pgn_live_verifier_state").toString();
    if(liveState.isEmpty()||liveState.contains("ARCHIVE")||liveState.contains("EXACT GAP"))return 10;
    std::cout<<"PASS local receipt/originaltime/realzero/missingretention/no chain-credit mutation\n";
}
