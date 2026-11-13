---
source: d2bebc77b1
---
![De MIDI-pagina van de instellingen: ingangsapparaten, uitgangsapparaten met master sync, en externe sync](/assets/img/wiki/settings-midi.webp)

LYNX SHARE neemt noten en controllerbewegingen aan van elk MIDI-ingangsapparaat. De apparaten stel je in op de MIDI-pagina van de instellingen: `Opties › MIDI-instellingen...`, of `F10`. Zonder hardware spelen ook het typetoetsenbord en het touch-toetsenbord noten.

## Een keyboard aansluiten

1. Sluit het keyboard aan en open `Opties › MIDI-instellingen...` (`F10`).
2. Zoek het onder `Invoerapparaten` en vink het aan. Staat het er niet bij, klik dan op `Lijst vernieuwen`.
3. Laat de twee vakken op `Elk MIDI-kanaal` en `Bespeelt de geselecteerde kanalen` staan.
4. Sluit de instellingen en selecteer een kanaal in het [Channel Rack](/wiki/channel-rack/).
5. Speel. Het geselecteerde kanaal klinkt.

## Invoerapparaten

| Instelling | Wat het doet |
|---|---|
| Aankruisvak | Zet het apparaat aan of uit |
| `Elk MIDI-kanaal` | Luistert naar elk MIDI-kanaal, of naar één kanaal. `MPE-controller` staat aan het eind van deze lijst |
| `Bespeelt de geselecteerde kanalen` | Noten gaan naar de kanalen die in het Channel Rack geselecteerd zijn, of altijd naar één vast kanaal |
| `Velocity` | `Zoals gespeeld`, `Zacht (licht spelen klinkt luider)`, `Hard (vereist harder spelen)` of `Vast (elke noot op volle velocity)` |
| `Kanaal van de performance-modus` | Noten op dit MIDI-kanaal lanceren Playlist-clips zolang Performance-modus aan staat |
| `Songmarker-sprong` | Noten op dit MIDI-kanaal verplaatsen het nummer naar een marker in plaats van te spelen |
| `Type controller` | `Automatisch detecteren`, `Generieke controller` of een controllerprofiel |
| `Voetpedaal bepaalt het einde van noten` | Standaard aan. Een sustainpedaal (CC 64) of een sostenutopedaal (CC 66) houdt de noten vast. Uit gaat het pedaal als controller naar de plugins |

## Een knop aan een regelaar koppelen

1. Klik met rechts op de knop of fader in LYNX SHARE.
2. Kies `Koppelen aan MIDI-controller (beweeg een knop op de controller)`.
3. Beweeg de knop of fader op je hardware. De koppeling is gemaakt.

De koppeling wordt met het project opgeslagen. Om haar te verwijderen klik je opnieuw met rechts op de regelaar en kies je `Controller ontkoppelen`. Het menu-item toont het CC-nummer dat in gebruik is.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| `Koppelen aan controller...` | geen | Opent het venster `Instellingen voor afstandsbediening` voor de regelaar, waar je de koppeling ervan met de hand ziet en instelt (hieronder) |
| `Globale koppeling overschrijven...` | none | Maakt een koppeling die in elk project werkt. Ze wordt in de instellingen bewaard, niet in het project |
| `Globale koppeling overschrijven... (voor alle projecten: beweeg een knop op de controller)` | geen | Maakt in één stap een globale koppeling: beweeg daarna de hardwareregelaar. Het item zonder de haakjes opent in plaats daarvan het venster `Instellingen voor globale koppeling` |
| `Globale koppeling verwijderen` | none | Verwijdert haar weer |
| `Multilink naar controllers` | `Ctrl+J` | Zolang dit aan staat, worden de regelaars die je beweegt in volgorde gekoppeld aan de hardwareregelaars die je daarna beweegt. Het staat in het menu `Opties` |
| `Pickup (takeover-modus)` | none | Op de MIDI-pagina. Een controller beweegt een parameter pas nadat hij de huidige waarde heeft bereikt, zodat niets springt |

