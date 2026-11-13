---
source: a79e43f031
---
LYNX SHARE slaat projecten op als `.lynx`-bestanden. Het opent ook projecten van andere programma's, laadt de gangbare audiobestanden en exporteert audio, MIDI en projecten voor andere programma's. De tabel toont elk formaat van 0.1.26 alpha.

Een project dat uit het formaat van een ander programma is geopend, is een import. `Bestand › Opslaan` vraagt dan waar een `.lynx`-bestand moet komen, en het originele bestand wordt nooit gewijzigd.

## Een project van een ander programma openen

1. Kies `Bestand › Openen...` (`Ctrl+O`).
2. Kies een `.flp`-, `.als`-, `.flm`-, `.mid`- of `.zip`-bestand.
3. Kies bij een `.flp` `Help › Last FL Studio import report` om te lezen wat benaderd of weggelaten is.
4. Kies `Bestand › Opslaan` en kies waar het `.lynx`-bestand moet komen.

## Formaten

| Formaat | Openen | Opslaan / Exporteren | Opmerkingen |
|---|---|---|---|
| `.lynx` | `Bestand › Openen...` | `Bestand › Opslaan`, met automatisch opslaan | Het eigen projectformaat van LYNX SHARE (JSON) |
| `.zip` | `Bestand › Openen...` pakt het uit naar een map en opent het project erin | `Bestand › Exporteren › Project met samples (.zip)...` | Opent ook het gezipte looppakket van FL Studio |
| `.lynxpack` | Opent na een samenvatting. Een deel wordt samengevoegd met het project van de host | `Bestand › Online sessie › Project versturen...` | Een project of een deel dat in een [online sessie](/wiki/online-sessions/) is verstuurd. Delen met plugins van derden reizen mee als audiorenders plus de plugininstellingen |
| `.flp` | `Bestand › Openen...` | `Bestand › Exporteren › FL Studio-project...`, een FL Studio 2026-project of een gezipt looppakket | Native plugins spelen op nagebouwde engines van wisselende exactheid. FLEX-packs worden nooit geopend. Bij export worden instrumenten en effecten die alleen in LYNX bestaan naar audio gerenderd of vervangen door de dichtstbijzijnde FL-plugin, en modulators worden weggelaten. Zie [Overstappen van FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Bestand › Openen...` | Nee | FL Studio Mobile-project. Gedeeltelijk: andere instrumenten dan MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, de Slicer en DirectWave spelen de ingebouwde synth |
| `.als` | `Bestand › Openen...`, sets van Live 11 en 12 | `Bestand › Exporteren › Ableton Live-set (.als)...`, voor Live 12 of Live 11 | Bij import worden devices van Live zonder equivalent in LYNX SHARE weggelaten. Bij export worden de eigen instrumenten en effecten van LYNX SHARE niet omgezet, en vervallen automatisering van effectparameters en routes van insert naar insert. Zie [Overstappen van Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `Bestand › Openen...` maakt een nieuw project. `Bestand › Importeren › MIDI-bestand...` en de Piano Roll importeren in het huidige | `Bestand › Exporteren › MIDI-bestand...` (`Ctrl+Shift+M`) | |
| `.wav` | Als sample of audioclip | `Bestand › Exporteren › Wave-bestand...`, 16-bit, 24-bit of 32-bit float | Optioneel dither, tempo-informatie, loopmarkeringen en Playlistmarkeringen |
| `.mp3` | Als sample of audioclip | `Bestand › Exporteren › MP3-bestand...` | Constante bitrate |
| `.ogg` | Als sample of audioclip | `Bestand › Exporteren › OGG-bestand...` | |
| `.flac` | Als sample of audioclip | `Bestand › Exporteren › FLAC-bestand...`, compressie 0 tot 8 | Verliesvrij op elk niveau |
| `.aif`, `.aiff` | Als sample of audioclip | Nee | |
| `.wv` | Als sample | Nee | WavPack-samples, zoals meegeleverd in de packs van FL Studio |
| `.sf2`, `.sf3` | Gespeeld door LYNX Soundfont | Nee | SoundFonts |
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
| `Bestand › Exporteren › Projectbotten...` | Schrijft kanaal- en effectpresets en scores naar een map |

Het exportvenster rendert het nummer, het huidige pattern of de selectie in de Playlist.

## Vanaf de opdrachtregel

Op Windows heet het programma `LYNX.exe`. Op macOS en Linux heet het `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` en `--export-flp` accepteren ook een `.als` als invoer.

## Goed om te weten

- Audiobestanden langer dan 30 seconden worden vanuit een schijfcache gestreamd.
- Een `.flp`, `.als` of `.flm` wordt bij opslaan nooit geschreven. De exportopdrachten schrijven nieuwe bestanden.
- `.wv`-bestanden worden alleen gelezen.
- Een `.zip` wordt eerst naar een map uitgepakt, daarna wordt het project erin geopend.

## Gerelateerd

- [Projecten importeren](/wiki/importing-projects/)
- [Exporteren](/wiki/exporting/)
- [Opdrachtregel](/wiki/command-line/)
- [Overstappen van FL Studio](/wiki/coming-from-fl-studio/)
- [Overstappen van Ableton Live](/wiki/coming-from-ableton/)
