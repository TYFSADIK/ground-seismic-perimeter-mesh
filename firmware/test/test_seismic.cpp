// Host tests: c++ -std=c++17 -O2 -Ilib/seismic test/test_seismic.cpp -o /tmp/t && /tmp/t [--emit]
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include "mesh.h"
using namespace seismic;

static void gen(float* x, int win, const char* kind, std::mt19937& rng) {
  std::normal_distribution<float> n(0, 1);
  for (int i = 0; i < kN; ++i) {
    const float t = (win * kN + i) / float(kFs);
    float v = 0.0005f * n(rng);                              // sensor floor
    if (!strcmp(kind, "vehicle")) v += 0.02f * std::sin(2 * M_PI * 9 * t) + 0.01f * std::sin(2 * M_PI * 14 * t);
    if (!strcmp(kind, "foot")) {                             // 2 steps/s, ~40 Hz damped impacts
      float ph = std::fmod(t, 0.5f);
      v += 0.05f * std::exp(-ph * 60) * std::sin(2 * M_PI * 40 * ph);
    }
    if (!strcmp(kind, "wind")) v += 0.004f * n(rng) * (1 + 0.3f * std::sin(2 * M_PI * 0.2 * t));
    x[i] = v;
  }
}

static Label run(const char* kind, int windows = 8) {
  std::mt19937 rng(3); FeatureExtractor fx; Classifier c; float x[kN]; Decision d{};
  for (int w = 0; w < windows; ++w) { gen(x, w, kind, rng); d = c.classify(fx.compute(x)); }
  return d.label;
}

int main(int argc, char** argv) {
  // SHA-256 / HMAC known answers (RFC 4231 test case 2)
  {
    const char* key = "Jefe"; const char* msg = "what do ya want for nothing?";
    auto m = hmac_sha256((const uint8_t*)key, 4, (const uint8_t*)msg, strlen(msg));
    const uint8_t want[4] = {0x5b, 0xdc, 0xc1, 0x46};
    assert(memcmp(m.data(), want, 4) == 0);
  }
  assert(run("quiet") == Label::Quiet);
  assert(run("vehicle") == Label::Vehicle);
  assert(run("foot") == Label::Footsteps);
  assert(run("wind") == Label::Wind);

  const uint8_t key[32] = {1, 2, 3};
  Features f; f.rms = 0.012f; f.band = {0.7f, 0.2f, 0.1f};
  Packet p = make_detection(7, 42, 1000, f, {Label::Vehicle, Rule::LowBandSustained, 0.7f});
  sign(p, key, 32);
  assert(crc_ok(p) && verify(p, key, 32));
  Relay r; Packet q = p;
  assert(r.should_forward(q, 3) && q.hops_left == 3 && verify(q, key, 32));   // relays don't break the tag
  Packet dup = p; assert(!r.should_forward(dup, 3));                         // dedup
  Packet bad = p; bad.label = uint8_t(Label::Footsteps); assert(!verify(bad, key, 32));
  StoreForward<4> sf; for (int i = 0; i < 6; ++i) { Packet x = p; x.seq = i; sf.push(x); }
  Packet o; assert(sf.size() == 4 && sf.dropped() == 2 && sf.pop(o) && o.seq == 2 && o.replayed == 1);

  if (argc > 1 && !strcmp(argv[1], "--emit")) {   // for the Python gateway cross-check
    for (size_t i = 0; i < sizeof(p); ++i) std::printf("%02x", reinterpret_cast<uint8_t*>(&p)[i]);
    std::printf("\n");
    return 0;
  }
  std::puts("seismic host tests passed");
}
