#include "science/record_submissions.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc<2)return 1;
 const auto ref=pgn::science::readRecordReference(QString::fromLocal8Bit(argv[1]));if(!ref.valid()){std::cerr<<ref.error.toStdString();return 2;}
 if(ref.minimumPotentialGap!=1532||pgn::science::improvesRecordReference(ref,1000,"878591183389293815983"))return 3;
 auto changed=ref;changed.starts[1600]="200000000000000000000";
 if(!pgn::science::improvesRecordReference(changed,1600,"190000000000000000001")||pgn::science::improvesRecordReference(changed,1600,"200000000000000000000"))return 4;
 if(pgn::science::improvesRecordReference(changed,1600,"199999999999999999999X"))return 5;
 if(argc==3){const auto result=pgn::science::collectRecordSubmissions(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[1]));std::cout<<"records="<<result.records.size()<<" examined="<<result.examined<<" pending="<<result.pending<<" error="<<result.error.toStdString()<<'\n'<<result.text.toStdString();if(!result.error.isEmpty())return 6;}
 const QVector<pgn::science::SubmissionRecord> pairs{{848,"199932000019059617039","199932000019059617887",{}}};
 const auto direct=pgn::science::collectRecordSubmissionsFromPairs(pairs,QString::fromLocal8Bit(argv[1]));if(!direct.error.isEmpty()||direct.examined!=1||!direct.records.isEmpty())return 7;
 auto malformed=pairs;malformed[0].upper="199932000019059617886";if(pgn::science::collectRecordSubmissionsFromPairs(malformed,QString::fromLocal8Bit(argv[1])).error.isEmpty())return 8;
 std::atomic_bool stop{true};const auto cancelled=pgn::science::collectRecordSubmissionsFromPairs(pairs,QString::fromLocal8Bit(argv[1]),&stop);if(!cancelled.cancelled||!cancelled.records.isEmpty())return 9;
 std::cout<<"Record reference exact-coordinate checks PASS; no submission or network acceptance\n";return 0;
}
