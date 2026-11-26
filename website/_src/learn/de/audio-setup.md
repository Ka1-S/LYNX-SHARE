---
source: 297095d385
---
## Treiber und Ausgang

![Die Audio-Seite der Einstellungen](/assets/img/learn/audio-setup-1.webp)

1. Öffne `Optionen › Audioeinstellungen...`.
2. Wähle den Treiber. Unter Windows funktioniert `LYNX ASIO (integriert)` mit jedem Gerät und braucht keinen eigenen Treiber; der ASIO-Treiber deiner Schnittstelle, FL Studio ASIO und `Windows Audio (gemeinsam)` oder `Windows Audio (exklusiv, niedrigste Latenz)` sind auch da. Core Audio auf dem Mac, PipeWire oder ALSA unter Linux.
3. `Ausgabegerät` ist, wo du LYNX hörst. `System default` folgt dem Ausgang, den dein Computer benutzt.
4. `Eingabegerät` ist, wovon du aufnimmst (ein Mikrofon, ein Gitarreneingang). `Samplerate` ist meist 44100 oder 48000 Hz.

## Puffergröße

![Die Puffergrößen mit ihrer Latenz](/assets/img/learn/audio-setup-2.webp)

1. `Puffergröße`: Ein kleinerer Puffer reagiert schneller, wenn du Keyboard spielst; ein größerer gibt der CPU mehr Luft.
2. Knistern oder Aussetzer? Erhöhe ihn. Noten kommen beim Spielen zu spät? Senke ihn.

## Den Ton testen

![Hilfe, Audioausgabe testen](/assets/img/learn/audio-setup-3.webp)

1. `Testton abspielen` spielt einen kurzen Ton über den gewählten Ausgang.
2. Von überall tut `Hilfe › Audioausgabe testen...` dasselbe.

## Aufgenommene Takes zu spät?

1. Deine Schnittstelle braucht einen Moment, um den Klang herein- und wieder hinauszubringen, deshalb kann ein Take etwas zu spät landen.
2. Verbinde auf derselben Audio-Seite unter `Aufnahme-Latenzausgleich` einen Ausgang per Kabel mit einem Eingang (oder nutze den Loopback deiner Schnittstelle) und klicke auf `Round Trip messen`.
3. LYNX spielt ein paar Klicks, misst, wie spät sie zurückkommen, und legt aufgenommene Takes von da an passend in die Zeit.

Siehe auch [Audio-Einrichtung](/wiki/audio-setup/) und [Fehlerbehebung](/wiki/troubleshooting/).
