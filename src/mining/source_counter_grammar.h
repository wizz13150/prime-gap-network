#pragma once
#include <QRegularExpression>
#include <QString>
namespace pgn::mining {
// Display telemetry accepts counter rows only. Native engines add emitted_gaps;
// GAP records still travel exclusively through the conserved raw byte stream.
inline const QRegularExpression& sourceDisplayCounterGrammar(){
    static const QRegularExpression pattern([]()->QString{
        const QString integer="(?:0|[1-9][0-9]{0,19})";
        return "^\\[pgn-counters\\] time_ns=("+integer+") positions="+integer+" gaps="+integer+
            " tests="+integer+" batches="+integer+"(?: processed_gaps="+integer+"(?: emitted_gaps="+integer+")?)?$";
    }());
    return pattern;
}
}
