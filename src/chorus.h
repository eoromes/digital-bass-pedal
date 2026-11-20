#pragma once

// Bass chorus: only the band above split_hz goes through the modulated delay,
// the lows stay dry and centered. Same constraints as NoiseGate: inline, no
// library calls (the LFO is a parabolic sine).
class Chorus {
 public:
  static constexpr unsigned SIZE = 2048;  // power of two, 42 ms at 48 kHz

  static constexpr float CENTER_MS = 12;

  void init(float rate, float mix_, float depth_ms, float rate_hz) {
    fs = rate;
    split = 6.2831853f * 150.0f / rate;
    center = CENTER_MS * 0.001f * rate;
    mix = mix_;
    set_depth_ms(depth_ms);
    set_rate_hz(rate_hz);
  }

  // Delay swings over CENTER_MS +- depth; the buffer holds 42 ms at 48 kHz.
  void set_depth_ms(float ms) {
    depth_ms = ms < 0 ? 0 : ms > 10 ? 10 : ms;
    depth = depth_ms * 0.001f * fs;
  }

  void set_rate_hz(float hz) {
    rate_hz = hz < 0.1f ? 0.1f : hz > 8 ? 8 : hz;
    lfo_step = 4.0f * rate_hz / fs;
  }

  float get_depth_ms() const { return depth_ms; }
  float get_rate_hz() const { return rate_hz; }

  __attribute__((always_inline)) float process(float x) {
    low += split * (x - low);
    float high = x - low;
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
    return x + mix * (a + frac * (b - a));
  }

  float mix = 0;

 private:
  float buf[SIZE] = {};
  float fs = 0, split = 0, lfo_step = 0, center = 0, depth = 0, depth_ms = 0, rate_hz = 0;
  float low = 0, phase = 0;
  unsigned w = 0;
};
