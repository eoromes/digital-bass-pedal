#pragma once

// Pieces shared by the chorus and the flanger. They run in the sampling loop:
// inline, no library calls.

// Parabolic-sine LFO with output in [-1, 1].
struct Lfo {
  float step = 0, phase = 0;

  void set_rate(float hz, float fs) { step = 4.0f * hz / fs; }

  __attribute__((always_inline)) float next() {
    // Phase runs over [-2, 2): triangle in [-1, 1], then shaped into a sine.
    phase += step;
    if (phase >= 2.0f) phase -= 4.0f;
    float tri = phase < -1.0f ? -2.0f - phase : phase > 1.0f ? 2.0f - phase : phase;
    return tri * (2.0f - (tri < 0 ? -tri : tri));
  }
};

// Delay line read at a fractional delay with linear interpolation. SIZE is a
// power of two.
template <unsigned SIZE>
struct DelayLine {
  float buf[SIZE] = {};
  unsigned w = 0;

  __attribute__((always_inline)) void push(float x) {
    w = (w + 1) & (SIZE - 1);
    buf[w] = x;
  }

  // delay in samples behind the newest pushed sample. w stays wrapped: a float
  // of an ever-growing index would lose precision.
  __attribute__((always_inline)) float tap(float delay) const {
    float pos = (float)(w + SIZE) - delay;
    unsigned i = (unsigned)pos;
    float frac = pos - (float)i;
    float a = buf[i & (SIZE - 1)], b = buf[(i + 1) & (SIZE - 1)];
    return a + frac * (b - a);
  }
};
