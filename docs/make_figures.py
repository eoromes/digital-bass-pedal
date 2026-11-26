#!/usr/bin/env python3
"""Generate the article figures from the raw recordings.

    .venv/bin/python docs/make_figures.py [--data DIR]

Recordings are 16 kHz mono WAV files made with `tools/listen.py --save`,
where sample = (raw ADC value - 2048) * 16. The attack figure
(bass_attacks.png) comes from `tools/view.py`.
"""

import argparse
import os
import wave

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "figures")
DATA = os.path.join(HERE, "..", ".claude", "artigo", "dados")
FS = 16000
HISS_BAND = (300, 7000)  # "hiss": the noise above the mains hum

plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.alpha": 0.3})


def load(name):
    with wave.open(os.path.join(DATA, name)) as w:
        d = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    return d.astype(np.float64) / 16 + 2048


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name), dpi=130)
    plt.close(fig)


def welch(x, nseg=4096):
    x = x - x.mean()
    win = np.hanning(nseg)
    scale = 1.0 / (FS * (win ** 2).sum())
    acc, n = 0, 0
    for i in range(0, len(x) - nseg + 1, nseg // 2):
        acc = acc + np.abs(np.fft.rfft(x[i:i + nseg] * win)) ** 2 * scale
        n += 1
    psd = acc / n
    psd[1:-1] *= 2
    return np.fft.rfftfreq(nseg, 1 / FS), psd


def db(p):
    return 10 * np.log10(p + 1e-20)


def hiss(name):
    """RMS of the 300 Hz - 7 kHz band, in LSB."""
    f, psd = welch(load(name))
    m = (f >= HISS_BAND[0]) & (f < HISS_BAND[1])
    return np.sqrt(psd[m].sum() * (f[1] - f[0]))


def fig_signal_chain():
    fig, ax = plt.subplots(figsize=(13, 3.9))
    ax.set(xlim=(0, 13), ylim=(0, 3.9))
    ax.axis("off")
    edge, ink, soft = "#b9b6aa", "#1f1f1f", "#6b6b6b"

    def box(x, y, w, h, title, lines=(), dashed=False):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=0.12",
                                    fc="white", ec=edge, lw=1.4, ls="--" if dashed else "-"))
        n = len(lines)
        ax.text(x + w / 2, y + h / 2 + 0.17 * n, title, ha="center", va="center",
                fontsize=11, fontweight="bold", color=ink)
        for i, line in enumerate(lines):
            ax.text(x + w / 2, y + h / 2 + 0.17 * n - 0.34 * (i + 1), line, ha="center",
                    va="center", fontsize=9, color=soft)

    def arrow(x0, y0, x1, y1):
        ax.annotate("", (x1, y1), (x0, y0),
                    arrowprops=dict(arrowstyle="-|>", color=edge, lw=1.4, shrinkA=0, shrinkB=0))

    y, h = 2.0, 1.4
    box(0.1, y + 0.25, 0.9, 0.9, "Bass")
    box(1.4, y, 2.2, h, "Analog front end", ("MCP6002 on 3.3 V", "buffer, gain 1-11×", "bias + 10.7 kHz LPF"))
    box(4.0, y, 2.1, h, "Built-in ADC1", ("GPIO33, DMA", "384 kHz, mean of 8", "→ 48 kHz"))
    box(6.5, y, 2.2, h, "ESP32 effects", ("gate, distortion,", "flanger, chorus,", "limiter"))
    box(9.1, y, 2.2, h, "PCM5102A DAC", ("I2S, 48 kHz", "+ volume knob"))
    box(11.7, y + 0.25, 1.2, 0.9, "Amp")
    for x0, x1, yy in ((1.0, 1.4, y + 0.7), (3.6, 4.0, y + 0.7), (6.1, 6.5, y + 0.7),
                       (8.7, 9.1, y + 0.7), (11.3, 11.7, y + 0.7)):
        arrow(x0, yy, x1, yy)
    box(6.3, 0.15, 2.6, 1.15, "PC via USB", ("test bench: levels, settings,", "recordings"), dashed=True)
    arrow(7.6, y, 7.6, 1.3)
    ax.text(0.1, 3.75, "Signal chain of the pedal", fontsize=13, fontweight="bold", color=ink, va="top")
    save(fig, "signal_chain.png")


