#pragma once

#include <math.h>

// Bass chorus laid out like the classic bass chorus pedals: a 12 dB/octave
// high-pass (the "low filter", 100 Hz by default) feeds the modulated delay,
// so the lows below it stay dry, in tune and centered. mix crossfades only the
// band above it: 0 is dry, 0.5 is dry and delayed at equal level (the deepest
// chorus) and 1 is the delayed copy alone (vibrato).
//
// process() runs in the sampling loop: inline, no library calls (the LFO is a
// parabolic sine). The setters run on the other core and may call libm.
class Chorus {
 public:
  static constexpr unsigned SIZE = 512;  // power of two, 32 ms at 16 kHz

  static constexpr float CENTER_MS = 8;
  static constexpr float MAX_DEPTH_MS = 4;

  void init(float rate, float mix_, float depth_ms, float rate_hz, float split_hz) {
    fs = rate;
    center = CENTER_MS * 0.001f * rate;
    set_mix(mix_);
    set_depth_ms(depth_ms);
    set_rate_hz(rate_hz);
    set_split_hz(split_hz);
  }

  void set_mix(float m) { mix = m < 0 ? 0 : m > 1 ? 1 : m; }

  // Delay swings over CENTER_MS +- depth.
  void set_depth_ms(float ms) {
    depth_ms = ms < 0 ? 0 : ms > MAX_DEPTH_MS ? MAX_DEPTH_MS : ms;
    depth = depth_ms * 0.001f * fs;
  }

  void set_rate_hz(float hz) {
    rate_hz = hz < 0.1f ? 0.1f : hz > 3 ? 3 : hz;
    lfo_step = 4.0f * rate_hz / fs;
  }

  // Butterworth high-pass as a state-variable filter, which stays stable when
  // the cutoff changes while it runs.
  void set_split_hz(float hz) {
    split_hz = hz < 50 ? 50 : hz > 500 ? 500 : hz;
    float g = tanf(3.14159265f * split_hz / fs);
    float a1 = 1.0f / (1.0f + g * (g + K));
    hp_a2 = g * a1;
    hp_a3 = g * hp_a2;
    hp_a1 = a1;
  }

  float get_mix() const { return mix; }
  float get_depth_ms() const { return depth_ms; }
  float get_rate_hz() const { return rate_hz; }
  float get_split_hz() const { return split_hz; }

  __attribute__((always_inline)) float process(float x) {
    float v3 = x - ic2;
    float v1 = hp_a1 * ic1 + hp_a2 * v3;
    float v2 = ic2 + hp_a2 * ic1 + hp_a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    float high = x - K * v1 - v2;

    w = (w + 1) & (SIZE - 1);
    buf[w] = high;

    // Phase runs over [-2, 2): triangle in [-1, 1], then shaped into a sine.
    phase += lfo_step;
    if (phase >= 2.0f) phase -= 4.0f;
    float tri = phase < -1.0f ? -2.0f - phase : phase > 1.0f ? 2.0f - phase : phase;
    float lfo = tri * (2.0f - (tri < 0 ? -tri : tri));

    // w stays wrapped: a float of an ever-growing index would lose precision.
    float pos = (float)(w + SIZE) - (center + depth * lfo);
    unsigned i = (unsigned)pos;
    float frac = pos - (float)i;
    float a = buf[i & (SIZE - 1)], b = buf[(i + 1) & (SIZE - 1)];
    float wet = a + frac * (b - a);
    return x + mix * (wet - high);
  }

 private:
  static constexpr float K = 1.41421356f;  // 1/Q, Butterworth

  float buf[SIZE] = {};
  float fs = 0, lfo_step = 0, center = 0, depth = 0;
  float mix = 0, depth_ms = 0, rate_hz = 0, split_hz = 0;
  float hp_a1 = 0, hp_a2 = 0, hp_a3 = 0, ic1 = 0, ic2 = 0;
  float phase = 0;
  unsigned w = 0;
};
