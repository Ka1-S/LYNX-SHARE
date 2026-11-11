---
source: 8df354b49e
---
![De Browser links met zijn drie tabs, het zoekvak, de mappenboom en de voorbeluisterstrook](/assets/img/wiki/browser-tree.webp)

De Browser toont je samplemappen, de inhoud van het huidige project en de plugins die LYNX SHARE kent. Druk op `Alt+F8` om hem te tonen of te verbergen. `F8` alleen opent de Plugin-kiezer, een aparte schermvullende weergave van elk instrument en effect.

## Een sample in het project laden

1. Druk op `Alt+F8` om de Browser te tonen.
2. Klik in de tab `ALLES` op een map om hem te openen.
3. Klik op een audiobestand om het te horen. Met `Up` en `Down` loop je door de bestanden en beluister je ze een voor een.
4. Dubbelklik op het bestand voor een nieuw kanaal met de sample in het [Channel Rack](/wiki/channel-rack/).
5. Of sleep het bestand naar de [Playlist](/wiki/playlist/) voor een audioclip.

## Tabs

| Tab | Toont |
|---|---|
| ALL | Het huidige project, de Plugindatabase en alle samplemappen |
| PROJECT | Alleen wat het huidige project gebruikt: patterns, effecten, generators, automatisering, audiobestanden |
| MET STER | De bestanden en mappen waaraan je een ster gaf (rechtsklik, dan `Met ster`) |

## Bladeren en voorbeluisteren

- Klik op een map om hem te openen of te sluiten. Rechtsklik erop voor een menu (alles inklappen, tonen in de bestandsbeheerder).
- Klik op een audiobestand om het te horen. De voorbeluisterstrook onder de lijst toont de golfvorm. Klik op de strook om het bestand opnieuw te horen, of sleep vanaf de strook alsof het het bestand zelf is.
- Het paneel boven de voorbeluisterstrook toont datum, grootte en pad van het geselecteerde bestand.

## Een sample gebruiken

| Actie | Wat het doet |
|---|---|
| Dubbelklikken | Nieuw kanaal met de sample |
| Naar het Channel Rack slepen | Nieuw kanaal, of laat het op een rij vallen om het daar te laden |
| Naar de Playlist slepen | Audioclip |
| `Enter` | Laadt het bestand in het geselecteerde kanaal |
| `Shift+Up` / `Shift+Down` | Gaat naar het vorige of volgende bestand en laadt het in het geselecteerde kanaal |

Met de laatste kun je verschillende samples ter plekke beluisteren terwijl het pattern speelt.

## Andere bestandstypen

- **Project**: dubbelklikken opent het.
- **MIDI-bestand**: dubbelklikken opent het als project.
- **SoundFont of SFZ**: klikken speelt het voor, dubbelklikken maakt een LYNX Soundfont-kanaal (of laadt het in het geselecteerde kanaal).
- **Mixerpreset**: dubbelklikken laadt het op de geselecteerde Mixer-inserts, of sleep het naar een insert.
- **Effectpreset**: sleep het naar een Mixer-insert.

## Plugindatabase

Het onderdeel Plugindatabase toont de ingebouwde instrumenten en effecten van LYNX SHARE en de geïnstalleerde plugins. Met `Shift+F8` spring je erheen.

- Dubbelklik op een instrument om het als nieuw kanaal toe te voegen, of sleep het naar het Channel Rack.
- Dubbelklik op een effect om het toe te voegen aan de geselecteerde [Mixer](/wiki/mixer/)-insert, of sleep het naar een insert of een effectslot.
- `F2` hernoemt de geselecteerde plugin.
- Rechtsklik op een plugin en kies `Naar map verplaatsen` om plugins in mappen te sorteren. Het slotmenu van de Mixer en het toevoegmenu van het Channel Rack tonen dan elke hoofdmap.

## De tab PROJECT

Patterns in deze lijst kun je aanklikken om ze het huidige pattern te maken, dubbelklikken om ze in de [Piano Roll](/wiki/piano-roll/) te openen, of naar de Playlist slepen. Het onderdeel van het huidige project toont ook de bewerkgeschiedenis: klik op een stap om ernaar terug te gaan.

## Zoeken

Druk op `Ctrl+F` om de cursor in het zoekvak te zetten. Het zoekt in de geselecteerde map (of in alle mappen) en daaronder. In de tab PROJECT zoekt het in de onderdelen van het project.

Alle woorden moeten overeenkomen. Het vak begrijpt ook `OR`, een minteken om een woord uit te sluiten, en jokertekens met `*`:

```
kick -loop
snare OR clap
hat*.wav
```

De mappen worden op de achtergrond geïndexeerd. Met `Zoekindex vernieuwen` in het menu van de Browser bouw je de index opnieuw op.

## Je eigen mappen

Gebruik `Map toevoegen` in de maplijst van de Browser om een map met samples, projecten of SoundFonts toe te voegen. Onder `Opties › Bestandsinstellingen (zoekmappen)` staan de zoekmappen.

## Toetsen

De pijltoetsen, `Enter` en `F2` werken alleen als de Browser de focus heeft.

| Commando | Toets | Wat het doet |
|---|---|---|
| Browser tonen | `Alt+F8` | Toont of verbergt de Browser |
| Plugin-kiezer | `F8` | Opent de schermvullende Plugin-kiezer |
| Plugindatabase | `Shift+F8` | Springt naar het onderdeel Plugindatabase |
| Zoeken | `Ctrl+F` | Zet de cursor in het zoekvak |
| Doorlopen en voorbeluisteren | `Up` / `Down` | Selecteert het vorige of volgende bestand en speelt het af |
| Map sluiten | `Left` | Sluit een map of gaat naar de bovenliggende map |
| Map openen | `Right` | Opent een map |
| Laden | `Enter` | Laadt het bestand in het geselecteerde kanaal |
| Doorlopen en laden | `Shift+Up` / `Shift+Down` | Gaat naar het vorige of volgende bestand en laadt het in het geselecteerde kanaal |
| Plugin hernoemen | `F2` | Hernoemt de geselecteerde plugin in de Plugindatabase |

## Goed om te weten

- Een map uit de lijst halen verwijdert hem niet van de schijf.
- `F8` en `Alt+F8` zijn verschillende dingen. `F8` is de Plugin-kiezer, `Alt+F8` is de Browser.
- Zoekresultaten komen uit een index die op de achtergrond wordt opgebouwd. Met `Zoekindex vernieuwen` in het menu van de Browser bouw je hem opnieuw op.
- `Enter` laadt het bestand in het geselecteerde kanaal. Dubbelklikken maakt een nieuw kanaal.

## Gerelateerd

- [Channel Rack](/wiki/channel-rack/)
- [Plugins van derden](/wiki/plugins/)
- [Bestandsformaten](/wiki/file-formats/)
- [Bewerkgeschiedenis](/wiki/undo-history/)
- [Instrumenten en effecten](/instruments-and-effects/)
