// Explainable threshold classifier. Every decision carries the rule that fired.
#pragma once
#include <cstdint>
#include "seismic_features.h"

namespace seismic {

enum class Label : uint8_t { Quiet = 0, Vehicle = 1, Footsteps = 2, Wind = 3, Unknown = 4 };
enum class Rule : uint8_t { BelowNoiseGate = 0, LowBandSustained = 1, CadencePeak = 2, HighBandDiffuse = 3, NoRule = 4 };

struct Thresholds {
  float noise_gate_rms = 0.002f;     // g; set to ~3x measured quiet-night RMS per node
  float vehicle_low_frac = 0.6f;
  int   vehicle_min_windows = 3;     // sustained: a passing vehicle lasts seconds
  float foot_cadence = 0.45f;
  float foot_kurtosis = 4.0f;        // impulsive (Gaussian = 3)
  float wind_high_frac = 0.4f;
};

struct Decision { Label label; Rule rule; float score; };

class Classifier {
 public:
  explicit Classifier(Thresholds t = {}) : t_(t) {}

  Decision classify(const Features& f) {
    if (f.rms < t_.noise_gate_rms) { low_run_ = 0; return {Label::Quiet, Rule::BelowNoiseGate, 0}; }
    low_run_ = f.band[0] >= t_.vehicle_low_frac ? low_run_ + 1 : 0;
    if (f.cadence >= t_.foot_cadence && f.kurtosis >= t_.foot_kurtosis)
      return {Label::Footsteps, Rule::CadencePeak, f.cadence};
    if (low_run_ >= t_.vehicle_min_windows)
      return {Label::Vehicle, Rule::LowBandSustained, f.band[0]};
    if (f.band[2] >= t_.wind_high_frac && f.kurtosis < t_.foot_kurtosis)
      return {Label::Wind, Rule::HighBandDiffuse, f.band[2]};
    return {Label::Unknown, Rule::NoRule, 0};
  }

 private:
  Thresholds t_;
  int low_run_ = 0;
};

}  // namespace seismic
