#!/usr/bin/env python3
"""Recebe o áudio do ESP32 pela serial (firmware em MODE_AUDIO) e escreve PCM
16 bits mono no stdout, para tocar com pw-play:

    python3 tools/listen.py | pw-play --raw --format s16 --rate 16000 --channels 1 -

Só usa a biblioteca padrão. No stderr mostra, a cada segundo, a taxa real de
amostras, a faixa de valores brutos do ADC (para conferir clipping) e o
RMS do sinal em LSB (sem nada tocando, isso é o ruído de fundo).
"""

import argparse
import array
import os
import sys
import termios
import time
import tty


def open_serial(port: str, baud: int) -> int:
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}")
    attrs[4] = attrs[5] = speed  # ispeed / ospeed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--gain", type=float, default=8.0,
                    help="ganho digital aplicado antes de tocar (padrão 8)")
    args = ap.parse_args()

    if sys.stdout.isatty():
        sys.exit("Redirecione a saída para o pw-play (veja o topo do arquivo).")

    fd = open_serial(args.port, args.baud)
    out = sys.stdout.buffer

    high = None      # primeiro byte da amostra, esperando o segundo
    dc = 2048.0      # média móvel para tirar o offset do ADC
    count = 0
    lo, hi = 4095, 0
    sq = 0.0         # soma dos quadrados, para o RMS em LSB
    t0 = time.monotonic()

    while True:
        data = os.read(fd, 4096)
        samples = array.array("h")
        for b in data:
            if b & 0x80:
                high = b & 0x3F
                continue
            if high is None:
                continue  # perdemos sincronia; espera o próximo byte alto
            v = (high << 6) | b
            high = None

            lo, hi = min(lo, v), max(hi, v)
            dc += (v - dc) * 0.0005  # passa-alta de ~1,3 Hz a 16 kHz
            sq += (v - dc) ** 2
            s = (v - dc) * 16 * args.gain
            samples.append(int(max(-32768, min(32767, s))))
            count += 1

        out.write(samples.tobytes())
        out.flush()

        now = time.monotonic()
        if now - t0 >= 1.0:
            rate = count / (now - t0)
            rms = (sq / count) ** 0.5 if count else 0.0
            warn = ""
            if lo <= 5 or hi >= 4090:
                warn = "  <- encostando em 0/4095 (clipando)"
            print(f"{rate:7.0f} amostras/s  bruto {lo:4d}..{hi:4d}"
                  f"  rms {rms:6.1f} LSB{warn}", file=sys.stderr, flush=True)
            count, lo, hi, sq, t0 = 0, 4095, 0, 0.0, now


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, BrokenPipeError):
        pass
