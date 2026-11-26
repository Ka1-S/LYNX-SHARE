---
title: Set up your audio
summary: No sound, or crackles? Choose the driver and the output device, set the buffer size and play a test sound, in Options › Audio settings.
---
## Driver and output

![The Audio page of Settings](/assets/img/learn/audio-setup-1.webp)

1. Open `Options › Audio settings...`.
2. Pick the driver. On Windows, `LYNX ASIO (built in)` works with any device and needs no driver of its own; your interface's own ASIO driver, FL Studio ASIO and `Windows Audio (shared)` or `Windows Audio (exclusive, lowest latency)` are there too. Core Audio on a Mac, PipeWire or ALSA on Linux.
3. `Output device` is where you hear LYNX. `System default` follows the output your computer uses.
4. `Input device` is what you record from (a microphone, a guitar input). `Sample rate` is usually 44100 or 48000 Hz.

## Buffer size

![The buffer sizes, with their latency](/assets/img/learn/audio-setup-2.webp)

1. `Buffer size`: a smaller buffer answers faster when you play a keyboard; a larger one gives the CPU more room.
2. Crackles or dropouts? Raise it. Notes come late when you play? Lower it.

## Test the sound

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Play a test sound` plays a short sound through the chosen output.
2. From anywhere, `Help › Test audio output...` does the same.

## Recorded takes late?

1. Your interface needs a moment to bring the sound in and out again, so a take can land a little late.
2. On the same Audio page, under `Recording latency compensation`, connect an output to an input with a cable (or use your interface's loopback) and click `Measure the round trip`.
3. LYNX plays a few clicks, measures how late they come back, and from then on puts recorded takes in time.

See also [Audio setup](/wiki/audio-setup/) and [Troubleshooting](/wiki/troubleshooting/).
