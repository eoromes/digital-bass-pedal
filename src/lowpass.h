#pragma once

#include <math.h>

// 2nd-order Butterworth low-pass (RBJ biquad). Cuts the ADC hiss above the
// bass range: most of the hiss energy is in the treble, where the bass has
// little. Input and output in ADC LSB around the rest level.
class LowPass {
 public:
  void init(float rate, float cutoff_hz) {
    fs = rate;
    set_cutoff(cutoff_hz);
  }

  void set_cutoff(float hz) {
    cutoff = hz < 1500.0f ? 1500.0f : hz > 7500.0f ? 7500.0f : hz;
    float w = 2.0f * (float)M_PI * cutoff / fs;
    float alpha = sinf(w) / (2.0f * 0.70710678f);
    float c = cosf(w), a0 = 1.0f + alpha;
    b0 = (1.0f - c) / 2.0f / a0;
    b1 = (1.0f - c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
  }

  float get_cutoff() const { return cutoff; }

  __attribute__((always_inline)) float process(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }

 private:
  float fs = 16000, cutoff = 3000;
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};
