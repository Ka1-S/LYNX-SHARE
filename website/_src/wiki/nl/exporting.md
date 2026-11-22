---
source: b05790879f
---
![Het exportvenster met WAV gekozen: bereik, staart en bestanden links, formaat rechts, mixopties eronder](/assets/img/wiki/export.webp)

Alles wat LYNX SHARE verlaat gaat via `Bestand › Exporteren`. De audioformaten (WAV, MP3, OGG, FLAC, AIFF en M4A) delen één exportvenster. MIDI, FL Studio-projecten, Ableton Live-sets, DAWproject-bestanden en gezipte projecten hebben eigen items in hetzelfde menu.

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

De knoppen kiezen het formaat. M4A is er alleen als het systeem een AAC-encoder heeft: op Windows en macOS, niet op Linux. Met `Ook opslaan` schrijf je andere formaten uit dezelfde render, met dezelfde naam.

| Formaat | Opties |
|---|---|
| WAV | `Bitdiepte`: 16 bits, 24 bits of 32 bits (float). `Dither`: `Uit`, `TPDF` of `TPDF + noise shaping` |
| MP3 | `Bitrate`, constant |
| OGG | Bitrate |
| FLAC | `Bitdiepte`, `Compressie` van `0 (snelst)` tot `8 (kleinst)`, `Dither`. Verliesvrij op elk niveau |
| AIFF | `Bitdiepte`: 16 bits of 24 bits. `Dither` |
| M4A | `Bitrate`: 96, 128, 160 of 192 kbps. AAC, geschreven door de encoder van het systeem |

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

Als het nummer secties in de sectiebaan heeft, knipt `Eén bestand per sectie` het bij de secties en noemt elk bestand naar zijn sectie. Bij stems krijgt elke sectie een eigen set.

`Bestand › Exporteren › Alle Playlist-tracks` en `Alle Playlist-trackgroepen` schrijven één bestand per Playlist-track of per groep. In het menu van de Mixer opent `Opnemen naar schijf › Gearmde tracks renderen naar wave-bestand(en)...` (`Alt+R`) het exportvenster alleen voor de gearmde inserts.

## Andere items van het menu Exporteren

| Opdracht | Toets | Wat het doet |
|---|---|---|
| `MIDI-bestand...` | `Ctrl+Shift+M` | Schrijft de noten als standaard MIDI-bestand (`.mid`) |
| `Masteren...` | `Ctrl+Alt+M` | Rendert het project en masterd het op een loudnessdoel in LUFS, op je eigen computer |
| `Gezipt looppakket...` | none | Het project en elke sample die het gebruikt in één `.zip`, als het gezipte looppakket van FL Studio |
| `Projectgegevensbestanden...` | none | Kopieert elk bestand dat het project gebruikt naar een map die je kiest; het project blijft zijn eigen bestanden gebruiken |
| `FL Studio-project...` | none | Een `.flp`, of een gezipt looppakket met elke sample |
| `Ableton Live-set (.als)...` | none | Een set voor Live 12 of Live 11 |
| `DAWproject (.dawproject)...` | none | Een project voor Bitwig Studio, Studio One, Cubase en andere apps die DAWproject lezen |
| `Voor andere apps...` | none | Eén map met stems, MIDI-bestanden, een Live-set, een FL Studio-project, de mixdown en een README, gezipt als je wilt |
| `Projectbotten...` | none | Kanaal- en effectpresets en scores, in een map |

## Exporteren naar FL Studio, Ableton Live en DAWproject

De `.flp` is een FL Studio 2026-project. Samplers, audioclips, de eigen generators en effecten van FL Studio, plugins van derden, patterns, de Playlist, de Mixer en automatisering gaan mee met hun instellingen. Voor dingen die FL Studio niet heeft, kies je tussen `Naar audio renderen (klinkt hetzelfde)` en `De dichtstbijzijnde FL-plugin gebruiken (blijft bewerkbaar, kan anders klinken)`. Bij de tweede keuze worden instrumenten die alleen in LYNX bestaan weggelaten. Modulators die de songpositie volgen worden automatiseringsclips: een gesynchroniseerde LFO wordt de clip-LFO van FL Studio, en steps, willekeurige bronnen en macro's worden punten. Followers, envelopes, keytrack en velocity worden weggelaten. Een rapport toont wat gerenderd, vervangen of weggelaten is. Details staan in [Overstappen van FL Studio](/wiki/coming-from-fl-studio/).

De `.als` bevat een MIDI-track per kanaal, audiotracks, groepen, returns, sends, racks en Drum Racks, VST2- en VST3-plugins met hun staat, markers, en automatisering van volume, pan en tempo. De eigen instrumenten en effecten van LYNX SHARE worden niet omgezet, maar hun tracks en noten blijven behouden. Helix is de uitzondering: dat wordt de Wavetable van Live. Automatisering van effectparameters en routes van insert naar insert vervallen. Details staan in [Overstappen van Ableton Live](/wiki/coming-from-ableton/).

De `.dawproject` bevat tracks, mappen, clips, noten, audio met zijn warpmarkers, automatisering, tempo, markers, de mixer met sends, en VST3-, VST2- en CLAP-plugins met hun staat. De eigen instrumenten en effecten van LYNX SHARE worden zo geschreven dat LYNX SHARE ze weer kan openen. Het clipraster, andere arrangementen en enkele details per noot worden weggelaten, en een rapport somt ze op.

`Bestand › Een kopie opslaan als FL Studio Mobile-project (.flm)...` schrijft een `.flm` voor FL Studio Mobile en kopieert de samples naar een map `<name> Samples` ernaast. Voor het opslaan somt een venster op wat de kopie verliest, zoals bij `Bestand › Een kopie opslaan voor een oudere LYNX...`. De `.flm` bewaart de noten en patterns (FL Studio Mobile heeft één pattern per track), het tempo, de tellen per maat, de toonsoort en de akkoordenset, de niveaus, panning, mute en solo van de mixer en de sends naar bussen, de eigen instrumenten van FL Studio Mobile (MiniSynth, SuperSaw, GMSynth en Transistor Bass, en de MiniSynth en Transistor Bass van FL Studio), drumsamplers als drumkit, Layers als een rack met meerdere instrumenten, elk Pocket-effect (FLM) met zijn mix en audioclips met niveau, panning, toonhoogte en omkering. Ze schrijft ook de DW Sampler (met zijn zones) en 3x Osc, en automatiseringsclips voor mixerniveau en panning, voor de Pocket-effecten (FLM) en voor de eigen waarden van DW Sampler en GMSynth. Andere instrumenten (voorlopig ook de Slicer en de SoundFont Player van FL Studio Mobile) houden hun noten op de MiniSynth van FL Studio Mobile, en effecten zonder FLM-module en automatisering van andere doelen, zoals de eigen knoppen van de synths en plugins van derden, vallen weg; alles staat in de lijst.

## Goed om te weten

- `Dither` is grijs bij MP3, OGG, M4A en 32-bit WAV, en de opties voor tempo en markers zijn grijs bij elk formaat behalve WAV.
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
