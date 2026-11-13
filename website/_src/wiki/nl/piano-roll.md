---
source: a73763934f
---
![De Piano Roll](/assets/img/pianoroll.webp)

De Piano Roll toont de noten van één kanaal in het huidige pattern, met links een klavier en onderaan een baan voor waarden per noot. Druk op `F7` om hem te tonen of te verbergen, of kies `Piano Roll` in het menu van een kanaal in het [Channel Rack](/wiki/channel-rack/).

Het kanaalvak in de werkbalk kiest welk kanaal je bewerkt. Kanalen die in dit pattern noten hebben, zijn gemarkeerd. Het wiel boven het vak loopt langs de kanalen met noten, `Ctrl`+wiel langs alle kanalen.

## Wat noten schrijven

1. Klik in het Channel Rack met rechts op de naam van een kanaal en kies `Piano Roll`.
2. Druk op `P` voor het Potlood.
3. Klik in het raster om een noot toe te voegen. Houd de knop ingedrukt en sleep om hem te plaatsen.
4. Sleep het einde van de noot om hem langer of korter te maken.
5. Klik met rechts op een noot om hem te verwijderen.
6. Druk op `Space` om het pattern te horen.

## Gereedschappen

![De werkbalk van de Piano Roll met het kanaalvak, de gereedschappen en het snapvak](/assets/img/wiki/pianoroll-tools.webp)

Elk gereedschap heeft een lettertoets. De letters werken zolang de typtoetsenbordpiano (`Ctrl+T`) uit staat.

| Gereedschap | Toets | Wat het doet |
|---|---|---|
| Potlood | `P` | Klikken voegt een noot toe, slepen plaatst hem |
| Kwast | `B` | Slepen schildert een reeks noten. Met `Shift` één voor één |
| Drumkwast | `N` | Eén hit per rastercel op de toets onder de muis |
| Gum | `D` | Klikken of slepen verwijdert |
| Mute | `T` | Klikken of slepen mute of unmute noten |
| Mes | `C` | Sleep een lijn om dwars door noten te knippen, klikken knipt op dat punt |
| Lasso | `E` | Slepen trekt een selectiekader. `Shift` voegt toe |
| Zoom | `Z` | Klikken zoomt in, een bereik slepen zoomt daarnaartoe, rechtsklikken zoomt uit |
| Luisteren | `Y` | Klikken of slepen speelt de noten onder de cursor |

## Met noten werken

- Sleep een noot om hem te verplaatsen. `Shift`+slepen maakt een kopie.
- Sleep het einde van een noot om hem aan te passen. Het begin kan ook gesleept worden, waarbij het einde op zijn plek blijft.
- Rechtsklikken verwijdert een noot, met elk gereedschap.
- Dubbelklik op een noot om zijn eigenschappen te openen.
- `Alt`+wiel boven een noot verandert zijn velocity.
- `Ctrl`+slepen selecteert. `Ctrl`+klik voegt een noot toe aan de selectie of haalt hem eruit.
- Houd `Alt` ingedrukt tijdens het slepen om Snap te negeren. `Backspace` zet snap uit en aan.
- Zijn er meerdere noten geselecteerd, dan rekt of drukt het stretch-handvat ze samen in de tijd.

## Klavier en liniaal

Klik op een toets van het klavier om hem te horen. Verder naar rechts op de toets is harder. `Ctrl`+klik op een toets selecteert zijn noten. Slepen met de middelste knop op het klavier maakt de toetsen hoger of lager. Dat doet ook het verticale zoomwiel, het kleine vierkantje aan het eind van de tijdscrollbalk: sleep het omhoog of omlaag, of gebruik het muiswiel erboven.

`Beeld › Klavierstijl` in het menu van de Piano Roll kiest tussen `Vlakke toetsen` en `Pianotoetsen (lijnen tussen de witte toetsen)`, dat een traditioneel klavier tekent.

Klik op de liniaal om de positie in te stellen. `Ctrl`+slepen, met rechts slepen of dubbelklikken-slepen op de liniaal selecteert de noten in een tijdbereik. `Ctrl`+wiel zoomt en `Shift`+wiel scrolt.

## Werkbalk

| Instelling | Wat het doet |
|---|---|
| Snap | "Hoofd" volgt de snap in de hoofdwerkbalk, of kies er een voor dit venster. |
| Akkoord | Klikken maakt het gekozen akkoord, met de aangeklikte noot als grondtoon. `Shift+N` gaat terug naar losse noten. Dezelfde lijst is `Akkoordstempel` in het menu van de Piano Roll. |
| Toonladder | Licht de noten van een toonladder op. Een aparte knop snapt nieuwe en verplaatste noten, en binnenkomende MIDI, naar de gekozen toonladder. Verplaatste noten gaan dan in toonladderstappen, zodat een akkoord in de toonladder blijft, en `Up` en `Down` verplaatsen één toonladderstap. |
| Slide (`S`) | Nieuwe noten zijn slide-noten. De noot die klinkt glijdt naar ze toe (808 glide). |
| Ghost notes (`Alt+V`) | Toont de noten van de andere kanalen achter de jouwe. |
| Nootkleur | Nieuwe noten krijgen deze kleur. Er zijn er 16. |
| Volgen | De weergave scrolt mee met de afspeelkop terwijl het pattern speelt. |
| Banenvak | Kiest wat de onderste baan bewerkt: Velocity, Pan, Fijne pitch, Release, Mod X, Mod Y, Kans of Velocity-bereik. |

