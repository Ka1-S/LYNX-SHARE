---
source: ab2c2f14eb
---
![De pagina Algemeen van het venster Instellingen, met de paginatabs bovenaan](/assets/img/wiki/settings-general.webp)

Het venster Instellingen heeft acht pagina's: Audio, MIDI, Algemeen, Thema, Bestanden, LYNX extras, Project en Info. De eerste zes horen bij het programma. Project en Info horen bij het geopende project en worden ermee opgeslagen. Elke pagina heeft een eigen item bovenaan het menu `Opties`.

## Een instelling wijzigen

1. Open het menu `Opties` en kies een pagina, bijvoorbeeld `Algemene instellingen...`.
2. Klik bovenaan op een andere tab om naar een andere pagina te gaan.
3. Wijzig de instelling. Ze werkt direct, behalve de taal en het interfacelettertype, die werken zodra LYNX SHARE opnieuw start.

`F10` opent de MIDI-pagina en `F11` de pagina Info.

## Audio

`Opties › Audio-instellingen...`. Driver, apparaten, samplefrequentie, buffergrootte en opname. [Audio instellen](/wiki/audio-setup/) beschrijft deze pagina. Onder `Opname` biedt `Bestandsformaat` nu ook `WAV 16 bit (de samplefrequentie van het apparaat)`.

## MIDI

![De MIDI-pagina: ingangsapparaten, uitgangsapparaten met master sync, en externe sync](/assets/img/wiki/settings-midi.webp)

`Opties › MIDI-instellingen...`

| Instelling | Wat het doet |
|---|---|
| Invoerapparaten | Elk apparaat kan worden uitgezet, naar één MIDI-kanaal luisteren, en een vast kanaal van het Channel Rack bespelen in plaats van de geselecteerde |
| `Type controller` | Per ingangsapparaat: `Automatisch detecteren`, `Generieke controller` of een controllerprofiel, zoals Mackie Control. De regel ernaast noemt de poort die de lampjes en het display krijgt |
| `MPE-controller` | Aan het eind van de MIDI-kanaallijst van een apparaat. Elke noot komt met zijn eigen bend, timbre en druk. Een tweede vak stelt de ledenkanalen in |
| `Lijst vernieuwen` | Zoekt opnieuw naar apparaten en leest je controllerprofielen opnieuw in |
| `Map met controllerprofielen` | Opent de map voor je eigen controllerprofielen (`*.lynxctl`) |
| `Master-sync versturen` | Stuurt MIDI clock met Song Position Pointer, of MIDI Time Code, naar een uitgangsapparaat. `Offset (ms)` stuurt hem later of eerder |
| `All notes off versturen` | Stuurt all notes off naar het apparaat bij init en na een export |
| `Sync met externe clock` | LYNX SHARE volgt de MIDI-clock van de gekozen ingang: tempo, start, continue, stop en positie |
| `Velocity` | Voor noten van MIDI-apparaten: zoals gespeeld, zacht, hard of vast |
| `Kanaal van de performance-modus` | Noten op dit kanaal lanceren Playlist-clips in Performance-modus |
| `Songmarker-sprong` | Noten op dit kanaal verplaatsen het nummer naar een marker in plaats van te spelen |
| `Pickup (takeover-modus)` | Een controller beweegt een parameter pas nadat hij de huidige waarde heeft bereikt |
| `Voetpedaal bepaalt het einde van noten` | Standaard aan: het sustain- en het sostenutopedaal houden noten vast. Uit gaat het pedaal naar de plugins |
| `Een gamecontroller gebruiken` | Een gamecontroller bedient het transport en kan net als een MIDI-controller worden gekoppeld. Standaard uit |
| Koppelingen | De controllerkoppelingen van dit project, daarna de globale, met `Ontkoppelen` en `Alles verwijderen` |

## Algemeen

`Opties › Algemene instellingen...`

