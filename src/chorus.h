#pragma once

// Chorus on the whole signal, low notes included: a delayed copy whose delay
// swings with a slow LFO is mixed with the dry signal. mix crossfades the
// two: 0 is dry, 0.5 is dry and delayed at equal level (the deepest chorus)
// and 1 is the delayed copy alone (vibrato).
//
// process() runs in the sampling loop: inline, no library calls (the LFO is a
// parabolic sine).
class Chorus {
 public:
  static constexpr unsigned SIZE = 1024;  // power of two, 21 ms at 48 kHz

  static constexpr float CENTER_MS = 8;
  static constexpr float MAX_DEPTH_MS = 4;

  void init(float rate, float mix_, float depth_ms, float rate_hz) {
    fs = rate;
    center = CENTER_MS * 0.001f * rate;
    set_mix(mix_);
    set_depth_ms(depth_ms);
    set_rate_hz(rate_hz);
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

  float get_mix() const { return mix; }
  float get_depth_ms() const { return depth_ms; }
  float get_rate_hz() const { return rate_hz; }

  __attribute__((always_inline)) float process(float x) {
    w = (w + 1) & (SIZE - 1);
    buf[w] = x;

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
    return x + mix * (wet - x);
  }

 private:
  float buf[SIZE] = {};
  float fs = 0, lfo_step = 0, center = 0, depth = 0;
  float mix = 0, depth_ms = 0, rate_hz = 0;
  float phase = 0;
  unsigned w = 0;
};
