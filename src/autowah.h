#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

// Auto-wah for bass: a resonant band-pass (Chamberlin state variable filter)
// swept from 120 Hz to 1 kHz, mixed with some of the dry signal to keep the
// fundamental. Two modes: the envelope of the playing level moves it (in dB,
// so it keeps moving as a note decays), or a sine LFO moves it back and forth
// on its own. Input and output in ADC LSB around the rest level.
//
// Tuned on a recorded take. The band-pass gain rises with the centre
// frequency (WET_LO at 120 Hz to WET_HI at 1 kHz): at 120 Hz it sits on the
// fundamental, at 1 kHz only on weak harmonics, and a fixed gain made one end
// 5x too loud or the other inaudible. A soft limiter rounds the rare peaks.
class AutoWah {
 public:
  static constexpr float F_MIN = 120.0f, F_MAX = 1000.0f, Q_INV = 1.0f / 5.0f;
  static constexpr float DRY = 0.25f, WET_LO = 1.5f, WET_HI = 30.0f, LIMIT = 1800.0f;
  // Envelope mode: at sensitivity 1, an envelope of TOP_LSB opens the filter
  // fully and RANGE_DB below it closes it. With 1.5 on the recorded take, the
  // centre moved between ~270 and ~730 Hz (10th-90th percentile).
  static constexpr float TOP_LSB = 1350.0f, RANGE_DB = 30.0f;
  static constexpr unsigned CONTROL = 16;

  // Per-control-step math without libm calls (powf/log10f/cosf cost ~10k
  // cycles once per 16 samples): log2/exp2 from the float bits, cosine table.
  static float fast_log2(float x) {
    uint32_t b;
    memcpy(&b, &x, 4);
    float e = (float)((int)((b >> 23) & 255) - 128);
    b = (b & 0x7FFFFF) | 0x3F800000;
    float m;
    memcpy(&m, &b, 4);
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
  }

  static float fast_exp2(float p) {
    float fl = p < 0 ? (float)(int)p - 1.0f : (float)(int)p;
    float f = p - fl;
    uint32_t b = (uint32_t)((int)fl + 127) << 23;
    float s;
    memcpy(&s, &b, 4);
    return s * (1.0f + f * (0.6565f + f * 0.3435f));
  }

  void init(float rate, float sensitivity, float lfo_hz) {
    fs = rate;
    for (int i = 0; i < 256; i++) cos_table[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / 256);
    k_min = 2.0f * (float)M_PI * F_MIN / rate;
    oct_f = log2f(F_MAX / F_MIN);
    oct_w = log2f(WET_HI / WET_LO);
    att = 1.0f - expf(-1.0f / (0.005f * rate));
    rel = 1.0f - expf(-1.0f / (0.120f * rate));
    set_sensitivity(sensitivity);
    set_lfo_hz(lfo_hz);
  }

  void set_sensitivity(float s) {
    sens = s < 0.25f ? 0.25f : s > 8.0f ? 8.0f : s;
    range_oct = RANGE_DB / 6.0206f;
    bottom_oct = log2f(TOP_LSB / sens) - range_oct;
  }

  void set_lfo_hz(float hz) {
    lfo_hz = hz < 0.2f ? 0.2f : hz > 6.0f ? 6.0f : hz;
    lfo_step = (uint32_t)(4294967296.0 * lfo_hz * CONTROL / fs);
  }

  float get_sensitivity() const { return sens; }
  float get_lfo_hz() const { return lfo_hz; }

  bool lfo = false;

  __attribute__((always_inline)) float process(float x) {
    float a = x < 0 ? -x : x;
    env += (a > env ? att : rel) * (a - env);
    if (++count == CONTROL) {
      count = 0;
      float c;
      if (lfo) {
        phase += lfo_step;
        c = cos_table[phase >> 24];
      } else {
        c = (fast_log2(env + 1e-3f) - bottom_oct) / range_oct;
        c = c < 0 ? 0 : c > 1 ? 1 : c;
      }
      k = k_min * fast_exp2(c * oct_f);
      wet = WET_LO * Q_INV * fast_exp2(c * oct_w);
    }
    low += k * band;
    float high = x - low - Q_INV * band;
    band += k * high;
    float y = (DRY * x + wet * band) * (1.0f / LIMIT);
    y = y > 3.0f ? 3.0f : y < -3.0f ? -3.0f : y;
    return LIMIT * y * (27.0f + y * y) / (27.0f + 9.0f * y * y);
  }

 private:
  float fs = 16000, att = 0, rel = 0, sens = 1, bottom_oct = 0, range_oct = 1;
  float k_min = 0, oct_f = 0, oct_w = 0, lfo_hz = 1;
  uint32_t lfo_step = 0, phase = 0;
  float cos_table[256];
  float env = 0, low = 0, band = 0, k = 0, wet = 0;
  unsigned count = 0;
};
