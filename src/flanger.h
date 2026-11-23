#pragma once

#include "modulation.h"

// Flanger: the signal mixed half and half with a copy delayed by 0.2 to 4 ms,
// the delay swept by a slow LFO, with part of the delayed signal fed back
// into the delay line. The notches the mix cuts sweep up and down the
// spectrum, and the feedback sharpens them into resonant peaks. The output is
// scaled down as the feedback rises, which tames those peaks but doesn't
// remove them: their gain is about 1.2 at the default feedback of 0.6 and 3
// at 0.9. Input and output in ADC LSB around the rest level.
class Flanger {
 public:
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
    lfo.set_rate(rate_hz, fs);
  }

  void set_feedback(float f) {
    feedback = f < 0 ? 0 : f > 0.9f ? 0.9f : f;
    out_gain = 0.5f * (1.0f - 0.5f * feedback);
  }

  float get_rate_hz() const { return rate_hz; }
  float get_feedback() const { return feedback; }

  __attribute__((always_inline)) float process(float x) {
    float wet = line.tap(lo + span * (0.5f + 0.5f * lfo.next()));
    line.push(x + feedback * wet);
    return out_gain * (x + wet);
  }

 private:
  DelayLine<512> line;  // 10.7 ms at 48 kHz
  Lfo lfo;
  float fs = 0, lo = 0, span = 0, out_gain = 0.5f;
  float rate_hz = 0, feedback = 0;
};
