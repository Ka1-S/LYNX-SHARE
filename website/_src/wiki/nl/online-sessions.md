---
source: 7db9a73959
---
Er zijn twee manieren om met anderen aan een project te werken, en beide staan onder `Bestand › Online sessie`. Een pakket is een bestand dat je naar een vriend stuurt, die een deel terugstuurt. Het heeft geen netwerk, account of server nodig. Een livesessie verbindt iedereen via internet door het LYNX SHARE-relay, en elke wijziging bereikt iedereen. Aan één project doen maximaal 4 mensen mee, jou meegerekend.

## Een project naar een vriend sturen

![Project versturen](/assets/img/wiki/session-send.webp)

1. Open het project en kies `Bestand › Online sessie › Project versturen...`.
2. Typ je naam en de naam van je vriend.
3. Kies wat je vriend houdt als de sessie eindigt: `Alleen het eigen deel (aanbevolen)` of `Een volledige kopie van het project`.
4. Sla het `.lynxpack`-bestand op.
5. Stuur het bestand op een manier die je zelf wilt: e-mail, een gedeelde map, een usb-stick.

Het pakket bevat het project, elke sample en opname die het gebruikt, en audiorenders ("stand-ins") van de delen die een plugin van derden gebruiken. Je vriend hoort die delen zonder jouw plugins te hebben.

## Een deel opnemen en terugsturen

De vriend doet dit:

1. Open de `.lynxpack` met `Bestand › Openen...`. Een samenvatting toont wie hem stuurde en welke mixer-inserts en Playlist-tracks van jou zijn. Klik op `Openen`.
2. Voeg je eigen kanalen, patterns en audio toe op jouw inserts en Playlist-tracks. Het materiaal van de host blijft zoals het is.
3. Kies `Bestand › Online sessie › Mijn deel terugsturen...` en stuur het nieuwe pakket naar de host.

Elke gast krijgt een kleur, 20 mixer-inserts en 16 Playlist-tracks.

## Een deel samenvoegen

De host doet dit:

1. Open het project dat verstuurd is.
2. Kies `Bestand › Online sessie › Een deel samenvoegen...` en kies het pakket dat je vriend teruggestuurd heeft.
3. De kanalen, patterns, noten, clips en inserts van je vriend worden in zijn kleur toegevoegd. Aan het jouwe verandert niets.

Samenvoegen is één stap ongedaan maken. Een nieuwer deel van dezelfde vriend vervangt het eerdere.

## Een livesessie starten of eraan deelnemen

![Livesessie starten](/assets/img/wiki/session-start.webp)

De host start de sessie:

1. Open het project en kies `Bestand › Online sessie › Livesessie starten...`.
2. Typ je naam.
3. Kies onder `Gasten mogen bewerken` voor `Alleen de eigen kanalen, patterns, inserts en rijen (aanbevolen)` of `Alles`. Kies onder `Als de sessie eindigt, houden gasten` hun eigen deel of een volledige kopie.
4. Klik op `Begin`, kopieer dan de uitnodigingscode en stuur hem naar maximaal 3 mensen.
5. Als iemand vraagt om deel te nemen, klik je op `Binnenlaten`.

Een gast neemt deel:

1. Kies `Bestand › Online sessie › Deelnemen aan livesessie...`.
2. Typ je naam, plak de uitnodigingscode en klik op `Deelnemen`.
3. Wacht tot de host je binnenlaat. Het project van de host opent.

## Tijdens een livesessie

![Sessiechat](/assets/img/wiki/session-chat.webp)

Het menu `Bestand › Online sessie` heeft deze opdrachten zolang een sessie loopt:

| Opdracht | Wat het doet |
|---|---|
| `Sessiechat...` | Opent de chat. Het menu-item toont het aantal ongelezen berichten |
| `Het transport van de host volgen` | Voor gasten: afspelen, stoppen en positie volgen de host |
| `Mensen en rechten...` | Voor de host: wat elke persoon mag veranderen, en wat elk aan het eind houdt |
| `Deel plugins met gasten...` | Voor de host: welke van je plugins de gasten mogen gebruiken. Het menu-item toont hoeveel er gedeeld worden |
| `Uitnodigingscode tonen...` | Voor de host: toont de code opnieuw, met een knop `Kopiëren` |
| `Livesessie beëindigen` | Voor de host. Iedereen vertrekt en de code werkt niet meer |
| `Livesessie verlaten` | Voor een gast. Jouw deel van het project blijft bij jou |

