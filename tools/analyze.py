#!/usr/bin/env python3
"""Noise analysis of recordings made with `tools/listen.py --save`.

    .venv/bin/python tools/analyze.py idle.wav playing.wav ...

For each file, writes <name>_analysis.png (waveform, spectrogram, average
spectrum against an ideal 12-bit ADC, ADC code histogram) and prints RMS,
spectral peaks (tagged when they fall on 60 Hz mains harmonics or on the
250 Hz serial block rate), the share of noise power below 200 Hz and the
number of spikes. With several files, also writes comparison.png.
"""

import argparse
import os
import wave

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

BLOCK_RATE = 16000 / 64
MIN_SAMPLES = 4096


def load(path):
    with wave.open(path) as w:
        fs = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    return data.astype(np.float64) / 16 + 2048, fs


def ideal_db(fs):
    """Quantization noise of an ideal 12-bit ADC, LSB²/12 spread up to fs/2."""
    return 10 * np.log10((1 / 12) / (fs / 2))


def welch(x, fs, nseg=4096):
    x = x - x.mean()
    nseg = min(nseg, len(x))
    win = np.hanning(nseg)
    scale = 1.0 / (fs * (win ** 2).sum())
    segs = [x[i:i + nseg] * win for i in range(0, len(x) - nseg + 1, nseg // 2)]
    psd = np.mean([np.abs(np.fft.rfft(s)) ** 2 for s in segs], axis=0) * scale
    psd[1:-1] *= 2
    return np.fft.rfftfreq(nseg, 1 / fs), psd


def find_peaks(f, psd, min_db=10, top=12):
    db = 10 * np.log10(psd + 1e-20)
    k = max(3, int(100 / (f[1] - f[0])) | 1)
    pad = np.pad(db, k // 2, mode="edge")
    floor = np.array([np.median(pad[i:i + k]) for i in range(len(db))])
    peaks = [i for i in range(2, len(db) - 1)
             if db[i] > db[i - 1] and db[i] >= db[i + 1] and db[i] - floor[i] > min_db]
    peaks.sort(key=lambda i: db[i] - floor[i], reverse=True)
    return [(f[i], db[i], db[i] - floor[i]) for i in peaks[:top]]


def mains_frequency(f, psd):
    """Mains is never exactly 60 Hz; the error grows with the harmonic number."""
    df = f[1] - f[0]
    harmonics = np.arange(1, int(f[-1] / 60))
    candidates = np.arange(59.8, 60.2, 0.005)
    power = [psd[np.round(harmonics * c / df).astype(int)].sum() for c in candidates]
    return candidates[int(np.argmax(power))]


def tag(freq, df, mains):
    tags = []
    for base, name in ((mains, "mains"), (BLOCK_RATE, "serial 250 Hz")):
        h = round(freq / base)
        if h >= 1 and abs(freq - h * base) <= 0.6 * df:
            tags.append(f"{name} x{h}")
    return ", ".join(tags)


def spikes(raw, k=6.0):
    x = raw - raw.mean()
    x = (x - np.convolve(x, np.ones(9) / 9, mode="same"))[4:-4]
    sigma = np.median(np.abs(x - np.median(x))) * 1.4826 + 1e-9
    return int(np.sum(np.abs(x) > k * sigma)), sigma


def analyze(path):
    raw, fs = load(path)
    if len(raw) < MIN_SAMPLES:
        print(f"\n== {path}: only {len(raw)} samples, skipped")
        return None
    duration = len(raw) / fs
    x = raw - raw.mean()
    f, psd = welch(raw, fs)
    df = f[1] - f[0]
    low = psd[(f > 1) & (f < 200)].sum() / psd[f > 1].sum()
    n_spikes, sigma = spikes(raw)
    peaks = find_peaks(f, psd)
    mains = mains_frequency(f, psd)

    print(f"\n== {path}  ({duration:.1f} s, {fs} Hz)")
    print(f"   raw {raw.min():.0f}..{raw.max():.0f}, mean {raw.mean():.1f}")
    print(f"   RMS {x.std():.1f} LSB; robust sample-to-sample sigma {sigma:.1f} LSB")
    print(f"   {100 * low:.0f}% of the power is below 200 Hz")
    print(f"   spikes (> 6 sigma): {n_spikes} ({n_spikes / duration:.1f}/s)")
    print(f"   mains frequency: {mains:.3f} Hz")
    if peaks:
        print("   peaks above the floor:")
        for freq, _, above in peaks:
            t = tag(freq, df, mains)
            print(f"     {freq:7.1f} Hz  {above:5.1f} dB" + (f"   <- {t}" if t else ""))

    fig, ax = plt.subplots(4, 1, figsize=(12, 14))
    ax[0].plot(np.arange(len(raw)) / fs, raw, lw=0.4)
    ax[0].set(title=f"{os.path.basename(path)}: waveform", xlabel="time (s)",
              ylabel="raw ADC value")
    ax[1].specgram(x, NFFT=1024, Fs=fs, noverlap=512, cmap="magma", scale="dB",
                   vmin=ideal_db(fs) - 10)
    ax[1].set(title="spectrogram", xlabel="time (s)", ylabel="frequency (Hz)")
    ax[2].semilogx(f[1:], 10 * np.log10(psd[1:] + 1e-20), lw=0.8, label="measured")
    ax[2].axhline(ideal_db(fs), color="k", ls="--", lw=0.8,
                  label="ideal 12-bit ADC (quantization only)")
    for freq, level, _ in peaks[:8]:
        ax[2].annotate(f"{freq:.0f}", (freq, level), fontsize=8,
                       textcoords="offset points", xytext=(0, 4), ha="center")
    ax[2].set(title="average spectrum", xlabel="frequency (Hz)", ylabel="dB (LSB²/Hz)",
              xlim=(10, fs / 2))
    ax[2].legend()
    lo, hi = int(np.percentile(raw, 0.5)), int(np.percentile(raw, 99.5))
    counts = np.bincount(raw.round().astype(int), minlength=4096)[lo:hi + 1]
    ax[3].bar(np.arange(lo, hi + 1), counts, width=1.0)
    ax[3].set(title="ADC code histogram (gaps = codes the ADC never outputs)",
              xlabel="raw value", ylabel="count")
    for a in ax[[0, 2, 3]]:
        a.grid(alpha=0.3, which="both")
    fig.tight_layout()
    out = f"{os.path.splitext(path)[0]}_analysis.png"
    fig.savefig(out, dpi=110)
    plt.close(fig)
    print(f"   -> {out}")
    return path, f, psd, fs


def compare(results, out):
    fig, ax = plt.subplots(figsize=(12, 6))
    for path, f, psd, _ in results:
        ax.semilogx(f[1:], 10 * np.log10(psd[1:] + 1e-20), lw=0.8,
                    label=os.path.basename(path))
    fs = results[0][3]
    ax.axhline(ideal_db(fs), color="k", ls="--", lw=0.8, label="ideal 12-bit ADC")
    ax.set(title="spectrum comparison", xlabel="frequency (Hz)", ylabel="dB (LSB²/Hz)",
           xlim=(10, fs / 2))
    ax.grid(alpha=0.3, which="both")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out, dpi=110)
    print(f"\n-> {out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wavs", nargs="+")
    args = ap.parse_args()
    results = [r for r in (analyze(p) for p in args.wavs) if r]
    if len(results) > 1:
        compare(results, os.path.join(os.path.dirname(args.wavs[0]), "comparison.png"))


if __name__ == "__main__":
    main()
