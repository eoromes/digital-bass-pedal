#!/usr/bin/env python3
"""Interactive view of a `tools/listen.py --save` recording, to look for
clipping, distortion and noise bursts.

    .venv/bin/python tools/view.py recording.wav [--gain 1] [--png name --no-show]

Window 1: what the ADC read, what was sent to the speaker (same processing as
listen.py, clipped samples in red), high-frequency noise envelope and
spectrogram. Window 2: the strongest attacks, with the estimated note; where
the dashed line keeps going and the solid one is flat, the output clipped.
--gain must match the one used while listening.
"""

import argparse
import math
import wave

import numpy as np

FS = 16000
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def load(path):
    with wave.open(path) as w:
        d = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    return d.astype(np.float64) / 16 + 2048


def like_listen(raw, gain):
    x = np.empty_like(raw)
    dc = raw[0]
    for i, v in enumerate(raw):
        dc += (v - dc) * 0.0005
        x[i] = v - dc
    ideal = x * 16 * gain
    return x, ideal, np.clip(ideal, -32768, 32767)


def hf_envelope(raw, win_ms=10):
    d = np.diff(raw, prepend=raw[0])
    n = int(FS * win_ms / 1000)
    return np.sqrt(np.convolve(d ** 2, np.ones(n) / n, "same") / 2)


def attacks(x, n=3, gap_s=0.25):
    env = np.convolve(np.abs(x), np.ones(80) / 80, "same")
    picks = []
    for i in np.argsort(env)[::-1]:
        if all(abs(i - p) > gap_s * FS for p in picks):
            picks.append(i)
            if len(picks) == n:
                break
    starts = []
    for p in sorted(picks):
        a = p
        while a > 0 and env[a] > 0.2 * env[p]:
            a -= 1
        starts.append(a)
    return starts


def estimate_note(seg):
    seg = seg - seg.mean()
    lo, hi = int(FS / 400), int(FS / 30)
    if np.std(seg) < 1 or len(seg) <= hi:
        return None, ""
    ac = np.correlate(seg, seg, "full")[len(seg) - 1:]
    below_zero = np.flatnonzero(ac[:hi] < 0)
    if not len(below_zero):
        return None, ""
    start = max(lo, below_zero[0])
    f = FS / (start + int(np.argmax(ac[start:hi])))
    midi = round(69 + 12 * math.log2(f / 440))
    return f, f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--gain", type=float, default=1.0)
    ap.add_argument("--png", help="base name for saving the figures")
    ap.add_argument("--no-show", action="store_true")
    args = ap.parse_args()

    import matplotlib
    if args.no_show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    raw = load(args.wav)
    t = np.arange(len(raw)) / FS
    x, ideal, out = like_listen(raw, args.gain)
    clip = np.abs(ideal) > 32767
    limit = 32767 / (16 * args.gain)
    adc_clip = (raw <= 0) | (raw >= 4095)

    print(f"{args.wav}: {len(raw) / FS:.1f} s")
    print(f"  ADC range {raw.min():.0f}..{raw.max():.0f}, {adc_clip.sum()} samples at the limits")
    print(f"  signal peak +{x.max():.0f} / {x.min():.0f} LSB from the center")
    print(f"  with --gain {args.gain:g} the output clips at ±{limit:.0f} LSB: "
          f"{100 * clip.mean():.2f}% of the samples")
    print(f"  highest gain without clipping: {32767 / (16 * np.abs(x).max()):.2f}")

    fig, ax = plt.subplots(4, 1, figsize=(14, 10), sharex=True,
                           gridspec_kw={"height_ratios": [3, 3, 1.5, 2]})
    fig.canvas.manager.set_window_title(f"{args.wav} — overview")
    center = raw.mean()
    ax[0].plot(t, raw, lw=0.4, color="C0")
    for y, color, label in ((0, "k", "ADC limit"), (4095, "k", None),
                            (center + limit, "C1", f"output clips (--gain {args.gain:g})"),
                            (center - limit, "C1", None)):
        ax[0].axhline(y, color=color, ls="--", lw=0.8, label=label)
    if clip.any():
        ax[0].plot(t[clip], raw[clip], ".", ms=1.5, color="C3", label="clipped at the output")
    ax[0].set(ylabel="raw ADC value", ylim=(-100, 4200), title="What the ADC read")
    ax[0].legend(loc="upper right", fontsize=8)

    ax[1].plot(t, out, lw=0.4, color="C0")
    if clip.any():
        ax[1].plot(t[clip], out[clip], ".", ms=1.5, color="C3")
    for y in (32767, -32768):
        ax[1].axhline(y, color="C3", ls="--", lw=0.8)
    ax[1].set(ylabel="16-bit sample", ylim=(-36000, 36000),
              title=f"Sent to the speaker (--gain {args.gain:g}): "
                    f"{100 * clip.mean():.1f}% clipped (red)")

    ax[2].plot(t, hf_envelope(raw), lw=0.5, color="C2")
    ax[2].set(ylabel="LSB", yscale="log",
              title="High-frequency noise envelope (10 ms) — bursts show up as regular peaks")

    ax[3].specgram(raw - raw.mean(), NFFT=1024, Fs=FS, noverlap=768, cmap="magma", vmin=-60)
    ax[3].set(ylabel="Hz", ylim=(0, 4000), xlabel="time (s)", title="Spectrogram")
    for a in ax[:3]:
        a.grid(alpha=0.3)
    fig.tight_layout()

    starts = attacks(x)
    fig2, ax2 = plt.subplots(len(starts), 1, figsize=(14, 3.2 * len(starts)), squeeze=False)
    fig2.canvas.manager.set_window_title(f"{args.wav} — strongest attacks")
    for a, p in zip(ax2[:, 0], starts):
        s, e = max(0, p - int(0.01 * FS)), min(len(raw), p + int(0.09 * FS))
        tt = (np.arange(s, e) - p) / FS * 1000
        f, name = estimate_note(x[p + int(0.03 * FS):p + int(0.09 * FS)])
        a.plot(tt, ideal[s:e], color="0.6", lw=1, ls="--", label="without clipping")
        a.plot(tt, out[s:e], color="C0", lw=1, label="played")
        c = clip[s:e]
        if c.any():
            a.plot(tt[c], out[s:e][c], ".", ms=3, color="C3", label="clipped")
        for y in (32767, -32768):
            a.axhline(y, color="C3", ls=":", lw=0.8)
        note = f"{name} ({f:.1f} Hz)" if f else "note not estimated"
        a.set(title=f"Attack at {p / FS:.2f} s — {note} — {100 * c.mean():.0f}% clipped",
              xlabel="ms from the start of the attack", ylabel="16-bit sample")
        a.grid(alpha=0.3)
        a.legend(loc="upper right", fontsize=8)
    fig2.tight_layout()

    if args.png:
        base = args.png.removesuffix(".png")
        fig.savefig(f"{base}_overview.png", dpi=110)
        fig2.savefig(f"{base}_attacks.png", dpi=110)
        print(f"  -> {base}_overview.png, {base}_attacks.png")
    if not args.no_show:
        plt.show()


if __name__ == "__main__":
    main()
