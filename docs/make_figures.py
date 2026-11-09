#!/usr/bin/env python3
"""Generate the article figures from the raw recordings.

    .venv/bin/python docs/make_figures.py [--data DIR]

Recordings are 16 kHz mono WAV files made with `tools/listen.py --save`,
where sample = (raw ADC value - 2048) * 16.
"""

import argparse
import csv
import os
import wave

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "figures")
DATA = os.path.join(HERE, "..", ".claude", "artigo", "dados")
FS = 16000
IDEAL_DB = 10 * np.log10((1 / 12) / (FS / 2))  # ideal 12-bit quantization floor

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


def hf_envelope(x, win_ms=10):
    """High-frequency noise envelope, from sample-to-sample differences."""
    d = np.diff(x)
    n = int(FS * win_ms / 1000)
    return np.sqrt(np.convolve(d ** 2, np.ones(n) / n, "same") / 2)


def fig_ground():
    pairs = [("E1_vol0_terra-solto_gpio4.wav", "E2_vol0_terra-firme_gpio4.wav",
              "Bass volume at 0"),
             ("E1_parado_terra-solto_gpio4.wav", "E2_parado_terra-firme_gpio4.wav",
              "Bass resting, hand on the strings")]
    fig, ax = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    for a, (before, after, title) in zip(ax, pairs):
        for name, label, color in ((before, "loose jack ground", "C3"),
                                   (after, "tightened jack ground", "C0")):
            f, psd = welch(load(name))
            a.semilogx(f[1:], db(psd[1:]), lw=0.8, color=color, label=label)
        for h in range(1, 18):
            a.axvline(60 * h, color="0.6", lw=0.5, ls=":", zorder=0)
        a.axhline(IDEAL_DB, color="k", ls="--", lw=0.8,
                  label="ideal 12-bit ADC (quantization only)")
        a.set(title=title, ylabel="dB (LSB²/Hz)", xlim=(20, FS / 2))
        a.legend(loc="upper right")
    ax[1].set_xlabel("frequency (Hz)  —  dotted lines: multiples of 60 Hz")
    save(fig, "ground_hum.png")


def fig_missing_codes():
    fig, ax = plt.subplots(2, 1, figsize=(10, 7))
    x = load("E1_vol0_terra-solto_gpio4.wav")
    lo, hi = int(np.percentile(x, 0.5)), int(np.percentile(x, 99.5))
    c = np.bincount(x.round().astype(int), minlength=4096)
    ax[0].bar(np.arange(lo, hi + 1), c[lo:hi + 1], width=1.0)
    for k in range(lo - lo % 16 + 8, hi + 1, 16):
        ax[0].axvline(k, color="C3", lw=0.6, ls=":")
    ax[0].set(title="No signal: how often each ADC code occurs  —  dotted: codes 16k + 8",
              xlabel="raw ADC value", ylabel="count")
    x = load("E1_tocando_terra-solto_gpio4.wav")
    c = np.bincount(x.round().astype(int), minlength=4096)
    ax[1].bar(np.arange(1450, 1650), c[1450:1650], width=1.0)
    ax[1].set(title="Playing: codes 1450 to 1650 (the ADC skips 1536–1553)",
              xlabel="raw ADC value", ylabel="count")
    save(fig, "adc_missing_codes.png")


