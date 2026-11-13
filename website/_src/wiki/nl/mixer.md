---
source: b6e9ef0dd0
---
![De Mixer](/assets/img/mixer.webp)

Elk kanaal speelt via een Mixer-insert. Elke insert heeft een fader, pan, mute en solo, een piekmeter, tien effectslots met elk een dry/wet-hoeveelheid, en een ingebouwde EQ. Alle inserts komen uiteindelijk in de Master tenzij je ze elders naartoe routeert. Druk op `F9` om de Mixer te tonen of te verbergen.

Een project kan tot 127 inserts hebben. De Mixer heeft een toevoegstrook voor een nieuwe. Het vak in de titelbalk van de Mixer kiest een van acht striplayouts, van `Compact` tot `Extra groot`.

## Een effect op een kanaal zetten

1. Selecteer in het [Channel Rack](/wiki/channel-rack/) het kanaal en druk op `Ctrl+L`. Het kanaal gaat naar een vrije insert, die zijn naam en kleur overneemt.
2. Druk op `F9` om de Mixer te tonen en klik op de strip van die insert.
3. Klik in het effectenpaneel op een leeg slot en kies een effect.
4. Klik op de naam van het slot om het venster van het effect te tonen of te verbergen.
5. Klik op het lampje van het slot om het effect te bypassen en te vergelijken.

## Een kanaal naar een insert sturen

Sleep in het Channel Rack het routevak van het kanaal (of gebruik er het wiel op) om het insertnummer te kiezen. Dubbelklikken op het vak toont die insert in de Mixer.

`Ctrl+L` in het Channel Rack werkt op meerdere geselecteerde kanalen tegelijk. Elk gaat naar zijn eigen vrije insert.

Met de focus op de Mixer koppelt `Ctrl+L` de geselecteerde kanalen aan de geselecteerde insert, en `Ctrl+Shift+L` koppelt ze aan opeenvolgende inserts vanaf die insert.

## Inserts selecteren en verplaatsen

- Klik op een strip om hem te selecteren.
- `Ctrl`+klik, of slepen over strips, selecteert een bereik. `Ctrl+Shift`+klik voegt er een toe of haalt er een weg.
- Sleep een strip opzij, of druk op `Alt+Left` en `Alt+Right`, om de geselecteerde inserts te verplaatsen.
- Klik met rechts op een strip voor het insertmenu. `F2` hernoemt en kleurt de geselecteerde insert.
- `Del`, of `Delete` in het insertmenu, verwijdert de geselecteerde inserts. LYNX SHARE vraagt eerst om bevestiging. Vink `Mijn keuze onthouden` aan om de vraag de volgende keer over te slaan.

Inserts verplaatsen, invoegen of verwijderen laat sends, sidechains en automatisering bij hun insert.

In het onderdeel `Mixer` van het insertmenu en in `Opties › Mixer` staan nog twee commando's. `Inserts ordenen zoals de Playlist` zet de inserts die aan Playlist-tracks zijn gekoppeld vooraan, in de volgorde van de Playlist. Met `Koppelingen naar verwijderde inserts grijs bewaren (LYNX)` aan blijven de sends die naar een verwijderde insert gingen, grijs, staan, zodat je ze opnieuw kunt verbinden.

## Mute en solo

Klik op het mutelampje om een insert te muten. Klik met rechts of `Ctrl`+klik erop voor solo. `S` zet de geselecteerde inserts solo, en `Alt+S` zet ze solo samen met de inserts die ze voeden.

Een gemute insert houdt zijn meter in beweging. In nieuwe projecten en in geïmporteerde FL Studio-projecten blijft hij ook zijn effecten draaien, en alleen zijn uitgang naar de Master en zijn sends worden afgesneden. Om te voorkomen dat een gemute insert nog rekent, zet je `Gedempte inserts verwerken niets (LYNX, bespaart CPU; dit project)` aan in het onderdeel `Mixer` van het insertmenu of in `Opties › Mixer`. Zijn meter stopt dan en zijn effecten beginnen schoon als je de mute opheft.

## Effecten

Het effectenpaneel toont de tien slots van de geselecteerde insert.

- Sleep een slot, of gebruik het wiel erboven, om het omhoog of omlaag te verplaatsen.
- Klik met rechts op een slot voor zijn opties (vervangen, verplaatsen, verwijderen).
- Zolang het menu van een slot open is, verwijdert `Del` de plugin in dat slot.
- `Groeperen in audio-effectrack` in het menu van het slot zet het effect in een nieuw rack, en `Alles groeperen in audio-effectrack` doet dat voor de hele keten. `Groep opheffen` zet de effecten van een rack met één keten terug in de slots.
- De schakelaar in de kop van het paneel zet alle effecten van de insert aan of uit.
- Je kunt een effect uit de Plugindatabase van de [Browser](/wiki/browser/) naar een insert of een effectslot slepen.
- `Heldere stem` in het insertmenu zet een kant-en-klare ketting voor het opschonen van zang op de insert.

## Edison in een slot

Edison, de audio-editor en recorder, kan in elk effectslot zitten, meerdere per insert. Zijn venster is de audio-editor met een rij opnameregelaars.

