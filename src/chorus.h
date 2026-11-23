#pragma once

#include <math.h>

#include "modulation.h"

// Two-voice chorus: two taps of one delay line swing in opposite directions
// with a slow LFO, which sounds wider than a single voice. mix is how much
// chorus is added: 0 is dry and 1 puts the two voices at the level of the dry
// signal (the strongest chorus); the output is scaled so the level stays
// about the same.
//
// The low notes get chorus too, but less of it: below LOW_HZ the voices are
// mixed in at LOW_SHARE of mix. Dry and delayed at equal level would cut a
// notch at 1 / (2 x 12 ms) = 42 Hz, right on the low E fundamental (17 to
// 36 dB down at mix 1); at LOW_SHARE the notch is at most ~6 dB.
class Chorus {
 public:
  static constexpr float CENTER_MS = 12;
  static constexpr float MAX_DEPTH_MS = 8;
  static constexpr float LOW_HZ = 120, LOW_SHARE = 0.35f;

  void init(float rate, float mix_, float depth_ms, float rate_hz) {
    fs = rate;
    center = CENTER_MS * 0.001f * rate;
    split = 1.0f - expf(-2.0f * (float)M_PI * LOW_HZ / rate);
    set_mix(mix_);
    set_depth_ms(depth_ms);
    set_rate_hz(rate_hz);
  }

  void set_mix(float m) {
    mix = m < 0 ? 0 : m > 1 ? 1 : m;
    mix_low = mix * LOW_SHARE;
    norm = 1.0f / (1.0f + mix);
    norm_low = 1.0f / (1.0f + mix_low);
  }

  // Each tap swings over CENTER_MS +- depth.
  void set_depth_ms(float ms) {
    depth_ms = ms < 0 ? 0 : ms > MAX_DEPTH_MS ? MAX_DEPTH_MS : ms;
    depth = depth_ms * 0.001f * fs;
  }

  void set_rate_hz(float hz) {
    rate_hz = hz < 0.1f ? 0.1f : hz > 5 ? 5 : hz;
    lfo.set_rate(rate_hz, fs);
  }

  float get_mix() const { return mix; }
  float get_depth_ms() const { return depth_ms; }
  float get_rate_hz() const { return rate_hz; }

  __attribute__((always_inline)) float process(float x) {
    line.push(x);
    float l = lfo.next();
    float wet = 0.5f * (line.tap(center + depth * l) + line.tap(center - depth * l));
    // One-pole split of the dry and the delayed signal at LOW_HZ.
    x_low += split * (x - x_low);
    wet_low += split * (wet - wet_low);
    float low = (x_low + mix_low * wet_low) * norm_low;
    float high = (x - x_low + mix * (wet - wet_low)) * norm;
    return low + high;
  }

 private:
  DelayLine<2048> line;  // 42 ms at 48 kHz
  Lfo lfo;
  float fs = 0, center = 0, depth = 0, split = 0;
  float mix = 0, mix_low = 0, norm = 1, norm_low = 1, depth_ms = 0, rate_hz = 0;
  float x_low = 0, wet_low = 0;
};