def fig_bursts():
    fig, ax = plt.subplots(3, 1, figsize=(10, 9))
    for name, label, color in (("E2_parado_terra-firme_gpio4.wav", "GPIO4 (ADC2)", "C0"),
                               ("E3_parado_terra-firme_gpio34.wav", "GPIO34 (ADC1)", "C1")):
        env = hf_envelope(load(name))
        ax[0].plot(np.arange(len(env)) / FS, env, lw=0.6, color=color, label=label)
    ax[0].set(xlim=(0, 12), xlabel="time (s)", ylabel="LSB RMS (10 ms window)",
              title="High-frequency noise envelope, bass resting")
    ax[0].legend(loc="upper right")

    x = load("E2_parado_terra-firme_gpio4.wav")
    env = hf_envelope(x)
    k = int(np.argmax(env[FS:FS * 3])) + FS
    a, b = k - int(0.15 * FS), k + int(0.15 * FS)
    ax[1].plot((np.arange(a, b) - k) / FS * 1000, x[a:b], lw=0.6)
    ax[1].set(xlabel="time from the centre of the burst (ms)", ylabel="raw ADC value",
              title="Zoom on one burst (GPIO4)")

    inside = env > np.percentile(env, 90)
    outside = env < np.percentile(env, 40)

    def spec(mask):
        segs = []
        for s in range(0, len(x) - 512, 512):
            if mask[s:s + 512].all():
                seg = x[s:s + 512] - x[s:s + 512].mean()
                segs.append(np.abs(np.fft.rfft(seg * np.hanning(512))) ** 2)
        return np.mean(segs, 0)
    f = np.fft.rfftfreq(512, 1 / FS)
    ax[2].semilogx(f[1:], db(spec(inside)[1:]), label="during bursts")
    ax[2].semilogx(f[1:], db(spec(outside)[1:]), label="between bursts")
    ax[2].set(xlim=(30, FS / 2), xlabel="frequency (Hz)", ylabel="dB (relative)",
              title="Spectrum during and between bursts (GPIO4)")
    ax[2].legend()
    save(fig, "noise_bursts.png")


def fig_listener_clipping():
    """The same attack played with --gain 8 (the old default) and --gain 1."""
    raw = load("E1_tocando_terra-solto_gpio4.wav")
    x = np.empty_like(raw)
    dc = 2048.0
    for i, v in enumerate(raw):  # same high-pass as the old listen.py
        dc += (v - dc) * 0.0005
        x[i] = v - dc
    p = int(2.29 * FS)
    s, e = p - int(0.01 * FS), p + int(0.09 * FS)
    tt = (np.arange(s, e) - p) / FS * 1000
    fig, ax = plt.subplots(2, 1, figsize=(10, 6.5), sharex=True)
    for a, g in zip(ax, (8, 1)):
        ideal = x[s:e] * 16 * g
        out = np.clip(ideal, -32768, 32767)
        cut = np.abs(ideal) > 32767
        tot = 100 * (np.abs(x * 16 * g) > 32767).mean()
        a.plot(tt, ideal, color="0.6", ls="--", lw=1, label="without clipping")
        a.plot(tt, out, color="C0", lw=1, label="sent to the speaker")
        if cut.any():
            a.plot(tt[cut], out[cut], ".", ms=3, color="C3", label="clipped")
        a.axhline(32767, color="C3", ls=":", lw=0.8)
        a.axhline(-32768, color="C3", ls=":", lw=0.8)
        a.set(ylabel="16-bit sample",
              title=f"Playback gain {g}: {100 * cut.mean():.0f}% of this attack clipped, "
                    f"{tot:.1f}% of the whole recording")
        a.legend(loc="upper right", fontsize=8)
    ax[1].set_xlabel("ms from the start of the attack (C#2, ~69.6 Hz)")
    save(fig, "listener_clipping.png")


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


