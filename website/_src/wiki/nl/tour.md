---
source: edf15b371e
---
![Het hoofdvenster met het demoproject: werkbalk bovenaan, Browser links, Playlist erachter en het Channel Rack dat erboven zweeft](/assets/img/wiki/desktop.webp)

LYNX SHARE heeft één hoofdvenster. Bovenaan loopt een werkbalk. Eronder ligt een werkruimte met vijf vensters: de Browser, de Playlist, het Channel Rack, de Piano Roll en de Mixer. Bij een nieuwe start staat de Browser links, vult de Playlist de rest en zweeft het Channel Rack erboven. De Piano Roll en de Mixer zijn verborgen totdat je ze oproept.

## Je weg vinden

1. Wijs met de muis naar een willekeurige regelaar. De hintbalk onder de menu's zegt wat hij doet en welke muisknoppen en toetsen hij accepteert.
2. Druk op `F6` om het Channel Rack te verbergen, en nog een keer `F6` om het te tonen.
3. Druk op `F9` om de Mixer te tonen, dan op `F7` voor de Piano Roll.
4. Sleep een venster aan zijn titelstrook om het te verplaatsen. Dubbelklik op de titel om het te maximaliseren, en nog eens om het te herstellen.
5. Kies `Beeld › Indeling resetten` om alles terug te zetten.

## De werkbalk

