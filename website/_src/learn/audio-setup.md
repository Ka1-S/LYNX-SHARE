---
title: Set up your audio
summary: No sound, or crackles? Choose the driver and the output device, set the buffer size and play a test sound, in Options › Audio settings.
---
## Driver and output

![The Audio page of Settings](/assets/img/learn/audio-setup-1.webp)

1. Open `Options › Audio settings...`.
2. Pick the driver: ASIO on Windows (your interface's own, or FL Studio ASIO), Core Audio on a Mac, PipeWire or ALSA on Linux.
3. `Output device` is where you hear LYNX. `System default` follows the output your computer uses.

## Buffer size

![The buffer sizes, with their latency](/assets/img/learn/audio-setup-2.webp)

1. `Buffer size`: a smaller buffer answers faster when you play a keyboard; a larger one gives the CPU more room.
2. Crackles or dropouts? Raise it. Notes come late when you play? Lower it.

## Test the sound

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Play a test sound` plays a short sound through the chosen output.
2. From anywhere, `Help › Test audio output...` does the same.

See also [Audio setup](/wiki/audio-setup/) and [Troubleshooting](/wiki/troubleshooting/).
