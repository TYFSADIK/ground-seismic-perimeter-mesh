// Window (1024 samples @ 1 kHz) -> feature vector: RMS, 3 band energies, kurtosis, footstep cadence.
// The feature vector, not the waveform, is all the mesh ever carries.
#pragma once
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include "fft.h"

namespace seismic {

constexpr int kFs = 1000;
constexpr int kN = 1024;
constexpr int kSub = 64;               // envelope sub-window -> 15.6 Hz envelope rate
constexpr int kEnvHist = 64;           // ~4.1 s of envelope history for cadence

// Band plan (engineering estimate; retune from measured spectra).
//   LOW  2-20 Hz   vehicles (engine/tyre), SM-24 resonance at 10 Hz
//   MID  20-60 Hz  footstep impacts
//   HIGH 60-200 Hz wind-coupled vegetation / sensor noise
struct Band { float lo, hi; };
constexpr std::array<Band, 3> kBands{{{2, 20}, {20, 60}, {60, 200}}};

struct Features {
  float rms = 0;
  std::array<float, 3> band{};   // fraction of 2-200 Hz energy per band (sums to ~1)
  float kurtosis = 0;            // impulsiveness: footsteps >> wind
  float cadence = 0;             // envelope autocorrelation peak in walking band (0..1)
  float cadence_hz = 0;
};

class FeatureExtractor {
 public:
  Features compute(const float* x) {
    Features f;
    float mean = 0;
    for (int i = 0; i < kN; ++i) mean += x[i];
    mean /= kN;
    float m2 = 0, m4 = 0;
    for (int i = 0; i < kN; ++i) {
      const float d = x[i] - mean;
      m2 += d * d; m4 += d * d * d * d;
      // Hann window into FFT buffer
      buf_[i] = {d * 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * i / (kN - 1))), 0.0f};
    }
    m2 /= kN; m4 /= kN;
    f.rms = std::sqrt(m2);
    f.kurtosis = m2 > 0 ? m4 / (m2 * m2) : 0;
    fft(buf_.data(), kN);
    float total = 0;
    std::array<float, 3> e{};
    for (int k = 1; k < kN / 2; ++k) {
      const float hz = float(k) * kFs / kN;
      const float p = std::norm(buf_[k]);
      for (size_t b = 0; b < kBands.size(); ++b)
        if (hz >= kBands[b].lo && hz < kBands[b].hi) { e[b] += p; total += p; }
    }
    for (size_t b = 0; b < 3; ++b) f.band[b] = total > 0 ? e[b] / total : 0;
    // Envelope: RMS of 64-sample sub-windows, appended to a rolling history.
    for (int s = 0; s < kN / kSub; ++s) {
      float acc = 0;
      for (int i = 0; i < kSub; ++i) { const float d = x[s * kSub + i] - mean; acc += d * d; }
      env_[env_pos_] = std::sqrt(acc / kSub);
      env_pos_ = (env_pos_ + 1) % kEnvHist;
      if (env_count_ < kEnvHist) ++env_count_;
    }
    cadence(f);
    return f;
  }

 private:
  void cadence(Features& f) {
    if (env_count_ < kEnvHist) return;
    const float env_rate = float(kFs) / kSub;
    std::array<float, kEnvHist> e{};
    float mean = 0;
    for (int i = 0; i < kEnvHist; ++i) { e[i] = env_[(env_pos_ + i) % kEnvHist]; mean += e[i]; }
    mean /= kEnvHist;
    float r0 = 0;
    for (auto& v : e) { v -= mean; r0 += v * v; }
    if (r0 <= 0) return;
    // Walking cadence 1.4-2.6 steps/s
    const int lag_lo = int(env_rate / 2.6f), lag_hi = int(env_rate / 1.4f) + 1;
    for (int lag = lag_lo; lag <= lag_hi; ++lag) {
      float r = 0;
      for (int i = 0; i + lag < kEnvHist; ++i) r += e[i] * e[i + lag];
      r /= r0;
      if (r > f.cadence) { f.cadence = r; f.cadence_hz = env_rate / lag; }
    }
  }

  std::array<std::complex<float>, kN> buf_{};
  std::array<float, kEnvHist> env_{};
  int env_pos_ = 0, env_count_ = 0;
};

}  // namespace seismic
