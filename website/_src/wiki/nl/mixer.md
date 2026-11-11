---
source: b2de12e24d
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

Inserts verplaatsen, invoegen of verwijderen laat sends, sidechains en automatisering bij hun insert.

## Mute en solo

Klik op het mutelampje om een insert te muten. Klik met rechts of `Ctrl`+klik erop voor solo. `S` zet de geselecteerde inserts solo, en `Alt+S` zet ze solo samen met de inserts die ze voeden.

## Effecten

Het effectenpaneel toont de tien slots van de geselecteerde insert.

- Sleep een slot, of gebruik het wiel erboven, om het omhoog of omlaag te verplaatsen.
- Klik met rechts op een slot voor zijn opties (vervangen, verplaatsen, verwijderen).
- `Del` verwijdert de plugin in het geselecteerde slot.
- De schakelaar in de kop van het paneel zet alle effecten van de insert aan of uit.
- Je kunt een effect uit de Plugindatabase van de [Browser](/wiki/browser/) naar een insert of een effectslot slepen.

## Insert-EQ

Elke insert heeft zijn eigen EQ. Sleep een band om frequentie en niveau in te stellen, gebruik het wiel voor de breedte en dubbelklik op een band om hem terug te zetten op 0 dB. Klik met rechts op de titel van de EQ om hem te bypassen. Wil je een Mixer zonder ingebouwde EQ, dan kunnen nieuwe projecten met een schone mixer beginnen.

## Routing, sends en sidechain

![De Mixer met de insert Kick geselecteerd, de sends als kabels onder de strips getekend en het effectenpaneel rechts](/assets/img/wiki/mixer-sends.webp)

Selecteer de insert die je wilt routeren. Elke andere strip heeft dan een routeschakelaar:

- Klik op de schakelaar op een andere strip om de geselecteerde insert daarheen te sturen. Slepen na het inschakelen stelt het sendniveau in.
- `Ctrl`+klik routeert alleen naar die insert.
- `Shift`+klik maakt een sidechain-send. `Ctrl+Shift`+klik maakt hem alleen sidechain.
- Rechtsklikken opent dezelfde keuzes als menu.

Effecten met een zij-ingang, zoals een compressor, hebben een kiezer `Sidechain van` die de inserts toont waarnaar geluisterd kan worden. Een keuze maakt de route voor je aan.

## Groepen en VCA-faders

Mixergroepen vouw je in met hun pijltje (of `Alt`+klik voor alle groepen). Een VCA-fader verandert het niveau van de lidinserts terwijl ze hun eigen faders houden. Maak er een aan via het insertmenu: kies onder Group `VCA › Nieuwe VCA van selectie`.

## Opnemen

Elke insert heeft een opname-armknop. Met een gearmde insert nemen Rec en Play zijn audio-ingang op in een Playlist-clip. `Shift+R` armt de geselecteerde insert en start de opname in één stap. Zie [Opnemen](/wiki/recording/).

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
| Effect verwijderen | `Del` | Verwijdert de plugin in het geselecteerde slot |
| Armen en opnemen | `Shift+R` | Armt de geselecteerde insert en start de opname |

## Goed om te weten

- `Renderen naar audio` (een insert die naar een audioclip wordt gebounced) klinkt ongeveer 5 dB luider dan de insert die het vervangt. Dit is een bekend probleem in de releasenotes van 0.1.25.
- `Ctrl+L` doet verschillende dingen in het Channel Rack en in de Mixer. In het rack kiest het vrije inserts. In de Mixer gebruikt het de geselecteerde insert.
- De routeschakelaars verschijnen op de andere strips, niet op de geselecteerde.
- De limiet is 127 inserts per project en tien effectslots per insert.

## Gerelateerd

- [Channel Rack](/wiki/channel-rack/)
- [Opnemen](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Plugins van derden](/wiki/plugins/)
- [Instrumenten en effecten](/instruments-and-effects/)
