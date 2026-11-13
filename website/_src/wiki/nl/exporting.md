---
source: da5e41c322
---
![Het exportvenster met WAV gekozen: bereik, staart en bestanden links, formaat rechts, mixopties eronder](/assets/img/wiki/export.webp)

Alles wat LYNX SHARE verlaat gaat via `Bestand › Exporteren`. De audioformaten (WAV, MP3, OGG en FLAC) delen één exportvenster. MIDI, FL Studio-projecten, Ableton Live-sets en gezipte projecten hebben eigen items in hetzelfde menu.

## Het nummer als WAV exporteren

1. Kies `Bestand › Exporteren › Wave-bestand...`.
2. Laat `Bereik` op `Song (Playlist)` staan en `Bestanden` op `Eén bestand (de mix)`.
3. Kies een `Bitdiepte`: 16 bits, 24 bits of 32 bits (float).
4. Zet `Staart` op `Rest laten staan`, zodat reverbs en releases na de laatste maat uitklinken.
5. Klik op `Exporteren...`, kies een naam en een map en wacht op het renderen.
6. Als het klaar is, meldt LYNX SHARE waar het bestand staat en wat het piekniveau is.

`Ctrl+R` opent het venster met het formaat dat je het laatst exporteerde. `Ctrl+Shift+R` opent het op MP3.

## Wat er gerenderd wordt

| Instelling | Wat het doet |
|---|---|
| `Bereik` | `Song (Playlist)`, `Huidig pattern` of `Playlist-selectie` |
| `Afspelen` | Hoe vaak het bereik in het bestand afspeelt, van `Eén keer` tot 16 keer |
| `Staart` | `Rest afknippen` stopt precies aan het einde. `Rest laten staan` laat het geluid uitklinken. `Rest omslaan` mengt de staart door in het begin, voor loops |
| `Langste staart` | 3, 6, 12 of 30 seconden. De staart stopt eerder als het geluid is weggestorven |
| `Bestanden` | Eén bestand, of stems (zie hieronder) |

## Uitvoerformaat

De vier knoppen kiezen het formaat. Met `Ook opslaan` schrijf je andere formaten uit dezelfde render, met dezelfde naam.

| Formaat | Opties |
|---|---|
| WAV | `Bitdiepte`: 16 bits, 24 bits of 32 bits (float). `Dither`: `Uit`, `TPDF` of `TPDF + noise shaping` |
| MP3 | `Bitrate`, constant |
| OGG | Bitrate |
| FLAC | `Bitdiepte`, `Compressie` van `0 (snelst)` tot `8 (kleinst)`, `Dither`. Verliesvrij op elk niveau |

`Samplefrequentie` en `Kanalen` gelden voor elk formaat. `Kanalen` is `Stereo`, `Mono (beide kanten gemixt)`, `Mono (alleen links)` of `Mono (alleen rechts)`.

## Mixopties

| Instelling | Wat het doet |
|---|---|
| `Normaliseren (piek naar 0 dB)` | Verhoogt of verlaagt het hele bestand zodat de luidste piek de volle schaal haalt |
| `Master-effecten` | Uit: de effecten van de Master-insert worden gebypasst |
| `Insert-effecten` | Uit: de effecten van de andere inserts worden gebypasst |
| `HQ voor alle plugins` | Plugins gebruiken hun renderkwaliteit. Uit: de export gebruikt de afspeelkwaliteit |
| `Plugin opwarmen` | Plugins van derden verwerken een moment stilte voordat de export begint |
| `Max. polyfonie uitschakelen` | Heft de stemlimiet van de kanalen op, zodat geen enkele noot wordt afgekapt |
| `PDC-stilte wegknippen` | Verwijdert de stilte die de latentiecompensatie van plugins aan het begin toevoegt |
| `Tempo-informatie`, `Loopmarkeringen`, `Playlistmarkeringen` | Alleen WAV: slaan het tempo, de loop en de markers van de Playlist op in het bestand |
| `Bestand(en) tonen na voltooiing` | Opent de bestandsbeheerder op het resultaat |

## Stems en tracks

