---
source: 3801d5daff
---
![De pagina Audio van het venster Instellingen, nog zonder gekozen uitvoerapparaat](/assets/img/wiki/settings-audio.webp)

Op de pagina Audio van het venster Instellingen kies je waar het geluid naartoe gaat en waar opnames vandaan komen. Open haar met `Opties › Audio-instellingen...`. De installatiewizard (`Opties › Installatiewizard (taal, audio, pluginmappen)...`) test de audio-uitvoer ook.

## Geluid krijgen

1. Open `Opties › Audio-instellingen...`.
2. Kies een `Driver`. De opmerking onder het vak zegt waar die driver goed voor is.
3. Kies een `Uitvoerapparaat`.
4. Klik op `Een testgeluid afspelen`.
5. Lees het onderdeel `Status`. Daar staan de draaiende driver, de samplefrequentie en de uitvoerlatentie, of `Er is geen audioapparaat geopend: kies een uitvoerapparaat.`

## Drivers

Het vak `Driver` toont de drivertypen van je systeem.

| Systeem | Driver | Wat het doet |
|---|---|---|
| Windows | Windows Audio (gedeeld) | Andere programma's houden hun geluid. Windows mengt alle programma's in vaste stappen, dus de buffer kan niet kleiner worden dan die stap |
| Windows | Windows Audio (gedeeld, periode met lage latentie) | Gedeeld, maar vraagt Windows om een kortere mengperiode. Slechts enkele drivers ondersteunen dit |
| Windows | Windows Audio (exclusief, laagste latentie) | LYNX SHARE gebruikt het apparaat alleen. Andere programma's zijn stil zolang het speelt |
| Windows | DirectSound (verouderd) | Oud Windows-audiopad met hoge latentie. Voor als de andere niet werken |
| Windows | ASIO | Drivers met lage latentie. Een ASIO-driver van de fabrikant werkt met het eigen apparaat |
| Windows | LYNX ASIO (ingebouwd) | De eigen ASIO-driver van LYNX SHARE, voor elk apparaat, voor afspelen en opnemen. Met `Driverinstellingen...` kies je de gedeelde of exclusieve modus en de apparaten |
| macOS | Core Audio | Het eigen audiosysteem, gedeeld met andere programma's |
| Linux | PipeWire (native) | LYNX SHARE praat rechtstreeks met PipeWire. `System default` volgt de uitvoer die in de geluidsinstellingen van het systeem staat |
| Linux | JACK / PipeWire | Voor een draaiende JACK-server, of PipeWire met `pipewire-jack` |
| Linux | ALSA | Praat met de geluidskaart. `default` of `pipewire` delen het apparaat, een `hw:`-apparaat neemt de kaart alleen voor LYNX SHARE |

Op Linux opent de eerste start de eerste van PipeWire, JACK en ALSA die apparaten heeft. Een opgeslagen keuze blijft bewaard.

## De instellingen op de pagina

| Instelling | Wat het doet |
|---|---|
| `Driver` | Het drivertype, uit de tabel hierboven |
| `Uitvoerapparaat` | Waar de Master afspeelt |
| `Invoerapparaat` | De bron voor audio-opname en voor de ingangen van Mixer-inserts. Standaard `(geen)` |
| `Samplefrequentie` | De frequenties die het apparaat aanbiedt |
| `Buffergrootte` | De groottes die het apparaat aanbiedt, elk met de latentie in milliseconden. Kleinere buffers reageren sneller als je speelt. Grotere geven de CPU meer ruimte, met minder geknetter |
| `Driverinstellingen...` | Opent het eigen venster van de driver (ASIO-drivers) |
| `Een testgeluid afspelen` | Speelt een testgeluid af via het uitvoerapparaat |
| `Ingangsaudio opnemen` | Neemt de eerste twee ingangskanalen op in een nieuwe insert als er geen insert gearmd is |
| `Bestandsformaat` | Opnames zijn WAV 24-bit of WAV 32-bit float, met de samplefrequentie van het apparaat |
| `De round trip meten` | Speelt 8 klikken van de `Ping-uitgang` naar de `Ping-ingang` via een kabel of loopback en meet de vertraging, zodat takes landen waar je ze speelde |
| `Fijnafstelling (samples)` | Verschuift elke nieuwe take. Positief is eerder op de Playlist |
| `Metronoomspoor` | De Mixer-track waarlangs het metronoom speelt. `---` is de Master. De klik zit nooit in een export |
| `Afgekapte noten spelen bij transport` | Speelt noten af die de startpositie kruisen |
| `Plug-ins resetten bij transport` | Wist de staarten van de effecten als het afspelen begint of springt |

## Als er geen geluid is

- Kijk eerst naar `Status`. Is er geen apparaat open, kies dan een uitvoerapparaat.
- Probeer een andere driver. Op Windows laat `Windows Audio (gedeeld)` andere programma's hun geluid houden, en `DirectSound (verouderd)` is er voor als de andere niet werken.
- Controleer de Master-fader in de [Mixer](/wiki/mixer/) en de knop `MASTER` in de werkbalk.
- In Pattern-modus speelt alleen het huidige pattern. Is het leeg, dan hoor je niets. Druk op `L` voor Song-modus.
- Als het geluid op Linux hapert, laat `Status` zien of de audiothread realtime-prioriteit kreeg. [Probleemoplossing](/wiki/troubleshooting/) bevat de oplossing.

## Goed om te weten

- ASIO bestaat alleen op Windows.
- Met `Windows Audio (gedeeld)` is de kleinste buffer de mengstap van het systeem. De opmerking onder het vak `Driver` geeft het getal voor jouw apparaat. Gebruik voor lagere latentie de exclusieve modus of een ASIO-driver.
- Met PipeWire loopt de buffer van 32 tot 2048 samples en de samplefrequentie van 44,1 tot 192 kHz. Houdt PipeWire zijn eigen frequentie aan, dan resamplet het de stream van LYNX SHARE.
- Met PipeWire valt LYNX SHARE terug op `System default` als een genoemd apparaat wordt losgekoppeld, en blijft de buffergrootte behouden.
- `Status` toont ook de audio-CPU-belasting en telt dropouts (buffer underruns).
- Je kunt de drivers en apparaten tonen zonder het venster te openen. Zie `--devices` in [Opdrachtregel](/wiki/command-line/).

## Gerelateerd

- [Instellingen](/wiki/settings/)
- [Opnemen](/wiki/recording/)
- [MIDI-controllers](/wiki/midi-controllers/)
- [Probleemoplossing](/wiki/troubleshooting/)
