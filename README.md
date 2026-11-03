# digital-guitar-pedal

Pedal digital de efeitos pra baixo, feito com um ESP32-S3. 

A ideia é simples: o sinal do baixo passa por um pré analógico, entra no ADC do ESP, os efeitos rodam no próprio chip e o áudio sai por um DAC I2S externo pro amplificador. Por enquanto tudo está montado em protoboard e alimentado pelo USB da placa.

```
baixo -> pré (MCP6002) -> ADC do ESP32-S3 -> DSP -> PCM5102A -> volume -> amp
```

## Hardware

- ESP32-S3-DevKitC-1 (ou clone N16R8)
- MCP6002 fazendo buffer de alta impedância, ganho ajustável (1x a 11x) e filtro anti-aliasing em ~11 kHz
- DAC I2S PCM5102A na saída (o S3 não tem DAC interno)
- 4 botões + 4 LEDs pra ligar/desligar os efeitos
- Opcionais: encoder rotativo, OLED 0,96" e um ADC PCM1808 pra comparar com o ADC interno

### Pinos

| Função | GPIO |
| --- | --- |
| Entrada de áudio (ADC1) | 4 |
| I2S BCK / WS / DOUT | 12 / 13 / 14 |
| Botões | 15, 16, 17, 18 |
| LEDs | 39, 40, 41, 42 |
| Encoder CLK / DT / SW | 5, 6, 7 |
| OLED SDA / SCL | 8 / 9 |

O PCM5102A precisa do SCK ligado no GND e da ponte XSMT em H, senão fica mudo.

## Efeitos planejados

- Overdrive / distorção
- Chorus
- Delay
- Compressor

O áudio roda a 50 kHz, em blocos de 64 amostras.

## Compilando

Usa [PlatformIO](https://platformio.org/):

```sh
pio run -t upload
pio device monitor
```

## Status

Ainda no começo. O firmware atual é só um teste da placa; o próximo passo é montar a entrada analógica e fazer o passthrough ADC -> DAC funcionar sem ruído.
