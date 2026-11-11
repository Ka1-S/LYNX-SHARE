---
source: 200df26182
---
![Het Channel Rack met steprijen](/assets/img/rack.webp)

Het Channel Rack toont elk instrument (kanaal) van het project, één per rij, met de steps van het huidige pattern. Druk op `F6` om het te tonen of te verbergen. Rechtsklikken op de Pattern-knop in het transport toont het ook.

## Een drumpattern maken

1. Druk op `F6` om het Channel Rack te tonen.
2. Klik op het vak `+ kanaal toevoegen` onder de laatste rij en kies een instrument uit het menu. Je kunt ook in de [Browser](/wiki/browser/) op een sample dubbelklikken voor een nieuw samplerkanaal.
3. Klik op steps in de rij van het kanaal om ze aan te zetten. Sleep om een reeks steps te tekenen.
4. Rechtsklik op een step om hem te wissen.
5. Druk op `Space` om af te spelen. In de Pattern-modus loopt het pattern in een lus.
6. Sleep aan de volumeknop van de rij om het niveau in te stellen.

## Steps programmeren

- Klik op een step om hem aan te zetten. Slepen tekent een reeks steps. Begint het slepen op een brandende step, dan wist het juist.
- Rechtsklikken of met rechts slepen wist.
- Klikken voorbij het einde van het pattern voegt een step toe en maakt het pattern langer.
- Een rij waarvan de noten geen gewone steps zijn (andere toetsen, langere noten, slides) toont een miniatuur van de noten. Klikken erop opent ze in de [Piano Roll](/wiki/piano-roll/).
- Je kunt audiobestanden op het rack laten vallen om kanalen te maken.

Het kanaalmenu (rechtsklik op de naam van het kanaal) bevat `Elke 2 steps vullen`, `Elke 4 steps vullen`, `Elke 8 steps vullen`, `Geavanceerd vullen`, en `Naar links roteren` en `Naar rechts roteren`.

## De grafiekbaan