De MIDI-pagina toont de koppelingen onder `Koppelingen (project, dan globaal)`. Een apparaat waarvan `Type controller` op `Generieke controller` staat, doet alleen wat je met de hand koppelt.

De parameters van een plugin van derden bereik je via `Pluginparameter automatiseren` in het menu van het kanaal of het menu van het effectslot. Elke parameter daar heeft dezelfde koppelitems. Zie [Automation](/wiki/automation/).

## Instellingen voor afstandsbediening

`Koppelen aan controller...` opent dit venster, zoals in FL Studio. `Instellingen voor globale koppeling` (uit `Globale koppeling overschrijven...`) heeft dezelfde velden behalve de koppelingenlijst, `Omni`, de interne controller en `Conflicten verwijderen`. Een dubbelklik op een koppeling in de lijst van de MIDI-pagina opent het ook.

| Instelling | Wat het doet |
|---|---|
| `Poort`, `Kanaal`, `Ctrl` | Het poortnummer van de MIDI-ingang, het MIDI-kanaal en het controllernummer. Klik met rechts op `Ctrl` voor de standaardcontrollers bij naam. `Noot C0` in die lijst maakt een notenkoppeling: draai het nummer vanaf daar omhoog voor een andere toets |
| `Automatisch detecteren` | Standaard aan. Beweeg een regelaar op je apparaat en de nummers worden ingevuld |
| `Omni` | De koppeling volgt het geselecteerde kanaal, of voor een effect in een mixerslot, de geselecteerde mixertrack |
| `Modus` | Hoe een noot- of knopkoppeling werkt. `Standaard` en `Hold` volgen de toets: zijn velocity zolang hij is ingedrukt, nul bij het loslaten. `Latch` schakelt bij elke druk tussen de twee uiteinden. `Inc` verhoogt de waarde bij elke druk. `Blijven` stelt de velocity in en houdt die na het loslaten vast |
| Internal controller | Koppelt de regelaar in plaats daarvan aan een van de interne controllers van het project. Alleen zichtbaar wanneer het project er een heeft. Met `Automatisering opnemen` kunnen de bewegingen van die controller als automatisering worden opgenomen |
| `Mappingformule` | Hoe de ingang op de regelaar wordt afgebeeld, met de variabele `Ingang`. `Presets` heeft de lijst formules van FL. Druk op `Enter` om een getypte formule toe te passen. De curve ernaast toont het resultaat |
| `Afvlakking` | Maakt de bewegingen van de regelaar vloeiender. `Tijd` bepaalt hoe lang: 1 pool voor negatieve waarden, 3 polen voor positieve |
| `Conflicten verwijderen` | Standaard aan. Verwijdert andere koppelingen die dezelfde controller gebruiken wanneer je op `Accepteren` klikt. Het wordt rood zolang zo'n koppeling bestaat |
| `Reset`, `Overslaan`, `Accepteren` | `Accepteren` bewaart de koppeling (één stap voor ongedaan maken). Met meerdere regelaars in de wachtrij door multilink gaat `Overslaan` naar de volgende |

## Controllerprofielen

Een controllerprofiel stelt de transportknoppen, faders, draaiknoppen, pads, lampjes en het display van een apparaat in, zonder dat je iets hoeft te koppelen.

1. Open `Opties › MIDI-instellingen...` (`F10`) en vink het apparaat aan onder `Invoerapparaten`.
2. Laat zijn `Type controller` op `Automatisch detecteren` staan. De naam tussen haakjes is het profiel dat uit de poortnaam van het apparaat is gevonden.
3. Staat er `Generieke controller`, kies dan zelf het profiel uit de lijst.
4. Lees de regel naast het vak. `Feedback to` noemt de uitgangspoort die de lampjes, motorfaders en displaytekst krijgt.

