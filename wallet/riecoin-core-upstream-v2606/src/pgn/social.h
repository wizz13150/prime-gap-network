// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license; see COPYING.
#ifndef PGN_SOCIAL_H
#define PGN_SOCIAL_H
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <univalue.h>

namespace pgn::social {
inline constexpr size_t MAX_FRAME = 24576;
// A PGN transport link is not a wallet identity or moderator capability.
struct Link {
  const int64_t id;
  explicit Link(int64_t node) : id(node) {}
  std::atomic<bool> closed{false}, capable{false}, tick_pending{false};
  std::mutex output_mutex;
  std::deque<std::string> output;
  // Accessed only by the single active Social job for this link.
  std::string remote_generation;
  uint64_t remote_cursor{};
  int64_t last_request_ms{};
};
bool Enabled();
void Start();
void Shutdown();
std::shared_ptr<Link> Open(int64_t node);
void Close(const std::shared_ptr<Link> &);
bool Receive(const std::shared_ptr<Link> &, std::string bounded_frame);
void Tick(const std::shared_ptr<Link> &);
std::optional<std::string> Drain(const std::shared_ptr<Link> &);
UniValue Submit(const std::string &canonical_message,
                const std::string &signature);
UniValue Events(const std::string &generation, uint64_t cursor, unsigned limit);
UniValue Peers();
} // namespace pgn::social
#endif