def fig_hiss_averaging():
    """Hiss with N DMA conversions averaged per 16 kHz output sample."""
    runs = {
        "GPIO39": {1: "m1", 2: "os2_m2", 4: "os4_m4", 8: "os8_m8", 16: "m16", 32: "os32_m32", 64: "os64_m64"},
        "GPIO33": {1: "m1", 16: "m16", 32: "os32_m32", 64: "os64_m64"},
    }
    ref = hiss("E18_vbias_divisor-so_analogread_gpio4.wav")
    fig, ax = plt.subplots(figsize=(9, 5))
    for (pin, files), color in zip(runs.items(), ("C1", "C0")):
        n = np.array(sorted(files))
        v = np.array([hiss(f"E18_vbias_divisor-so_dma_{files[k]}_{pin.lower()}.wav") for k in n])
        gap = n[1:] / n[:-1] > 2  # no measurements in between: dashed
        for i in range(len(n) - 1):
            ax.plot(n[i:i + 2], v[i:i + 2], "--" if gap[i] else "-", color=color, lw=2 if not gap[i] else 1.2)
        ax.plot(n, v, "o", color=color, ms=8, label=f"{pin}, DMA")
        ax.text(n[-1] * 1.12, v[-1], pin, va="center", fontsize=9, color=color)
        if pin == "GPIO39":
            nn = np.array([1, 64])
            ax.plot(nn, v[0] / np.sqrt(nn), "--", color="0.55", lw=1,
                    label="1/√N (independent noise)")
    ax.axhline(ref, color="0.3", ls=":", lw=1.2, label=f"analogRead on GPIO4: {ref:.1f} LSB")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xticks([1, 2, 4, 8, 16, 32, 64])
    ax.set_xticklabels(["1", "2", "4", "8", "16", "32", "64"])
    ax.set_yticks([1, 2, 4, 8])
    ax.set_yticklabels(["1", "2", "4", "8"])
    ax.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    ax.set(xlim=(0.8, 100), ylim=(0.9, 9), xlabel="conversions averaged per sample (N)",
           ylabel="hiss, 300 Hz–7 kHz (LSB RMS)",
           title="Averaging DMA conversions: the hiss falls like 1/√N up to 16, then stops")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(alpha=0.3, which="both")
    save(fig, "hiss_averaging.png")


def fig_hiss_pins():
    pins = ["gpio33", "gpio32", "gpio36", "gpio35", "gpio39", "gpio34"]
    vals = [hiss(f"E18_vbias_divisor-so_dma_m16_{p}.wav") for p in pins]
    names = [f"GPIO{p[4:]}, DMA ×16" for p in pins]
    names.append("GPIO4, analogRead")
    vals.append(hiss("E18_vbias_divisor-so_analogread_gpio4.wav"))
    colors = ["C0"] + ["#9cc3e4"] * (len(pins) - 1) + ["0.6"]
    y = np.arange(len(names))[::-1]
    fig, ax = plt.subplots(figsize=(9, 4.2))
    ax.barh(y, vals, color=colors, height=0.62)
    for yy, v in zip(y, vals):
        ax.text(v + 0.03, yy, f"{v:.2f}", va="center", fontsize=9)
    ax.set_yticks(y)
    ax.set_yticklabels(names)
    ax.set(xlim=(0, 2.6), xlabel="hiss, 300 Hz–7 kHz (LSB RMS)",
           title="Same voltage on every ADC1 pin: GPIO33 is the quietest")
    ax.grid(axis="y", visible=False)
    save(fig, "hiss_pins.png")


def fig_charger():
    fig, ax = plt.subplots(figsize=(10, 4.8))
    for name, label, color in (("E18_circuito_parado_dma_gpio33.wav", "laptop on its charger", "C3"),
                               ("E18_circuito_parado_bateria_dma_gpio33.wav", "laptop on battery", "C0")):
        x = load(name)
        f, psd = welch(x, 8192)
        ax.semilogx(f[1:], db(psd[1:]), lw=0.9, color=color,
                    label=f"{label}: {x.std():.1f} LSB RMS")
    for h in range(1, 18):
        ax.axvline(60 * h, color="0.75", lw=0.5, ls=":", zorder=0)
    ax.set(xlim=(20, FS / 2), xlabel="frequency (Hz)  —  dotted lines: multiples of 60 Hz up to 1 kHz",
           ylabel="dB (LSB²/Hz)",
           title="Full circuit, bass resting: the charger brings mains hum and its harmonics")
    ax.legend(loc="upper right")
    save(fig, "charger_battery.png")


