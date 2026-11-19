#pragma once

#include <math.h>

// Envelope filter (auto-wah) for bass: a resonant low-pass (Chamberlin state
// variable filter) whose cutoff follows the playing level, from 200 Hz when
// quiet to 2 kHz on a hard attack. A low-pass keeps the fundamental, which
// a band-pass would thin out. Input and output in ADC LSB around the rest
// level.
class AutoWah {
 public:
  static constexpr float F_MIN = 200.0f, F_MAX = 2000.0f, Q_INV = 1.0f / 4.0f;
  // At sensitivity 1, an envelope of FULL_LSB opens the filter fully. With
  // 0.7, a recorded bass take swept 210 Hz to 1.85 kHz (10th-90th percentile).
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
    return low;
  }

 private:
  float att = 0, rel = 0, k_min = 0, k_span = 0, sens = 1, scale = 0;
  float env = 0, low = 0, band = 0;
};
