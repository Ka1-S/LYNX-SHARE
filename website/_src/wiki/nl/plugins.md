---
source: 45c1275215
---
![Het onderste deel van het Pluginbeheer, met de opmerking dat elk pluginbestand in een apart proces wordt onderzocht](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE host instrument- en effectplugins van andere makers. Het vindt ze door de pluginmappen te scannen, en `Opties › Pluginbeheer...` toont wat het heeft gevonden. De eigen instrumenten en effecten van LYNX SHARE staan beschreven in [Ingebouwde instrumenten en effecten](/wiki/built-in/).

## Een plugin scannen en toevoegen

1. Kies `Opties › Plugins scannen`. De voortgang staat in de hintbalk en je kunt gewoon doorwerken.
2. Staat de plugin in een eigen map, open dan `Opties › Pluginbeheer...`, klik op `Pluginmappen...`, voeg de map toe en klik op `Nieuwe plugins zoeken`.
3. Druk op `F8` voor de Plugin-kiezer en typ een deel van de naam van de plugin.
4. Dubbelklik erop of druk op `Enter`. Een instrument wordt een nieuw kanaal. Een effect komt in de geselecteerde Mixer-insert.

Bij de eerste start toont de installatiewizard de pluginmappen en scant ze op de achtergrond. `Opties › Installatiewizard (taal, audio, pluginmappen)...` opent hem opnieuw. LYNX SHARE zoekt ook een paar seconden na elke start naar nieuwe plugins.

## Formaten per systeem

| Systeem | Gehoste formaten |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2 wordt op macOS niet gehost. Op Linux werken Windows-plugins via [yabridge](https://github.com/robbert-vdh/yabridge) en Wine, die je zelf installeert: voeg de map waarmee yabridge synchroniseert toe als pluginmap.

## Het Pluginbeheer

| Regelaar | Wat het doet |
|---|---|
| Zoekvak | Filtert op naam, fabrikant, formaat of bestand |
| `Alle`, `Instrumenten`, `Effecten`, `Favorieten`, `Mislukt` | Kiezen welke plugins worden getoond |
| Sterkolom | Markeert een favoriet |
| Kolom `Bits` | 32, 64 of 32+64 (Windows) |
| Kolom `Status` | `OK`, `Niet gevonden` of `Laden mislukt` |
| `Nieuwe plugins zoeken` | Scant naar bestanden die nog niet in de lijst staan |
| `Herscannen...` | Vergeet de mislukte bestanden en onderzoekt elk bestand opnieuw |
| `Pluginmappen...` | Bewerkt de mappen die worden gescand |
| `Bestand toevoegen...` | Voegt één pluginbestand toe |

Elk pluginbestand wordt in een apart proces onderzocht. Een bestand dat crasht of 30 seconden vastloopt, wordt als mislukt gemarkeerd en later overgeslagen. Op Windows staan de mislukte bestanden met de reden in `%APPDATA%\LYNX\scan-failures.log`.

De standaardmappen worden gescand, plus die je zelf toevoegt:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` en `Steinberg\VSTPlugins` onder Program Files, en de mappen die FL Studio gebruikt als het is geïnstalleerd.
- macOS: `VST3`, `Components` en `CLAP` in `~/Library/Audio/Plug-Ins` en `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` en dezelfde mappen onder `/usr/lib` en `/usr/local/lib`.

## Plugins in een apart proces draaien

Standaard draait een plugin binnen LYNX SHARE, dus een plugin die crasht kan het programma meetrekken. Als optie draait elke plugin in een eigen proces.

1. Open `Opties › Algemene instellingen...`.
2. Zet `Plugins in een apart proces draaien (beschermt LYNX SHARE tegen crashes)` aan.
3. Open het project opnieuw om het toe te passen op plugins die al open zijn.

Als zo'n plugin crasht of niet meer reageert, wordt hij gemute, verschijnt er een melding en blijft LYNX SHARE draaien. Sla het project op en open het opnieuw om de plugin weer te laden. De editor van de plugin opent in een eigen venster.

Om dit voor één plugin te doen, gebruik je `In een apart proces draaien` of `Always run <plugin> in a separate process` in het wrappermenu van het pluginvenster. Als de editor van een plugin crasht, biedt de melding die verschijnt ook de tweede aan.

Dit werkt op Windows en Linux, en sinds 0.1.25 op macOS.

## 32-bit plugins (alleen Windows)

Op Windows draaien 32-bit VST2- en VST3-plugins via een brug (`LYNX-bridge32.exe`). De brug voegt geen latentie toe. Een plugin die daarin crasht, stopt op zichzelf terwijl LYNX SHARE blijft afspelen. De editor van een plugin in de brug opent in een eigen venster. Als zowel een 32-bit- als een 64-bit-build van een plugin is geïnstalleerd, kiest `Bitgrootte` in `Wrapper-instellingen` van het pluginvenster welke wordt geladen.

## Prestatiemonitor voor plugins

![De prestatiemonitor voor plugins: één rij per instrument en effect met zijn aandeel in de verwerkingstijd](/assets/img/wiki/plugin-monitor.webp)

`Extra › Prestatiemonitor voor plugins...` toont elk instrument en effect met de verwerkingstijd die het kost: nu, gemiddeld en op zijn piek. 100% betekent één processorkern die de hele duur van de audio bezig is.

| Instelling | Wat het doet |
|---|---|
| `Zoeken` | Filtert op plugin, kanaal of insert |
| `Alleen plugins` | Verbergt de eigen instrumenten en effecten van LYNX SHARE |
| `Bevriezen` | Stopt het bijwerken van de getallen |
| `Resetten bij afspelen` | Begint gemiddelden en pieken opnieuw bij elke start van het afspelen |
| `Reset` | Wist gemiddelden en pieken nu |

## Als een plugin niet is geïnstalleerd

LYNX SHARE zoekt dezelfde plugin eerst op identificatie, dan op bestand, dan op naam. Een project dat met de VST2 is opgeslagen, opent met de VST3 van hetzelfde product en andersom, maar dan speelt de plugin met zijn standaardinstellingen.

Is een effect helemaal niet geïnstalleerd, dan neemt een LYNX SHARE-effect met dezelfde rol zijn slot over en blijft het origineel in het project. Zie [Ingebouwde instrumenten en effecten](/wiki/built-in/). `Help › Ontbrekende bestanden...` toont de plugins die zijn vervangen.

## Goed om te weten

- Het venster van een gehoste plugin heeft een presetbalk: de eigen programma's van de plugin, je presets, `Preset opslaan...`, `Presetbestand laden...` en `Presetbestand exporteren...`. `.fxp`-, `.fxb`- en `.vstpreset`-bestanden laden alleen in een plugin die binnen LYNX SHARE draait, niet in een apart proces.
- De monitor meet alleen zolang zijn venster open is.
- Ontbrekende instrumenten worden nog niet vervangen. Een ontbrekende instrumentplugin wordt een ingebouwde synth, gekozen op de naam van het kanaal.

## Gerelateerd

- [Ingebouwde instrumenten en effecten](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Probleemoplossing](/wiki/troubleshooting/)