- `Audio-editor openen` (`Ctrl+E`) in het insertmenu laadt Edison in het eerste lege slot van de insert en opent hem. `Audio-editor openen en opnemen` (`Shift+E`) laadt hem klaar om op te nemen wanneer de song speelt.
- De opnamemodi zijn `Nu`, `Bij ingang` (de standaard: hij wacht tot de ingang een drempel passeert), `Ingang` en `Bij afspelen`. `Toevoegen` zet een nieuwe opname na de audio die er al staat. De maximale opnameduur is `Onbeperkt` of 1, 3, 5, 10, 30 of 60 minuten: een nieuw slot start op 30, `Audio-editor openen en opnemen` zet 5.
- `Sync` speelt de audio van Edison op de tijd van de song terwijl de song speelt.
- Edison blijft opnemen nadat je zijn venster sluit.
- `Extra › Macro's › Alle Edison-instanties verwijderen` leegt na een vraag elk Edison-slot in het project.
- Edison-slots in FL Studio-projecten (.flp) openen met hun audio.

## Insert-EQ

Elke insert heeft zijn eigen EQ. Sleep een band om frequentie en niveau in te stellen, gebruik het wiel voor de breedte en dubbelklik op een band om hem terug te zetten op 0 dB. Klik met rechts op de titel van de EQ om hem te bypassen. Wil je een Mixer zonder ingebouwde EQ, dan kunnen nieuwe projecten met een schone mixer beginnen.

## Routing, sends en sidechain

![De Mixer met de insert Kick geselecteerd, de sends als kabels onder de strips getekend en het effectenpaneel rechts](/assets/img/wiki/mixer-sends.webp)

Selecteer de insert die je wilt routeren. Elke andere strip heeft dan een routeschakelaar:

- Klik op de schakelaar op een andere strip om de geselecteerde insert daarheen te sturen. Slepen na het inschakelen stelt het sendniveau in.
- `Ctrl`+klik routeert alleen naar die insert.
- `Shift`+klik maakt een sidechain-send. `Ctrl+Shift`+klik maakt hem alleen sidechain.
- Rechtsklikken opent dezelfde keuzes als menu.
- Voor een pre-fader send klik je met rechts op de routeschakelaar of de levelknop van de send en vink je `Pre-fader-send (de fader van dit spoor verandert hem niet)` aan. De send is dan gemarkeerd met `PRE`.

Effecten met een zij-ingang, zoals een compressor, hebben een kiezer `Sidechain van` die de inserts toont waarnaar geluisterd kan worden. Een keuze maakt de route voor je aan.

## Groepen en VCA-faders

Mixergroepen vouw je in met hun pijltje (of `Alt`+klik voor alle groepen). Een VCA-fader verandert het niveau van de lidinserts terwijl ze hun eigen faders houden. Maak er een aan via het insertmenu: kies onder Group `VCA › Nieuwe VCA van selectie`.

## Opnemen

Elke insert heeft een opname-armknop. Met een gearmde insert nemen Rec en Play zijn audio-ingang op in een Playlist-clip. `Shift+R` armt de geselecteerde insert en start de opname in één stap. Zie [Opnemen](/wiki/recording/).

Wanneer je een externe ingang kiest voor een insert die ook andere audio opneemt, vraagt LYNX SHARE of hij moet omschakelen naar `Alleen externe ingang`, zodat de take alleen de ingang bevat. `Mixer › Opnemen naar schijf › Latentiecompensatie` in het insertmenu staat standaard aan. Wanneer het uit staat, worden opgenomen takes zonder enige latentiecompensatie geplaatst.

## Toetsen

`Ctrl+L` en `Ctrl+Shift+L` hebben deze betekenis alleen als de Mixer de focus heeft.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| Kanalen koppelen | `Ctrl+L` | Koppelt de geselecteerde kanalen aan de geselecteerde insert |
| Koppelen aan opeenvolgende inserts | `Ctrl+Shift+L` | Koppelt de geselecteerde kanalen aan inserts vanaf de geselecteerde |
| Inserts verplaatsen | `Alt+Left` / `Alt+Right` | Verplaatst de geselecteerde inserts |
| Hernoemen en kleuren | `F2` | Hernoemt en kleurt de geselecteerde insert |
| Solo | `S` | Zet de geselecteerde inserts solo |
| Solo met bronnen | `Alt+S` | Zet de geselecteerde inserts en de inserts die ze voeden solo |
| Inserts verwijderen | `Del` | Verwijdert de geselecteerde inserts, na een vraag |
| Armen en opnemen | `Shift+R` | Armt de geselecteerde insert en start de opname |
| Open audio editor | `Ctrl+E` | Laadt Edison in het eerste lege slot van de insert en opent hem |
| Open audio editor and record | `Shift+E` | Hetzelfde, klaar om op te nemen wanneer de song speelt |

## Goed om te weten

- `Luidheidsmeter naast de Master (LYNX)`, in `Opties › Mixer`, toont de loudness op korte termijn en de geïntegreerde loudness (LUFS) en de true peak van de uitgang van de Master, met een knop `Reset`.
- Een instrumentplugin met meerdere uitgangen kan elke uitgang op een eigen insert laten spelen. Zie [Plugins van derden](/wiki/plugins/).
- `Ctrl+L` doet verschillende dingen in het Channel Rack en in de Mixer. In het rack kiest het vrije inserts. In de Mixer gebruikt het de geselecteerde insert.
- De routeschakelaars verschijnen op de andere strips, niet op de geselecteerde.
- De limiet is 127 inserts per project en tien effectslots per insert.
- De opname van Edison wordt bewaard als WAV-bestand in de map van het project, niet in het projectbestand zelf.
- De Sessiemixer onder het clipraster hoort alleen bij de Clip-launch-werkwijze. Hij toont dezelfde inserts als deze Mixer. Zie [Lay-outs](/wiki/layouts/).

## Gerelateerd

- [Channel Rack](/wiki/channel-rack/)
- [Opnemen](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Plugins van derden](/wiki/plugins/)
- [Instrumenten en effecten](/instruments-and-effects/)
