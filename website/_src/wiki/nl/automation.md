---
source: b96330a448
---
![De automatiseringseditor met een curve die over zes maten stijgt](/assets/img/wiki/automation.webp)

Een Automation clip is een curve op de [Playlist](/wiki/playlist/) die één regelaar beweegt terwijl het nummer speelt: het volume van een kanaal, een mixerfader, een pluginparameter, het tempo. De clip heeft een eigen editorvenster, de automatiseringseditor.

Er zijn nog drie andere manieren om een regelaar te bewegen. Events zijn waarden die in een pattern zijn opgeslagen. Een koppeling laat een MIDI-knop of een interne controller de regelaar aansturen. Een modulator laat een knop om zijn eigen waarde heen wiebelen.

## Een knop automatiseren

1. Zet de afspeelkop waar de automatisering moet beginnen, of selecteer een tijdbereik in de Playlist.
2. Klik met rechts op de knop of fader en kies `Automation clip maken`.
3. De clip komt op de eerste lege Playlist-track en de automatiseringseditor opent. De curve begint als een vlakke lijn op de huidige waarde van de regelaar.
4. Klik in de editor om een punt toe te voegen en sleep het naar zijn plek.
5. Houd `Alt` ingedrukt en sleep een segment omhoog of omlaag om het te buigen.
6. Speel het nummer af in Song-modus. De regelaar volgt de curve.

Andere manieren om dezelfde clip te krijgen:

- `Toevoegen › Automatisering voor laatst aangepaste parameter` maakt een clip voor de regelaar die je het laatst bewoog.
- De uitlezing van de laatst aangepaste parameter en `Extra › Laatst aangepast` openen hetzelfde menu als een rechtsklik op de regelaar.
- Open voor een plugin van derden het menu van het kanaal in het Channel Rack of het menu van de effectslot in de Mixer en gebruik `Pluginparameter automatiseren`. `MIDI-controller automatiseren (CC, aftertouch, pitch bend)` staat er direct naast.

Om de editor opnieuw te openen, dubbelklik je op de clip, of klik je er met rechts op en kies je `Punten bewerken...`. `Automation clip bewerken...` in het eigen menu van de regelaar doet hetzelfde.

## De automatiseringseditor

| Opdracht | Toets | Wat het doet |
|---|---|---|
| Nieuw punt | Klik of rechtsklik, dan slepen | Voegt een punt toe en verplaatst het zolang je de knop vasthoudt |
| Een punt verplaatsen | Slepen | `Shift` behoudt de tijd, `Ctrl` behoudt de waarde, `Alt` negeert Snap |
| Een segment buigen | `Alt`+omhoog of omlaag slepen | Verandert de spanning van het segment onder de muis |
| Spanningshandvat | Omhoog of omlaag slepen | Buigt het segment. `Ctrl` is fijner, rechtsklikken maakt het recht |
| Punt op de curve | `Shift`+rechtsklik | Voegt een punt toe op de lijn |
| Puntmenu | Rechtsklik op een punt | `Waarde typen...`, `Waarde kopiëren`, `Waarde plakken`, `Punt verwijderen` en `Hele curve` om alles te schalen, te spiegelen of recht te trekken |
| Zoom | `Ctrl`+wiel | Zoomt de weergave |
| Scrollen | Slepen met de middelste knop | Verplaatst de weergave |

## LFO-modus

![De automatiseringseditor in LFO-modus, met een driehoeksgolf en de knoppen SPD, TNS, SKEW, PW en AMNT](/assets/img/wiki/automation-lfo.webp)

Vink `LFO` bovenaan de editor aan. Een golfvorm bepaalt nu de waarde van de clip, en de punten die je tekent stellen de amplitude van de LFO in in plaats van de waarde zelf.

