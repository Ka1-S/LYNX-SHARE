---
source: 358890815a
---
`Bestand › Openen...` (`Ctrl+O`) opent de eigen `.lynx`-projecten van LYNX SHARE en ook `.flp`-, `.als`-, `.flm`- en `.mid`-bestanden. Een project uit een ander programma is een import: het wordt een nieuw, niet-opgeslagen LYNX SHARE-project, en het originele bestand wordt nooit overschreven.

## Een project uit een ander programma openen

1. Kies `Bestand › Openen...` en selecteer het `.flp`-, `.als`-, `.flm`- of `.mid`-bestand.
2. Wacht terwijl LYNX SHARE de plugins en samples van het project zoekt.
3. Als iets niet is gevonden, opent het venster `Ontbrekende bestanden`. Wijs het naar je samples, of sluit het.
4. De titelbalk toont nu waar het project vandaan komt, bijvoorbeeld "(imported from FL Studio)".
5. Kies `Bestand › Opslaan`. LYNX SHARE vraagt waar een `.lynx`-bestand moet komen.

## Wat wordt overgenomen

| Formaat | Omgezet | Niet omgezet |
|---|---|---|
| `.flp` (FL Studio) | Kanalen, samples, patterns, slide- en portanoten, elk arrangement, markers, wijzigingen van de maatsoort, automatisering, controllerkoppelingen, de Mixer met routing, sidechains, track-EQ en native effecten | FLEX-packs zijn versleuteld en worden nooit geopend. Een FLEX-kanaal krijgt een synthpatch die op de naam van de preset wordt gekozen |
| `.als` (Live 11 en 12) | MIDI-, audio-, groeps- en returntracks, tempo, maatsoort, locators, de loop, volume, pan, mute, solo en sends met hun automatisering, Session-clips en scènes, warp-markeringen, racks en Drum Racks, en veel eigen devices van Live (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift en meer) als die van LYNX SHARE | Devices van Live zonder equivalent in LYNX SHARE (Hybrid Reverb, Grain Delay, Amp, Roar, FM Operator-patches en andere) worden weggelaten en vermeld. MIDI Effect Racks worden niet omgezet |
| `.flm` (FL Studio Mobile) | Tempo, maatsoort, één mixer-insert per kanaal, één Playlist-track per baan, patterns, audioclips met offset en pitch, drumkits, DW Sampler en `.dwp`-programma's, de Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth en SuperSaw, de effecten, de automatisering en de akkoordensets | Andere FLM-generators spelen de ingebouwde synth |
| `.mid` | Noten, maatsoorten en elke tempowijziging | Een MIDI-bestand bevat geen geluiden: kanalen krijgen de LYNX SHARE-synth of een MIDI Out-kanaal |

Plugins van derden in VST2-, VST3- en CLAP-formaat in een `.flp`, en VST2- en VST3-plugins in een `.als`, laden met hun opgeslagen staat als dezelfde plugin op je computer is geïnstalleerd.

## De eigen plugins van FL Studio

De generators en effecten van FL Studio worden niet geladen. LYNX SHARE leest hun opgeslagen instellingen en speelt ze af op engines die het heeft nagebouwd. Elke engine heeft een kwaliteitsniveau, van `Exact` (bitidentiek aan het origineel bij elke geteste staat) tot `Placeholder` (de instellingen blijven bewaard, maar de plugin is stil of laat audio ongewijzigd door). De niveaus worden uitgelegd in [Overstappen van FL Studio](/wiki/coming-from-fl-studio/).

Een `.flp` openen werkt zonder dat FL Studio is geïnstalleerd. Sommige engines hebben golven, samples of impulsen nodig die tijdens het draaien uit je eigen FL Studio-installatie worden gelezen.

## Ontbrekende samples

![Het venster Missing files op de tab Samples: gevonden bestanden in groen, ontbrekende in amber, met Search folder, Locate en Skip](/assets/img/wiki/missing-files.webp)

De tab `Samples` toont elke sample die niet is gevonden, met het opgeslagen pad en wat hem gebruikt.