![De werkbalk: menu's en hintbalk, transport, masterknoppen, opname-opties, snap en pattern, vensterknoppen, bestandsknoppen, akkoordweergave, meters en uitgangsmonitor](/assets/img/wiki/toolbar-1920.webp)

Van links naar rechts, met de standaardindeling:

| Groep | Wat erin zit |
|---|---|
| Menu en hintbalk | De menu's `Bestand`, `Bewerken`, `Toevoegen`, `Patterns`, `Beeld`, `Opties`, `Extra` en `Help`, met de hintbalk eronder. Altijd eerst |
| Transport | De modusknoppen `Pattern` en `Song`, Afspelen, Stop en Opnemen, het tempo in BPM en de positie als maat, tel en step |
| Mastervolume en -pitch | De volumeknop `MASTER` en de knop `PITCH`, in cents |
| Opname-opties | Metronoom, aftellen, loop-opname, stapinvoer en de andere opnameschakelaars |
| Snap en pattern | De hoofdsnap (standaard `Lijn (volgt zoom)`), het huidige pattern, een knop voor een nieuw pattern en het patternmenu |
| Venstersneltoetsen | Eén knop per venster, en de Plugin-kiezer |
| Bestand en ongedaan maken | Nieuw, Openen, Opslaan, Exporteren, Ongedaan maken en Opnieuw |
| Akkoorddetectie | Noemt het akkoord dat op een MIDI-apparaat of het typtoetsenbord wordt gespeeld |
| CPU, geheugen en polyfonie | Audio-CPU-belasting, gebruikt geheugen en het aantal voices |
| Uitgangsmonitor | Een oscilloscoop van de uitgang. Een klik loopt door de andere modi |

Er bestaat ook een groep met de naam `Multilink naar controllers`, die standaard verborgen is.

`Opties › Werkbalk` wijzigt de werkbalk: `Werkbalk bewerken (sleep panelen om ze te verplaatsen, toe te voegen of te verwijderen)`, `Scheidingslijnen`, `Werkbalkpanelen verbergen`, `Presets`, een schakelaar voor elke groep, en `Pictogramgrootte` (klein, middel of groot).

## Transport

| Regelaar | Toets | Wat het doet |
|---|---|---|
| Pattern-modus | `L` | Speelt het huidige pattern in een lus. Rechtsklik op de knop om het Channel Rack te tonen |
| Song-modus | `L` | Speelt het arrangement van de Playlist. Rechtsklik op de knop om de Playlist te tonen |
| Afspelen | `Space` | Speelt af. Nog een keer ingedrukt stopt het en gaat het terug naar waar het begon. `Ctrl+Space` pauzeert |
| Stop | | Stopt en keert terug naar het begin. Dubbelklikken stuurt ook all notes off |
| Opnemen | `R` | Opname aan of uit. Zie [Opnemen](/wiki/recording/) |

## De hintbalk

In rust staat in de hintbalk `Point at anything for help`, met `F1` voor het sneltoetsenoverzicht, `F5` tot `F9` voor de vensters en `Space` voor afspelen en stoppen. Boven een regelaar noemt hij de regelaar en toont wat slepen, `Ctrl`, `Alt` en rechtsklikken daar doen. Lange taken zoals de pluginscan op de achtergrond melden hun voortgang erin.

## De vijf vensters

| Venster | Toets | Waarvoor |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Pattern-, audio- en automatiseringsclips arrangeren tot een song |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Eén rij per instrument, met de steps van het huidige pattern |
| [Piano Roll](/wiki/piano-roll/) | `F7` | De noten van één kanaal in het huidige pattern |
| [Mixer](/wiki/mixer/) | `F9` | Inserts met faders, effecten en routing |
| [Browser](/wiki/browser/) | `Alt+F8` | Samples, presets, de inhoud van het project en de plugindatabase |

Elke toets toont het venster en geeft het de focus, of verbergt het als het de focus al heeft. `F8` alleen opent de Plugin-kiezer, die niet een van de vijf is. De vensterknoppen van de werkbalk doen hetzelfde als de toetsen. Rechtsklikken op een knop toont dat venster gemaximaliseerd. `Enter` maximaliseert of herstelt de Playlist.

## Hoe vensters bewegen en vastzetten

Elk venster heeft een titelstrook. Links daarvan zit een menuknop met de eigen opties van het venster, dan `Losgemaakt (eigen venster)`, `Gemaximaliseerd` en `Sluiten`. Rechts zitten de knoppen voor maximaliseren en sluiten.

- Sleep de titelstrook om een venster te verplaatsen. Het klikt vast aan de andere vensters en aan de randen. Houd `Alt` ingedrukt om zonder vastklikken te verplaatsen.
- Sleep aan een rand om de grootte te wijzigen.
- `Losgemaakt (eigen venster)` maakt van het venster een apart systeemvenster, voor een tweede monitor. Een losgemaakt venster heeft een knop `Vastzetten` die het terugzet. `Beeld › Losgemaakte panelen (eigen venster)` toont ze alle vijf.
- `Beeld › Panelen buiten beeld terughalen` haalt een venster terug dat buiten de werkruimte is beland.
- `Beeld › Indeling opslaan als...` bewaart de indeling onder een naam, en `Beeld › Indeling laden` haalt er een terug.

De Browser vormt de achtergrond van de werkruimte: hij blijft aan de zijkant vastgezet en de andere vensters blijven altijd erboven.

## Goed om te weten

- Totdat je iets verplaatst, de grootte wijzigt of losmaakt, nemen de vensters hun standaardplaats weer in wanneer het hoofdvenster van grootte verandert. Daarna wordt je indeling onthouden en schaalt ze met het venster mee.
- `F12` sluit de plugin-, kanaalinstellings- en toolvensters. Het sluit de vijf hoofdvensters niet.
- Zolang het pianotoetsenbord van het typtoetsenbord aan staat (`Ctrl+T`), spelen lettertoetsen noten en liggen de meeste sneltoetsen met één letter stil.
- `Beeld › Lay-outstijl` wisselt tussen de Pattern-werkwijze (de standaard) en de Clipstart-werkwijze, die naast de Playlist een clipraster toevoegt.

## Gerelateerd

- [Je eerste nummer](/wiki/first-song/)
- [Lay-outs](/wiki/layouts/)
- [Sneltoetsen](/wiki/shortcuts/)
- [Instellingen](/wiki/settings/)
