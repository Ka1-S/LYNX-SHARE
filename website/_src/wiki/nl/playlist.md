---
source: 1b54c6493f
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
- Het menu van een audioclip volgt de indeling van FL Studio in twee kolommen. De linkerkolom heeft de sectie `Audioclip` (`Voorbeluisteren`, `Gemute`, `Hernoemen en kleuren...`, `Kleur wijzigen...`, `Willekeurige kleur`, `Pictogram wijzigen...`, `Bronkanaal selecteren`, `Kanaalinstellingen`, `Uniek maken`, `Alle vergelijkbare clips selecteren`, `Delete`, `Eigenschappen...`) en de sectie `Regio` (`Regio selecteren`, `Chop`). De rechterkolom heeft `Sample` (`Uniek maken als sample`, `Sample bewerken`, `Stems uit sample halen`, `Tempo detecteren`, `Aanpassen aan tempo`), `Automatisering` (`Automatiseren › Panning` of `Volume`, `Crossfade met`) en `Fades` (`Fades herstellen`). De eigen items van LYNX SHARE, zoals `Hier splitsen`, `Reverse`, `Normaliseren` en `Omzetten naar MIDI`, komen als laatste, onder een streep.
- `Automatiseren › Panning` of `Volume` maakt een automatiseringsclip zo lang als de audioclip, op zijn track, voor de pan of het volume van de Levels adjustment van het kanaal.
- Houd `Alt` ingedrukt tijdens het slepen om de snap te negeren.
- Audioclips hebben fade-handvatten en een clipgain die je omhoog of omlaag sleept. Klik met rechts op een fade-handvat voor het menu, met `Crossfade maken`. Klik met rechts op het gain-handvat voor `Gain resetten` en `Normaliseren`.
- `Shift+M` schakelt Stretch: de grootte van een audioclip wijzigen rekt dan de audio in plaats van hem af te knippen.
- `Shift+Left` en `Shift+Right` verplaatsen de geselecteerde clips één snapstap, `Shift+Up` en `Shift+Down` één track. `Alt+Left` en `Alt+Right` verplaatsen ze één schermpixel bij de huidige zoom, zonder snap. Zonder geselecteerde clip verschuiven alle clips.
- `Uniek maken als sample` in het menu van een audioclip geeft de clip een nieuw bestand met wat hij speelde, met pitch, stretch en reverse al in de audio verwerkt.
- Een audioclip met warpmarkers toont die als kleine vlaggetjes op zijn bovenrand. Sleep een vlaggetje zijwaarts om de marker te verplaatsen.

## De menu's Edit en Select

Het menu van de Playlist heeft een submenu `Bewerken` en een submenu `Selecteren`. `Bewerken` bevat de clipcommando's: knippen, kopiëren en plakken, `Naar links schuiven`, `Naar rechts schuiven`, `Omhoog schuiven`, `Omlaag schuiven`, `Uniek maken`, `Uniek maken als sample`, `Ruimte invoegen`, `Ruimte verwijderen`, de samenvoegcommando's en de fade-opties. `Selecteren` heeft `Selecteren op geselecteerde bron` (`Shift+C`), `Selectie omkeren` (`Shift+I`), `Deselecteren` (`Ctrl+D`), de gemute en de overlappende clips, en `Magische lasso`. Met `Magische lasso` aan beweeg je de muis in een lus om clips heen, zonder een knop in te drukken, om ze te selecteren.

## Tracks

In de kop van een track selecteert klikken, voegt `Ctrl`+klikken toe of haalt weg, selecteert `Shift`+klikken een reeks en opent rechtsklikken het trackmenu. `Shift`+wiel verplaatst de track omhoog of omlaag. Het lampje van de track mute hem (klikken) of zet hem solo (`Ctrl`+klikken of rechtsklikken). Sleep de deler tussen twee tracks om er één in grootte te wijzigen, of `Shift`+slepen om ze allemaal aan te passen.