| Instelling | Wat het doet |
|---|---|
| `Lay-outstijl` | Patternwerkwijze (standaard), Clipstart-werkwijze of Eenvoudige werkwijze |
| `Standaardsjabloon`, `Project bij het starten` | De template die opent met `Bestand › Nieuw`, en wat opent bij het opstarten: `Leeg project`, `Standaardsjabloon` of `Laatst gebruikt project`. Met `Bestand › Opslaan als sjabloon...` voeg je je eigen toe |
| `Sessieclips uit Live-sets` | Waar de sessieclips van een geïmporteerde Live-set heen gaan: het clipraster, of de Playlist na het arrangement |
| `Taal`, `Lettertype van de interface` | Toegepast bij de volgende start |
| `Interfacegrootte`, `Menudichtheid` | Schaal van de hele interface, en compacte of ruime menu's |
| `Golfvormen` | Schoon (de kleur van de clip) of spectrale kleuren, eventueel met gescheiden stereokanalen |
| `Nootnamen`, `Zwarte toetsen` | Engelse, Germaanse of solfège-namen, met kruisen of mollen |
| `Pluginstijl` | Het uiterlijk van de ingebouwde pluginvensters: automatisch, of één stijl voor elke plugin |
| `Animaties`, `Animatiesnelheid` | Interface-animaties, en hoe vaak de afspeelkop en de meters bewegen |
| `Tekenen met de GPU (Direct2D)` | Zet uit als het grafische stuurprogramma crasht of het scherm flikkert |
| Opstarten | Splashscreen, opstartgeluid, en bij elke start op de achtergrond zoeken naar nieuwe plugins |
| Bewerken | Typtoetsenbord als piano, kanaaleditors open houden, de seektijd onthouden, uitgerekte patternclips loopen |
| `Alle nieuwe plug-ins losmaken` | Aan: het venster van een plugin die je toevoegt opent losgemaakt, bovenop de panelen. Uit (standaard): het opent vastgemaakt in het hoofdvenster |
| `Monofoon schilderen in de pianorol` | Aan: het Tekenen-gereedschap vervangt de noten onder elke nieuwe noot, en Shift tekent erboven. Uit: andersom |
| `Gekoppelde modules automatisch selecteren` | Een pluginvenster focussen selecteert zijn mixertrack. Standaard aan |
| `Voorrang van het typtoetsenbord standaard uitschakelen` | Voor plugins die je vanaf dat moment toevoegt. Standaard uit |
| `Maximaal aantal ongedaan-maakstappen` | 25 tot 1000 |
| `Berichten beheren` | Onder `Tips en waarschuwingen`. Aangevinkte meldingen worden getoond. Vink er een uit om zijn vraag over te slaan |
| Back-up | Hoe vaak een back-upkopie wordt opgeslagen, hoeveel er per project bewaard blijven, en `Back-upmap openen` |
| `Plugins in een apart proces draaien` | Een crashende plugin haalt LYNX SHARE niet onderuit. Standaard uit |
| `Ontbrekende plugins vervangen door LYNX-SHARE-equivalenten` | Standaard aan. Als een project een plugin van derden gebruikt die niet is geïnstalleerd, laadt de vervanger van LYNX SHARE met overeenkomstige instellingen waar hij die heeft. Het project behoudt de originele plugin en gebruikt hem weer zodra hij is geïnstalleerd |
| `Aantal open plug-in-editors beperken` | Maximaal 25 pluginvensters blijven open. Standaard aan |
| `Crashrapporten` | Vragen na een crash of freeze (standaard), altijd versturen of nooit versturen |
| `Automatisch controleren op updates` | De updatecontrole bij het opstarten, met `Nu controleren` |

## Thema

`Opties › Thema (kleuren, achtergrond)...`

| Instelling | Wat het doet |
|---|---|
| Themavak | Kiest een preset. `Importeren...` en `Exporteren...` lezen en schrijven `.lynxtheme`-bestanden, `Preset opslaan...` bewaart het huidige thema, `Standaard` zet het terug |
| `Accentkleur`, `Achtergrond`, `Werkruimte` | De drie kleuren van een thema. De kleur van de werkruimte kan automatisch de achtergrond volgen |
| Werkruimteafbeelding | Een PNG, JPEG of GIF achter de vensters, met `Afbeeldingsschaal`, `Dekking afbeelding` en `Dimmen` |
| Opstartgeluid | Je eigen geluid van 30 seconden of korter, of dat van LYNX SHARE zelf |

## Bestanden

`Opties › Bestandsinstellingen (zoekmappen)...`

| Instelling | Wat het doet |
|---|---|
| Zoekmappen voor ontbrekende samples | Het venster Ontbrekende bestanden zoekt zelf in deze mappen en hun submappen |
| Browsermappen | Getoond in de maplijst van de Browser, en ook doorzocht op ontbrekende samples |
| Pluginmappen | Gescand op plugins door `Opties › Plugins scannen` |

## LYNX extras

`Opties › LYNX-extra's...`. De eigen toevoegingen van LYNX SHARE op één plek. Alle staan standaard uit. Elke schakelaar is dezelfde instelling als in het menu of de pagina die zijn regel noemt.

