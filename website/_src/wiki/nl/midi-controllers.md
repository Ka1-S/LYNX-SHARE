---
source: 3b54a39c97
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
| `Elk MIDI-kanaal` | Luistert naar elk MIDI-kanaal, of naar één kanaal |
| `Bespeelt de geselecteerde kanalen` | Noten gaan naar de kanalen die in het Channel Rack geselecteerd zijn, of altijd naar één vast kanaal |
| `Velocity` | `Zoals gespeeld`, `Zacht (licht spelen klinkt luider)`, `Hard (vereist harder spelen)` of `Vast (elke noot op volle velocity)` |
| `Kanaal van de performance-modus` | Noten op dit MIDI-kanaal lanceren Playlist-clips zolang Performance-modus aan staat |
| `Songmarker-sprong` | Noten op dit MIDI-kanaal verplaatsen het nummer naar een marker in plaats van te spelen |

## Een knop aan een regelaar koppelen

1. Klik met rechts op de knop of fader in LYNX SHARE.
2. Kies `Koppelen aan MIDI-controller (beweeg een knop op de controller)`.
3. Beweeg de knop of fader op je hardware. De koppeling is gemaakt.

De koppeling wordt met het project opgeslagen. Om haar te verwijderen klik je opnieuw met rechts op de regelaar en kies je `Controller ontkoppelen`. Het menu-item toont het CC-nummer dat in gebruik is.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| `Override global link...` | none | Maakt een koppeling die in elk project werkt. Ze wordt in de instellingen bewaard, niet in het project |
| `Globale koppeling verwijderen` | none | Verwijdert haar weer |
| `Multilink naar controllers` | `Ctrl+J` | Zolang dit aan staat, worden de regelaars die je beweegt in volgorde gekoppeld aan de hardwareregelaars die je daarna beweegt. Het staat in het menu `Opties` |
| `Pickup (takeover-modus)` | none | Op de MIDI-pagina. Een controller beweegt een parameter pas nadat hij de huidige waarde heeft bereikt, zodat niets springt |

De MIDI-pagina toont de koppelingen onder `Koppelingen (project, dan globaal)`. `Type controller` op dezelfde pagina leest `Generieke controller`.

De parameters van een plugin van derden bereik je via `Pluginparameter automatiseren` in het menu van het kanaal of het menu van het effectslot. Elke parameter daar heeft dezelfde koppelitems. Zie [Automation](/wiki/automation/).

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

## Gerelateerd

- [Opnemen](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Instellingen](/wiki/settings/)
- [Sneltoetsen](/wiki/shortcuts/)