In `Mensen en rechten...` krijgt elke persoon een van vier rollen: `De eigen kanalen, patterns, inserts en rijen`, `De eigen delen + tempo en projectinstellingen`, `Alles` of `Alleen luisteren (geen bewerkingen)`. Een toestemming uitzetten verwijdert nooit iets.

## Plugins die de anderen niet hebben

Een kanaal of insert waarvan de plugin niet op jouw computer is geïnstalleerd, speelt een stand-in en toont een oranje badge `R` in het Channel Rack en de Mixer.

- In een pakket zijn de stand-ins audiorenders die worden gemaakt als het pakket wordt opgeslagen. Ze volgen clips die je verplaatst, maar niet wijzigingen in de noten van een plugin die je niet hebt. Vraag de eigenaar om een nieuw pakket.
- In een livesessie worden delen die een plugin gebruiken die iemand niet heeft, gerenderd door degene die hem wel heeft en als audio naar de anderen gestreamd.

## Deel je plugins met de gasten

In een livesessie kan de host de gasten plugins laten gebruiken die alleen de host heeft.

De host doet dit:

1. Kies `Bestand › Online sessie › Deel plugins met gasten...`.
2. Vink de plugins aan die je wilt delen. `Zoeken in je plugins` beperkt de lijst. Er wordt niets gedeeld totdat je het aanvinkt.

Een gast doet dit:

1. Open het pluginmenu van een van je kanalen of effectslots. De gedeelde plugins staan bovenaan, onder `On <host>'s computer`.
2. Kies er een. In zijn venster staan de parameters van de plugin als schuifregelaars, met `Parameters zoeken`.
3. Voeg noten toe en beweeg de schuifregelaars. Het venster toont `Updating...` terwijl de computer van de host rendert, daarna `Bijgewerkt`.

- De plugin blijft op de computer van de host en draait onder de licentie van de host. LYNX SHARE stuurt de plugin, zijn bestanden, zijn presets of zijn licentie nooit door.
- De gast hoort een render die na een moment de noten en waarden volgt. Het wordt niet live afgespeeld en de gast krijgt het eigen venster van de plugin niet.
- Zolang de host weg is, speelt de laatste render. Het delen stopt als de sessie eindigt, en de laatste render blijft.
- Een gast met de rol `Alleen luisteren (geen bewerkingen)` kan gedeelde plugins niet gebruiken.
- Bij sommige pluginlicenties mogen anderen de plugin niet bedienen, ook niet op afstand. Controleer elke licentie voordat je deelt.

## Wat er over het netwerk gaat

Pakketten zijn bestanden. Er wordt nergens iets naartoe geüpload.

Een livesessie maakt alleen verbinding zolang je in een sessie zit. Alles gaat via het relay `relay.lynx-share.com`, end-to-end versleuteld met een sleutel die uit de uitnodigingscode komt. Het relay geeft de gegevens door zonder ze te kunnen lezen. Het ziet je IP-adres, wanneer je verbonden was en hoeveel bytes er doorheen gingen. De andere mensen in de sessie zien je naam, je bewerkingen, de samples die jouw deel gebruikt en of je afspeelt of opneemt. Ze zien je IP-adres niet. Details staan in het [privacybeleid](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Goed om te weten

- `Alleen het eigen deel` is een beleefdheid tussen muzikanten, geen kopieerbeveiliging. Het pakket is een gewone zip.
- Beide kanten moeten dezelfde versie van LYNX SHARE gebruiken. De samenvatting van het pakket waarschuwt als ze verschillen.
- Een gast wiens verbinding wegvalt, maakt zelf opnieuw verbinding en houdt zijn bewerkingen.
- Sluit LYNX SHARE van de host tijdens een sessie, dan biedt de volgende start aan om hem te hervatten, en de gasten verbinden zelf opnieuw.
- Als LYNX SHARE van een gast tijdens een sessie sluit, vraagt de volgende start of je weer naar binnen wilt. Met `Weer meedoen` kom je terug als dezelfde persoon, met je deel en alles wat je had veranderd. Dit werkt 12 uur lang.
- De Linux-versie kan sinds 0.1.26 een livesessie hosten.
- In een livesessie brengen ongedaan maken en opnieuw alleen je eigen bewerkingen terug.
- Iedereen met de uitnodigingscode kan vragen om deel te nemen, maar pas nadat de host hem binnenlaat.

## Gerelateerd

- [Wat alleen LYNX SHARE doet: online sessies](/why/#online)
- [Bestandsformaten](/wiki/file-formats/)
- [Plugins van derden](/wiki/plugins/)
- [Ongedaan maken en geschiedenis](/wiki/undo-history/)
- [Probleemoplossing](/wiki/troubleshooting/)