![Het exportvenster ingesteld op FLAC met één bestand per mixer-insert, in totaal negen bestanden](/assets/img/wiki/export-flac-stems.webp)

`Bestanden` heeft drie stemmodi:

- `Eén per mixer-insert (stems)`: een bestand voor elke insert die geluid krijgt.
- `Eén per kanaal (stems)`: een bestand voor elk kanaal dat speelt.
- `Eén per kanaal, alleen instrumenten (geen sends of bussen)`.

Vink `Ook de volledige mix` aan om de mix naast de stems te krijgen. De regel onderaan toont hoeveel bestanden er geschreven worden.

`Bestand › Exporteren › Alle Playlist-tracks` en `Alle Playlist-trackgroepen` schrijven één bestand per Playlist-track of per groep. In het menu van de Mixer opent `Opnemen naar schijf › Gearmde tracks renderen naar wave-bestand(en)...` (`Alt+R`) het exportvenster alleen voor de gearmde inserts.

## Andere items van het menu Exporteren

| Opdracht | Toets | Wat het doet |
|---|---|---|
| `MIDI-bestand...` | `Ctrl+Shift+M` | Schrijft de noten als standaard MIDI-bestand (`.mid`) |
| `Masteren...` | `Ctrl+Alt+M` | Rendert het project en masterd het op een loudnessdoel in LUFS, op je eigen computer |
| `Project met samples (.zip)...` | none | Het project en zijn samples in één archief |
| `FL Studio-project...` | none | Een `.flp`, of een gezipt looppakket met elke sample |
| `Ableton Live-set (.als)...` | none | Een set voor Live 12 of Live 11 |
| `Projectbotten...` | none | Kanaal- en effectpresets en scores, in een map |

## Exporteren naar FL Studio en Ableton Live

De `.flp` is een FL Studio 2026-project. Samplers, audioclips, de eigen generators en effecten van FL Studio, plugins van derden, patterns, de Playlist, de Mixer en automatisering gaan mee met hun instellingen. Voor dingen die FL Studio niet heeft, kies je tussen `Naar audio renderen (klinkt hetzelfde)` en `De dichtstbijzijnde FL-plugin gebruiken (blijft bewerkbaar, kan anders klinken)`. Bij de tweede keuze worden instrumenten die alleen in LYNX bestaan weggelaten. Modulators worden altijd weggelaten. Een rapport toont wat gerenderd, vervangen of weggelaten is. Details staan in [Overstappen van FL Studio](/wiki/coming-from-fl-studio/).

De `.als` bevat een MIDI-track per kanaal, audiotracks, groepen, returns, sends, racks en Drum Racks, VST2- en VST3-plugins met hun staat, markers, en automatisering van volume, pan en tempo. De eigen instrumenten en effecten van LYNX SHARE worden niet omgezet, maar hun tracks en noten blijven behouden. Automatisering van effectparameters en routes van insert naar insert vervallen. Details staan in [Overstappen van Ableton Live](/wiki/coming-from-ableton/).

## Goed om te weten

- `Dither` is grijs bij MP3, en de opties voor tempo en markers zijn grijs bij elk formaat behalve WAV.
- Bij `Playlist-selectie` zonder selectie in de Playlist wordt het hele nummer gerenderd. Het venster waarschuwt je.
- Als de Master-insert gemute is, waarschuwt het venster dat het bestand stil zal zijn.
- `Ook een video met deze mix schrijven` verschijnt alleen als het project een videotrack heeft, en het heeft FFmpeg nodig.
- `Realtime via de hardware renderen` verschijnt alleen als het project een actieve hardware-insert heeft. Die export duurt zo lang als het nummer.
- Renderen naar audio, waarbij een insert naar een audioclip wordt gebounced, klinkt in deze versie ongeveer 5 dB luider dan de insert die het vervangt.

## Gerelateerd

- [Bestandsformaten](/wiki/file-formats/)
- [Opdrachtregel](/wiki/command-line/)
- [Overstappen van FL Studio](/wiki/coming-from-fl-studio/)
- [Overstappen van Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
