---
source: d5cc625221
---
![De Clip-launch-indeling: een clipraster met scènes naast de Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE start in de Pattern-werkwijze: een Channel Rack, patterns en een Playlist. De Clipstart-werkwijze is optioneel en voegt naast de Playlist een clipraster met scènes toe. LYNX SHARE leest en schrijft `.als`-sets. Het is niet gelieerd aan Ableton.

## Een Live-set openen

1. Kies `Bestand › Openen...` (`Ctrl+O`) en selecteer het `.als`-bestand. Sets die zijn opgeslagen door Live 11 en Live 12 worden gelezen.
2. Kies `Beeld › Lay-outstijl › Clipstart-werkwijze` om met het clipraster te werken, waar de Session-clips en scènes van de set terechtkomen.
3. Gebruik de schakelaar `Grid / Both / Song` in de titelbalk van de Playlist om het raster, de Playlist of beide te tonen.
4. Kies `Bestand › Opslaan`. Het vraagt waar een `.lynx`-bestand moet komen. Het `.als`-bestand blijft ongewijzigd.

`Beeld › Lay-outstijl › Patroonwerkwijze (standaard)` gaat terug. Dezelfde keuze staat in `Opties › Algemene instellingen...`. Patterns en het Channel Rack blijven in beide indelingen beschikbaar.

## Wat waarmee overeenkomt

| In Live | In LYNX SHARE |
|---|---|
| Session-weergave | Het clipraster van de Clipstart-werkwijze |
| Arrangement-weergave | De Playlist |
| Tracks | Playlist-tracks, de kolommen van het clipraster |
| Scènes | Scènes, de rijen van het clipraster. Een scène kan bij het starten het tempo instellen |
| Startkwantisering | `Globaal`, `Geen`, 8 maten tot 1 maat, of een nootwaarde van 1/2 tot 1/32, desgewenst per clip |
| Startmodi | `Trigger`, `Gate`, `Wisselen` en `Herhalen` |
| Follow actions | Actie A en Actie B, elk met een kans |
| Clip-stopknoppen | Een leeg slot kan een stopknop hebben |
| Terug naar Arrangement | `Terug naar Playlist` |
| Warp-modi | `Tellen`, `Tonen`, `Textuur` en `Complex` in `Kanaalinstellingen › Time-stretch` |
| Simpler, Sampler en drum racks | Samplers van LYNX SHARE |
| `.als`-set | `.lynx`-projectbestand |

## Het clipraster

Een slot bevat een patternclip of een audioclip.

- Een track speelt één clip tegelijk. Een gestarte clip vervangt de clip die speelt.
- Een start wacht op de volgende stap van de startkwantisering.
- `Legato` start de nieuwe clip op de positie die de vervangen clip had bereikt. `Loop` geldt per clip.
- Scènes kunnen worden ingevoegd, verwijderd en hernoemd.
- Een track die een clip heeft gestart, blijft stil in de Playlist totdat je op `Terug naar Playlist` drukt. Een optie geeft tracks terug wanneer het transport stopt.

Follow actions lopen af na een aantal maten of doorgangen van de clip. De acties zijn `Geen actie`, `Stop`, `Opnieuw afspelen`, `Vorige`, `Volgende`, `Eerste`, `Laatste`, `Willekeurig` en `Overig`. Een schakelaar zet follow actions voor het hele raster aan of uit.

## Wat uit een .als wordt geïmporteerd

- MIDI-, audio-, groeps- en returntracks, tempo, maatsoort, locators, de loop, mixerinstellingen (volume, pan, mute, solo, sends) en hun automatisering.
- VST2- en VST3-plugins met hun staat, als dezelfde plugin is geïnstalleerd. Zie [Plugins van derden](/wiki/plugins/).
- Session-clips en scènes, in het clipraster, met hun startmodus, kwantisering, legato, loop, follow actions en stopknoppen. Om ze in plaats daarvan op de Playlist te zetten, één sectie per scène, wijzig je `Sessieclips uit Live-sets` in `Opties › Algemene instellingen...`.
- Warp-markeringen.

## Een .als exporteren

1. Kies `Bestand › Exporteren › Ableton Live-set (.als)...`.
2. Kies onder `Opslaan voor` `Live 12` of `Live 11 (opent ook in Live 12)`.
3. Kopieer de samples onder `Samples` naar de projectmap van de set, of verwijs ernaar waar ze staan.
4. Kies een map. De set wordt opgeslagen als `<name> Project/<name>.als`.

| Geschreven | Niet geschreven |
|---|---|
| Een MIDI-track per kanaal, met de noten als MIDI-clips | De eigen instrumenten en effecten van LYNX SHARE. Hun tracks en noten blijven behouden |
| Audiotracks met stretch, transpositie, gain, fades en warp-modus | Automatisering van effectparameters |
| Groepen, returns en sends | Routes van insert naar insert |
| VST2- en VST3-plugins met hun staat | |
| Markers, en automatisering van volume, pan en tempo | |
| Het clipraster als Session-clips en scènes, racks en Drum Racks | |

Een rapport na de export toont wat is weggelaten.

## Goed om te weten

- Veel eigen devices van Live komen binnen als die van LYNX SHARE, met hun instellingen en automatisering: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift en Operator (zonder FM). De andere worden weggelaten en vermeld in het importrapport.
- Instrument- en Audio Effect Racks komen binnen met hun ketens, zones en macro's, en een Drum Rack wordt het Drum Rack van LYNX SHARE. De MIDI Effect Racks van Live worden nog niet omgezet en staan in het importrapport.
- Het importrapport toont wat is weggelaten.
- Live knipt overlappende audioclips in een geëxporteerde set bij.
- Warp-markeringen uit een Live-set blijven bij het importeren behouden en worden bij het exporteren teruggeschreven.

## Gerelateerd

- [Playlist](/wiki/playlist/)
- [Lay-outs](/wiki/layouts/)
- [Projecten importeren](/wiki/importing-projects/)
- [Bestandsformaten](/wiki/file-formats/)
- [Twee werkwijzen in één programma](/why/#workflows)