| Profiel | Wat het toewijst |
|---|---|
| `Mackie Control Universal` | Faders naar het volume van de inserts en de Master. V-Pots naar pan, naar de parameters van de plugin van het geselecteerde kanaal of naar de volumes van het Channel Rack. Arm, solo, mute en select per strip, faderbanken, afspelen, stop, opnemen, terugspoelen, vooruitspoelen, Pattern- of Song-modus, metronoom, ongedaan maken en het jogwiel. Het display toont namen en waarden |
| `Mackie Control Extender (XT)` | Hetzelfde voor 8 strips extra. De units delen één bank, in de volgorde van de lijst `Invoerapparaten` |
| `Novation Launchkey MK3 (DAW mode)` | Draaiknoppen naar pan, volume of pluginparameters, faders naar volume, faderknoppen selecteren een insert. Knoppen voor bank, afspelen, stop, opnemen, loop, metronoom en ongedaan maken. De Session-pads zijn de 16 steps van het geselecteerde kanaal. Gebruik het op de tweede poort van het keyboard |
| `Akai APC Key 25 mk2` | Draaiknoppen naar volume, pan of pluginparameters. De twee bovenste padrijen zijn de 16 steps van het geselecteerde kanaal en de drie eronder spelen noten. Trackknoppen selecteren een insert. Stop, afspelen en opnemen. Gebruik het op de tweede poort |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | De Mackie Control-indeling, op de MCU/HUI-poort van het keyboard |
| `Korg nanoKONTROL2 (Mackie Control mode)` | De Mackie Control-indeling. Hij wordt niet herkend: start het apparaat in een Mackie Control-modus en kies dit type met de hand |

Draaiknoppen en faders die een positie sturen, gebruiken soft takeover: ze verplaatsen een regelaar pas nadat ze zijn waarde hebben bereikt. Regelaars die je met de hand hebt gekoppeld, behouden hun koppelingen. `Map met controllerprofielen` opent de map voor je eigen profielen (`*.lynxctl`), en `Lijst vernieuwen` leest ze opnieuw in.

De ingebouwde profielen zijn geschreven op basis van de documentatie van elke fabrikant. Ze zijn nog niet op de hardware uitgeprobeerd.

## MPE-controllers

MPE zit in een eerste fase. Een MPE-controller speelt elke noot met zijn eigen pitchbend, timbre (CC 74) en druk.

1. Open op de MIDI-pagina het vak van het apparaat waarin `Elk MIDI-kanaal` staat en kies `MPE-controller`.
2. Kies in het vak dat ernaast verschijnt de ledenkanalen, van `Kanaal 2` tot `Channels 2-16`.
3. Speel. Zie [Opname](/wiki/recording/) om op te nemen.

- Een opgenomen noot bewaart van elk één waarde: zijn bend als fijne pitch, zijn timbre als Mod X en zijn sterkste druk als Mod Y. Het is geen curve.
- Van de ingebouwde instrumenten volgen de Sampler en de ingebouwde synth deze waarden.
- Een plugin van derden ontvangt MPE als `MPE` aan staat onder `MIDI` in de `Wrapper-instellingen` van het pluginvenster. Elke noot gaat dan uit op een eigen MIDI-kanaal, 2 tot 16.
- Het is nog niet uitgeprobeerd met een echte MPE-controller.

## MIDI Map Mode en Key Map Mode

In de [Clip-launch-werkwijze](/wiki/layouts/) kunnen clips, scenes en knoppen worden gekoppeld zoals in Ableton Live. De knoppen `MIDI` en `Toonsoort` naast de schakelaar `Beide`, `Raster`, `Song` zetten de modi aan, net als `Ctrl+M` en `Ctrl+K`.

1. Druk op `Ctrl+M` voor MIDI Map Mode, of op `Ctrl+K` voor Key Map Mode. Elk element dat kan worden gekoppeld krijgt een kleur, blauw voor MIDI en oranje voor toetsen.
2. Klik op een element: een clipslot, een scene, de stopknop van een track, `Alle clips stoppen`, play, stop, record, `Sessie-opname`, het metronoom, of een knop van de Sessiemixer of de Device View.
3. Beweeg een regelaar of druk op een toets van je MIDI-apparaat, of druk in Key Map Mode op een computertoets. Het element toont zijn toewijzing, bijvoorbeeld `1/19` (kanaal en controller).
4. De koppelingen staan in een lijst in plaats van de Browser: Channel, Note/Control of Key, Path, Name, Min en Max. Sleep Min of Max, of dubbelklik om een waarde te typen. Klik met rechts op een rij voor `Bereik omkeren` en `Delete`.
5. Druk nogmaals op dezelfde toets om de modus te beëindigen.

