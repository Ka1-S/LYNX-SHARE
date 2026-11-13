---
source: 094cbefd18
---
![Het venster Opname vraagt wat er opgenomen moet worden: audio, noten en automatisering, alles, of audio in de audio-editor](/assets/img/wiki/record-dialog.webp)

De opnameknop in het transport, of de toets `R`, zet opnemen aan. De eerste keer vraagt een venster wat je wilt opnemen. Audio gaat als audioclips naar de [Playlist](/wiki/playlist/). Noten gaan in het huidige pattern. De regelaars die je beweegt worden automatisering.

## Een microfoon of instrument opnemen

1. Open `Opties › Audio-instellingen...` en kies een ingangsapparaat.
2. Open de [Mixer](/wiki/mixer/) (`F9`) en selecteer een lege insert.
3. Klik op het ingangsvak boven de effectslots van de insert en kies een ingang. Stereoparen staan eerst, losse kanalen zijn gemarkeerd met `(mono)`.
4. Klik op de opname-armknop naast het ingangsvak.
5. Druk op `R` en kies `Audio, in de Playlist als audioclip`.
6. Druk op afspelen als het afspelen niet vanzelf begon, en speel.
7. Druk op stop. De take is een audioclip op de Playlist.

`Shift+R` doet stap 4 tot 6 met één toets: het armt de geselecteerde insert, neemt alleen audio op en start het afspelen. Druk er nogmaals op om te stoppen.

Wanneer je een ingang kiest voor een insert die meer opneemt dan alleen die ingang (andere kanalen die er naartoe sturen worden ook opgenomen), vraagt LYNX SHARE, zoals FL Studio doet, of hij moet omschakelen naar `Alleen externe ingang`. Antwoord `Ja` om de ingang alleen op die insert op te nemen.

## Audio-opname met één klik

`Extra › Audio opnemen met één klik` start een take in één stap, zoals in FL Studio. Het werkt op de geselecteerde mixerinsert. Met de Master of niets geselecteerd neemt het de eerste insert met een ingang, of anders een vrije insert.

| Commando | Wat het doet |
|---|---|
| `In de Edison-audio-editor/-recorder` | Zet Edison op de insert, ingesteld om op te nemen wanneer het afspelen begint, en start het afspelen. Zie [Mixer](/wiki/mixer/) |
| `In de playlist als audioclip` | Schakelt over naar Song-modus, armeert de insert, vraagt om zijn ingang als hij er geen heeft, en start opname en afspelen. Inserts die al zijn gearmeerd nemen op zoals ze zijn |

## Wat het venster Opname biedt

| Keuze | Wat het doet |
|---|---|
| `Audio, in de Playlist als audioclip` | Armt de geselecteerde mixer-insert en vraagt om de ingang als hij er geen heeft |
| `Noten en automatisering` | Neemt noten op van een MIDI-keyboard of het typetoetsenbord, en de regelaars die je beweegt |
| `Alles` | Noten, automatisering en audio van de gearmde inserts |
| `Audio, in de audio-editor` | Neemt de ingang op in een nieuwe audio-editor terwijl het nummer speelt |

Vink `Niet meer vragen` aan om je keuze te houden. Klik daarna met rechts op de opnameknop voor het opnamefilter: `Automatisering`, `Noten`, `Audio`, `Clips`, en `Vragen wat er opgenomen moet worden wanneer de opname begint` om het venster terug te halen.

## Het ingangsvak van een insert

![De Mixer met insert 3 gearmd en zijn ingangsvak ingesteld op ingangen 1 en 2](/assets/img/wiki/mixer-input.webp)

