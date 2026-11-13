// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license; see COPYING.
#include <addresstype.h>
#include <algorithm>
#include <chainparams.h>
#include <common/args.h>
#include <common/signmessage.h>
#include <condition_variable>
#include <crypto/sha256.h>
#include <cstdio>
#include <dbwrapper.h>
#include <functional>
#include <key_io.h>
#include <map>
#include <pgn/component_version.h>
#include <pgn/social.h>
#include <pgn/social_storage.h>
#include <random.h>
#include <set>
#include <stdexcept>
#include <thread>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <util/time.h>

namespace pgn::social {
namespace {
constexpr size_t MAX_STORE = 16 * 1024 * 1024, MAX_RECORDS = 2000,
                 MAX_WAITING = 32;
std::string Genesis() {
  return Params().GetConsensus().hashGenesisBlock.GetHex();
}
int64_t Now() { return GetTime<std::chrono::milliseconds>().count(); }
std::string Hash(const std::string &bytes) {
  unsigned char out[32];
  CSHA256()
      .Write(reinterpret_cast<const unsigned char *>(bytes.data()),
             bytes.size())
      .Finalize(out);
  return HexStr(out);
}
bool Hex(const std::string &value, size_t size) {
  return value.size() == size &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
bool Ascii(const std::string &text) {
  return std::all_of(text.begin(), text.end(),
                     [](unsigned char c) { return c >= 32 && c <= 126; });
}
std::string Canonical(const UniValue &value) {
  if (value.isObject()) {
    std::map<std::string, UniValue> sorted;
    const auto &keys = value.getKeys();
    const auto &values = value.getValues();
    for (size_t i = 0; i < keys.size(); ++i)
      if (!sorted.emplace(keys[i], values[i]).second)
        throw std::runtime_error("Duplicate canonical key");
    std::string out = "{";
    for (const auto &[key, entry] : sorted) {
      if (out.size() > 1)
        out += ",";
      out += UniValue(key).write() + ":" + Canonical(entry);
    }
    return out + "}";
  }
  if (value.isArray()) {
    std::string out = "[";
    for (const auto &entry : value.getValues()) {
      if (out.size() > 1)
        out += ",";
      out += Canonical(entry);
    }
    return out + "]";
  }
  if (value.isStr()) {
    if (!Ascii(value.get_str()))
      throw std::runtime_error("Canonical metadata must be ASCII");
    return value.write();
  }
  if (value.isNum()) {
    const auto n = value.getInt<int64_t>();
    if (n < 0 || n > 9007199254740991LL ||
        std::to_string(n) != value.getValStr())
      throw std::runtime_error("Safe canonical integer required");
    return value.write();
  }
  throw std::runtime_error("Unsupported canonical value");
}
UniValue Obj() { return UniValue(UniValue::VOBJ); }
UniValue Frame(const std::string &op) {
  auto out = Obj();
  out.pushKV("op", op);
  return out;
}
UniValue Status(const std::string &state, const std::string &proof,
                const std::string &reason = {}) {
  auto out = Obj();
  out.pushKV("state", state);
  out.pushKV("proof_id", proof);
  out.pushKV("genesis", Genesis());
  if (!reason.empty())
    out.pushKV("reason", reason);
  return out;
}
std::string String(const UniValue &value, const char *field) {
  return value[field].get_str();
}
// Strict UTF8, counting UTF16 code units like the Qt editor. No normalization.
bool Utf8Body(const std::vector<unsigned char> &raw) {
  size_t units = 0;
  bool nonspace = false;
  for (size_t i = 0; i < raw.size();) {
    const auto a = raw[i++];
    uint32_t cp{};
    int count{};
    if (a < 0x80)
      cp = a;
    else if (a >= 0xc2 && a <= 0xdf) {
      cp = a & 31;
      count = 1;
    } else if (a >= 0xe0 && a <= 0xef) {
      cp = a & 15;
      count = 2;
    } else if (a >= 0xf0 && a <= 0xf4) {
      cp = a & 7;
      count = 3;
    } else
      return false;
    for (int j = 0; j < count; ++j) {
      if (i >= raw.size() || (raw[i] & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (raw[i++] & 63);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) ||
        (count == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff))
      return false;
    units += cp > 0xffff ? 2 : 1;
    if (units > 4000)
      return false;
    if (cp > 32)
      nonspace = true;
  }
  return nonspace;
}
struct Record {
  uint64_t seq{};
  std::string key, nonce, message, signature, event;
  size_t bytes{};
  int64_t created_ms{};
};
class RetentionBoundary final : public std::runtime_error {
public:
  const int64_t floor;
  explicit RetentionBoundary(int64_t value)
      : std::runtime_error("Valid signed event is outside the local retained "
                           "admission window; it was not reinserted."),
        floor(value) {}
};
enum class ProofUse { Admission, StoredHistory };
Record Verify(const std::string &message, const std::string &signature,
              ProofUse use = ProofUse::Admission) {
  if (message.size() < 64 || message.size() > MAX_FRAME || signature.empty() ||
      signature.size() > 512)
    throw std::runtime_error("Bounded Social proof required");
  const auto split = message.find('\n');
  if (split == std::string::npos)
    throw std::runtime_error("Social domain required");
  const auto domain = message.substr(0, split);
  const bool chat = domain == "PGN/SOCIAL/EVENT/1",
             receipt = domain == "PGN/SOCIAL/RECEIPT/1";
  if (!chat && !receipt)
    throw std::runtime_error(
        "Only chat and signed receipts are accepted in this lot");
  UniValue object;
  if (!object.read(message.substr(split + 1)) || !object.isObject() ||
      Canonical(object) != message.substr(split + 1))
    throw std::runtime_error("Canonical Social document required");
  std::set<std::string> expected{
      "v",      "genesis",    "origin", "sender_script", "signing_address",
      "device", "created_ms", "nonce",  "channel",       "event_id"};
  if (chat)
    expected.insert({"kind", "parents", "bbcode_b64"});
  else
    expected.insert("recipient_script");
  const auto keys = object.getKeys();
  if (std::set<std::string>(keys.begin(), keys.end()) != expected ||
      keys.size() != expected.size())
    throw std::runtime_error("Unexpected Social fields");
  const auto script = String(object, "sender_script"),
             address = String(object, "signing_address"),
             nonce = String(object, "nonce"),
             event = String(object, "event_id"),
             channel = String(object, "channel");
  if (object["v"].getInt<int>() != 1 ||
      String(object, "genesis") != Genesis() ||
      String(object, "origin") != "pgn-social://" + Genesis())
    throw std::runtime_error("Social genesis or version mismatch");
  if (!Hex(script, 44) || script.substr(0, 4) != "0014" || !Hex(nonce, 64) ||
      !Hex(String(object, "device"), 32) || !Hex(event, 64))
    throw std::runtime_error("Invalid stable signing context");
  if (channel.empty() || channel.size() > 64 ||
      !((channel[0] >= 'a' && channel[0] <= 'z') ||
        (channel[0] >= '0' && channel[0] <= '9')) ||
      !std::all_of(channel.begin(), channel.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
               c == '_' || c == '-';
      }))
    throw std::runtime_error("Invalid channel ID");
  const auto created = object["created_ms"].getInt<int64_t>();
  if (use == ProofUse::Admission && created > Now() + 60000)
    throw std::runtime_error("Social event from the future");
  if (chat) {
    if (String(object, "kind") != "chat" || !object["parents"].isArray() ||
        object["parents"].size() > 4)
      throw std::runtime_error("Unsupported chat event");
    std::set<std::string> parents;
    for (const auto &parent : object["parents"].getValues())
      if (!Hex(parent.get_str(), 64) ||
          !parents.insert(parent.get_str()).second)
        throw std::runtime_error("Invalid causal parent");
    const auto encoded = String(object, "bbcode_b64");
    const auto decoded = DecodeBase64(encoded);
    if (encoded.size() > 16000 || !decoded ||
        EncodeBase64(*decoded) != encoded || !Utf8Body(*decoded))
      throw std::runtime_error("Invalid canonical BBCode");
    auto payload = Obj();
    for (const auto &key : keys)
      if (key != "event_id")
        payload.pushKV(key, object[key]);
    if (Hash("PGN/SOCIAL/EVENT-ID/1\n" + Canonical(payload)) != event)
      throw std::runtime_error("Content event ID mismatch");
  } else if (!Hex(String(object, "recipient_script"), 44) ||
             String(object, "recipient_script").substr(0, 4) != "0014")
    throw std::runtime_error("Invalid receipt recipient");
  const auto raw = DecodeBase64(signature);
  if (!raw || raw->size() < 100 || raw->size() > 140 || (*raw)[0] != 2 ||
      EncodeBase64(*raw) != signature)
    throw std::runtime_error("Native simple P2WPKH proof required");
  const auto script_bytes = TryParseHex<unsigned char>(script);
  CScript native_script(script_bytes->begin(), script_bytes->end());
  CTxDestination destination;
  if (!ExtractDestination(native_script, destination) ||
      EncodeDestination(destination) != address)
    throw std::runtime_error("Native signing address/script mismatch");
  auto proof = *raw;
  if (MessageVerifyBIP322(destination, proof, message,
                          MessageVerificationResult::ERR_ADDRESS_NO_KEY) !=
      MessageVerificationResult::OK)
    throw std::runtime_error("Native BIP322 signature refused");
  return {0,
          Hash("PGN/SOCIAL/PROOF-ID/1\n" + message),
          script + ":" + nonce,
          message,
          signature,
          event,
          message.size() + signature.size() + 256,
          created};
}

class Service {
  struct Job {
    std::shared_ptr<Link> link;
    std::function<void()> run;
  };
  std::mutex queue_mutex, data_mutex, commit_mutex, links_mutex;
  std::condition_variable wake;
  std::map<int64_t, std::deque<Job>> pending;
  std::deque<int64_t> turns;
  std::set<int64_t> active;
  std::vector<std::weak_ptr<Link>> links;
  std::vector<std::thread> workers;
  std::deque<Record> records;
  std::unique_ptr<CDBWrapper> database;
  std::map<std::string, std::string> proof_cache;
  std::set<std::string> nonces;
  std::atomic<uint64_t> storage_batches{0}, storage_logical_bytes{0};
  std::map<std::string, UniValue> statuses;
  std::set<std::string> submitting;
  std::atomic<uint64_t> rejected{0}, dropped{0}, committed{0};
  size_t waiting{}, store_bytes{};
  int64_t admission_floor_ms{-1}, private_migration_floor_ms{};
  uint64_t sequence{};
  std::string generation{GetRandHash().GetHex()}, failure;
  bool stop{};
  std::atomic<bool> loaded{false}, recovery_needed{false}, recovering{false};
  std::atomic<uint64_t> recovery_attempts{0}, recoveries{0};
  std::chrono::steady_clock::time_point recovery_due{};
  unsigned recovery_backoff_seconds{1};
  fs::path directory;
  leveldb::Env *testing_env{};
  bool Enqueue(std::shared_ptr<Link> link, std::function<void()> fn) {
    std::lock_guard guard(queue_mutex);
    const auto id = link ? link->id : -1;
    if (stop || waiting >= MAX_WAITING ||
        (pending.contains(id) && pending[id].size() >= 8)) {
      ++dropped;
      return false;
    }
    if (!pending.contains(id))
      turns.push_back(id);
    pending[id].push_back({std::move(link), std::move(fn)});
    ++waiting;
    wake.notify_all();
    return true;
  }
  void RequestRecovery() {
    {
      std::lock_guard guard(queue_mutex);
      if (!recovery_needed.exchange(true)) {
        recovery_backoff_seconds = 1;
        recovery_due =
            std::chrono::steady_clock::now() + std::chrono::seconds(1);
      }
    }
    wake.notify_all();
  }
  enum class RecoveryOutcome { Recovered, RetryIO, NeedsReview };
  RecoveryOutcome RecoverStorage() {
    ++recovery_attempts;
    // All verifier jobs have drained before this control task is reserved.
    // Reopen only this Social CDBWrapper. No Core/wallet/chain/mining lifecycle
    // callback, cs_main, RPC credential or process action exists here.
    std::lock_guard serial(commit_mutex);
    database.reset();
    try {
      Load();
      std::string metadata;
      {
        std::lock_guard guard(data_mutex);
        metadata = Metadata(generation, sequence, store_bytes, records.size(),
                            admission_floor_ms, private_migration_floor_ms)
                       .write();
      }
      CDBBatch probe(*database);
      probe.Write(std::string{"meta"}, metadata);
      SyncBatch(probe);
      {
        std::lock_guard guard(data_mutex);
        failure.clear();
        std::erase_if(statuses, [](const auto &item) {
          return item.second["state"].get_str() == "uncertain";
        });
      }
      ++recoveries;
      return RecoveryOutcome::Recovered;
    } catch (const StorageCommitUncertain &error) {
      std::lock_guard guard(data_mutex);
      failure = error.what();
      return RecoveryOutcome::RetryIO;
    } catch (const dbwrapper_error &) {
      std::lock_guard guard(data_mutex);
      failure =
          "Social storage is still unavailable; exact pending proof retained.";
      return RecoveryOutcome::RetryIO;
    } catch (const std::exception &error) {
      std::lock_guard guard(data_mutex);
      failure = error.what();
      return RecoveryOutcome::NeedsReview;
    }
  }
  void Loop(bool loader) {
    if (loader) {
      try {
        Load();
      } catch (const dbwrapper_error &) {
        {
          std::lock_guard guard(data_mutex);
          failure = "Social storage is unavailable; reconciliation pending.";
        }
        RequestRecovery();
      } catch (const std::exception &error) {
        std::lock_guard guard(data_mutex);
        failure = error.what();
      }
      {
        std::lock_guard guard(queue_mutex);
        loaded = true;
      }
      wake.notify_all();
    }
    for (;;) {
      Job job;
      int64_t id{};
      bool repair = false;
      {
        std::unique_lock guard(queue_mutex);
        wake.wait_for(guard, std::chrono::milliseconds(250), [&] {
          if (stop)
            return true;
          if (!loaded || recovering)
            return false;
          if (recovery_needed)
            return active.empty() &&
                   std::chrono::steady_clock::now() >= recovery_due;
          return std::any_of(turns.begin(), turns.end(), [&](int64_t key) {
            return !active.contains(key);
          });
        });
        if (stop)
          return;
        if (!loaded || recovering)
          continue;
        if (recovery_needed) {
          if (!active.empty() ||
              std::chrono::steady_clock::now() < recovery_due)
            continue;
          recovering = true;
          repair = true;
        } else {
          const auto at =
              std::find_if(turns.begin(), turns.end(),
                           [&](int64_t key) { return !active.contains(key); });
          if (at == turns.end())
            continue;
          id = *at;
          turns.erase(at);
          job = std::move(pending[id].front());
          pending[id].pop_front();
          --waiting;
          if (pending[id].empty())
            pending.erase(id);
          else
            turns.push_back(id);
          active.insert(id);
        }
      }
      if (repair) {
        const auto result = RecoverStorage();
        {
          std::lock_guard guard(queue_mutex);
          recovering = false;
          if (result == RecoveryOutcome::Recovered ||
              result == RecoveryOutcome::NeedsReview) {
            recovery_needed = false;
            recovery_backoff_seconds = 1;
          } else {
            recovery_backoff_seconds =
                std::min(30u, recovery_backoff_seconds * 2);
            recovery_due = std::chrono::steady_clock::now() +
                           std::chrono::seconds(recovery_backoff_seconds);
          }
        }
        wake.notify_all();
        continue;
      }
      try {
        if (!job.link || !job.link->closed)
          job.run();
      } catch (const StorageCommitUncertain
                   &) { /* State is held/reconciling, not an invalid signature
                           or miner fault. */
      } catch (const std::exception &) {
        ++rejected;
      }
      {
        std::lock_guard guard(queue_mutex);
        active.erase(id);
      }
      wake.notify_all();
    }
  }
  bool Output(const std::shared_ptr<Link> &link, UniValue frame) {
    const auto bytes = frame.write();
    if (link->closed || bytes.size() > MAX_FRAME - 9)
      return false;
    std::lock_guard guard(link->output_mutex);
    if (link->output.size() >= 8)
      return false;
    link->output.push_back(bytes);
    return true;
  }
  void Ask(const std::shared_ptr<Link> &link) {
    auto frame = Frame("get");
    frame.pushKV("generation", link->remote_generation);
    frame.pushKV("cursor", link->remote_cursor);
    if (Output(link, std::move(frame)))
      link->last_request_ms = Now();
  }
  std::string RecordKey(uint64_t value) const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string key = "r/";
    for (int shift = 60; shift >= 0; shift -= 4)
      key += digits[(value >> shift) & 15];
    return key;
  }
  UniValue EncodeRecord(const Record &record) const {
    auto out = Obj();
    out.pushKV("seq", record.seq);
    out.pushKV("canonical_message", record.message);
    out.pushKV("signature", record.signature);
    return out;
  }
  UniValue Metadata(const std::string &gen, uint64_t seq, size_t size,
                    size_t count, int64_t floor, int64_t migration) const {
    auto out = Obj();
    out.pushKV("schema", "PGN_SOCIAL_STORE_V3");
    out.pushKV("genesis", Genesis());
    out.pushKV("generation", gen);
    out.pushKV("sequence", seq);
    out.pushKV("bytes", uint64_t(size));
    out.pushKV("count", uint64_t(count));
    out.pushKV("admission_floor_ms", floor);
    out.pushKV("private_migration_floor_ms", migration);
    return out;
  }
  void AddBatchRecord(CDBBatch &batch, const Record &record) const {
    batch.Write(RecordKey(record.seq), EncodeRecord(record).write());
    batch.Write("id/" + record.key, record.seq);
    batch.Write("nonce/" + record.nonce, record.seq);
  }
  void SyncBatch(CDBBatch &batch) {
    const auto bytes = batch.ApproximateSize();
    try {
      CommitSocialBatch(*database, batch);
    } catch (const StorageCommitUncertain &error) {
      {
        std::lock_guard guard(data_mutex);
        failure = error.what();
      }
      RequestRecovery();
      throw;
    }
    ++storage_batches;
    storage_logical_bytes += bytes;
  }
  void LoadLegacy(std::deque<Record> &values, std::string &gen, uint64_t &seq) {
    const auto file = directory / "events.json";
    if (!fs::exists(file))
      return;
    if (fs::file_size(file) > MAX_STORE)
      throw std::runtime_error("Legacy Social history exceeds its bound");
    FILE *handle = fsbridge::fopen(file, "rb");
    if (!handle)
      throw std::runtime_error("Legacy Social history unavailable");
    std::string bytes(fs::file_size(file), '\0');
    const auto n = std::fread(bytes.data(), 1, bytes.size(), handle);
    std::fclose(handle);
    UniValue saved;
    if (n != bytes.size() || !saved.read(bytes) ||
        String(saved, "genesis") != Genesis() ||
        !Hex(String(saved, "generation"), 64) || !saved["records"].isArray() ||
        saved["records"].size() > MAX_RECORDS)
      throw std::runtime_error("Legacy Social history context invalid");
    for (const auto &entry : saved["records"].getValues()) {
      auto record = Verify(String(entry, "canonical_message"),
                           String(entry, "signature"), ProofUse::StoredHistory);
      record.seq = entry["seq"].getInt<uint64_t>();
      values.push_back(std::move(record));
    }
    gen = String(saved, "generation");
    seq = saved["sequence"].getInt<uint64_t>();
  }
  void Load() {
    fs::create_directories(directory);
    database =
        std::make_unique<CDBWrapper>(DBParams{.path = directory / "events-v2",
                                              .cache_bytes = 4 * 1024 * 1024,
                                              .testing_env = testing_env});
    std::deque<Record> values;
    std::string gen = generation, meta;
    uint64_t seq = 0;
    int64_t floor = -1, migration = 0;
    bool old_private_store = false;
    bool migrating = false;
    if (database->Read(std::string{"meta"}, meta)) {
      UniValue saved;
      if (!saved.read(meta) ||
          (String(saved, "schema") != "PGN_SOCIAL_STORE_V2" &&
           String(saved, "schema") != "PGN_SOCIAL_STORE_V3") ||
          String(saved, "genesis") != Genesis() ||
          !Hex(String(saved, "generation"), 64))
        throw std::runtime_error("Social database metadata invalid");
      gen = String(saved, "generation");
      seq = saved["sequence"].getInt<uint64_t>();
      if (String(saved, "schema") == "PGN_SOCIAL_STORE_V3") {
        floor = saved["admission_floor_ms"].getInt<int64_t>();
        migration = saved["private_migration_floor_ms"].getInt<int64_t>();
        if (floor < -1 || floor > 9007199254740991LL || migration < 0 ||
            (migration > 0 && migration > floor))
          throw std::runtime_error(
              "Social admission boundary metadata invalid");
      } else {
        old_private_store = true;
        migrating = true;
      }
      std::unique_ptr<CDBIterator> it(database->NewIterator());
      for (it->Seek(RecordKey(0)); it->Valid(); it->Next()) {
        std::string key, serialized;
        if (!it->GetKey(key))
          throw std::runtime_error("Social database key invalid");
        if (!key.starts_with("r/"))
          break;
        if (values.size() >= MAX_RECORDS || !it->GetValue(serialized) ||
            serialized.size() > MAX_FRAME + 1024)
          throw std::runtime_error("Social database record bound exceeded");
        UniValue entry;
        if (!entry.read(serialized))
          throw std::runtime_error("Social database record invalid");
        auto record =
            Verify(String(entry, "canonical_message"),
                   String(entry, "signature"), ProofUse::StoredHistory);
        record.seq = entry["seq"].getInt<uint64_t>();
        uint64_t indexed{};
        if (key != RecordKey(record.seq) ||
            !database->Read("id/" + record.key, indexed) ||
            indexed != record.seq ||
            !database->Read("nonce/" + record.nonce, indexed) ||
            indexed != record.seq)
          throw std::runtime_error("Social database indexes invalid");
        values.push_back(std::move(record));
      }
      size_t bytes = 0;
      for (const auto &record : values)
        bytes += record.bytes;
      if (saved["count"].getInt<uint64_t>() != values.size() ||
          saved["bytes"].getInt<uint64_t>() != bytes)
        throw std::runtime_error("Social database retention metadata mismatch");
    } else {
      std::unique_ptr<CDBIterator> it(database->NewIterator());
      it->SeekToFirst();
      if (it->Valid())
        throw std::runtime_error("Social database missing atomic metadata");
      old_private_store = fs::exists(directory / "events.json");
      LoadLegacy(values, gen, seq);
      migrating = true;
    }
    std::map<std::string, std::string> proofs;
    std::set<std::string> seen_nonces;
    uint64_t last = 0;
    size_t size = 0;
    for (const auto &record : values) {
      if (record.seq <= last ||
          !proofs
               .emplace(record.key,
                        Hash(record.message + "\n" + record.signature))
               .second ||
          !seen_nonces.insert(record.nonce).second)
        throw std::runtime_error("Social history replay/cursor invalid");
      last = record.seq;
      size += record.bytes;
    }
    if (seq < last || seq >= 9007199254740991ULL || size > MAX_STORE / 2)
      throw std::runtime_error("Social history bounds invalid");
    if (old_private_store) {
      // No public Core13 Social store existed. Private v1/v2 never recorded an
      // eviction floor: do not fabricate a past value. Preserve all retained
      // proofs and explicitly establish a conservative one-time new boundary.
      migration = Now() + 60000;
      for (const auto &record : values)
        migration = std::max(migration, record.created_ms);
      floor = migration;
    }
    if (migrating) {
      CDBBatch batch(*database);
      for (const auto &record : values)
        AddBatchRecord(batch, record);
      batch.Write(
          std::string{"meta"},
          Metadata(gen, seq, size, values.size(), floor, migration).write());
      SyncBatch(batch);
    }
    std::lock_guard guard(data_mutex);
    records = std::move(values);
    proof_cache = std::move(proofs);
    nonces = std::move(seen_nonces);
    generation = gen;
    sequence = seq;
    store_bytes = size;
    admission_floor_ms = floor;
    private_migration_floor_ms = migration;
  }
  void Commit(const std::string &message, const std::string &signature) {
    const auto key = Hash("PGN/SOCIAL/PROOF-ID/1\n" + message),
               context = Hash(message + "\n" + signature);
    bool retained = false;
    {
      std::lock_guard guard(data_mutex);
      const auto at = proof_cache.find(key);
      if (at != proof_cache.end() && at->second == context)
        return;
      retained = at != proof_cache.end();
    }
    auto record =
        Verify(message, signature,
               retained ? ProofUse::StoredHistory : ProofUse::Admission);
    std::lock_guard serial(commit_mutex);
    std::string gen;
    uint64_t seq{};
    int64_t next_floor{};
    size_t size{}, count{}, remove_count{};
    std::vector<Record> evicted;
    {
      std::lock_guard guard(data_mutex);
      if (!failure.empty())
        throw StorageCommitUncertain{};
      if (proof_cache.contains(record.key))
        return;
      // Existing retained proofs remain idempotent even below this boundary.
      // Only a NEW insertion is gated; cryptographic validity is unchanged.
      if (admission_floor_ms >= 0 && record.created_ms <= admission_floor_ms)
        throw RetentionBoundary(admission_floor_ms);
      if (nonces.contains(record.nonce))
        throw std::runtime_error("Social nonce replay");
      gen = generation;
      seq = sequence + 1;
      size = store_bytes + record.bytes;
      count = records.size() + 1;
      next_floor = admission_floor_ms;
      if (seq >= 9007199254740991ULL)
        throw std::runtime_error("Social cursor exhausted");
      while (count - remove_count > MAX_RECORDS || size > MAX_STORE / 2) {
        if (remove_count >= records.size())
          throw std::runtime_error("Social record exceeds retention budget");
        const auto &old = records[remove_count++];
        next_floor = std::max(next_floor, old.created_ms);
        size -= old.bytes;
        evicted.push_back(old);
      }
    }
    record.seq = seq;
    CDBBatch batch(*database);
    AddBatchRecord(batch, record);
    for (const auto &old : evicted) {
      batch.Erase(RecordKey(old.seq));
      batch.Erase("id/" + old.key);
      batch.Erase("nonce/" + old.nonce);
    }
    batch.Write(std::string{"meta"},
                Metadata(gen, seq, size, count - remove_count, next_floor,
                         private_migration_floor_ms)
                    .write());
    SyncBatch(batch);
    // Publish only after the durable atomic WriteBatch. No full deque copy and
    // no whole-history rewrite. Work is O(new bytes +evicted records),
    // amortized.
    try {
      std::lock_guard guard(data_mutex);
      for (size_t i = 0; i < remove_count; ++i) {
        proof_cache.erase(records.front().key);
        nonces.erase(records.front().nonce);
        records.pop_front();
      }
      proof_cache[record.key] = context;
      nonces.insert(record.nonce);
      records.push_back(std::move(record));
      sequence = seq;
      store_bytes = size;
      admission_floor_ms = next_floor;
    } catch (const std::exception &) {
      // The batch is already durable. A cache publication failure can never be
      // reported as a definitive rejection of this signed event.
      const StorageCommitUncertain uncertain;
      {
        std::lock_guard guard(data_mutex);
        failure = uncertain.what();
      }
      RequestRecovery();
      throw uncertain;
    }
    ++committed;
  }
  void OnFrame(const std::shared_ptr<Link> &link, const std::string &bytes) {
    UniValue frame;
    if (!frame.read(bytes) || !frame.isObject())
      return;
    const auto op = String(frame, "op");
    if (op == "cap") {
      if (frame["v"].getInt<int>() != 1 ||
          String(frame, "genesis") != Genesis())
        return;
      link->capable = true;
      Ask(link);
      return;
    }
    if (!link->capable)
      return;
    if (op == "get") {
      const auto page = Read(String(frame, "generation"),
                             frame["cursor"].getInt<uint64_t>(), 1);
      if (page.exists("error"))
        return;
      const auto &items = page["records"].getValues();
      if (!items.empty()) {
        auto item = Frame("event");
        item.pushKV("canonical_message", items[0]["canonical_message"]);
        item.pushKV("signature", items[0]["signature"]);
        if (!Output(link, std::move(item)))
          return;
      }
      auto end = Frame("end");
      for (const char *key : {"generation", "next_cursor", "more", "reset"})
        end.pushKV(key, page[key]);
      Output(link, std::move(end));
      return;
    }
    if (op == "event") {
      Commit(String(frame, "canonical_message"), String(frame, "signature"));
      return;
    }
    if (op == "end") {
      const auto gen = String(frame, "generation");
      if (!Hex(gen, 64))
        return;
      link->remote_generation = gen;
      link->remote_cursor = frame["next_cursor"].getInt<uint64_t>();
      if (frame["more"].get_bool())
        Ask(link);
      return;
    }
  }

public:
  explicit Service(leveldb::Env *environment = nullptr, fs::path storage = {})
      : directory(storage.empty() ? gArgs.GetDataDirNet() / "social"
                                  : std::move(storage)),
        testing_env(environment) {
    workers.emplace_back([this] { Loop(true); });
    workers.emplace_back([this] { Loop(false); });
  }
  ~Service() { Shutdown(); }
  void Shutdown() {
    {
      std::lock_guard guard(queue_mutex);
      stop = true;
    }
    wake.notify_all();
    for (auto &worker : workers)
      if (worker.joinable())
        worker.join();
  }
  std::shared_ptr<Link> Connect(int64_t node) {
    auto link = std::make_shared<Link>(node);
    {
      std::lock_guard guard(links_mutex);
      std::erase_if(links, [](const auto &weak) { return weak.expired(); });
      links.push_back(link);
    }
    auto cap = Frame("cap");
    cap.pushKV("v", 1);
    cap.pushKV("genesis", Genesis());
    Output(link, std::move(cap));
    return link;
  }
  bool Input(const std::shared_ptr<Link> &link, std::string bytes) {
    if (!link || link->closed || bytes.size() > MAX_FRAME - 9)
      return false;
    return Enqueue(
        link, [this, link, bytes = std::move(bytes)] { OnFrame(link, bytes); });
  }
  void Pump(const std::shared_ptr<Link> &link) {
    if (!link || link->closed || !link->capable ||
        link->tick_pending.exchange(true))
      return;
    if (!Enqueue(link, [this, link] {
          link->tick_pending = false;
          if (Now() - link->last_request_ms >= 1000)
            Ask(link);
        }))
      link->tick_pending = false;
  }
  UniValue SubmitProof(const std::string &message,
                       const std::string &signature) {
    if (message.size() > MAX_FRAME || signature.size() > 512)
      return Status("rejected", {}, "Bounded Social proof required");
    const auto proof = Hash(message + "\n" + signature);
    {
      std::lock_guard guard(data_mutex);
      if (!failure.empty())
        return Status("uncertain", proof, failure);
      if (statuses.contains(proof))
        return statuses.at(proof);
      if (submitting.contains(proof))
        return Status("queued", proof);
      if (submitting.size() >= 8)
        return Status("uncertain", proof,
                      "Social RPC queue full; retry same proof");
      submitting.insert(proof);
    }
    if (!Enqueue({}, [this, message, signature, proof] {
          UniValue value;
          try {
            Commit(message, signature);
            value = Status("accepted_local", proof);
          } catch (const StorageCommitUncertain &error) {
            value = Status("uncertain", proof, error.what());
          } catch (const RetentionBoundary &error) {
            value = Status("rejected", proof, error.what());
            value.pushKV("reason_code", "retention_boundary");
            value.pushKV("signature_valid", true);
            value.pushKV("admission_floor_ms", error.floor);
          } catch (const std::exception &error) {
            value = Status("rejected", proof, error.what());
          }
          std::lock_guard guard(data_mutex);
          submitting.erase(proof);
          if (statuses.size() >= 128)
            statuses.erase(statuses.begin());
          statuses[proof] = std::move(value);
        })) {
      std::lock_guard guard(data_mutex);
      submitting.erase(proof);
      return Status("uncertain", proof,
                    "Social verifier queue full; retry same proof");
    }
    return Status("queued", proof);
  }
  UniValue Read(const std::string &requested, uint64_t cursor, unsigned limit) {
    if (limit < 1 || limit > 50)
      throw std::runtime_error("Social page must contain1through50 records");
    std::lock_guard guard(data_mutex);
    auto out = Obj();
    out.pushKV("genesis", Genesis());
    out.pushKV("generation", generation);
    out.pushKV("admission_floor_ms", admission_floor_ms);
    out.pushKV("private_migration_floor_ms", private_migration_floor_ms);
    if (!failure.empty()) {
      out.pushKV("error", failure);
      return out;
    }
    const bool reset = (!requested.empty() && requested != generation) ||
                       cursor > sequence ||
                       (!records.empty() && cursor < records.front().seq - 1);
    if (reset)
      cursor = records.empty() ? sequence : records.front().seq - 1;
    UniValue array(UniValue::VARR);
    size_t bytes = 0;
    uint64_t next = cursor;
    bool more = false;
    for (const auto &r : records) {
      if (r.seq <= cursor)
        continue;
      if (array.size() >= limit || bytes + r.bytes > MAX_FRAME - 1024) {
        more = true;
        break;
      }
      auto item = Obj();
      item.pushKV("seq", r.seq);
      item.pushKV("canonical_message", r.message);
      item.pushKV("signature", r.signature);
      array.push_back(item);
      bytes += r.bytes;
      next = r.seq;
    }
    out.pushKV("records", array);
    out.pushKV("next_cursor", next);
    out.pushKV("more", more);
    out.pushKV("reset", reset);
    out.pushKV("loading", !loaded || recovering);
    out.pushKV("retention",
               "bounded16MiB/2000records; oldest records may be pruned");
    return out;
  }
  UniValue PeerInfo() {
    auto out = Obj();
    out.pushKV("genesis", Genesis());
    out.pushKV("enabled", true);
    out.pushKV("storage_recovery_pending", recovery_needed.load());
    out.pushKV("storage_recovering", recovering.load());
    out.pushKV("storage_recovery_attempts", recovery_attempts.load());
    out.pushKV("storage_recoveries", recoveries.load());
    out.pushKV("loading", !loaded || recovering);
    out.pushKV("rejected", rejected.load());
    out.pushKV("dropped", dropped.load());
    out.pushKV("committed", committed.load());
    out.pushKV("storage_schema", "PGN_SOCIAL_STORE_V3");
    {
      std::lock_guard guard(data_mutex);
      out.pushKV("admission_floor_ms", admission_floor_ms);
      out.pushKV("private_migration_floor_ms", private_migration_floor_ms);
    }
    out.pushKV("component_version", PGN_CORE_COMPONENT_VERSION);
    out.pushKV("storage_batches_since_start", storage_batches.load());
    out.pushKV("storage_logical_bytes_since_start",
               storage_logical_bytes.load());
    UniValue array(UniValue::VARR);
    {
      std::lock_guard guard(links_mutex);
      for (const auto &weak : links)
        if (auto link = weak.lock(); link && !link->closed && link->capable) {
          auto item = Obj();
          item.pushKV("node_id", link->id);
          item.pushKV("matching_capability", true);
          array.push_back(item);
        }
    }
    out.pushKV("relay_peers", array);
    out.pushKV("wallet_presence_proven", false);
    {
      std::lock_guard guard(queue_mutex);
      out.pushKV("waiting", uint64_t(waiting));
      out.pushKV("active", uint64_t(active.size()));
    }
    return out;
  }
};
std::atomic<Service *> existing_service{nullptr};
Service &Get() {
  static Service service;
  existing_service.store(&service);
  return service;
}
} // namespace
bool Enabled() { return gArgs.GetBoolArg("-pgnsocial", true); }
void Start() {
  if (Enabled())
    (void)Get();
}
void Shutdown() {
  if (auto *service = existing_service.load())
    service->Shutdown();
}
std::shared_ptr<Link> Open(int64_t node) {
  return Enabled() ? Get().Connect(node) : nullptr;
}
void Close(const std::shared_ptr<Link> &link) {
  if (link)
    link->closed = true;
}
bool Receive(const std::shared_ptr<Link> &link, std::string bytes) {
  return link && Enabled() && Get().Input(link, std::move(bytes));
}
void Tick(const std::shared_ptr<Link> &link) {
  if (link)
    Get().Pump(link);
}
std::optional<std::string> Drain(const std::shared_ptr<Link> &link) {
  if (!link || link->closed)
    return {};
  std::unique_lock guard(link->output_mutex, std::try_to_lock);
  if (!guard.owns_lock() || link->output.empty())
    return {};
  auto bytes = std::move(link->output.front());
  link->output.pop_front();
  return bytes;
}
UniValue Submit(const std::string &message, const std::string &signature) {
  if (!Enabled())
    return Status("rejected", {}, "Core Social relay is disabled");
  return Get().SubmitProof(message, signature);
}
UniValue Events(const std::string &generation, uint64_t cursor,
                unsigned limit) {
  if (!Enabled())
    throw std::runtime_error("Core Social relay is disabled");
  return Get().Read(generation, cursor, limit);
}
UniValue Peers() {
  if (!Enabled()) {
    auto out = Obj();
    out.pushKV("enabled", false);
    out.pushKV("genesis", Genesis());
    return out;
  }
  return Get().PeerInfo();
}
} // namespace pgn::social
