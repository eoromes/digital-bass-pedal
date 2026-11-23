#pragma once

// Soft limiter at the end of the chain, in ADC LSB around the rest level. One
// LSB is 2^20 in the DAC's 32-bit slot, so DAC full scale is +-2048 LSB.
// Below KNEE it passes the signal untouched; above, a cubic curve bends it
// smoothly to CEIL, where it stays. The flanger's resonant peaks and loud
// notes through the distortion get rounded off instead of hard-clipped by the
// DAC.
struct Limiter {
  static constexpr float KNEE = 1500.0f, CEIL = 2000.0f;
  // Excess over KNEE at which the curve reaches CEIL (slope 1 at the knee).
  static constexpr float W = 1.5f * (CEIL - KNEE);

  __attribute__((always_inline)) static float process(float s) {
    float a = s < 0 ? -s : s;
    if (a <= KNEE) return s;
    float e = (a - KNEE) * (1.0f / W);
    e = e > 1.0f ? 1.0f : e;
    float y = KNEE + (CEIL - KNEE) * 1.5f * (e - e * e * e * (1.0f / 3.0f));
    return s < 0 ? -y : y;
  }
};
