// LoRa mesh: fixed-format signed packets, controlled flooding with hop limit + (src,seq) dedup,
// and a store-and-forward ring for detections when the gateway is unreachable (T6 outage replay).
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "classifier.h"
#include "sha256.h"

namespace seismic {

constexpr uint8_t kVersion = 1;
enum class PktType : uint8_t { Detection = 1, Heartbeat = 2, Ack = 3 };

#pragma pack(push, 1)
struct Packet {
  uint8_t version = kVersion;
  uint8_t type = uint8_t(PktType::Detection);
  uint8_t src = 0;
  uint8_t hops_left = 4;           // kill switch for flooding; NOT covered by the tag (relays decrement it)
  uint16_t seq = 0;                // per-source
  uint32_t t_s = 0;                // node uptime or GPS/RTC seconds
  uint8_t label = 0, rule = 0;
  uint16_t rms_ug = 0;             // RMS in micro-g (saturating)
  uint8_t band_pct[3] = {0, 0, 0};
  uint8_t cadence_pct = 0;
  uint8_t replayed = 0;            // 1 if sent from the store-and-forward buffer
  uint8_t tag[8] = {0};            // truncated HMAC-SHA256 over everything except hops_left and tag
  uint16_t crc = 0;
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 29, "packet layout changed: update gateway/decode.py");

inline uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; ++i) { c ^= uint16_t(d[i]) << 8; for (int b = 0; b < 8; ++b) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1; }
  return c;
}

inline void auth_bytes(const Packet& p, uint8_t* out, size_t& n) {
  Packet c = p; c.hops_left = 0; std::memset(c.tag, 0, 8); c.crc = 0;
  n = offsetof(Packet, tag);
  std::memcpy(out, &c, n);
}

inline void sign(Packet& p, const uint8_t* key, size_t klen) {
  uint8_t buf[sizeof(Packet)]; size_t n;
  auth_bytes(p, buf, n);
  auto mac = hmac_sha256(key, klen, buf, n);
  std::memcpy(p.tag, mac.data(), 8);
  p.crc = crc16(reinterpret_cast<const uint8_t*>(&p), sizeof(p) - 2);
}

inline bool crc_ok(const Packet& p) { return p.crc == crc16(reinterpret_cast<const uint8_t*>(&p), sizeof(p) - 2); }

inline bool verify(const Packet& p, const uint8_t* key, size_t klen) {
  uint8_t buf[sizeof(Packet)]; size_t n;
  auth_bytes(p, buf, n);
  auto mac = hmac_sha256(key, klen, buf, n);
  uint8_t diff = 0;
  for (int i = 0; i < 8; ++i) diff |= mac[i] ^ p.tag[i];   // constant time
  return diff == 0;
}

inline Packet make_detection(uint8_t src, uint16_t seq, uint32_t t, const Features& f, const Decision& d) {
  Packet p; p.src = src; p.seq = seq; p.t_s = t;
  p.label = uint8_t(d.label); p.rule = uint8_t(d.rule);
  const float ug = f.rms * 1e6f; p.rms_ug = ug > 65535 ? 65535 : uint16_t(ug);
  for (int i = 0; i < 3; ++i) p.band_pct[i] = uint8_t(f.band[i] * 100 + 0.5f);
  p.cadence_pct = uint8_t((f.cadence < 0 ? 0 : f.cadence) * 100 + 0.5f);
  return p;
}

// Flooding relay: returns true if this node should rebroadcast `p` (after decrementing hops).
class Relay {
 public:
  bool should_forward(Packet& p, uint8_t self) {
    if (!crc_ok(p) || p.src == self) return false;
    const uint32_t key = uint32_t(p.src) << 16 | p.seq;
    for (auto k : seen_) if (k == key) return false;
    seen_[pos_] = key; pos_ = (pos_ + 1) % seen_.size();
    if (p.hops_left == 0) return false;
    --p.hops_left;
    p.crc = crc16(reinterpret_cast<const uint8_t*>(&p), sizeof(p) - 2);
    return true;
  }
 private:
  std::array<uint32_t, 64> seen_{};
  size_t pos_ = 0;
};

// Store-and-forward: keep un-acked detections; replay oldest-first when the gateway is back.
template <size_t N = 128>
class StoreForward {
 public:
  void push(const Packet& p) {
    buf_[head_] = p; head_ = (head_ + 1) % N;
    if (count_ < N) ++count_; else { tail_ = (tail_ + 1) % N; ++dropped_; }
  }
  bool pop(Packet& out) {
    if (!count_) return false;
    out = buf_[tail_]; out.replayed = 1; tail_ = (tail_ + 1) % N; --count_;
    return true;
  }
  size_t size() const { return count_; }
  size_t dropped() const { return dropped_; }
 private:
  std::array<Packet, N> buf_{};
  size_t head_ = 0, tail_ = 0, count_ = 0, dropped_ = 0;
};

}  // namespace seismic
