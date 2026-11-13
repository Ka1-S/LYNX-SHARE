---
source: e15b0e3ba5
---
![De Clipstart-werkwijze: links een raster van clips en scènes, rechts de Picker en de Playlist](/assets/img/cliplaunch.webp)

`Beeld › Lay-outstijl` heeft drie items: `Patroonwerkwijze (standaard)`, `Clipstart-werkwijze` en `Eenvoudige werkwijze`. Dezelfde keuze staat in `Opties › Algemene instellingen`, onder Werkwijze. De keuze blijft bewaard als je het programma sluit.

De Patroonwerkwijze is het Channel Rack, patterns en de Playlist. De Clipstart-werkwijze voegt naast de Playlist een clipraster met scènes toe. Patterns en het Channel Rack blijven beschikbaar.

## Clips lanceren in het raster

1. Kies `Beeld › Lay-outstijl › Clipstart-werkwijze`. Het clipraster verschijnt naast de Playlist.
2. Klik met rechts op een leeg slot, kies `Patroon hier plaatsen` en kies een pattern. Dubbelklikken op een leeg slot zet het huidige pattern erin.
3. Klik op de driehoek links van de clip om hem te lanceren. Staat het transport stil, dan start het.
4. Klik op de driehoek van een scène, in de kolom `Scènes`, om elke clip in die rij te lanceren.
5. Klik op `Alle clips stoppen` om ze te stoppen.
6. Klik op `Terug naar Playlist` om de tracks hun Playlist-clips weer te laten spelen.

## Wat er op het scherm verandert

- Het clipraster staat naast de Playlist. Playlist-tracks zijn de kolommen en scènes de rijen.
- De titelbalk van de Playlist krijgt een schakelaar `Beide`, `Raster`, `Song`. `Raster` toont alleen het raster, `Song` alleen de Playlist.
- De eerste keer is het Channel Rack verborgen. `F6` of de knop in de werkbalk toont het.
- De automatiseringsbanen van de Playlist worden ingeschakeld.
- De vensterindeling wordt per stijl bewaard, dus elke stijl komt terug zoals je hem achterliet.
- Onder het raster toont of verbergt de knop `Mixer` de Sessiemixer, zoals in Ableton Live: `In/Uit` (`Audio van`, `Monitor`, `Audio naar`), `Sends`, `Spoorvertraging`, en het volume van elke track met zijn activator, solo, arm-knop en meter, plus de strip `Hoofd`.
- `Grooves`, onder het raster, toont de Groove Pool. `Groove op noten toepassen` schrijft een groove in de noten van een MIDI-clip.
- De knoppen `MIDI` en `Toonsoort` starten MIDI Map Mode en Key Map Mode. Zie [MIDI-controllers](/wiki/midi-controllers/).

## Het raster

Een gelanceerde clip start niet meteen. Hij wacht op het volgende lanceerkwantiseringspunt en zijn driehoek knippert tot dan. Het vak onderaan het raster stelt de globale lanceerkwantisering in, die elke clip gebruikt die op `Globaal` staat. Een stop wacht op dezelfde manier.

Klik met rechts op een clip voor zijn instellingen.

| Instelling | Wat het doet |
|---|---|
| `Startkwantisering` | Wanneer deze clip start: `Globaal`, `Geen`, `8 maten` tot `1/32` |
| `Startmodus` | `Trigger`, `Gate`, `Wisselen` of `Herhalen` |
| `Legato (positie behouden)` | De clip neemt de afspeelpositie over van de clip die daarvoor op die track speelde |
| `Loop` | Aan: de clip herhaalt. Uit: hij speelt één keer, daarna stopt de track |
| `Vervolgactie` | Wat er na de clip gebeurt: twee acties met elk een kans, en de tijd waarna ze optreden |
| `Delete` | Maakt het slot leeg |

Klik met rechts op een leeg slot voor `Stopknop (een scène stopt dit spoor hier)`. Klikken op het vierkantje links van een leeg slot stopt die track. Klik met rechts op een scène voor `Scène invoegen`, `Scène eronder toevoegen`, `Scène verwijderen`, `Scène hernoemen...` en het tempo van de scène.

