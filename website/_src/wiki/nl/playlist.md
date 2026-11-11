---
source: e767f4482b
---
![De Playlist met clips op tracks](/assets/img/playlist.webp)

In de Playlist worden patterns een song. Op de tracks staan drie soorten clips: patternclips, audioclips en automatiseringsclips. Druk op `F5` om hem te tonen of te verbergen, en op `Enter` om hem te maximaliseren of te herstellen.

Het transport heeft twee modi. De Pattern-modus speelt het huidige pattern in een lus. De Song-modus speelt de Playlist. Druk op `L` om te wisselen.

## Een pattern in de song plaatsen

1. Druk op `F5` om de Playlist te tonen.
2. Klik in het Picker-paneel op een pattern. De Picker toont de patterns, audio en automatisering van het project, en wat je aanklikt wordt de clipbron.
3. Druk op `P` voor het Potlood.
4. Klik op een track om de clip te plaatsen. Houd de knop ingedrukt en sleep om hem te positioneren.
5. Druk op `B` voor de Kwast en sleep langs een track om clips aan elkaar te schilderen.
6. Druk op `L` voor de Song-modus en dan op `Space` om af te spelen.

Audiobestanden kun je vanuit de [Browser](/wiki/browser/) naar de Playlist slepen.

## Tools

![De werkbalk van de Playlist met het arrangementvak, de tools en het snapvak](/assets/img/wiki/playlist-tools.webp)

De letters werken zolang de pianofunctie van het typtoetsenbord (`Ctrl+T`) uit staat.

| Tool | Toets | Wat het doet |
|---|---|---|
| Potlood | `P` | Klikken plaatst de clip van de Picker |
| Kwast | `B` | Slepen schildert clips aan elkaar |
| Gum | `D` | Klikken of slepen verwijdert |
| Mute | `T` | Klikken of slepen mute of unmute clips |
| Mes | `C` | Sleep een lijn om dwars door clips te knippen, klikken knipt op dat punt |
| Lasso | `E` | Slepen trekt een selectiekader. `Shift` voegt toe |
| Zoom | `Z` | Klikken zoomt in, een bereik slepen zoomt daarheen, rechtsklikken zoomt uit |
| Luisteren | `Y` | Klikken of slepen speelt af vanaf dat punt |
| Slip | `S` | Slepen verschuift de inhoud van de clip binnen de clip |

## Werken met clips

- Sleep een clip om hem te verplaatsen. `Shift`+slepen maakt een kopie.
- Sleep het einde van een clip om hem aan te passen. Het begin slepen trimt hem terwijl de inhoud op zijn plaats blijft.
- Rechtsklikken verwijdert een clip. `Ctrl`+slepen selecteert.
- Dubbelklik op een clip om hem te openen.
- De knop linksboven op een clip opent het clipmenu (uniek maken, hernoemen en kleuren, chop, en voor audio: pitch, aanpassen aan tempo, reverse).
- Houd `Alt` ingedrukt tijdens het slepen om de snap te negeren.
- Audioclips hebben fade-handvatten en een clipgain die je omhoog of omlaag sleept.
- `Shift+M` schakelt Stretch: de grootte van een audioclip wijzigen rekt dan de audio in plaats van hem af te knippen.

## Tracks

In de kop van een track selecteert klikken, voegt `Ctrl`+klikken toe of haalt weg, selecteert `Shift`+klikken een reeks en opent rechtsklikken het trackmenu. `Shift`+wiel verplaatst de track omhoog of omlaag. Het lampje van de track mute hem (klikken) of zet hem solo (`Ctrl`+klikken of rechtsklikken). Sleep de deler tussen twee tracks om er één in grootte te wijzigen, of `Shift`+slepen om ze allemaal aan te passen.

## Liniaal, loop en markers

- Klik op de liniaal om de songpositie in te stellen.
- Met rechts slepen, `Ctrl`+slepen of dubbelklikken-en-slepen op de liniaal maak je een tijdselectie, die ook de loop is. `Shift`+slepen verplaatst haar.
- `Ctrl+Ins` voegt de lengte van de tijdselectie in over alle tracks, `Ctrl+Del` verwijdert haar.
- `Alt+T` voegt een marker toe. Klik op een marker om ernaartoe te springen.
- `Ctrl`+wiel zoomt, `Alt`+wiel wijzigt de trackhoogte.

## Automatisering

Rechtsklik op een knop of fader en kies `Automation clip maken`. De clip verschijnt in de Playlist. Sleep met het Potlood de punten ervan en klik op de curve om er een toe te voegen. Automatiseringsbanen onder tracks zijn optioneel en staan standaard uit. Zie [Automation](/wiki/automation/).

## Arrangementen

Het arrangementvak in de werkbalk wisselt tussen arrangementen. Een project kan er meerdere hebben.

## Toetsen

| Commando | Toets | Wat het doet |
|---|---|---|
| Alles selecteren | `Ctrl+A` | Selecteert elke clip |
| Kopiëren, knippen, plakken | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopieert, knipt of plakt clips |
| Dupliceren | `Ctrl+B` | Dupliceert de selectie |
| Verwijderen | `Del` | Verwijdert de geselecteerde clips |
| Ripple-verwijdering | `Shift+Del` | Verwijdert de geselecteerde clips en sluit de gaten |
| Ripple-bewerking | `Shift+S` | Zet ripple-bewerking aan of uit |
| Mute, unmute | `Alt+M` / `Shift+Alt+M` | Mute of unmute de geselecteerde clips |
| Uniek maken | `Ctrl+Shift+V` | Maakt de geselecteerde clips uniek |
| Samenvoegen | `Ctrl+G` | Voegt patternclips samen |
| Stretch | `Shift+M` | Schakelt Stretch voor het aanpassen van audioclips |
| Tijd invoegen | `Ctrl+Ins` | Voegt de lengte van de tijdselectie in over alle tracks |
| Tijd verwijderen | `Ctrl+Del` | Verwijdert de tijdselectie over alle tracks |
| Marker toevoegen | `Alt+T` | Voegt een marker toe |
| Zoom | `PageUp` / `PageDown` | Zoomt in of uit |
| Passend zoomen | `Shift+4` | Zoomt zodat alles zichtbaar is |
| Maximaliseren | `Enter` | Maximaliseert of herstelt de Playlist |

## Goed om te weten

- De Playlist hoor je alleen in de Song-modus. In de Pattern-modus speelt `Space` het huidige pattern, wat er ook op de tracks staat.
- `Beeld › Lay-outstijl › Clipstart-werkwijze` voegt naast de Playlist een raster van clips en scènes toe. Het is optioneel. Zie [Lay-outs](/wiki/layouts/).
- Bij meerdere overlappende automatiseringsclips op één regelaar kan de keuze welke speelt afwijken van die van FL Studio (releasenotities van 0.1.25).
- Een tempo dat door een gekoppelde MIDI-controller wordt verschoven, wijzigt de grootte van audioclips niet.
- Op Linux heeft video FFmpeg nodig.

## Gerelateerd

- [Channel Rack](/wiki/channel-rack/)
- [Automation](/wiki/automation/)
- [Opnemen](/wiki/recording/)
- [Exporteren](/wiki/exporting/)
- [Lay-outs](/wiki/layouts/)
