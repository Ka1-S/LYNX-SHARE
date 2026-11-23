---
source: e1ff018baf
---
LYNX SHARE slaat projecten op als `.lynx`-bestanden. Het opent ook projecten van andere programma's, laadt de gangbare audiobestanden en exporteert audio, MIDI en projecten voor andere programma's. De tabel toont elk formaat van 0.1.31 alpha.

Een project dat uit het formaat van een ander programma is geopend, is een import. `Bestand › Opslaan` vraagt dan waar een `.lynx`-bestand moet komen, en het originele bestand wordt nooit gewijzigd.

## Een project van een ander programma openen

1. Kies `Bestand › Openen...` (`Ctrl+O`).
2. Kies een `.flp`-, `.als`-, `.flm`-, `.dawproject`-, `.band`-, `.blx`-, `.mid`- of `.zip`-bestand.
3. Kies `Help › Laatste projectimportrapport` om te lezen wat benaderd of weggelaten is.
4. Kies `Bestand › Opslaan` en kies waar het `.lynx`-bestand moet komen.

## Formaten

| Formaat | Openen | Opslaan / Exporteren | Opmerkingen |
|---|---|---|---|
| `.lynx` | `Bestand › Openen...` | `Bestand › Opslaan`, met automatisch opslaan | Het eigen projectformaat van LYNX SHARE (JSON). `Bestand › Een kopie opslaan voor een oudere LYNX...` schrijft een kopie die 0.1.27 of 0.1.28 opent, en somt op wat die versie niet kan bewaren |
| `.zip` | `Bestand › Openen...` pakt het uit naar een map en opent het project erin | `Bestand › Exporteren › Gezipt looppakket...` | Opent ook het gezipte looppakket van FL Studio |
| `.lynxpack` | Opent na een samenvatting. Een deel wordt samengevoegd met het project van de host | `Bestand › Online sessie › Project versturen...` | Een project of een deel dat in een [online sessie](/wiki/online-sessions/) is verstuurd. Delen met plugins van derden reizen mee als audiorenders plus de plugininstellingen |
| `.flp` | `Bestand › Openen...` | `Bestand › Exporteren › FL Studio-project...`, een FL Studio 2026-project of een gezipt looppakket | Native plugins spelen op nagebouwde engines van wisselende exactheid. FLEX-packs worden nooit geopend. Bij export worden instrumenten en effecten die alleen in LYNX bestaan naar audio gerenderd of vervangen door de dichtstbijzijnde FL-plugin, en modulators worden weggelaten. Zie [Overstappen van FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Bestand › Openen...` | `Bestand › Een kopie opslaan als FL Studio Mobile-project (.flm)...` | FL Studio Mobile-project. Gedeeltelijk: andere instrumenten dan MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, de Slicer en DirectWave spelen de ingebouwde synth. De export is een kopie: ze bewaart de noten, het tempo, de mixer, de eigen instrumenten van FL Studio Mobile, de DW Sampler, 3x Osc, drumkits, effecten en de automatisering van mixerniveau, panning, effecten, DW Sampler en GMSynth, met de samples ernaast. In de kopie houden andere instrumenten hun noten op de MiniSynth, en effecten zonder FLM-module en automatisering van andere doelen (de eigen knoppen van de synths, plugins van derden) vallen weg; een venster somt alles op voor het opslaan. |
| `.als` | `Bestand › Openen...`, sets van Live 11 en 12 | `Bestand › Exporteren › Ableton Live-set (.als)...`, voor Live 12 of Live 11 | Bij import worden devices van Live zonder equivalent in LYNX SHARE weggelaten. Bij export worden de eigen instrumenten en effecten van LYNX SHARE niet omgezet (Helix wordt Wavetable), en vervallen automatisering van effectparameters en routes van insert naar insert. Zie [Overstappen van Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `Bestand › Openen...` | `Bestand › Exporteren › DAWproject (.dawproject)...` | Het open uitwisselingsformaat van Bitwig, Studio One, Cubase en andere apps: tracks, mappen, clips, noten, audio met zijn warpmarkers, automatisering, tempo, markers, de mixer met sends, en VST3-, VST2- en CLAP-plugins met hun status. Noteneffecten en MIDI-expressies worden bij import weggelaten; het clipraster, andere arrangementen en enkele details per noot bij export. Beide richtingen vermelden wat is weggelaten |
| `.band` | `Bestand › Openen...` (het pakket, zijn `projectData`-bestand of een zip ervan) | Nee | GarageBand 10- of GarageBand voor iOS-project, als tracks van de Eenvoudige lay-out. Gedeeltelijk: instrumenttracks spelen de ingebouwde synth en drums, behalve Sampler-instrumenten die in GarageBand voor iOS zijn gemaakt. Projecten van GarageBand 11 en ouder zijn niet leesbaar |
| `.blx` | `Bestand › Openen...`, of zijn map met de import `Map...` van de Eenvoudige lay-out | Nee | BandLab-project dat door Cakewalk of Sonar is gedownload, als tracks van de Eenvoudige lay-out met regio's, tempo, toonsoort, de mix, volume- en panautomatisering en de gebruikelijke effecten. Gedeeltelijk: de instrumentgeluiden en andere pedalen van BandLab staan in het importrapport |
| `.mid`, `.midi` | `Bestand › Openen...` maakt een nieuw project. `Bestand › Importeren › MIDI-bestand...` en de Piano Roll importeren in het huidige | `Bestand › Exporteren › MIDI-bestand...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `Bestand › Openen...` | Nee | Trackermodules (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). De instrumenten worden Sampler-kanalen met hun samples, het nummer wordt patternclips met zijn tempowisselingen, en `Beeld › Tracker` bewerkt de noten. Gedeeltelijk: effecten zoals slides, vibrato en arpeggio's, envelopes en volumewijzigingen op een klinkende noot staan in het importrapport |
| `.wav` | Als sample of audioclip | `Bestand › Exporteren › Wave-bestand...`, 16-bit, 24-bit of 32-bit float | Optioneel dither, tempo-informatie, loopmarkeringen en Playlistmarkeringen |
| `.mp3` | Als sample of audioclip | `Bestand › Exporteren › MP3-bestand...` | Constante bitrate |
| `.ogg` | Als sample of audioclip | `Bestand › Exporteren › OGG-bestand...` | |
| `.flac` | Als sample of audioclip | `Bestand › Exporteren › FLAC-bestand...`, compressie 0 tot 8 | Verliesvrij op elk niveau |
| `.aif`, `.aiff` | Als sample of audioclip | `Bestand › Exporteren › AIFF-bestand...`, 16-bit of 24-bit | |
| `.m4a` | – | `Bestand › Exporteren › M4A-bestand...` | Alleen export. AAC met de encoder van het systeem op Windows en macOS; niet beschikbaar op Linux |
| `.wv` | Als sample | Nee | WavPack-samples, zoals meegeleverd in de packs van FL Studio |
| `.sf2`, `.sf3` | Gespeeld door LYNX Soundfont | Nee | SoundFonts |
| `.dwp`, `.dwb` | Dubbelklik in de Browser, een drop op het Channel Rack, of `Programma / bank openen...` van DirectWave | Opslaan van programma's en banken van DirectWave | Programma's en banken van DirectWave. De samples worden gezocht waar DirectWave ze zoekt |
| `.gig` | Als DirectWave-kanaal | Nee | GigaStudio-instrumenten: velocitylagen, round robin, loops, stemming en de amp-envelope. Release-trigger-samples, keyswitches en gecomprimeerde samples worden nog niet gelezen |
| `.ncw` | Als sample of audioclip | Nee | De gecomprimeerde samples van Kontakt. Kontakt `.nki`-instrumenten hebben Kontakt zelf nodig als plugin; beschermde libraries worden nooit ontsleuteld |
| `.fst` | Vanuit de Browser | Nee | Preset van een native plugin van FL Studio. Alleen voor de plugins waarvoor LYNX SHARE een engine heeft |
| `.fsc` | Piano Roll en Event-editor | Piano Roll en Event-editor | Score- en automatiseringsbestand van FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Presetbestand laden...` in het presetmenu van een gehoste plugin | `Preset opslaan...` en `Presetbestand exporteren...` | Presets van plugins van derden. `.fxp` en `.fxb` zijn voor VST2-plugins en `.vstpreset` voor VST3-plugins, en alleen zolang de plugin binnen LYNX SHARE draait |
| Videobestand | `Bestand › Importeren › Videobestand...` zet een video op de Playlist | `Ook een video met deze mix schrijven` in het exportvenster | Import gebruikt op Windows en macOS de decoders van het systeem en heeft op Linux FFmpeg nodig. Export heeft FFmpeg nodig |

## Andere exports

| Opdracht | Wat het doet |
|---|---|
| Optie `Bestanden` van het exportvenster | Stems: één bestand per mixer-insert, per kanaal, of per kanaal met alleen instrumenten |
| `Bestand › Exporteren › Alle Playlist-tracks` | Eén bestand per Playlist-track |
| `Bestand › Exporteren › Alle Playlist-trackgroepen` | Eén bestand per trackgroep |
| `Bestand › Exporteren › Masteren...` | Rendert het project en masterd het op een loudnessdoel, op je eigen computer |
| `Bestand › Exporteren › Voor andere apps...` | Eén map met stems, MIDI-bestanden, een Ableton Live-set, een FL Studio-project, de mixdown en een README, gezipt als je wilt |
| `Bestand › Exporteren › Projectbotten...` | Schrijft kanaal- en effectpresets en scores naar een map |

Het exportvenster rendert het nummer, het huidige pattern of de selectie in de Playlist.

## Vanaf de opdrachtregel

Op Windows heet het programma `LYNX.exe`. Op macOS en Linux heet het `LYNX`.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` en `--export-flp` accepteren ook een `.als` als invoer.

## Goed om te weten

- Audiobestanden langer dan 30 seconden worden vanuit een schijfcache gestreamd.
- Een `.flp`, `.als`, `.flm`, `.dawproject`, `.band` of `.blx` wordt bij opslaan nooit geschreven. De exportopdrachten schrijven nieuwe bestanden.
- `.wv`-bestanden worden alleen gelezen.
- Een `.zip` wordt eerst naar een map uitgepakt, daarna wordt het project erin geopend.

## Gerelateerd

- [Projecten importeren](/wiki/importing-projects/)
- [Exporteren](/wiki/exporting/)
- [Opdrachtregel](/wiki/command-line/)
- [Overstappen van FL Studio](/wiki/coming-from-fl-studio/)
- [Overstappen van Ableton Live](/wiki/coming-from-ableton/)
