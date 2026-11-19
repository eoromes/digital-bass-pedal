#pragma once

#include <math.h>

// Envelope filter (auto-wah) for bass: a resonant band-pass (Chamberlin state
// variable filter) whose centre follows the playing level, from 120 Hz when
// quiet to 900 Hz on a hard attack, mixed with some of the dry signal to keep
// the fundamental. Input and output in ADC LSB around the rest level.
//
// Tuned on a recorded take: a sweep up to 2 kHz, or a low-pass response,
// barely changed the sound, because almost all of the bass energy is below
// ~500 Hz. The band-pass alone is ~19x quieter than the input, hence WET.
class AutoWah {
 public:
  static constexpr float F_MIN = 120.0f, F_MAX = 900.0f, Q_INV = 1.0f / 5.0f;
  static constexpr float DRY = 0.3f, WET = 10.0f;
  // At sensitivity 1, an envelope of FULL_LSB opens the filter fully. With
  // 0.7, the recorded take swept 124 Hz to 800 Hz (10th-90th percentile).
  static constexpr float FULL_LSB = 400.0f;

  void init(float rate, float sensitivity) {
    att = 1.0f - expf(-1.0f / (0.005f * rate));
    rel = 1.0f - expf(-1.0f / (0.120f * rate));
    k_min = 2.0f * (float)M_PI * F_MIN / rate;
    k_span = 2.0f * (float)M_PI * (F_MAX - F_MIN) / rate;
    set_sensitivity(sensitivity);
  }

  void set_sensitivity(float s) {
    sens = s < 0.2f ? 0.2f : s > 2.0f ? 2.0f : s;
    scale = sens / FULL_LSB;
  }

  float get_sensitivity() const { return sens; }

  __attribute__((always_inline)) float process(float x) {
    float a = x < 0 ? -x : x;
    env += (a > env ? att : rel) * (a - env);
    float c = env * scale;
    c = c > 1.0f ? 1.0f : c;
    float f = k_min + k_span * c;
    low += f * band;
    float high = x - low - Q_INV * band;
    band += f * high;
    return DRY * x + WET * Q_INV * band;
  }

 private:
  float att = 0, rel = 0, k_min = 0, k_span = 0, sens = 1, scale = 0;
  float env = 0, low = 0, band = 0;
};