Clips kunnen via hun menu worden geknipt, gekopieerd, geplakt en gedupliceerd (`Ctrl+D`), en tussen slots en van of naar de Playlist worden gesleept. Een pattern of een sample die uit de Browser wordt gesleept, of audiobestanden die van buiten worden neergezet, vullen het slot waarop ze landen. Het menu van een scene heeft ook `Dupliceren` en `Scène vastleggen en invoegen`, dat de clips die nu spelen in een nieuwe scene zet. `Beeld › Tempo en maatsoort van scènes` toont voor elke scene een tempo en maatsoort.

Het slotmenu heeft ook twee opties voor het hele raster: `Sporen teruggeven aan de Playlist wanneer het transport stopt` en `Vervolgacties in het hele raster`.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| Verplaatsen | Pijltoetsen | Selecteert een ander slot |
| Clip lanceren | `Enter` | Lanceert de geselecteerde clip |
| Scène lanceren | `Ctrl+Enter` | Lanceert de geselecteerde scène |
| Clip verwijderen | `Del` | Maakt het geselecteerde slot leeg |
| Draw Mode | `B` | Een sleepbeweging in een automatiseringsbaan tekent stappen zo breed als het raster, of uit de losse hand met snap uit of `Alt` ingedrukt, zoals in Ableton Live. Ook in `Opties` |

## Ableton Live-sets

Als je een Live-set opent, komen de Session-clips en scènes ervan met hun lanceerinstellingen in het clipraster. `Bestand › Exporteren › Ableton Live-set (.als)` schrijft het raster terug als Session-clips. `Opties › Algemene instellingen` heeft `Sessieclips uit Live-sets`, waar `Playlist, na het arrangement` ze in plaats daarvan op de Playlist zet.

## De Simple-werkwijze

`Beeld › Lay-outstijl › Eenvoudige werkwijze` is één venster met de tracks en de tijdlijn, voor een eerste song. `Beeld › Spoordock` toont onder de tijdlijn een dock voor de geselecteerde track, met de tabs `Effecten`, `Instrument`, `Klavier` en `Editor`. In deze werkwijze heeft het menu `Help` de items `Tour van het eerste nummer` en `Welkomstscherm...`, dat een eerste song in zeven stappen, een demosong of een leeg project aanbiedt. Zie [Simple-lay-out](/wiki/simple-layout/).

`Songtekst en notities` en `Referentie-afstemming...` in het menu `Beeld`, en `Heldere stem` in het menu van een Mixer-insert, zijn er in elke lay-out. In de Simple-werkwijze zit `Heldere stem` ook in het menu van een track.

## Terugschakelen

Kies `Beeld › Lay-outstijl › Patroonwerkwijze (standaard)`. De vensterindeling die je in de Patroonwerkwijze had komt terug, en de automatiseringsbanen keren terug naar hun vorige instelling. De clips in het raster blijven in het project.

## Goed om te weten

- Een track die een clip lanceerde blijft in de Playlist stil tot je op `Terug naar Playlist` drukt. De optie `Sporen teruggeven aan de Playlist wanneer het transport stopt` doet dat bij elke stop voor je.
- De nieuwe toetsen van het raster (`B`, `Q`, `Ctrl+M`, `Ctrl+K` en andere) werken zo alleen in de Clip-launch-werkwijze. In de Pattern-werkwijze behouden ze de betekenis van FL Studio.
- Een scene in 6/8 speelt een maat van drie tellen, omdat de maatsoort kwartnoten telt.
- Vervolgacties bestaan voor clips, niet voor scènes.
- Automatiseringsbanen die je in de Clipstart-werkwijze met de hand inschakelt blijven niet behouden als je teruggaat.
- Als de Patroonwerkwijze is gekozen, wordt niets van het raster geladen.

## Gerelateerd

- [Playlist](/wiki/playlist/)
- [Overstappen van Ableton Live](/wiki/coming-from-ableton/)
- [Projecten importeren](/wiki/importing-projects/)
- [Wat alleen LYNX doet: workflows](/why/#workflows)

Het derde item is één venster voor een eerste nummer. Zie [Eenvoudige lay-out](/wiki/simple-layout/).