| Instelling | Wat het doet |
|---|---|
| `Bron audio-ingang` | De apparaatingang die deze insert afspeelt en opneemt, of `(geen)` |
| `Opnemen` | Waar de take wordt afgetapt: `Alleen externe ingang`, `Externe en mixeringang`, `Na de effecten`, `Na de EQ`, `Na niveau en panning` of `Na de track` |
| `Ook de droge ingang als tweede take opnemen (LYNX)` | Bewaart de onbewerkte ingang naast een bewerkte take |
| `Externe ingang monitoren` | `Uit`, `Indien gearmd`, `Auto (indien gearmd, niet tijdens afspelen)` of `Aan` |
| `Record safe (kan niet worden gearmd)` | Beschermt de insert ertegen per ongeluk gearmd te worden |

## Noten opnemen

Noten komen van een MIDI-keyboard, het typetoetsenbord (`Ctrl+T`) of het aanraaktoetsenbord, en gaan naar de geselecteerde kanalen in het huidige pattern. Zie [MIDI-controllers](/wiki/midi-controllers/).

- `Opgenomen noten mengen` (`Ctrl+B`) kiest tussen overdub en vervangen. Aan: nieuwe noten voegen zich bij de noten van het pattern. Uit: de oude noten van een opgenomen kanaal worden gewist.
- `Opgenomen noten kwantiseren op de globale snap` staat in de opname-opties.
- `Stapinvoer` (`Ctrl+E`) werkt als het transport stilstaat: elke noot die je speelt gaat een snapstap na de andere in het pattern.
- `MIDI vangen` (`Ctrl+Shift+Alt+R`) zet de noten die je net speelde in het pattern, ook al stond opnemen uit.
- Het sustainpedaal (CC 64) en het sostenutopedaal (CC 66) houden de noten vast die je speelt, en een opgenomen noot duurt tot het pedaal omhoogkomt. Een Layer-kanaal geeft het pedaal door aan zijn kanalen. Met `Voetpedaal bepaalt het einde van noten` uit in `Opties › MIDI-instellingen...` gaat het pedaal als controller naar de plugins.
- In Song-modus blijven noten die je opneemt in een patternclip die langer is dan zijn pattern staan waar je ze speelde, en het pattern groeit mee. De take wordt niet teruggevouwen over de eerste maten van het pattern. Met `Uitgerekte patroonclips herhalen hun patroon (LYNX)` aan komen alleen noten die je speelt over maten waar al noten stonden in de loop.

Regelaars die je tijdens het opnemen beweegt, worden in Song-modus als automation clips geschreven en in Pattern-modus als pattern-events. Zie [Automation](/wiki/automation/).

## Opname-opties

De opties staan in `Opties › Opname` en in het rechtsklikmenu van de opnameknop.

| Opdracht | Toets | Wat het doet |
|---|---|---|
| `Aftellen` | `Ctrl+P` | Uit, 1 maat, 2 maten of 4 maten metronoom voordat de opname begint. `Ctrl+P` schakelt tussen uit en de laatst gekozen lengte. De keuze blijft bewaard wanneer je het programma sluit; een nieuwe installatie start met 1 maat, zoals FL Studio |
| `Metronoom` | `Ctrl+M` | Klik tijdens het afspelen |
| `Wachten op de eerste noot om te starten` | `Ctrl+I` | De opname wacht tot je speelt |
| `Loop-opname` | none | De opname loopt in een loop in plaats van door te lopen |
| `Audio: punch in/out op de loopregio` | none | Speelt door de loopregio en houdt alleen de audio erbinnen |
| `Audio: pre-roll` en `post-roll` | none | Uit, 1, 2 of 4 maten gespeeld vóór de punch-in en na de punch-out |
| `Opnemen start het afspelen` | none | Opnemen aanzetten drukt ook op afspelen |
| `Inserts ontwapenen wanneer de opname stopt` | none | Gearmde inserts worden na elke take losgelaten |

