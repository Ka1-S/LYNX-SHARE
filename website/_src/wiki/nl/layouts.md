---
source: 1c353ba51c
---
![De Clipstart-werkwijze: links een raster van clips en scènes, rechts de Picker en de Playlist](/assets/img/cliplaunch.webp)

`Beeld › Lay-outstijl` heeft in 0.1.25 twee items: `Patroonwerkwijze (standaard)` en `Clipstart-werkwijze`. Dezelfde keuze staat in `Opties › Algemene instellingen`, onder Werkwijze. De keuze blijft bewaard als je het programma sluit.

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

Het slotmenu heeft ook twee opties voor het hele raster: `Sporen teruggeven aan de Playlist wanneer het transport stopt` en `Vervolgacties in het hele raster`.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| Verplaatsen | Pijltoetsen | Selecteert een ander slot |
| Clip lanceren | `Enter` | Lanceert de geselecteerde clip |
| Scène lanceren | `Ctrl+Enter` | Lanceert de geselecteerde scène |
| Clip verwijderen | `Del` | Maakt het geselecteerde slot leeg |

## Ableton Live-sets

Als je een Live-set opent, komen de Session-clips en scènes ervan met hun lanceerinstellingen in het clipraster. `Bestand › Exporteren › Ableton Live-set (.als)` schrijft het raster terug als Session-clips. `Opties › Algemene instellingen` heeft `Sessieclips uit Live-sets`, waar `Playlist, na het arrangement` ze in plaats daarvan op de Playlist zet.

## Terugschakelen

Kies `Beeld › Lay-outstijl › Patroonwerkwijze (standaard)`. De vensterindeling die je in de Patroonwerkwijze had komt terug, en de automatiseringsbanen keren terug naar hun vorige instelling. De clips in het raster blijven in het project.

## Goed om te weten

- Een track die een clip lanceerde blijft in de Playlist stil tot je op `Terug naar Playlist` drukt. De optie `Sporen teruggeven aan de Playlist wanneer het transport stopt` doet dat bij elke stop voor je.
- In 0.1.25 vul je een slot via zijn menu of met een dubbelklik. Je kunt geen clip of bestand naar een slot slepen.
- Vervolgacties bestaan voor clips, niet voor scènes.
- Automatiseringsbanen die je in de Clipstart-werkwijze met de hand inschakelt blijven niet behouden als je teruggaat.
- Als de Patroonwerkwijze is gekozen, wordt niets van het raster geladen.

## Gerelateerd

- [Playlist](/wiki/playlist/)
- [Overstappen van Ableton Live](/wiki/coming-from-ableton/)
- [Projecten importeren](/wiki/importing-projects/)
- [Wat alleen LYNX doet: workflows](/why/#workflows)

Een vereenvoudigde beginnersindeling zit niet in 0.1.25. Zie [Eenvoudige lay-out](/wiki/simple-layout/).
