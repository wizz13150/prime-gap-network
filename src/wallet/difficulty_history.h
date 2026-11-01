#pragma once
#include "wallet/chain_observation.h"
#include <QString>
#include <QJsonObject>
#include <optional>
struct sqlite3;
namespace pgn::wallet {
struct DifficultyHeader {
 QString genesis,hash,previous,difficulty;
 qint64 height{};
 bool ancestryComplete{};
};
struct DifficultySummary {
 QString genesis,tipHash,error;QDateTime observedUtc;
 qint64 height{},points{},missing{};std::optional<double> minimum,maximum;
 bool available{};
};
// Append-only, chain-bound observations. An ancestry-complete flag concerns
// stored header linkage only, never PGN consensus approval or network credit.
// Reads of an absent archive do not create a database. There is no row cap.
class DifficultyHistoryStore {
public:
 explicit DifficultyHistoryStore(QString file);
 ~DifficultyHistoryStore();
 DifficultyHistoryStore(const DifficultyHistoryStore&)=delete;
 DifficultyHistoryStore& operator=(const DifficultyHistoryStore&)=delete;
 const QString& file()const{return file_;}
 QString record(const CoreEpoch&,const QString& genesis,const QString& expectedHash,const QJsonObject& header,const QDateTime& observedUtc);
 QString activate(const ChainObservation& anchor,const QString& tipHash);
 std::optional<DifficultyHeader> find(const QString& genesis,const QString& hash)const;
 DifficultySummary summary(const QString& genesis)const;
private:
 sqlite3* writer();QString file_;sqlite3* db_{};
};
}
