---
source: a6d9063ec3
---
LYNX SHARE is een gratis, opensource DAW voor Windows, macOS en Linux. Het werkt met patterns: je programmeert ze in het Channel Rack en de Piano Roll, arrangeert ze in de Playlist en mixt ze in de Mixer. Het opent FL Studio-projecten (`.flp`) en Ableton Live-sets (`.als`), en slaat zijn eigen `.lynx`-formaat op.

Het is een alpha. Verwacht bugs en sla vaak op.

## Aan de praat krijgen

1. Controleer in de tabel hieronder of je systeem wordt ondersteund.
2. Download en installeer het. [Installeren](/wiki/install/) bevat de stappen voor elk systeem.
3. Start het. Een korte installatiewizard kiest de taal, test de audio-uitgang en toont de pluginmappen, die daarna op de achtergrond worden gescand. Je kunt hem overslaan en later opnieuw openen met `Opties › Installatiewizard (taal, audio, pluginmappen)...`.
4. Hoor je niets, ga dan naar [Audio instellen](/wiki/audio-setup/).
5. Lees [Rondleiding door het venster](/wiki/tour/) en maak dan [Je eerste nummer](/wiki/first-song/).

## Wat je nodig hebt

| Systeem | Vereiste |
|---|---|
| Windows | Windows 10 of 11, 64-bit |
| macOS | macOS 11 Big Sur of nieuwer, Intel of Apple silicon |
| Linux | x86_64 met glibc 2.35 of nieuwer (Ubuntu 22.04+, Debian 12+, Fedora 36+, actuele Arch of Manjaro), X11 of XWayland |

Een audio-interface heb je niet nodig. LYNX SHARE speelt af via het geluidsapparaat dat je systeem al gebruikt. FL Studio heb je ook niet nodig. Als het op dezelfde computer is geïnstalleerd, vindt LYNX SHARE de samples ervan en gebruikt het dezelfde pluginbestanden voor `.flp`-projecten.

Plugins van derden zijn optioneel. LYNX SHARE heeft eigen instrumenten en effecten, die in de [catalogus](/instruments-and-effects/) staan.

## Je werk opslaan

`Ctrl+S` slaat op, `Ctrl+Shift+S` slaat op onder een nieuwe naam en `Ctrl+Alt+S` slaat een nieuwe versie op (Song_2, Song_3 enzovoort). Er is een automatische opslag, bereikbaar via `Bestand › Map voor automatisch opslaan openen` en `Bestand › Terugzetten naar laatste back-up...`.

`Bestand › Nieuw vanuit sjabloon` heeft vier beginpunten: Empty, Basic beat, Trap with 808 en House.

## Het demoproject

Het Windows-installatieprogramma en de zip, de macOS-`.dmg` en de Linux-`.tar.gz` bevatten een demoproject, `Demo.lynx`. Open het met `Bestand › Openen...` (`Ctrl+O`). Op Windows staat het in de installatiemap, naast `LYNX.exe`.

Je kunt het ook vanuit een terminal genereren. Zie [Opdrachtregel](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Waar nu heen

| Je wilt | Lees |
|---|---|
| Elk venster leren kennen | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Een project uit een ander programma openen | [Overstappen van FL Studio](/wiki/coming-from-fl-studio/), [Overstappen van Ableton Live](/wiki/coming-from-ableton/) |
| Je VST-, CLAP-, LV2- of AU-plugins gebruiken | [Plugins](/wiki/plugins/) |
| Een toets opzoeken | [Sneltoetsen](/wiki/shortcuts/) |
| Een probleem oplossen | [Probleemoplossing](/wiki/troubleshooting/) en de [FAQ](/faq/) |

## Goed om te weten

- Het programmabestand heet `LYNX.exe` op Windows en `LYNX` op macOS en Linux. De map met instellingen heet ook `LYNX`.
- Op Windows en macOS kan de eerste start een systeemwaarschuwing tonen, omdat de Windows-build nog niet is ondertekend en de macOS-build niet is genotariseerd. [Installeren](/wiki/install/) legt uit waarop je moet klikken.
- De interface is standaard Engels op een systeem waarvan de taal LYNX SHARE niet heeft. Je kunt dat wijzigen in `Opties › Algemene instellingen...`. Een nieuwe taal geldt na een herstart.
- FLEX-packs in FL Studio-projecten zijn versleuteld en worden nooit geopend. Die kanalen krijgen een synthpatch die op de naam van de preset wordt gekozen.

## Gerelateerd

- [Installeren](/wiki/install/)
- [Audio instellen](/wiki/audio-setup/)
- [Rondleiding door het venster](/wiki/tour/)
- [Je eerste nummer](/wiki/first-song/)
- [Download](/download/)
