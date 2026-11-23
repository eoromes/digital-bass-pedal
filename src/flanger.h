#pragma once

// Flanger: the signal mixed half and half with a copy delayed by 0.2 to 4 ms,
// the delay swept by a slow LFO, with part of the delayed signal fed back
// into the delay line. The notches the mix cuts sweep up and down the
// spectrum, and the feedback sharpens them into resonant peaks; the output is
// scaled down as the feedback rises so those peaks don't clip. Input and
// output in ADC LSB around the rest level.
//
// process() runs in the sampling loop: inline, no library calls (the LFO is a
// parabolic sine).
class Flanger {
 public:
  static constexpr unsigned SIZE = 512;  // power of two, 10.7 ms at 48 kHz
  static constexpr float MIN_MS = 0.2f, MAX_MS = 4.0f;

  void init(float rate, float rate_hz, float feedback_) {
    fs = rate;
    lo = MIN_MS * 0.001f * rate;
    span = (MAX_MS - MIN_MS) * 0.001f * rate;
    set_rate_hz(rate_hz);
    set_feedback(feedback_);
  }

  void set_rate_hz(float hz) {
    rate_hz = hz < 0.05f ? 0.05f : hz > 3 ? 3 : hz;
    lfo_step = 4.0f * rate_hz / fs;
  }

  void set_feedback(float f) {
    feedback = f < 0 ? 0 : f > 0.9f ? 0.9f : f;
    out_gain = 0.5f * (1.0f - 0.5f * feedback);
  }

  float get_rate_hz() const { return rate_hz; }
  float get_feedback() const { return feedback; }

  __attribute__((always_inline)) float process(float x) {
    // Phase runs over [-2, 2): triangle in [-1, 1], then shaped into a sine.
    phase += lfo_step;
    if (phase >= 2.0f) phase -= 4.0f;
    float tri = phase < -1.0f ? -2.0f - phase : phase > 1.0f ? 2.0f - phase : phase;
    float lfo = tri * (2.0f - (tri < 0 ? -tri : tri));

    float delay = lo + span * (0.5f + 0.5f * lfo);
    float pos = (float)(w + SIZE) - delay;
    unsigned i = (unsigned)pos;
    float frac = pos - (float)i;
    float a = buf[i & (SIZE - 1)], b = buf[(i + 1) & (SIZE - 1)];
    float wet = a + frac * (b - a);

    w = (w + 1) & (SIZE - 1);
    buf[w] = x + feedback * wet;
    return out_gain * (x + wet);
  }

 private:
  float buf[SIZE] = {};
  float fs = 0, lo = 0, span = 0, lfo_step = 0, out_gain = 0.5f;
  float rate_hz = 0, feedback = 0, phase = 0;
  unsigned w = 0;
};
