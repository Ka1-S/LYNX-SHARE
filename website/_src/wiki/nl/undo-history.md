---
source: 9f36c60cc7
---
![Het venster Bewerkgeschiedenis: zes stappen van Start tot Add notes, met Mute: Kick als de huidige](/assets/img/wiki/history.webp)

Elke bewerking van het project is een stap in de geschiedenis van ongedaan maken: een noot die je toevoegt, een clip die je verplaatst, een fader, een routingwijziging, het tempo. `Bewerken › Bewerkgeschiedenis...` (`Ctrl+Alt+H`) toont de stappen als lijst. Los daarvan slaat LYNX SHARE back-upkopieën van het project op terwijl je werkt.

## Terug naar een eerdere stap

1. Kies `Bewerken › Bewerkgeschiedenis...`, of druk op `Ctrl+Alt+H`.
2. De lijst toont de stappen met de nieuwste onderaan. De eerste rij, `Begin`, is het project zoals het werd geopend.
3. Klik op een stap. Het project gaat terug naar de staat direct erna en de latere stappen worden grijs.
4. Klik op een grijze stap om weer vooruit te gaan.
5. Maak een nieuwe bewerking om verder te gaan vanaf de stap die je koos. De grijze stappen zijn dan weg.

## De toetsen voor ongedaan maken

`Ctrl+Z` werkt niet zoals in de meeste programma's. Het maakt de laatste stap ongedaan en, nogmaals ingedrukt, doet het die opnieuw, zodat het wisselt tussen de laatste twee staten. Gebruik `Ctrl+Alt+Z` om verder terug te gaan.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| `Ongedaan maken` | `Ctrl+Z` | Maakt de laatste stap ongedaan. Na een ongedaan maken doet het de volgende stap opnieuw, en het menu-item luidt `Ongedaan maken (opnieuw)` |
| `Achterwaarts ongedaan maken` | `Ctrl+Alt+Z` | Gaat altijd terug, één stap per druk |
| `Opnieuw` | `Ctrl+Y` of `Ctrl+Shift+Z` | Gaat vooruit |
| `Bewerkgeschiedenis...` | `Ctrl+Alt+H` | Opent de lijst |

Na elk ongedaan maken of opnieuw doen noemt de hintbalk de stap en toont het niveau, bijvoorbeeld `Level 2/3`.

Heb je liever het gebruikelijke gedrag, zet dan `Alternatieve modus voor ongedaan maken (klassiek ongedaan maken, in plaats van ongedaan maken/opnieuw op dezelfde sneltoets)` aan in `Opties › Algemene instellingen...`. `Ctrl+Z` gaat dan altijd terug.

## Wat een stap is en wat niet

- Bewerkingen van het project zijn stappen: noten, patterns, clips, kanalen, mixerniveaus en routing, effecten, automatisering, tempo, mutes.
- Een opname is een stap. Een deel samenvoegen in een [online sessie](/wiki/online-sessions/) ook, en `Comp maken van deze takes`.
- Dingen die het project niet veranderen zijn geen stappen: zoomen, scrollen, vensters verplaatsen en de eigen instellingen van het programma.
- Opslaan en exporteren worden niet ongedaan gemaakt. De bestanden die ze schreven blijven.
- De eigen pluginvensters van LYNX SHARE hebben een apart ongedaan maken en opnieuw voor de wijzigingen van die plugin, naast de instellingen A en B.

## Instellingen

Beide staan in `Opties › Algemene instellingen...`.

| Instelling | Wat het doet |
|---|---|
| `Maximaal aantal ongedaan-maakstappen` | Hoeveel stappen er bewaard worden: 25, 50, 100, 200, 500 of 1000. Standaard is 100 |
| `Alternatieve modus voor ongedaan maken` | Maakt van `Ctrl+Z` een klassiek ongedaan maken |

## Back-ups

LYNX SHARE slaat standaard elke 2 minuten een back-upkopie van het geopende project op en bewaart per project de 10 nieuwste. Een back-up is een gewoon `.lynx`-bestand in de map `autosave` binnen de instellingenmap, genoemd naar het project met `(autosaved on ...)` en de datum en tijd.

Elke keer dat je over een bestaand projectbestand heen opslaat, wordt de versie die wordt vervangen gekopieerd naar dezelfde map met `(overwritten on ...)` in de naam.

| Opdracht | Wat het doet |
|---|---|
| `Bestand › Terugzetten naar laatste back-up...` | Vervangt het geopende project door zijn nieuwste back-up, na een vraag. Het projectbestand zelf verandert pas als je opslaat |
| `Bestand › Map voor automatisch opslaan openen` | Toont de back-ups in de bestandsbeheerder |
| `Bestand › Nieuwe versie opslaan` (`Ctrl+Alt+S`) | Slaat op als `Song_2`, `Song_3` enzovoort, naast het huidige bestand |

De sectie `Back-up` van `Opties › Algemene instellingen...` heeft deze instellingen:

| Instelling | Wat het doet |
|---|---|
| `Back-up opslaan` | `Nooit`, of een interval in minuten |
| `Back-ups per project bewaard` | 1, 3, 5, 10, 20 of 50 |
| `Wacht met back-ups zolang het nummer speelt of er op een MIDI-keyboard wordt gespeeld` | Houdt een back-up tegen zodat hij je niet onderbreekt |
| `Langste wachttijd tussen back-ups` | 5 tot 60 minuten. Na deze tijd wordt een back-up gemaakt, ook terwijl je speelt |

Als de laatste sessie onverwacht eindigde, biedt de volgende start aan de nieuwste back-up van die sessie te openen.

## Goed om te weten

- De geschiedenis wordt niet met het project opgeslagen. Een project openen of een nieuw beginnen maakt haar leeg.
- Als de geschiedenis vol is, vallen de oudste stappen weg.
- In een online livesessie brengen ongedaan maken en opnieuw alleen je eigen bewerkingen terug, nooit die van anderen.
- Met `Back-up opslaan` op `Nooit` worden de kopieën die ontstaan als je over een bestand heen opslaat nog steeds bewaard.
- De instellingenmap is `%APPDATA%\LYNX` op Windows, `~/Library/Application Support/LYNX` op macOS en `~/.config/LYNX` op Linux.

## Gerelateerd

- [Sneltoetsen](/wiki/shortcuts/)
- [Instellingen](/wiki/settings/)
- [Bestandsformaten](/wiki/file-formats/)
- [Probleemoplossing](/wiki/troubleshooting/)
- [Wat alleen LYNX SHARE doet: crashveiligheid](/why/#crash)
