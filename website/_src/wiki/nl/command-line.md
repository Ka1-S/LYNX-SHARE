---
source: 98829adcb8
---
LYNX SHARE draait ook zonder zijn venster te openen. Eén optie rendert een project naar audio, andere zetten het om naar een FL Studio-project of een Ableton Live-set, en een aantal bestaat voor tests. De eigen controles van het project gebruiken dezelfde opties.

Het programma heet `LYNX.exe` op Windows en `LYNX` op macOS en Linux. Op macOS is het volledige pad `/Applications/LYNX.app/Contents/MacOS/LYNX`. De opties zijn op elk systeem hetzelfde.

## Een project naar een bestand renderen

1. Open een terminal in de map met het programma. Op Windows is dat de installatiemap of de map waarin je de zip hebt uitgepakt.
2. Voer `LYNX.exe --export project.flp out.flac` uit, met je eigen project en uitvoerbestand.
3. Wacht tot het programma afsluit. Er opent geen venster.
4. Lees `out.flac.txt`, dat naast het uitvoerbestand wordt geschreven. Bij een geïmporteerd project staat daarin wat is omgezet, benaderd en weggelaten, en welke plugins zijn gevonden of ontbreken.

## Een project openen

```
LYNX.exe project.lynx
```

Start LYNX SHARE met het venster en opent het bestand. Het accepteert `.lynx`, `.flp`, `.als`, `.flm` en `.mid`. Een relatief pad wordt vanaf de huidige map genomen.

## Opties voor dagelijks gebruik

| Optie | Wat het doet |
|---|---|
| `--export <project> <out>` | Rendert het hele nummer met zijn staart, zoals het exportvenster doet. De extensie van `<out>` bepaalt het formaat: `.wav` (24-bit), `.flac`, `.ogg` of `.mp3` (320 kbps) |
| `--export ... --pattern` | Rendert alleen het huidige pattern |
| `--export ... --insert N` | Rendert alleen de uitgang van Mixer-insert N |
| `--export ... --rate N` | Rendert met een andere samplefrequentie, in Hz |
| `--stems <project> <base.wav>` | Schrijft één WAV per Mixer-insert met kanalen: `base - 1.wav`, `base - 2.wav` enzovoort |
| `--export-flp <project> <out.flp>` | Schrijft een FL Studio-project. Met `out.zip` schrijft het een gezipt looppakket |
| `--export-flp ... --substitute` | Vervangt plugins die alleen in LYNX bestaan door de dichtstbijzijnde FL-plugin in plaats van ze naar audio te renderen |
| `--export-als <project> <folder>` | Schrijft een Ableton Live-set naar `<folder>/<name> Project/<name>.als`, voor Live 12 |
| `--export-als ... --live11` | Schrijft de set voor Live 11 |
| `--devices <report.txt>` | Toont de audiodrivertypen en apparaten van deze computer |
| `--showcase <out.lynx>` | Schrijft het eigen demoproject van LYNX SHARE |
| `--lang <id>` | Gebruikt voor deze keer een andere interfacetaal, bijvoorbeeld `--lang pt-BR` |
| `--menu-entry on` of `uit` | Alleen Linux AppImage: voegt LYNX SHARE toe aan het toepassingenmenu, of verwijdert het |

`--export`, `--stems` en `--export-flp` nemen een `.lynx`, `.flp` of `.als` als project. `--export-als` neemt ook `.flm`.

Voorbeelden:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Opties voor tests

Deze bestaan om LYNX SHARE zelf te controleren. Ze zijn nuttig in een bugmelding.

| Optie | Wat het doet |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Voert de zelftest uit zonder audioapparaat. Het rapport eindigt met `result=OK` als de test slaagt |
| `--snapshot <prefix> [project]` | Tekent de interface en de vensters ervan naar PNG-bestanden die naar het voorvoegsel zijn vernoemd. De afbeeldingen in deze wiki zijn zo gemaakt |
| `--render <project> <out.wav> <seconds>` | Rendert dat aantal seconden van het huidige pattern naar een 24-bit WAV |
| `--plugintest <plugin> <report.txt>` | Laadt en test één VST3- of CLAP-plugin |
| `--sandboxtest <plugin> <report.txt>` | Test een plugin in een apart proces, ook een die vastloopt |
| `--scantest <folder> <report.txt>` | Scant een map op plugins, in aparte processen |
| `--fldb <report.txt> [project.flp]` | Meldt de hier gevonden FL Studio-installatie, de plugindatabase ervan en de pluginovereenkomsten van een project |
| `--devicetest <type> <device> <report.txt>` | Opent een audiodriver voor één seconde stilte en meldt het resultaat |
| `--uiperf <project> <report.txt>` | Meet hoe lang het tekenen van de Mixer, Playlist en het Channel Rack duurt |

Voorbeelden:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Goed om te weten

- Resultaten gaan naar bestanden, niet naar de terminal. `--export` schrijft `<out>.txt`, `--export-flp` schrijft `<out.flp>.txt`, `--export-als` schrijft `export-als.txt` in de map en `--stems` schrijft `<base.wav>.txt`.
- Het programma leest de standaardinvoer. Voeg in een shell-lus `< /dev/null` toe aan elke aanroep.
- `--snapshot` tekent op 1280x720, tenzij de omgevingsvariabele `LYNX_SNAPSHOT_SIZE` iets anders zegt, bijvoorbeeld `1920x1080`.
- `--lang` accepteert de id's van de talen in `Opties › Algemene instellingen...`, zoals `en`, `pt-BR`, `de`, `es`, `fr` of `ja`. Een onbekende id wordt genegeerd.
- `--export` gebruikt hetzelfde renderpad als het exportvenster, maar geen van de andere opties van dat venster. Gebruik voor dither, bitdiepte of normaliseren het venster.
- De broncode heeft nog veel meer testopties. Die zijn intern en kunnen per versie veranderen.

## Gerelateerd

- [Exporteren](/wiki/exporting/)
- [Bestandsformaten](/wiki/file-formats/)
- [Bouwen vanuit de broncode](/wiki/build-from-source/)
- [Probleemoplossing](/wiki/troubleshooting/)
