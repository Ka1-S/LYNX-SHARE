---
source: 3bf7e0c0ab
---
LYNX SHARE behoudt de workflow die je kent: een Channel Rack, patterns, een Playlist en een Mixer, op dezelfde toetsen. Het opent `.flp`-projecten rechtstreeks en kan er ook een terugschrijven.

LYNX SHARE is een onafhankelijk programma. Het is niet verbonden aan Image-Line, wordt niet door Image-Line goedgekeurd of gesponsord, en bevat geen code van Image-Line. FL Studio is een gedeponeerd handelsmerk van Image-Line Software.

## Een .flp-project openen

1. Kies `Bestand › Openen...` (`Ctrl+O`) en selecteer de `.flp`.
2. LYNX SHARE zoekt de plugins en samples van het project en opent het dan. De titel toont "(imported from FL Studio)".
3. Kies `Help › Last FL Studio import report` om te lezen wat benaderd, weggelaten of niet gevonden is.
4. Kies `Help › Ontbrekende bestanden...` om de ontbrekende samples en plugins te zien.
5. Kies `Bestand › Opslaan`. Het vraagt waar een `.lynx`-bestand moet komen. De `.flp` zelf wordt nooit geschreven.

## Wat waarmee overeenkomt

| In FL Studio | In LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Dezelfde namen, op `F5`, `F6`, `F7` en `F9` |
| Plugin-kiezer | `F8` |
| Browser | `Alt+F8` |
| Pattern- / Song-modus | `L` |
| Ongedaan maken | `Ctrl+Z` maakt ongedaan, of doet opnieuw na een ongedaan maken. `Ctrl+Alt+Z` maakt achterwaarts ongedaan |
| Nieuwe versie opslaan | `Ctrl+Alt+S` |
| Exporteren | `Ctrl+R`, MP3 op `Ctrl+Shift+R`, MIDI op `Ctrl+Shift+M` |
| Mixer-inserts, sends en sidechain | Hetzelfde, met 10 effectslots per insert |
| Fruity Wrapper | Plugins van derden worden rechtstreeks gehost, zie [Plugins van derden](/wiki/plugins/) |
| Native generators en effecten | Spelen op engines die LYNX SHARE nagebouwd heeft, zie hieronder |
| `.flp`-projectbestand | `.lynx`-projectbestand |

`Help › Sneltoetsen` toont elke toets.

## Wat wordt geïmporteerd

- Kanalen, samples, patterns, slide- en porta-noten, elk arrangement, markers, wijzigingen van de maatsoort, automatisering en controllerkoppelingen.
- De Mixer: routing, niveaus, sidechains, track-EQ en native effecten met hun opgeslagen instellingen.
- VST3-, VST2- en CLAP-plugins van derden in Fruity Wrapper: vervangen door dezelfde plugin die op jouw computer staat, met de opgeslagen staat ervan.

## Native plugins van FL Studio

De eigen generators en effecten van FL Studio worden niet geladen. LYNX SHARE speelt ze op engines die het nagebouwd heeft, en elk heeft een kwaliteitsniveau:

| Niveau | Betekenis |
|---|---|
| Exact | Bit-identiek aan het origineel in elke geteste staat |
| Near-exact | Bit-identiek in de meeste staten, de rest wijkt af onder ongeveer -100 dB |
| Close | Hetzelfde algoritme, binnen -60 dB van het origineel in bijna alle geteste staten |
| Approximate | De opgeslagen instellingen worden afgebeeld op een ingebouwde synth of effect van LYNX SHARE |
| Placeholder | De staat blijft bewaard en de plugin is stil of laat audio ongewijzigd door |

Het niveau van elke engine, en wat elk nog mist, staat in [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Zie ook [Ingebouwde instrumenten en effecten](/wiki/built-in/).

## Wat jouw eigen FL Studio-installatie nodig heeft

Een `.flp` openen werkt zonder dat FL Studio is geïnstalleerd. Een installatie op dezelfde computer voegt twee dingen toe:

- De fabrieksamples en de plugindatabase van FL Studio, zodat samples en plugins zonder scan gevonden worden.
- De ingebouwde golfvormen, samples, impulsen en tabellen die sommige engines nodig hebben (FL Keys, Soundgoodizer, Maximus en de andere met een kruisje gemarkeerd in PLUGINS.md). Ze worden tijdens het draaien uit jouw installatie gelezen en worden niet met LYNX SHARE meegeleverd.

Op Linux zoekt LYNX SHARE naar een Windows-FL Studio onder Wine. Op macOS wordt naar de app-bundel gezocht, maar dat is niet getest.

## Terug exporteren naar .flp

1. Kies `Bestand › Exporteren › FL Studio-project...`.
2. Kies `FL Studio-project (.flp)` of `Gezipt looppakket (.zip, met alle samples)`.
3. Kies wat er gebeurt met dingen die FL Studio niet heeft: ze naar audio renderen (klinkt hetzelfde), of de dichtstbijzijnde FL-plugin gebruiken (blijft bewerkbaar, kan anders klinken, en instrumenten die alleen in LYNX bestaan worden weggelaten).

Het bestand is een FL Studio 2026-project. Samplers, audioclips, de eigen generators en effecten van FL, plugins van derden (in Fruity Wrapper), patterns, de Playlist, de Mixer en automatisering gaan mee met hun instellingen. Een rapport toont wat exact is overgegaan en wat is gerenderd, vervangen of weggelaten.

## FL Studio Mobile (.flm)

`Bestand › Openen...` accepteert ook `.flm`-projecten. De import is gedeeltelijk.

- Geïmporteerd: tempo, maatsoort, één mixer-insert per FLM-kanaal, één Playlist-track per lane, patterns (elke looppassage als clip), audioclips met offset en pitch, drumkits, DW Sampler en `.dwp`-programma's en de Slicer (op de eigen engines van FLM), de instrumenten MiniSynth, Transistor Bass, 3x Osc, GMSynth en SuperSaw, de effecten van FLM (als Pocket-effecten), de automatisering van level, pan, mute, solo, pitch bend, sends en instrumentparameters, en de akkoordensets en key locker.
- Niet geïmporteerd: de andere instrumenten van FLM, die de ingebouwde synth spelen.
- Samples worden gezocht naast de `.flm` en in de kopie van FL Studio van de FLM-bronnen. Een `.dwp` die niet gevonden wordt, speelt een vervanger.

## Goed om te weten

- FLEX-packs zijn versleuteld en worden nooit geopend. Een FLEX-kanaal krijgt een synthpatch die op de naam van zijn preset wordt gekozen.
- Modulators worden bij een `.flp`-export weggelaten, en het exportrapport zegt dat.
- Het importrapport toont wat door de ingebouwde synth is benaderd, wat geen equivalent had, welke plugins gevonden zijn of niet geïnstalleerd zijn, welke effecten zijn weggelaten of gedeeltelijk nagebouwd, en hoeveel samples ontbreken.
- Een niet-geïnstalleerd effect van derden wordt vervangen door een LYNX SHARE-effect met dezelfde rol. Het origineel blijft in het project.

## Gerelateerd

- [Ingebouwde instrumenten en effecten](/wiki/built-in/)
- [Plugins van derden](/wiki/plugins/)
- [Bestandsformaten](/wiki/file-formats/)
- [Sneltoetsen](/wiki/shortcuts/)
- [Wat alleen LYNX SHARE met FL-instellingen doet](/why/#fl-settings)