| Commando | Wat het doet |
|---|---|
| `Map doorzoeken...` | Doorzoekt een map en de submappen ervan naar alle ontbrekende samples |
| `Lokaliseren...` | Vraagt waar één sample staat. Andere samples in dezelfde map worden ook opgepikt |
| `Overslaan` | Laat de sample weg. Het kanaal ervan blijft stil |

`Toepassen` gebruikt de bestanden die zijn gevonden.

De tab `Automatisch gevonden` toont bestanden die LYNX SHARE zelf in een andere map heeft gevonden. Sla het project op om de nieuwe plekken te bewaren.

## Ontbrekende plugins

LYNX SHARE zoekt elke plugin van derden op identificatie, dan op bestand, dan op naam. Een project dat met de VST2 is opgeslagen, opent met de VST3 van hetzelfde product en andersom, maar dan met standaardinstellingen.

Als een effect niet is geïnstalleerd, neemt een LYNX SHARE-effect met dezelfde rol het slot over: een reverb voor een reverb, een compressor voor een compressor.

- Het slot toont de naam van de originele plugin met een LYNX SHARE-badge.
- Waar de opgeslagen staat gelezen kan worden, worden de instellingen vertaald. Anders begint de vervanger met een standaardpreset. De tabel met producten, hun vervangers en wat per product wordt vertaald staat in [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- Automatisering van de parameters van het origineel blijft bewaard, maar stuurt de vervanger niet aan.
- De verwijzing naar en de staat van de originele plugin blijven in het project. Zodra de plugin is geïnstalleerd, opent het project er weer mee.
- `Alle LYNX-SHARE-vervangers van dit project bypassen`, in het rechtsklikmenu van een slot met een vervanger, zet ze allemaal tegelijk uit.

De schakelaar is `Ontbrekende plugins vervangen door LYNX-SHARE-equivalenten` in `Opties › Algemene instellingen...`, standaard aan. Staat hij uit, dan wordt het slot van een ontbrekend effect weggelaten uit de mixer. Een ontbrekend instrument wordt niet vervangen. Het wordt een ingebouwde synth met een preset die op de naam van het kanaal wordt gekozen.

Om de echte plugin terug te krijgen, installeer je hem, kies je `Opties › Plugins scannen` en open je het project opnieuw.

## MIDI-bestanden

![Het venster Import MIDI data: tracks, kanalen, kanaaltype en opties](/assets/img/wiki/midi-import-dialog.webp)

`Bestand › Openen...` maakt van een MIDI-bestand een nieuw project. `Bestand › Importeren › MIDI-bestand...` opent eerst het venster `MIDI-gegevens importeren`, waar je kiest wat je binnenhaalt.

| Instelling | Wat het doet |
|---|---|
| `Welke tracks importeren?` | Alle tracks, of één |
| `Welke kanalen importeren?` | Klik op een MIDI-kanaal om het weg te laten. Rechtsklikken houdt alleen dat kanaal |
| `Kanaaltype` | `LYNX-SHARE-synth` of `MIDI Out` |
| `Nieuw project starten` | Begint een leeg project. Uit: voegt toe aan het geopende project |
| `Eén kanaal per track maken` | Uit: één kanaal per MIDI-kanaal |
| `Mixertracks instellen voor nieuwe kanalen` | Routeert de nieuwe kanalen naar opeenvolgende mixer-inserts |
| `Maatsoorten importeren` | De maatsoort van het bestand wordt die van het project |

`Ctrl+M` in de [Piano Roll](/wiki/piano-roll/) importeert een MIDI-bestand in het huidige kanaal.

## Goed om te weten

- `Help › Ontbrekende bestanden...` opent het venster later opnieuw. `Help › Last FL Studio import report` toont wat is benaderd, weggelaten of niet geïnstalleerd.
- Session-clips van een Live-set komen in het clipraster van de Clip-launch-indeling. `Sessieclips uit Live-sets` in `Opties › Algemene instellingen...` zet ze in plaats daarvan op de Playlist.

## Gerelateerd

- [Bestandsformaten](/wiki/file-formats/)
- [Overstappen van FL Studio](/wiki/coming-from-fl-studio/)
- [Overstappen van Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exporteren](/wiki/exporting/)
