#pragma once

#include <math.h>

// 2nd-order Butterworth low-pass (RBJ biquad), used as the anti-alias filter
// before the PC stream drops from 48 to 16 kHz. Input and output in ADC LSB
// around the rest level.
class LowPass {
 public:
  void init(float rate, float cutoff_hz) {
    float w = 2.0f * (float)M_PI * cutoff_hz / rate;
    float alpha = sinf(w) / (2.0f * 0.70710678f);
    float c = cosf(w), a0 = 1.0f + alpha;
    b0 = (1.0f - c) / 2.0f / a0;
    b1 = (1.0f - c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
  }

  __attribute__((always_inline)) float process(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }

 private:
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};
