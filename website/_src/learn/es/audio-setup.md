---
source: 297095d385
---
## Driver y salida

![La página Audio de la Configuración](/assets/img/learn/audio-setup-1.webp)

1. Abre `Opciones › Configuración de audio...`.
2. Elige el driver. En Windows, `LYNX ASIO (integrado)` funciona con cualquier dispositivo y no necesita un driver propio; el driver ASIO de tu interfaz, FL Studio ASIO y `Windows Audio (compartido)` o `Windows Audio (exclusivo, latencia mínima)` también están ahí. Core Audio en Mac, PipeWire o ALSA en Linux.
3. `Dispositivo de salida` es donde oyes LYNX. `System default` sigue la salida que usa tu ordenador.
4. `Dispositivo de entrada` es desde donde grabas (un micrófono, una entrada de guitarra). `Frec. muestreo` suele ser 44100 o 48000 Hz.

## Tamaño del búfer

![Los tamaños de búfer, con su latencia](/assets/img/learn/audio-setup-2.webp)

1. `Tamaño del búfer`: un búfer más pequeño responde más rápido cuando tocas un teclado; uno más grande da más margen a la CPU.
2. ¿Chasquidos o cortes? Súbelo. ¿Las notas llegan tarde al tocar? Bájalo.

## Prueba el sonido

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Reproducir un sonido de prueba` reproduce un sonido corto por la salida elegida.
2. Desde cualquier sitio, `Ayuda › Probar la salida de audio...` hace lo mismo.

## ¿Las tomas grabadas llegan tarde?

1. Tu interfaz necesita un momento para meter el sonido y volver a sacarlo, así que una toma puede quedar un poco tarde.
2. En la misma página Audio, en `Compensación de latencia de grabación`, conecta una salida a una entrada con un cable (o usa el loopback de tu interfaz) y haz clic en `Medir la ida y vuelta`.
3. LYNX reproduce unos clics, mide cuánto tardan en volver y, a partir de ahí, coloca las tomas grabadas a tiempo.

Mira también [Configuración de audio](/wiki/audio-setup/) y [Solución de problemas](/wiki/troubleshooting/).