def fig_core_split():
    fig, ax = plt.subplots(3, 1, figsize=(10, 10), gridspec_kw={"height_ratios": [3, 3, 2]})
    for a, (name, t0, guess, title) in zip(ax[:2], (
            ("E10_antes_agudo_gpio4.wav", 2.2, 194.5,
             "Before: sampling and Serial.write on the same core"),
            ("E10_depois_agudo_gpio4.wav", 4.2, 195.1,
             "After: core 1 only samples, core 0 sends"))):
        x = load(name)
        f, X = _spectrum(x, t0, t0 + 1.5)
        f0 = _f0(f, X, guess)
        d = 10 * np.log10(X / X.max())
        a.plot(f, d, lw=0.6, color="0.35")
        _mark_copies(a, f, d, f0, 1200, 8, 3)
        a.plot([], [], "v", color="C0", label=f"harmonics (f0 = {f0:.1f} Hz)")
        a.plot([], [], "v", color="C3", label="copy positions |k·f0 ± n·250 Hz|")
        a.set(xlim=(0, 1200), ylim=(-90, 5), ylabel="dB (relative to the fundamental)", title=title)
        a.legend(fontsize=8, loc="upper right")
    ax[1].set_xlabel("frequency (Hz)")
    cols = ["atraso_max_us", "atrasadas_10us", "atrasadas_10us_inicio_bloco"]  # old CSV format
    labels = ["max lateness (µs)", "samples late\n> 10 µs per second",
              "late > 10 µs at the\nstart of a block, per second"]
    vals = {}
    for s in ("antes", "depois"):
        with open(os.path.join(DATA, f"E10_{s}_agudo_gpio4.tempo.csv")) as fh:
            rows = list(csv.DictReader(fh))
        vals[s] = [np.median([int(r[n]) for r in rows]) for n in cols]
    xx = np.arange(len(cols))
    w = 0.38
    b1 = ax[2].bar(xx - w / 2, vals["antes"], w, label="before", color="C3")
    b2 = ax[2].bar(xx + w / 2, vals["depois"], w, label="after", color="C0")
    for b in list(b1) + list(b2):
        ax[2].text(b.get_x() + b.get_width() / 2, b.get_height() + 5, f"{b.get_height():.0f}",
                   ha="center", fontsize=9)
    ax[2].set_xticks(xx)
    ax[2].set_xticklabels(labels, fontsize=9)
    ax[2].set(title="Measured on the firmware (median per second)", ylim=(0, 600))
    ax[2].legend()
    save(fig, "core_split.png")


def fig_time_budget():
    with open(os.path.join(DATA, "E12_tocando_tempo_gpio4.tempo.csv")) as fh:
        rows = list(csv.DictReader(fh))
    med = lambda k: float(np.median([int(r[k]) for r in rows]))  # noqa: E731
    rmin, rmean, rmax = med("leitura_min_us"), med("leitura_media_us_x10") / 10, med("leitura_max_us")
    slack = med("folga_min_us")
    period = 1e6 / FS
    rest = period - rmax - slack
    fig, ax = plt.subplots(figsize=(10, 3.6))
    ax.barh([0], [rmean], color="C0", label=f"analogRead: mean {rmean:.1f} µs ({rmin:.0f} to {rmax:.0f})")
    ax.barh([0], [rmax - rmean], left=[rmean], color="C0", alpha=0.4, label="spread up to the max")
    ax.barh([0], [rest], left=[rmax], color="C1",
            label=f"rest of the loop (estimated {rest:.1f} to {period - rmin - slack:.1f} µs)")
    ax.barh([0], [slack], left=[rmax + rest], color="C2", label=f"minimum slack: {slack:.0f} µs")
    for per, label in ((period, "16 kHz (62.5 µs)"), (1e6 / 44100, "44.1 kHz (22.7 µs)"),
                       (1e6 / 48000, "48 kHz (20.8 µs)")):
        ax.axvline(per, color="k", ls="--", lw=0.8)
        ax.text(per, 0.45, label, rotation=90, va="bottom", ha="right", fontsize=8)
    ax.set(xlim=(0, 70), ylim=(-0.5, 1.4), yticks=[], xlabel="µs",
           title="Time budget of one sample with analogRead: the read takes ~2/3 of the period")
    ax.legend(fontsize=8, loc="upper left", ncol=2)
    save(fig, "sample_time_budget.png")