def _spectrum(x, t0, t1):
    seg = x[int(t0 * FS):int(t1 * FS)]
    seg = seg - seg.mean()
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    return np.fft.rfftfreq(len(seg), 1 / FS), X


def _f0(f, X, guess):
    return max(np.arange(guess * 0.98, guess * 1.02, 0.02),
               key=lambda q: sum(X[np.argmin(abs(f - k * q))] for k in range(1, 6)))


def _pw(f, X, fr):
    i = np.argmin(abs(f - fr))
    return X[max(i - 2, 0):i + 3].sum()


def _mark_copies(a, f, d, f0, fmax, kmax, nmax):
    for k in range(1, kmax):
        if k * f0 < fmax:
            a.plot(k * f0, d[np.argmin(abs(f - k * f0))] + 3, "v", color="C0", ms=6)
        for n in range(1, nmax + 1):
            for fr in (k * f0 + 250 * n, abs(k * f0 - 250 * n)):
                if 20 < fr < fmax:
                    i = np.argmin(abs(f - fr))
                    a.plot(fr, d[max(i - 2, 0):i + 3].max() + 3, "v", color="C3", ms=4)


def fig_timing_sidebands():
    x = load("E9_tocando_agudo_gpio4.wav")
    f, X = _spectrum(x, 2, 4)
    f0 = _f0(f, X, 206.9)
    d = 10 * np.log10(X / X.max())
    fig, ax = plt.subplots(2, 1, figsize=(10, 8))
    ax[0].plot(f, d, lw=0.6, color="0.3")
    _mark_copies(ax[0], f, d, f0, 1700, 9, 4)
    ax[0].plot([], [], "v", color="C0", label="note harmonics (k·f0)")
    ax[0].plot([], [], "v", color="C3", label="copy positions |k·f0 ± n·250 Hz|")
    ax[0].set(xlim=(0, 1700), ylim=(-75, 5), xlabel="frequency (Hz)",
              ylabel="dB (relative to the fundamental)",
              title=f"G#3 ({f0:.1f} Hz): besides the harmonics, peaks spaced 250 Hz apart")
    ax[0].legend(fontsize=8, loc="upper right")
    pts = []
    for t0, t1, guess, label, ks, c in ((2, 4, 206.9, "G#3, harmonics 1–4", (1, 2, 3, 4), "C1"),
                                         (38, 40, 55.2, "A1, harmonics 3–5", (3, 4, 5), "C2"),
                                         (25, 27, 275.9, "C#4, harmonic 1", (1,), "C4")):
        fs_, Xs = _spectrum(x, t0, t1)
        q = _f0(fs_, Xs, guess)
        fr = [k * q for k in ks]
        r = [10 * np.log10(((_pw(fs_, Xs, k * q + 250) + _pw(fs_, Xs, abs(k * q - 250))) / 2)
                           / _pw(fs_, Xs, k * q)) for k in ks]
        ax[1].semilogx(fr, r, "o", color=c, label=label)
        pts += list(zip(fr, r))
    fr = np.array([p[0] for p in pts])
    r = np.array([p[1] for p in pts])
    b = np.mean(r - 20 * np.log10(fr))
    ff = np.logspace(2, np.log10(1200), 50)
    ax[1].semilogx(ff, b + 20 * np.log10(ff), "k--", lw=0.8,
                   label="+6 dB per octave (late samples)")
    ax[1].set(xlabel="harmonic frequency (Hz)", ylabel="±250 Hz copy / harmonic (dB)",
              title="Copy size grows with frequency, not with amplitude")
    ax[1].legend(fontsize=8)
    ax[1].grid(alpha=0.3, which="both")
    save(fig, "timing_sidebands.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=DATA, help="folder with the raw recordings")
    DATA = ap.parse_args().data
    os.makedirs(OUT, exist_ok=True)
    fig_signal_chain()
    fig_timing_sidebands()
    fig_hiss_averaging()
    fig_hiss_pins()
    fig_charger()
    print("figures in", OUT)