![De grafiekbaan open onder het kanaal Kick, met één velocitybalk per step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` toont de grafiekbaan onder het geselecteerde kanaal. De knoppen links ervan kiezen wat je bewerkt: Velocity, Pan, Release, Fijne pitch, Mod X of Mod Y. Slepen stelt een waarde per step in, met rechts slepen teken je een helling, en `Alt`+klikken zet een step terug.

## De onderdelen van een rij

| Onderdeel | Wat het doet |
|---|---|
| Mute-lampje | Klikken mute het kanaal. `Ctrl`+klikken of rechtsklikken zet het solo. |
| Pan- en volumeknoppen | Slepen of het wiel gebruiken. |
| Routevak | Slepen of het wiel kiest de Mixer-insert. Dubbelklikken toont die insert in de Mixer. Rechtsklikken neemt de volgende vrije insert. |
| Naamknop | Klikken opent of sluit de editor van het kanaal. `Alt`+klikken houdt de andere editors open. `Ctrl`+klikken beluistert het kanaal. Omhoog of omlaag slepen wijzigt de volgorde. Rechtsklikken opent het kanaalmenu. |
| Selector | Klikken selecteert één kanaal. `Ctrl`+klikken of rechtsklikken voegt toe of haalt weg. `Shift`+klikken selecteert een reeks. Dubbelklikken selecteert alles. |

## Patterns

Het rack bewerkt altijd het huidige pattern. De patternkiezer van het rack toont de naam: klik op de naam voor de lijst, of gebruik de pijlen. `Shift`+wiel boven de titel van het rack wijzigt ook het pattern.

Het vak voor de patternlengte van het rack stelt de lengte in maten in (sleep eraan of gebruik het wiel). Dubbelklik erop voor een automatische lengte op basis van de noten.

De swingknop in de hoofdwerkbalk geldt voor alle kanalen. `Swing-mix` in het kanaalmenu bepaalt hoeveel één kanaal ermee meegaat.

| Commando | Toets | Wat het doet |
|---|---|---|
| Volgend pattern | `Numpad +` | Maakt het volgende pattern het huidige |
| Vorig pattern | `Numpad -` | Maakt het vorige pattern het huidige |
| Volgend leeg pattern | `F4` | Gaat naar het volgende lege pattern en vraagt om een naam |
| Hernoemen en kleuren | `F2` | Hernoemt en kleurt het huidige pattern |

## Noten spelen met het pc-toetsenbord

`Ctrl+T` maakt van het typtoetsenbord een piano: `Z` tot `M` en `Q` tot `P` spelen de geselecteerde kanalen. Zolang het aan staat, liggen de sneltoetsen met één letter van de editors stil.

## Toetsen

Deze werken zolang het Channel Rack de focus heeft.

| Commando | Toets | Wat het doet |
|---|---|---|
| Kanaal selecteren | `Up` / `Down` | Selecteert het kanaal erboven of eronder |
| Selectie uitbreiden | `Shift+Up` / `Shift+Down` | Voegt het kanaal erboven of eronder aan de selectie toe |
| Kanalen verplaatsen | `Alt+Up` / `Alt+Down` | Verplaatst de geselecteerde kanalen omhoog of omlaag |
| Alles selecteren | `Ctrl+A` | Selecteert alle kanalen |
| Knippen, kopiëren, plakken | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Knipt, kopieert of plakt de steps van de geselecteerde kanalen |
| Steps roteren | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Roteert de steps van de geselecteerde kanalen |
| Klonen | `Alt+C` | Kloont de geselecteerde kanalen |
| Verwijderen | `Alt+Del` | Verwijdert de geselecteerde kanalen |
| Groeperen | `Alt+G` | Zet de geselecteerde kanalen in een nieuwe filtergroep |
| Naar Mixer routeren | `Ctrl+L` | Stuurt de geselecteerde kanalen naar vrije Mixer-inserts |
| Mute | `1` tot `0` | Mute of unmute kanaal 1 tot 10 |
| Solo | `Ctrl+1` tot `Ctrl+0` | Zet kanaal 1 tot 10 solo |
| Grafiekbaan | `Ctrl+K` | Toont of verbergt de grafiekbaan |

## Een kanaal bevriezen

Bevriezen rendert een kanaal naar audio en speelt die audio in zijn plaats af. Het instrument wordt uit het geheugen gehaald, ook een plugin van derden, zodat het geen CPU en geen geheugen gebruikt.

1. Klik met rechts op het kanaal en kies `Bevriezen`. Als er meerdere kanalen zijn geselecteerd, staat er `Geselecteerde kanalen bevriezen`.
2. De rij krijgt een sneeuwvlokje. Het volume en de pan van het kanaal en de effecten van zijn insert werken nog als voorheen.
3. Om de noten of het instrument weer te wijzigen, klik je met rechts op het kanaal en kies je `Ontdooien`. Het instrument komt terug met de instellingen die het had.

Een bevroren kanaal kan niet worden bewerkt: zijn steps zijn vergrendeld en het venster van zijn instrument opent niet. Als de noten, het tempo of het arrangement na het bevriezen veranderen, verandert het sneeuwvlokje van kleur en blijft de eerder gemaakte audio spelen totdat je `Opnieuw bevriezen` kiest. Ontbreken de gerenderde audiobestanden, dan speelt het instrument in plaats daarvan.

Bevriezen werkt alleen op kanalen. Een Mixer-insert kan niet worden bevroren.

## Andere opties in het kanaalmenu

`Piano Roll`, `Hernoemen en kleuren`, `Vervangen` (het instrument verwisselen), `Klonen`, `Delete`, `Arpeggiator`, `Looppunt`, `MIDI wegschrijven naar`, `Kanalen sorteren`, `Ongebruikte kanalen selecteren` en `Kanaalinstellingen`.

## Goed om te weten

- Alleen het vak `+ kanaal toevoegen` opent het toevoegmenu.
- Het rack heeft geen eigen pattern. Het huidige pattern ergens wijzigen verandert wat het rack toont.
- Een rij met een miniatuur van noten kan niet als steps worden bewerkt. Gebruik daarvoor de Piano Roll.
- De cijfertoetsen bereiken alleen de eerste tien kanalen.
- De toetsen in de tabel werken alleen als het rack de focus heeft. Klik eerst op het rack.

## Gerelateerd

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Sneltoetsen](/wiki/shortcuts/)