`Beeld` in het menu van de Playlist kiest wat de trackkoppen tonen: `Bediening op audiotracks tonen`, `Niveaus op audiotracks tonen` en `Niveaus op instrumenttracks tonen`.

`Trackmodus` in het trackmenu maakt van een track een `Niet toegewezen`, een `Audiotrack` of een `Instrumenttrack`, zoals in FL Studio. `Audiotrack` toont eerst `Nieuwe mixertrack(s)` en daarna elke mixerinsert op nummer en naam. De keuze geldt voor alle geselecteerde tracks. `Instrumenttrack` heeft `Bestaand kanaal gebruiken`, `Meer plug-ins...` en de lijst instrumenten van het Channel rack.

De `+` linksboven in de trackkoppen opent dezelfde keuzes voor een nieuwe track: `Audiospoor toevoegen`, `Meer plug-ins...` en de instrumenten. Een rechtsklik op `+` voegt meteen een audiotrack toe.

## Patternblokken

Met `Patternblokken (huidig project)` aan in het menu van de Playlist heeft een onderdeel onder de tracks één rij per pattern. Daar plaatst het Potlood één blok en sleept het langs zijn rij, schildert de Kwast blokken en wist de Gum ze. Rechtsklikken wist met elk gereedschap.

## Liniaal, loop en markers

- Klik op de liniaal om de songpositie in te stellen.
- Met rechts slepen, `Ctrl`+slepen of dubbelklikken-en-slepen op de liniaal maak je een tijdselectie, die ook de loop is. `Shift`+slepen verplaatst haar.
- `Ctrl+Ins` voegt de lengte van de tijdselectie in over alle tracks, `Ctrl+Del` verwijdert haar.
- `Alt+T` voegt een marker toe. Klik op een marker om ernaartoe te springen.
- `Ctrl`+wiel zoomt, `Alt`+wiel wijzigt de trackhoogte.

## Automatisering

Rechtsklik op een knop of fader en kies `Automation clip maken`. De clip verschijnt in de Playlist. Sleep met het Potlood de punten ervan en klik op de curve om er een toe te voegen. Automatiseringsbanen onder tracks zijn optioneel en staan standaard uit. Zie [Automation](/wiki/automation/).

Klik met rechts op een punt voor zijn menu. De lijst `Modus` heeft de 13 curvemodi van FL Studio voor het segment dat bij dat punt eindigt: `Smooth`, `Hold`, drie enkele curves, drie dubbele curves, `Halve sinus`, `Trap`, `Vloeiende trap`, `Puls` en `Golf`. Een nieuw punt neemt de laatst gebruikte modus. Modi worden uit .flp-bestanden gelezen en ernaar geschreven.

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
| Clips verschuiven | `Shift+Left` / `Shift+Right` | Verplaatst de geselecteerde clips één snapstap |
| Naar een andere track verschuiven | `Shift+Up` / `Shift+Down` | Verplaatst de geselecteerde clips één track |
| Clips nudgen | `Alt+Left` / `Alt+Right` | Verplaatst de geselecteerde clips één schermpixel, zonder snap |
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
- Zoals in FL Studio speelt een pattern vanuit één clip tegelijk. Wanneer een latere clip van hetzelfde pattern begint, op welke track dan ook, start de eerdere clip vanaf daar geen noten meer. Clips van verschillende patterns spelen samen. Dit geldt in nieuwe projecten en FL Studio-projecten. Een .lynx-project dat vóór 0.1.27 is opgeslagen, blijft elke clip volledig afspelen.
- Een tempo dat door een gekoppelde MIDI-controller wordt verschoven, wijzigt de grootte van audioclips niet.
- Op Linux heeft video FFmpeg nodig.

## Gerelateerd

- [Channel Rack](/wiki/channel-rack/)
- [Automation](/wiki/automation/)
- [Opnemen](/wiki/recording/)
- [Exporteren](/wiki/exporting/)
- [Lay-outs](/wiki/layouts/)
