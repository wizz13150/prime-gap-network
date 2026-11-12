// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license; see COPYING.
#include <pgn/social.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <rpc/util.h>

namespace {
RPCMethod submitsocialevent() {
  return RPCMethod{
      "submitsocialevent",
      "Queue one bounded off-chain signed Social chat/receipt. accepted_local "
      "means durable here, not recipient delivery. No wallet key or client "
      "username accepted.",
      {{"canonical_message", RPCArg::Type::STR, RPCArg::Optional::NO,
        "Exact domain-separated canonical source"},
       {"signature", RPCArg::Type::STR, RPCArg::Optional::NO,
        "Native BIP322 SIMPLE proof"}},
      RPCResult{RPCResult::Type::OBJ_DYN,
                "",
                "Explicit queue/commit state",
                {{RPCResult::Type::ANY, "field", "State and provenance"}}},
      RPCExamples{""},
      [](const RPCMethod &, const JSONRPCRequest &request) -> UniValue {
        return pgn::social::Submit(request.params[0].get_str(),
                                   request.params[1].get_str());
      }};
}
RPCMethod getsocialevents() {
  return RPCMethod{
      "getsocialevents",
      "Read a bounded page of natively verified off-chain Social records. "
      "Cursor is local to this store generation; reset and pruning are "
      "explicit.",
      {{"generation", RPCArg::Type::STR, RPCArg::Default{""},
        "Previous store generation, or empty for first read"},
       {"cursor", RPCArg::Type::NUM, RPCArg::Default{0},
        "Last local sequence read"},
       {"limit", RPCArg::Type::NUM, RPCArg::Default{50},
        "One through50; total response records also bounded24KiB"}},
      RPCResult{RPCResult::Type::OBJ_DYN,
                "",
                "Verified records and next cursor",
                {{RPCResult::Type::ANY, "field", "Page and provenance"}}},
      RPCExamples{""},
      [](const RPCMethod &, const JSONRPCRequest &request) -> UniValue {
        const auto generation = request.params[0].isNull()
                                    ? std::string{}
                                    : request.params[0].get_str();
        const auto cursor = request.params[1].isNull()
                                ? 0
                                : request.params[1].getInt<int64_t>();
        const auto limit =
            request.params[2].isNull() ? 50 : request.params[2].getInt<int>();
        if (generation.size() > 64 || cursor < 0 || limit < 1 || limit > 50)
          throw JSONRPCError(RPC_INVALID_PARAMETER,
                             "Bounded Social cursor/page required");
        try {
          return pgn::social::Events(generation, uint64_t(cursor),
                                     unsigned(limit));
        } catch (const std::exception &error) {
          throw JSONRPCError(RPC_MISC_ERROR, error.what());
        }
      }};
}
RPCMethod getsocialpeers() {
  return RPCMethod{
      "getsocialpeers",
      "Read matching-capability PGN relay links and Social-only queue state. "
      "Relay peers are not authenticated wallet users, online miners or "
      "moderator identities.",
      {},
      RPCResult{RPCResult::Type::OBJ_DYN,
                "",
                "Local relay state",
                {{RPCResult::Type::ANY, "field", "Capabilities and queue"}}},
      RPCExamples{""},
      [](const RPCMethod &, const JSONRPCRequest &) -> UniValue {
        return pgn::social::Peers();
      }};
}
} // namespace
void RegisterPgnSocialRPCCommands(CRPCTable &table) {
  static const CRPCCommand commands[]{{"social", &submitsocialevent},
                                      {"social", &getsocialevents},
                                      {"social", &getsocialpeers}};
  for (const auto &command : commands)
    table.appendCommand(command.name, &command);
}
