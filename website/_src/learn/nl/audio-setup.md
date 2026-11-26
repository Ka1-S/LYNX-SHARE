---
source: 297095d385
---
## Driver en uitgang

![De Audio-pagina van de Instellingen](/assets/img/learn/audio-setup-1.webp)

1. Open `Opties › Audio-instellingen...`.
2. Kies de driver. Op Windows werkt `LYNX ASIO (ingebouwd)` met elk apparaat en heeft geen eigen driver nodig; de ASIO-driver van je interface zelf, FL Studio ASIO en `Windows Audio (gedeeld)` of `Windows Audio (exclusief, laagste latentie)` staan er ook. Core Audio op een Mac, PipeWire of ALSA op Linux.
3. `Uitvoerapparaat` is waar je LYNX hoort. `System default` volgt de uitgang die je computer gebruikt.
4. `Invoerapparaat` is waar je van opneemt (een microfoon, een gitaaringang). `Samplefrequentie` is meestal 44100 of 48000 Hz.

## Buffergrootte

![De buffergroottes, met hun latency](/assets/img/learn/audio-setup-2.webp)

1. `Buffergrootte`: een kleinere buffer reageert sneller als je een keyboard speelt; een grotere geeft de CPU meer ruimte.
2. Geknetter of haperingen? Verhoog hem. Noten komen te laat als je speelt? Verlaag hem.

## Test het geluid

![Help, Audio-uitgang testen](/assets/img/learn/audio-setup-3.webp)

1. `Een testgeluid afspelen` speelt een kort geluid af via de gekozen uitgang.
2. Overal doet `Help › Audio-uitgang testen...` hetzelfde.

## Opgenomen takes te laat?

1. Je interface heeft even nodig om het geluid binnen te halen en weer naar buiten te sturen, dus een take kan iets te laat terechtkomen.
2. Verbind op dezelfde Audio-pagina, onder `Latentiecompensatie bij opnemen`, een uitgang met een ingang via een kabel (of gebruik de loopback van je interface) en klik op `De round trip meten`.
3. LYNX speelt een paar klikken af, meet hoe laat ze terugkomen en zet opgenomen takes vanaf dan op tijd.

Zie ook [Audio instellen](/wiki/audio-setup/) en [Problemen oplossen](/wiki/troubleshooting/).