def fig_noise_floor():
    cases = [("E13_vbias_direto_gpio4.wav", "ADC alone: GPIO4 tied to the bias voltage", "C0"),
             ("E11_parado_com_mao_gpio4.wav", "circuit + bass, hand on the strings", "C2"),
             ("E11_parado_sem_mao_gpio4.wav", "circuit + bass, no hand", "C3")]
    fig, ax = plt.subplots(3, 1, figsize=(10, 11), gridspec_kw={"height_ratios": [3, 2, 2.4]})
    measured = []
    for name, label, color in cases:
        x = load(name)
        f, psd = welch(x)
        ax[0].semilogx(f[1:], db(psd[1:]), lw=0.8, color=color, label=label)
        measured.append((label, 20 * np.log10(1448 / x.std()), color))
    ax[0].axhline(IDEAL_DB, color="k", ls="--", lw=0.8, label="ideal 12-bit ADC")
    ax[0].set(xlim=(20, FS / 2), xlabel="frequency (Hz)", ylabel="dB (LSB²/Hz)",
              title="Where the noise is: the ADC alone vs. with the circuit and the bass")
    ax[0].legend(fontsize=8, loc="upper right")

    env = hf_envelope(load("E13_vbias_direto_gpio4.wav"))
    ax[1].plot(np.arange(len(env)) / FS, env, lw=0.5, color="C0")
    ax[1].set(xlabel="time (s)", ylabel="LSB (10 ms window)", yscale="log",
              title="With GPIO4 tied to the bias voltage (no op-amp, no bass), the bursts remain")

    refs = [("ideal 12-bit ADC (theoretical limit)", 20 * np.log10(1448 / (1 / np.sqrt(12))), "0.6"),
            ("PCM1808 ADC (datasheet: 99 dB)", 99, "0.4"),
            ("PCM5102A DAC (datasheet: 112 dB)", 112, "0.4")]
    names = [r[0] for r in measured + refs]
    vals = [r[1] for r in measured + refs]
    colors = [r[2] for r in measured + refs]
    y = np.arange(len(names))[::-1]
    ax[2].barh(y, vals, color=colors)
    for yy, v in zip(y, vals):
        ax[2].text(v + 1, yy, f"{v:.0f} dB", va="center", fontsize=8)
    ax[2].set_yticks(y)
    ax[2].set_yticklabels(names, fontsize=8)
    ax[2].set(xlim=(0, 125), xlabel="dB between a full-scale sine and the noise (total RMS)",
              title="Dynamic range: measured (colour) vs. references (grey)")
    save(fig, "adc_noise_floor.png")


def _ram_captures():
    """Split the 3 s RAM captures by their markers (0 then 4095 = serial
    stopped; 4095 then 0 = with serial traffic)."""
    x = load("E14_captura-ram_vbias_gpio4.wav")
    xi = np.round(x).astype(int)
    ns, m = 48000, 32
    out, i = [], 0
    while i < len(xi) - 2 * m - ns:
        a, b = xi[i:i + m], xi[i + m:i + 2 * m]
        if (a == 0).all() and (b == 4095).all():
            out.append(("off", x[i + 2 * m:i + 2 * m + ns]))
            i += 2 * m + ns
        elif (a == 4095).all() and (b == 0).all():
            out.append(("on", x[i + 2 * m:i + 2 * m + ns]))
            i += 2 * m + ns
        else:
            i += 1
    return out


def fig_bursts_serial_stopped():
    caps = _ram_captures()
    fig, ax = plt.subplots(3, 1, figsize=(10, 9))
    for a, kind, color, title in ((ax[0], "off", "C0", "Recorded to RAM with the serial link STOPPED"),
                                  (ax[1], "on", "C3", "Recorded to RAM with serial traffic (32 kB/s)")):
        for k, (_, sig) in enumerate([c for c in caps if c[0] == kind][:3]):
            env = hf_envelope(sig)
            a.plot(np.arange(len(env)) / FS + 3.2 * k, env, lw=0.5, color=color)
        a.set(yscale="log", ylim=(0.8, 12), xlabel="time (s) — three 3 s captures side by side",
              ylabel="LSB (10 ms window)", title=title)
    hiss = {t: [np.percentile(hf_envelope(s), 20) for tt, s in caps if tt == t] for t in ("off", "on")}
    ax[2].boxplot([hiss["off"], hiss["on"]],
                  tick_labels=[f"serial stopped ({len(hiss['off'])} captures)",
                               f"with traffic ({len(hiss['on'])} captures)"])
    ax[2].set(ylabel="hiss between bursts (LSB)",
              title="Bursts appear in both cases; traffic only adds ~0.2 LSB of hiss")
    save(fig, "bursts_serial_stopped.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=DATA, help="folder with the raw recordings")
    DATA = ap.parse_args().data
    os.makedirs(OUT, exist_ok=True)
    fig_ground()
    fig_missing_codes()
    fig_bursts()
    fig_listener_clipping()
    fig_timing_sidebands()
    fig_core_split()
    fig_time_budget()
    fig_noise_floor()
    fig_bursts_serial_stopped()
    print("figures in", OUT)