`Lage latentie tijdens opnemen` staat in het rechtsklikmenu van de opnameknop en in het insertmenu van de Mixer onder `Opnemen naar schijf`. Het staat standaard uit. Zolang een insert met een ingang is scherpgesteld, of zijn monitoring `Aan` staat, worden de effecten die latentie toevoegen op zijn weg naar de Master gebypasst. Dit verandert alleen wat je hoort: een insert houdt al zijn effecten als de take erachter wordt afgetapt, bijvoorbeeld met `Na de effecten`. De effecten komen terug als de insert niet meer is scherpgesteld.

`Latentiecompensatie` zit in hetzelfde menu `Opnemen naar schijf` en staat standaard aan, zoals in FL Studio. Het verschuift elke take terug met de round trip van het audioapparaat, de waarde `Fijnafstelling (samples)` van de audio-instellingen en de vertragingscompensatie van de plugins. Zet het uit om de takes te houden waar ze zijn opgenomen.

`Extra › Opname annuleren` gooit weg wat de huidige opnamedoorgang schreef (de noten, automatisering en audiotakes) en zet opnemen uit. Het afspelen gaat door en de scherpgestelde inserts blijven scherpgesteld.

## Opnemen in clipslots

In de [Clip-launch-werkwijze](/wiki/layouts/) kunnen takes in het clipraster in plaats van de Playlist, zoals in Ableton Live.

- Armeer een track met zijn arm-knop in de Sessiemixer. Zijn lege slots tonen dan een ronde opnameknop in plaats van het stopvierkant. Klik erop om in dat slot op te nemen.
- `Sessie-opname`, onder het raster, neemt de gearmeerde tracks op in de geselecteerde scene. Terwijl hun clips spelen, worden nieuwe noten eraan toegevoegd (overdub).
- `Vaste opnamelengte`, in het menu van een leeg slot, beëindigt een opname in een slot na 1 of meer maten, en de clip speelt door. `Uit` neemt op tot je stopt.

## Loopopname en takes

Met `Loop-opname` aan loopt de opname over de loopregio in Song-modus, of over het pattern in Pattern-modus. Met uit loopt de opname gewoon door.

Elke doorgang van een audioloopopname komt op de volgende vrije Playlist-track, en de tracks vormen een groep. `Opgenomen noten mengen` bepaalt wat er speelt nadat je stopt: uit mute elke doorgang, aan speelt ze allemaal. Klik met rechts op een track van de groep en kies `Comp maken van deze takes` om uit de doorgangen één take te bouwen. `Na loopopname speelt alleen de laatste doorgang` in de opname-opties gaat terug naar één track.

## Waar opnames worden opgeslagen

- Een opgeslagen project bewaart zijn takes in `Audio/Recorded`, naast het projectbestand.
- Een niet-opgeslagen project neemt op in `LYNX/Recordings` in je map Muziek.
- Takes zijn WAV-bestanden met de samplefrequentie van het audioapparaat, 16-bit, 24-bit of 32-bit float. Het formaat (`Bestandsformaat`) en de namen van de takes stel je in onder `Opname` in `Opties › Audio-instellingen...`.
- `Map met opnames openen` staat in het rechtsklikmenu van de opnameknop. `Bestand › Ongebruikte opnames verwijderen en opslaan...` verwijdert takes die het project niet meer gebruikt.

## Goed om te weten

- Een gearmde insert zonder ingang neemt niets op. LYNX SHARE zegt dat en wijst naar het ingangsvak.
- Opnemen weigert te starten als de schijf minder dan 30 seconden ruimte heeft, en waarschuwt onder 10 minuten.
- `Opties › Ingangsaudio opnemen` neemt de eerste twee ingangskanalen op in een nieuwe insert als niets gearmd is.
- Op macOS vraagt het systeem de eerste keer dat je opneemt om toegang tot de microfoon.
- Sluit LYNX SHARE tijdens het opnemen, dan biedt de volgende start aan de herstelde takes op de Playlist te zetten.

## Gerelateerd

- [Audio instellen](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automation](/wiki/automation/)
- [MIDI-controllers](/wiki/midi-controllers/)
- [Probleemoplossing](/wiki/troubleshooting/)