## Kans en velocity-bereik

Elke noot heeft een Kans en een Velocity-bereik. Kans is hoe waarschijnlijk het is dat de noot speelt elke keer dat hij aan de beurt komt. Met het Velocity-bereik varieert de velocity tussen de velocity van de noot en die velocity plus het bereik. Kies `Kans` of `Velocitybereik (LYNX)` in het banenvak om ze te bewerken. `Extra › Kans (LYNX)` in het menu van de Piano Roll groepeert de geselecteerde noten zodat ze allemaal of slechts één ervan speelt, en heeft `Kans en velocitybereik herstellen`.

## Notentools

Het menu `Extra` van de Piano roll opent de notentools van FL Studio als dialogen. Ze werken op de geselecteerde noten, of op alle noten wanneer er geen is geselecteerd. Enkele ervan:

| Tool | Toets | Wat het doet |
|---|---|---|
| Limit | `Alt+K` | Verplaatst noten met octaven naar een toetsenbereik en klemt ze dan vast. Onder `Toonsoort- / toonladderbeperking` houdt hij noten op een grondtoon en een van de akkoorden of toonladders van FL Studio, met `Snap` ingesteld op `Erboven`, `Eronder` of `Afwisselend`. `Omslaan naar boven / onder` en `Transponeren` bepalen waar noten buiten het bereik heen gaan |
| Flip | `Alt+Y` | `Horizontaal spiegelen` spiegelt de noten in de tijd. Met `Starttijden behouden`, eerst aan, worden de starttijden gespiegeld en behouden de noten hun lengte. `Verticaal spiegelen` spiegelt de toetsen rond het midden van hun bereik |
| Randomise | `Alt+R` | Willekeurige wijzigingen in velocity, pan, fijne toonhoogte, timing en lengte, en in Mod X, Mod Y en release |
| Scale levels | `Alt+X` | Schaalt een nootniveau van het begin tot het einde van de selectie, met `Offset`, `Midden` (de waarde waaromheen wordt geschaald) en `Spanning` |
| LFO | `Alt+O` | Schrijft een LFO-vorm in een nootniveau. Klik met rechts op een schuifregelaar `Periode` voor een lijst van 1 stap tot 512 stappen |

## MIDI-tools en scripts

`Extra › MIDI-tools...` in het menu van de Piano Roll opent een paneel met Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp en Velocity Shaper.

De kolom `Scripts` van het menu `Extra` toont de Piano Roll-scripts: die van LYNX SHARE zelf en `.pyscript`-bestanden, ook die in de scriptmappen van FL Studio als FL Studio is geïnstalleerd. `Scriptmap openen...` opent de map voor je eigen scripts, en `Laatste script opnieuw uitvoeren` herhaalt het laatste script.

## Toetsen

| Opdracht | Toets | Wat het doet |
|---|---|---|
| Alles selecteren | `Ctrl+A` | Selecteert elke noot |
| Deselecteren | `Ctrl+D` of `Esc` | Wist de selectie |
| Kopiëren, knippen, plakken | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopieert, knipt of plakt noten |
| Dupliceren | `Ctrl+B` | Dupliceert de selectie |
| Verwijderen | `Del` | Verwijdert de selectie |
| Transponeren | `Up` / `Down` | Verplaatst de selectie een halve toon |
| Octaaf transponeren | `Ctrl+Up` / `Ctrl+Down` | Verplaatst de selectie een octaaf |
| Nudgen | `Left` / `Right` | Verplaatst de selectie een rasterstap |
| Snel kwantiseren | `Ctrl+Q` | Kwantiseert begin en lengte |
| Begin kwantiseren | `Shift+Q` | Kwantiseert alleen het begin |
| Chop | `Ctrl+U` | Hakt de noten op het raster |
| Glue | `Ctrl+G` | Voegt aansluitende noten samen |
| Legato | `Ctrl+L` | Laat elke noot duren tot de volgende |
| Muten, unmuten | `Alt+M` / `Shift+Alt+M` | Mute of unmute de selectie |
| MIDI importeren | `Ctrl+M` | Importeert een MIDI-bestand in dit kanaal |
| MIDI exporteren | `Ctrl+Shift+M` | Exporteert het pattern als MIDI-bestand |
| Zoom | `PageUp` / `PageDown` | Zoomt in of uit |

De nootgereedschappen (Arpeggiëren, Strum, Spiegelen, Willekeurig maken, Quantize, Chop en andere) openen dialogen. Hun toetsen staan op de pagina [Sneltoetsen](/wiki/shortcuts/).

## Goed om te weten

- De Piano Roll toont één kanaal in één pattern. Verander je het huidige pattern, dan toont hij de noten van dat pattern.
- De gereedschapsletters doen niets zolang de typtoetsenbordpiano (`Ctrl+T`) aan staat.
- Rechtsklikken verwijdert met elk gereedschap, dus voor één noot hoef je niet naar de Gum te schakelen.
- `S` is hier de Slide-schakelaar. In de Playlist is dezelfde letter het gereedschap Slip.
- Noten die geen gewone steps zijn (andere toetsen, langere noten, slides) zorgen ervoor dat de rij van het kanaal in het Channel Rack een miniatuur toont in plaats van stepknoppen.

## Gerelateerd

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Sneltoetsen](/wiki/shortcuts/)
- [Bestandsformaten](/wiki/file-formats/)
