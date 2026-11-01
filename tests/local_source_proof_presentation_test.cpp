#include "ui/local_source_proof_presentation.h"
#include <iostream>
using namespace pgn::ui;
int main(){unsigned checks=0,failures=0;const auto check=[&](bool b){++checks;if(!b)++failures;};
    const QString run(32,'a');const QSet<QString> active{run};
    QJsonObject good{{"scope","LOCAL_CERTIFIED_PAIRS_ONLY"},{"run",run},{"declared_identity_sha256",QString(64,'a')},{"policy_sha256",QString(64,'b')},
        {"total","10000000000"},{"verified","9999999999"},{"rejected","1"},{"after_gap_id","10000000000"},{"source_watermark","10000000005"},
        {"more",true},{"error",""},{"network_credit",false},{"range_coverage",false}};
    auto p=localSourceProofPresentation(good,active);check(p.present&&p.active&&p.caption.contains("9999999999")&&p.caption.startsWith("LOCAL EXACT"));
    check(!localSourceProofPresentation(good,{}).present);
    auto change=good;change["run"]=QString(32,'c');check(!localSourceProofPresentation(change,active).present);
    change=good;change["more"]=false;change["source_watermark"]="10000000000";p=localSourceProofPresentation(change,active);check(p.present&&!p.active&&p.caption.contains("CAUGHT UP"));
    for(const auto key:{"network_credit","range_coverage"}){change=good;change[key]=true;check(!localSourceProofPresentation(change,active).present);}
    for(const auto key:{"scope","policy_sha256","declared_identity_sha256","total"}){change=good;change.remove(key);check(!localSourceProofPresentation(change,active).present);}
    for(const auto value:{"-1","01","9223372036854775808"}){change=good;change["total"]=value;check(!localSourceProofPresentation(change,active).present);}
    change=good;change["total"]=10000000000.;check(!localSourceProofPresentation(change,active).present); // No double-based integer identity.
    change=good;change["rejected"]="2";check(!localSourceProofPresentation(change,active).present);
    change=good;change["after_gap_id"]="10000000006";check(!localSourceProofPresentation(change,active).present);
    change=good;change["error"]="Actual disk error";p=localSourceProofPresentation(change,active);check(p.present&&!p.active&&p.caption.contains("HELD"));
    change["run"]=QString(32,'c');check(!localSourceProofPresentation(change,active).present);
    change["run"]="";check(localSourceProofPresentation(change,active).present); // Global worker integrity error, never chain success.
    change=good;change["run"]=QString(32,'c');change["verified"]="1";
    check(projectLocalSourceProofStatus(run,change,good)==good); // A's background backlog cannot erase current B.
    change["error"]="actual background disk error";const auto held=projectLocalSourceProofStatus(run,change,good);
    check(held.value("run").toString().isEmpty()&&localSourceProofPresentation(held,active).caption.contains("HELD"));
    std::cout<<"LOCAL_SOURCE_PROOF_PRESENTATION "<<checks<<" checks / "<<failures<<" failures; exact10billion counters, current-run only, no fake chain/range credit\n";return failures?1:0;
}
