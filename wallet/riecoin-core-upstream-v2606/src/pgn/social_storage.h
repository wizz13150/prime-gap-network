// Copyright (c) 2026 The PGN developers. MIT; see COPYING.
#ifndef PGN_SOCIAL_STORAGE_H
#define PGN_SOCIAL_STORAGE_H
#include <dbwrapper.h>
#include <stdexcept>

namespace pgn::social {
class StorageCommitUncertain final : public std::runtime_error {
public:
  StorageCommitUncertain()
      : std::runtime_error("Social storage commit is uncertain; pending proof "
                           "retained. Reconcile storage before retry.") {}
};
// A failed WAL Sync may follow a successful append. LevelDB deliberately marks
// this state indeterminate (db_impl.cc). Never translate it into a definitive
// rejection or let the caller create a different event for a retry.
inline void CommitSocialBatch(CDBWrapper &database, CDBBatch &batch) {
  try {
    database.WriteBatch(batch, true);
  } catch (const std::exception &) {
    throw StorageCommitUncertain{};
  }
}
} // namespace pgn::social
#endif