## Het typetoetsenbord

`Ctrl+T`, of `Opties › Typtoetsenbord als piano`, verandert het computertoetsenbord in een piano. De rijen van `Z` tot `M` en van `Q` tot `P` spelen de geselecteerde kanalen.

- Zolang het aan staat, zijn letter- en cijfertoetsen noten. De gereedschapstoetsen met één letter van de Piano Roll en de Playlist werken alleen als het uit staat.
- Het speelt altijd met de velocity van de Piano Roll. De instelling `Velocity` op de MIDI-pagina is voor MIDI-apparaten.
- Dezelfde schakelaar staat in `Opties › Algemene instellingen...`.

## Het touch-toetsenbord

![Het touch-toetsenbord: octaafknoppen, toetsgrootte, velocitymodus en een rij pianotoetsen vanaf C4](/assets/img/wiki/touch-keyboard.webp)

`Beeld › Aanraaktoetsenbord` (`Alt+F7`) opent een piano die je met de muis of een touchscreen bespeelt. Hij speelt het kanaal dat in het Channel Rack geselecteerd is en toont de naam ervan rechts.

| Instelling | Wat het doet |
|---|---|
| `-` en `+` | Een octaaf omlaag of omhoog |
| Toetsgrootte | `Kleine toetsen`, `Middelgrote toetsen` of `Grote toetsen` |
| `Velocity: waar je aanraakt` | Dicht bij de bovenkant van een toets speelt zacht, dicht bij de onderkant luid |
| `Velocity: vast` | Elke noot op de velocity die met de schuifregelaar is ingesteld |
| `Nootnamen` | Toont de namen op de toetsen |

## MIDI-sync

`Uitvoerapparaten` op de MIDI-pagina stuurt het transport naar hardware en andere software. Vink `Master-sync versturen` aan voor een apparaat en kies `MIDI clock` (met Start, Continue, Stop en Song Position Pointer) of MIDI Time Code. `Offset (ms)` stuurt de sync later bij een positieve waarde en eerder bij een negatieve. `All notes off versturen` stuurt dat bericht naar het apparaat bij init en na een export.

`Sync met externe clock` werkt andersom: LYNX SHARE volgt de MIDI-clock van de ingang die je kiest, met het tempo, Start, Continue, Stop en Song Position Pointer ervan. `Offset van de externe sync (milliseconden)` laat LYNX SHARE later spelen bij een positieve waarde en eerder bij een negatieve. `Status` toont of er een clock loopt.

## Goed om te weten

- Een ingang kiezen voor `Sync met externe clock` zet dat apparaat aan onder `Invoerapparaten`.
- Een tempo dat door een gekoppelde MIDI-controller wordt verschoven, verandert de lengte van audioclips niet. Typ of stel in plaats daarvan het uiteindelijke tempo in.
- Om te registreren wat je speelt, zie [Opnemen](/wiki/recording/). Binnenkomende noten kunnen in de [Piano Roll](/wiki/piano-roll/) op een toonladder worden gesnapt.
- MIDI CC, channel aftertouch en pitch bend die naar een plugin worden gestuurd, kun je automatiseren met `MIDI-controller automatiseren (CC, aftertouch, pitch bend)`.
- Een noot die door een pedaal wordt vastgehouden, wordt opgenomen tot het moment dat het pedaal omhoogkomt.
- Een automation clip neemt een regelaar niet terug terwijl je hem vanaf een controller beweegt. De regelaar volgt zijn automatisering weer als het afspelen de volgende keer start.
- Notenkoppelingen die in `Instellingen voor afstandsbediening` zijn gemaakt, worden niet naar een .flp-export geschreven.
- `Ctrl+M` en `Ctrl+K` starten de map-modi alleen in de Clip-launch-werkwijze. Map Mode heeft nog geen macrokoppeling of notenbereiken.

## Gerelateerd

- [Opnemen](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Instellingen](/wiki/settings/)
- [Sneltoetsen](/wiki/shortcuts/)