| Instelling | Wat het doet |
|---|---|
| `Luidheidsmeter naast de Master (LYNX)` | Loudness op korte termijn en geïntegreerd (LUFS) en de true peak van de Master, met een Reset-knop |
| `Alleen gebruikte inserts tonen` | Verbergt de Mixer-inserts die niets gebruikt |
| `Koppelingen naar verwijderde inserts grijs bewaren (LYNX)` | Een insert verwijderen laat de koppelingen die erin gingen staan, zodat ze opnieuw kunnen worden verbonden |
| `Mixergroepen volgen de Playlist (LYNX)` | De groepen van de Mixer weerspiegelen de trackgroepen van de Playlist |
| `Automatiseringsbanen` | Automatisering kan in banen onder een Playlist-track staan |
| `Liniaal: songmaten (LYNX)` | De liniaal van de Piano Roll toont de maatnummers van de plek in de song waar het pattern speelt |
| `De waarde van een regelaar naast de muis tonen bij aanwijzen (LYNX)` | De waarde verschijnt naast de cursor, niet alleen in de hintbalk |
| `Na loopopname speelt alleen de laatste doorgang` | De eerdere doorgangen blijven eronder, gemute |
| `W toont of verbergt het venster van het geselecteerde apparaat` | Een toets voor de Device-weergave |
| `Een apparaat selecteren selecteert de insert ervan in de Mixer` | De Mixer volgt het apparaat dat je in de Device-weergave kiest |
| `Een gamecontroller gebruiken` | Dezelfde schakelaar als op de MIDI-pagina |

## Project

![De pagina Project: tijdinstellingen, groove en stemming, timebase en Playlist-opties](/assets/img/wiki/settings-project.webp)

`Opties › Projectinstellingen (tempo, maatsoort)...`

| Instelling | Wat het doet |
|---|---|
| `Tijdinstellingen` | Tijdsindeling (tellen per maat, steps per tel) of een traditionele maatsoort (teller en noemer) |
| `Tempo`, `Tellen per maat` | Het tempo van het project en het aantal tellen in een maat |
| `Swing`, `Master-pitch` | De globale swing en de master-pitch in cents |
| `Automatische crossfades` | Overlappende audioclips op een track faden over de overlap in elkaar |
| `Comp-crossfade` | De crossfadelengte bij het comppen van takes, in milliseconden |
| `Afgekapte noten in clips spelen` | Een noot die voor een patternclip begint, speelt vanaf het begin van de clip |
| `Snelle declick voor afkapgroepen` | Snellere ramping als afkapgroepen noten afkappen |
| `Panwet` | Circular (midden op 0 dB, +3 dB aan de kant waarheen gepand wordt) of Triangular (de luidere kant behoudt zijn niveau). Voor Mixer-inserts en Sampler-kanalen |

## Info

`Opties › Projectinfo...`. Titel, auteur, genre, weblink en opmerkingen van het project, en `Deze pagina tonen wanneer het project wordt geopend`. Onderaan staat wanneer het project is gemaakt en de werktijd.

## Goed om te weten

- De timebase staat vast op 960 ticks per kwartnoot. Projecten uit FL Studio worden daarnaar omgezet.
- Wijzigingen van de maatsoort langs het nummer staan niet op de pagina Project. Voeg een maatsoortmarker toe op de liniaal van de Playlist.
- `Plugins in een apart proces draaien` geldt voor plugins die vanaf dat moment worden geopend. Open het project opnieuw om de andere te verplaatsen.
- De update-instellingen ontbreken in de Microsoft Store-versie, omdat de Store hem bijwerkt.
- Sneltoetsen staan in `Opties › Sneltoetsen beheren...`, niet op een pagina van Instellingen.
- Sommige schakelaars staan in menu's, niet in dit venster. `Lage latentie tijdens opnemen` staat in het menu met opnameopties naast de opnameknop en in het menu `Opnemen naar schijf` van de Mixer. `Klavierstijl` staat in het menu View van de Piano Roll.
- De plugin database is een map, `Plugindatabase` in de map `LYNX` van je documenten. Je kunt hem indelen in de bestandsbeheerder.

## Gerelateerd

- [Audio instellen](/wiki/audio-setup/)
- [MIDI-controllers](/wiki/midi-controllers/)
- [Sneltoetsen](/wiki/shortcuts/)
- [Plugins van derden](/wiki/plugins/)
- [Lay-outs](/wiki/layouts/)
