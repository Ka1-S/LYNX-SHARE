---
source: 37920663db
---
![De Clip-launch-indeling: een clipraster met scènes naast de Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE start in de Pattern-lay-out: een Channel Rack, patterns en een Playlist. De Clipstart-lay-out is optioneel en voegt naast de Playlist een clipraster met scènes toe. LYNX SHARE leest en schrijft `.als`-sets. Het is niet gelieerd aan Ableton.

Deze pagina is ook voor Bitwig Studio-gebruikers: het clipraster werkt op dezelfde manier, en de DAWproject-bestanden (`.dawproject`) van Bitwig openen met `Bestand › Openen...` en worden geschreven door `Bestand › Exporteren › DAWproject (.dawproject)...`.

## Je eerste stappen

1. **Kies de lay-out.** De eerste keer dat LYNX SHARE start, vraagt een welkomstkaart waar je van komt. Klik op `Ableton Live of Bitwig`: de Clipstart-lay-out opent. Later schakel je op elk moment over met `Beeld › Lay-outstijl › Clipstart-lay-out`.
2. **Vul een slot.** Sleep een sample of een pattern uit de Browser naar een slot, of klik met rechts op een leeg slot en kies `Patroon hier plaatsen`.
3. **Start.** Klik op de driehoek links van een clip, of op de driehoek van een scène in de kolom `Scènes` om de hele rij te starten. Of open een van je `.als`-sets (hieronder).

Wat anders is dan in Live:

- De Playlist is het arrangement, en de tracks ervan zijn de kolommen van het raster. Een track die een clip heeft gestart, blijft op het raster totdat je op `Terug naar Playlist` drukt.
- Buiten het raster volgen de toetsen FL Studio. `Ctrl+Z` maakt één stap ongedaan en doet die opnieuw als je nogmaals drukt.
- Alleen een deel van de eigen devices van Live wordt omgezet (zie Goed om te weten). Plugins van derden komen mee met hun staat.
- De Patroonlay-out, de werkwijze van FL Studio met een Channel Rack en patterns, werkt op hetzelfde project.

## Een Live-set openen

1. Kies `Bestand › Openen...` (`Ctrl+O`) en selecteer het `.als`-bestand. Sets die zijn opgeslagen door Live 11 en Live 12 worden gelezen.
2. Kies `Beeld › Lay-outstijl › Clipstart-lay-out` om met het clipraster te werken, waar de Session-clips en scènes van de set terechtkomen.
3. Gebruik de schakelaar `Grid / Both / Song` in de titelbalk van de Playlist om het raster, de Playlist of beide te tonen.
4. Kies `Bestand › Opslaan`. Het vraagt waar een `.lynx`-bestand moet komen. Het `.als`-bestand blijft ongewijzigd.

`Beeld › Lay-outstijl › Pattern-lay-out (standaard)` gaat terug. Dezelfde keuze staat in `Opties › Algemene instellingen...`. Patterns en het Channel Rack blijven in beide indelingen beschikbaar.

## Wat waarmee overeenkomt

| In Live | In LYNX SHARE |
|---|---|
| Session-weergave | Het clipraster van de Clipstart-lay-out |
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
| Een MIDI-track per kanaal, met de noten als MIDI-clips | De eigen instrumenten en effecten van LYNX SHARE, behalve Helix, dat Wavetable wordt. Hun tracks en noten blijven behouden |
| Audiotracks met stretch, transpositie, gain, fades en warp-modus | Automatisering van effectparameters |
| Groepen, returns en sends | Routes van insert naar insert |
| VST2- en VST3-plugins met hun staat | |
| Markers, en automatisering van volume, pan en tempo | |
| Het clipraster als Session-clips en scènes, racks en Drum Racks | |

Een rapport na de export toont wat is weggelaten.

## Goed om te weten

- Veel eigen devices van Live komen binnen als die van LYNX SHARE, met hun instellingen en automatisering: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable en Meld. De andere worden weggelaten en vermeld in het importrapport.
- Instrument- en Audio Effect Racks komen binnen met hun ketens, zones en macro's, en een Drum Rack wordt het Drum Rack van LYNX SHARE. De Arpeggiator van Live wordt de arpeggiator van het kanaal. Pitch, Velocity, Chord, Note Length en MIDI Effect Racks worden in de noten verwerkt.
- Het importrapport toont wat is weggelaten.
- In de Device View toont een rack de devices van zijn ketens inline, en de Macro Map-modus (de `Map`-knoppen van het rack) geeft een deviceparameter aan een macro, zoals in Live.
- De Session-mixer heeft de Solo / Cue-schakelaar van Live, met een `Cue Out` voor je hoofdtelefoon en een `Cue-volume`.
- Live knipt overlappende audioclips in een geëxporteerde set bij.
- Warp-markeringen uit een Live-set blijven bij het importeren behouden en worden bij het exporteren teruggeschreven.

## Gerelateerd

- [Playlist](/wiki/playlist/)
- [Lay-outs](/wiki/layouts/)
- [Projecten importeren](/wiki/importing-projects/)
- [Bestandsformaten](/wiki/file-formats/)
- [Twee lay-outs in één programma](/why/#workflows)
- [Leren: tutorials](/learn/)
