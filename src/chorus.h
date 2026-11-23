#pragma once

// Two-voice chorus on the whole signal, low notes included: two taps of one
// delay line swing in opposite directions with a slow LFO, which sounds wider
// than a single voice. mix is how much chorus is added: 0 is dry and 1 puts
// the two voices at the level of the dry signal (the strongest chorus); the
// output is scaled so the level stays about the same.
//
// process() runs in the sampling loop: inline, no library calls (the LFO is a
// parabolic sine).
class Chorus {
 public:
  static constexpr unsigned SIZE = 2048;  // power of two, 42 ms at 48 kHz

  static constexpr float CENTER_MS = 12;
  static constexpr float MAX_DEPTH_MS = 8;

  void init(float rate, float mix_, float depth_ms, float rate_hz) {
    fs = rate;
    center = CENTER_MS * 0.001f * rate;
    set_mix(mix_);
    set_depth_ms(depth_ms);
    set_rate_hz(rate_hz);
  }

  void set_mix(float m) { mix = m < 0 ? 0 : m > 1 ? 1 : m; }

  // Each tap swings over CENTER_MS +- depth.
  void set_depth_ms(float ms) {
    depth_ms = ms < 0 ? 0 : ms > MAX_DEPTH_MS ? MAX_DEPTH_MS : ms;
    depth = depth_ms * 0.001f * fs;
  }

  void set_rate_hz(float hz) {
    rate_hz = hz < 0.1f ? 0.1f : hz > 5 ? 5 : hz;
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

    float wet = 0.5f * (tap(center + depth * lfo) + tap(center - depth * lfo));
    return (x + mix * wet) / (1.0f + mix);
  }

 private:
  // Linear interpolation; w stays wrapped, since a float of an ever-growing
  // index would lose precision.
  __attribute__((always_inline)) float tap(float delay) {
    float pos = (float)(w + SIZE) - delay;
    unsigned i = (unsigned)pos;
    float frac = pos - (float)i;
    float a = buf[i & (SIZE - 1)], b = buf[(i + 1) & (SIZE - 1)];
    return a + frac * (b - a);
  }

  float buf[SIZE] = {};
  float fs = 0, lfo_step = 0, center = 0, depth = 0;
  float mix = 0, depth_ms = 0, rate_hz = 0;
  float phase = 0;
  unsigned w = 0;
};