| Instelling | Wat het doet |
|---|---|
| `SPD` | De lengte van één cyclus, in tellen |
| `TNS` | Spanning van de golf |
| `SKEW` | Scheefheid van de golf |
| `PW` | Pulsbreedte |
| `AMNT` | Hoe ver de LFO de waarde verplaatst |
| `MUL` | Aan: de LFO wordt vermenigvuldigd met het niveau van de envelope. Uit: hij wordt erbij opgeteld |

## Events in een pattern

![De Event-editor met volume-events als balken](/assets/img/wiki/eventeditor.webp)

Events horen bij een pattern en spelen overal waar dat pattern speelt. Klik met rechts op een regelaar en kies `Events in het pattern bewerken` om de Event-editor ervoor te openen.

- `Tekenen` zet waarden terwijl je sleept, en met rechts slepen wis je. `Lijn` tekent een rechte helling. `Selecteren` markeert een tijdbereik voor de gereedschappen onderaan.
- `Snap` loopt van een halve step tot 1 maat.
- `Toonladder`, `Offset` en `Willekeurig maken` veranderen de events van de selectie, of van het hele pattern.
- `LFO` schrijft een sinus, driehoek of blok over 1 tel tot 4 maten.
- `Wissen` verwijdert de events.
- `Naar automatisering...` zet de events om in een Automation clip en haalt ze uit het pattern.
- `Bestand` opent en bewaart de events als `.fsc`-bestand en importeert een MIDI-bestand.

`Song initialiseren met deze positie` in hetzelfde rechtsklikmenu slaat de waarde op waarnaar de regelaar wordt teruggezet elke keer dat het nummer start.

## Je bewegingen opnemen

Zet opnemen aan met `Automatisering` aangevinkt in het opnamefilter, druk op afspelen en beweeg de regelaar. In Song-modus worden de bewegingen een Automation clip. In Pattern-modus worden het events in het huidige pattern. Zie [Opnemen](/wiki/recording/).

## Koppelingen en modulators

- `Koppelen aan MIDI-controller (beweeg een knop op de controller)` komt aan bod in [MIDI-controllers](/wiki/midi-controllers/).
- `Koppelen aan interne controller` laat een Envelope-, Formula- of Peak Controller in het project de regelaar aansturen. Voeg er eerst een toe.
- `Moduleren` koppelt een modulator, een LFO of een willekeurige bron die een LYNX SHARE-knop om zijn eigen waarde heen beweegt. `Extra › Modulators...` toont ze in een lijst.

## Automatiseringsbanen

Banen staan standaard uit. Zet `Automatiseringsbanen` aan in het menu van de Playlist. Elke trackkop krijgt dan een knop die een baan onder de track toevoegt, en het regelaarmenu krijgt `Automatiseren in baan`. Klik in een baan om een punt toe te voegen of te slepen, klik met rechts om er een te verwijderen, en sleep met `Alt` om de lijn te buigen. Het menu van de baan heeft `Naar automatiseringsclip`.

## Goed om te weten

- Een nieuwe clip begint bij de tijdselectie als die er is, anders bij de afspeelkop in Song-modus. Zet `Automatiseringsclips maken op de afspeelpositie` uit in `Opties › Algemene instellingen...` om aan het begin van het nummer te beginnen.
- Meerdere clips op één regelaar spelen elk op hun eigen plek. Vóór zijn eerste clip houdt een regelaar zijn eigen waarde.
- Aan het begin van een clip bereikt de regelaar de nieuwe waarde in ongeveer 8 ms. FL Studio glijdt er naartoe, dus een geïmporteerd project kan op die punten licht afwijken.
- Waar clips op dezelfde regelaar overlappen, kan de clip die LYNX SHARE kiest verschillen van die FL Studio kiest.
- Modulators werken alleen op de eigen knoppen van LYNX SHARE en worden weggelaten als je exporteert naar `.flp`.

## Gerelateerd

- [Playlist](/wiki/playlist/)
- [Opnemen](/wiki/recording/)
- [MIDI-controllers](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Sneltoetsen](/wiki/shortcuts/)
