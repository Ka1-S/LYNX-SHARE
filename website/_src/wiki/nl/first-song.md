---
source: de4ead4a7a
---
Deze pagina loopt één keer het hele traject door: een drumpattern, een melodie, een arrangement, een mix en een geëxporteerd bestand. Elk onderdeel verwijst naar de pagina die het bijbehorende venster volledig behandelt. Hoor je op enig moment niets, zie dan [Audio instellen](/wiki/audio-setup/).

## Een drumpattern maken

1. Kies `Bestand › Nieuw vanuit sjabloon › Leeg`. Het project begint met vijf kanalen, Kick, Clap, Hat, Snare en Synth, en zonder steps.
2. Druk op `F6` als het [Channel Rack](/wiki/channel-rack/) niet zichtbaar is.
3. Klik op een paar steps in de rij van Kick. Slepen tekent er meerdere, en rechtsklikken wist.
4. Druk op `Space`. Het transport staat in de Pattern-modus, dus het pattern speelt in een lus. Druk nog een keer op `Space` om te stoppen.
5. Geef de rijen Clap, Hat en Snare hun eigen steps.
6. Druk op `F2` om het pattern een naam en kleur te geven.

Om een geluid toe te voegen dat er niet is, klik je op het vak `+ kanaal toevoegen` onder de laatste rij en kies je een instrument uit het menu. Voor een eigen sample open je de [Browser](/wiki/browser/) met `Alt+F8`, klik je op een audiobestand om het te horen en dubbelklik je erop om het als nieuw kanaal toe te voegen.

## Een melodie schrijven

1. Druk op `F4`. LYNX SHARE gaat naar het volgende lege pattern en vraagt om de naam.
2. Rechtsklik op de naam van het kanaal Synth en kies `Piano Roll`.
3. Klik met het Potlood (`P`) in het raster om een noot toe te voegen.
4. Sleep een noot om hem te verplaatsen en sleep aan het einde om de lengte te wijzigen. Rechtsklikken verwijdert een noot.
5. Druk op `Space` om het pattern te horen terwijl je bewerkt.

`Ctrl+T` maakt van het typtoetsenbord een piano, zodat je eerst noten kunt proberen met `Z` tot `M` en `Q` tot `P`. Zet het weer uit voordat je letter-sneltoetsen zoals `P` gebruikt.

## De patterns arrangeren

1. Druk op `F5` om de [Playlist](/wiki/playlist/) te tonen.
2. Klik in het Picker-paneel links van de Playlist op het drumpattern. Het is nu de clipbron.
3. Klik met het Potlood (`P`) op een track om het pattern te plaatsen. Klik verder naar rechts nog een keer om het opnieuw te plaatsen, of gebruik de Kwast (`B`) en sleep om clips aan elkaar te schilderen.
4. Klik in de Picker op het melodiepattern en plaats het op een andere track.
5. Druk op `L` om naar de Song-modus te schakelen, en dan op `Space`. De Playlist speelt vanaf de songpositie. Klik op de liniaal om hem in te stellen.

## Mixen en een effect toevoegen

1. Druk op `F9` om de [Mixer](/wiki/mixer/) te tonen. De vijf beginkanalen spelen al via insert 1 tot 5, en een kanaal dat je via het menu `+ kanaal toevoegen` toevoegt, neemt een vrije insert. Het nummer in het routevak van het kanaal in het Channel Rack geeft aan welke.
2. Om kanalen naar eigen vrije inserts te verplaatsen, selecteer je ze in het Channel Rack en druk je op `Ctrl+L`. Elke insert neemt de naam en kleur van het kanaal over.
3. Sleep aan de fader van een insert om het niveau in te stellen.
4. Klik op een insert om hem te selecteren. Het effectenpaneel rechts toont de tien slots ervan.
5. Klik op een leeg slot en kies een effect. Klik op de naam van het slot om het venster van het effect te tonen of te verbergen. Het lampje van het slot zet het effect op bypass.

## Het nummer exporteren

![Het exportvenster: bereik en staart links, formaat, bitdiepte en samplefrequentie rechts, mixopties eronder](/assets/img/wiki/export.webp)

1. Druk op `Ctrl+S` en sla eerst het project op.
2. Kies `Bestand › Exporteren › Wave-bestand...`. Voor andere formaten heeft hetzelfde menu `MP3-bestand...`, `OGG-bestand...` en `FLAC-bestand...`.
3. Zet `Bereik` op `Song (Playlist)`. De andere keuzes zijn `Huidig pattern` en `Playlist-selectie`.
4. Laat de andere opties zoals ze zijn.
5. Klik op `Exporteren...`, kies de bestandsnaam en wacht tot het renderen klaar is.

## Wat elke toets deed

| Commando | Toets | Wat het doet |
|---|---|---|
| Channel Rack | `F6` | Toont of verbergt het Channel Rack |
| Volgend leeg pattern | `F4` | Gaat naar het volgende lege pattern en geeft het een naam |
| Piano Roll | `F7` | Toont of verbergt de Piano Roll |
| Playlist | `F5` | Toont of verbergt de Playlist |
| Pattern- of Song-modus | `L` | Wisselt wat `Space` afspeelt |
| Mixer | `F9` | Toont of verbergt de Mixer |
| Naar vrije inserts sturen | `Ctrl+L` | Geeft de geselecteerde kanalen hun eigen Mixer-inserts |
| Opslaan | `Ctrl+S` | Slaat het project op als `.lynx` |
| Exporteren | `Ctrl+R` | Opent het exportvenster met het laatst gebruikte formaat |

## Goed om te weten

- In de Pattern-modus hoor je alleen het huidige pattern. Is de song na het arrangeren stil, controleer dan of het transport in de Song-modus staat.
- Het exportvenster begint op `Song (Playlist)` als het transport in de Song-modus staat en de Playlist clips heeft. Anders begint het op `Huidig pattern`.
- De klik van het metronoom is nooit onderdeel van een export.
- Onderaan het exportvenster staan de lengte van de song in maten, de totale tijd en de schijfruimte die het bestand nodig heeft.
- Een nieuw project heeft standaard een Limiter op de Master-insert. Die hoort bij wat je hoort en wat je exporteert.
- Ongedaan maken is `Ctrl+Z`. Nog een keer indrukken maakt die stap opnieuw. `Ctrl+Alt+Z` gaat verder terug.

## Gerelateerd

- [Rondleiding door het venster](/wiki/tour/)
- [Exporteren](/wiki/exporting/)
- [Automation](/wiki/automation/)
- [Opnemen](/wiki/recording/)
- [Instrumenten en effecten](/instruments-and-effects/)
