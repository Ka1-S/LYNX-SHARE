---
source: f6f0977999
---
LYNX SHARE mantiene il flusso di lavoro che conosci: un Channel Rack, i pattern, una Playlist e un Mixer, con gli stessi tasti. Apre direttamente i progetti `.flp` e può riscriverne uno.

LYNX SHARE è un programma indipendente. Non è affiliato a Image-Line, né approvato o sponsorizzato da Image-Line, e non contiene codice di Image-Line. FL Studio è un marchio registrato di Image-Line Software.

## I tuoi primi passi

1. **Scegli il layout.** La prima volta che LYNX SHARE si avvia, una scheda di benvenuto chiede da dove vieni. Clicca `FL Studio`: ottieni il layout a pattern, quello predefinito, con il Channel Rack, il Piano Roll, la Playlist e il Mixer. Salta sulla scheda fa lo stesso.
2. **Apri un progetto.** Apri uno dei tuoi progetti `.flp` (sotto), oppure clicca gli step nel Channel Rack per creare un beat.
3. **Trova i tuoi plugin.** LYNX SHARE scansiona in background le cartelle di plugin più comuni. Per una cartella tua, apri `Opzioni › Gestione plugin...`, clicca `Cartelle dei plugin...`, aggiungila e clicca `Trova nuovi plugin`.

Cosa cambia rispetto a FL Studio:

- I plugin propri di FL Studio suonano su motori ricreati da LYNX SHARE, ciascuno con un livello di qualità (sotto). FL Studio non serve per aprire un `.flp`.
- Le aggiunte proprie di LYNX SHARE restano spente finché non le attivi in `Opzioni › Extra di LYNX...`.
- Altri due layout lavorano sullo stesso progetto: a lancio di clip (una griglia di clip con scene) e semplice (una sola finestra con tracce, loop e registrazione rapida).
- Una [sessione online](/learn/online-session/) mette fino a 4 persone in un solo progetto, dal vivo.

## Aprire un progetto .flp

1. Scegli `File › Apri...` (`Ctrl+O`) e seleziona il `.flp`.
2. LYNX SHARE risolve i plugin e i sample del progetto, poi lo apre. Il titolo mostra "(imported from FL Studio)".
3. Scegli `Aiuto › Last FL Studio import report` per leggere cosa è stato approssimato, tralasciato o non trovato.
4. Scegli `Aiuto › File mancanti...` per vedere i sample e i plugin mancanti.
5. Scegli `File › Salva`. Chiede dove scrivere un file `.lynx`. Il `.flp` stesso non viene mai scritto.

## Cosa corrisponde a cosa

| In FL Studio | In LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Stessi nomi, su `F5`, `F6`, `F7` e `F9` |
| Selettore plugin | `F8` |
| Browser | `Alt+F8` |
| Modalità Pattern / Brano | `L` |
| Annulla | `Ctrl+Z` annulla, o ripete dopo un annullamento. `Ctrl+Alt+Z` annulla all'indietro |
| Salva nuova versione | `Ctrl+Alt+S` |
| Esporta | `Ctrl+R`, MP3 su `Ctrl+Shift+R`, MIDI su `Ctrl+Shift+M` |
| Insert, mandate e sidechain del Mixer | Gli stessi, con 10 slot effetto per insert |
| Fruity Wrapper | I plugin di terze parti sono ospitati direttamente, vedi [Plugin di terze parti](/wiki/plugins/) |
| Generatori ed effetti nativi | Suonano su motori ricreati da LYNX SHARE, vedi sotto |
| File di progetto `.flp` | File di progetto `.lynx` |

`Aiuto › Scorciatoie` elenca ogni tasto.

## Cosa viene importato

- Canali, sample, pattern, note slide e porta, ogni arrangiamento, marker, cambi di indicazione di tempo, automazione e collegamenti ai controller.
- Il Mixer: instradamento, livelli, sidechain, EQ di traccia ed effetti nativi con le loro impostazioni salvate.
- I plugin di terze parti VST3, VST2 e CLAP dentro Fruity Wrapper: sostituiti dallo stesso plugin installato sul tuo computer, con il suo stato salvato.

## I plugin nativi di FL Studio

I generatori e gli effetti propri di FL Studio non vengono caricati. LYNX SHARE li suona su motori che ha ricreato, e ognuno ha un livello di qualità:

| Livello | Significato |
|---|---|
| Exact | Identico bit per bit all'originale in ogni stato testato |
| Near-exact | Identico bit per bit nella maggior parte degli stati, il resto differisce sotto circa -100 dB |
| Close | Lo stesso algoritmo, entro -60 dB dall'originale in quasi tutti gli stati testati |
| Approximate | Le impostazioni salvate sono mappate su un synth o un effetto integrato di LYNX SHARE |
| Placeholder | Lo stato viene mantenuto e il plugin è muto o lascia passare l'audio |

Il livello di ogni motore, e cosa manca ancora a ciascuno, è in [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Vedi anche [Strumenti ed effetti integrati](/wiki/built-in/).

## Cosa richiede la tua installazione di FL Studio

Aprire un `.flp` funziona anche senza FL Studio installato. Un'installazione sullo stesso computer aggiunge due cose:

- I sample di fabbrica di FL Studio e il suo database dei plugin, così sample e plugin si trovano senza una scansione.
- Le forme d'onda, i sample, gli impulsi e le tabelle integrati di cui hanno bisogno alcuni motori (FL Keys, Soundgoodizer, Maximus e gli altri contrassegnati da un pugnale in PLUGINS.md). Vengono letti dalla tua installazione a runtime e non sono distribuiti con LYNX SHARE.

Su Linux, LYNX SHARE cerca un FL Studio per Windows sotto Wine. Su macOS cerca il bundle dell'app, ma non è stato testato.

## Esportare di nuovo in .flp

1. Scegli `File › Esporta › Progetto FL Studio...`.
2. Scegli `Progetto FL Studio (.flp)` oppure `Pacchetto zippato (.zip, con tutti i campioni)`.
3. Scegli cosa succede alle cose che FL Studio non ha: renderizzarle in audio (suona uguale), oppure usare il plugin FL più vicino (resta modificabile, può suonare diverso, e gli strumenti solo LYNX vengono tralasciati).

Il file è un progetto di FL Studio 2026. Sampler, clip audio, generatori ed effetti propri di FL, plugin di terze parti (dentro Fruity Wrapper), pattern, Playlist, Mixer e automazione passano con le loro impostazioni. Un report elenca cosa è passato esattamente e cosa è stato renderizzato, sostituito o tralasciato.

## FL Studio Mobile (.flm)

`File › Apri...` accetta anche progetti `.flm`. L'importazione è parziale.

- Importati: tempo, indicazione di tempo, un insert del Mixer per ogni canale FLM, una traccia della Playlist per corsia, pattern (ogni passaggio di loop come clip), clip audio con offset e pitch, drum kit, DW Sampler e programmi `.dwp` e lo Slicer (sui motori propri di FLM), gli strumenti MiniSynth, Transistor Bass, 3x Osc, GMSynth e SuperSaw, gli effetti di FLM (come effetti Pocket), l'automazione di livello, pan, mute, solo, pitch bend, mandate e parametri degli strumenti, e i set di accordi e il key locker.
- Non importati: gli altri strumenti di FLM, che suonano con il synth integrato.
- I sample vengono cercati accanto al `.flm` e nella copia di FL Studio delle risorse FLM. Un `.dwp` non trovato suona con un sostituto.

## Da sapere

- I pacchetti FLEX sono cifrati e non vengono mai aperti. Un canale FLEX riceve una patch di synth scelta in base al nome del suo preset.
- Quando esporti in `.flp`, i modulatori che seguono la posizione del brano diventano clip di automazione (un LFO sincronizzato diventa l'LFO di clip di FL Studio). Gli inseguitori, gli inviluppi, il keytrack e la velocity vengono lasciati fuori, e il report di esportazione lo dice.
- Il report di importazione elenca cosa è stato approssimato dal synth integrato, cosa non aveva equivalenti, quali plugin sono stati trovati o non sono installati, quali effetti sono stati tralasciati o riprodotti in parte e quanti sample mancano.
- Un effetto di terze parti non installato viene sostituito da un effetto di LYNX SHARE con lo stesso ruolo. L'originale resta nel progetto.

## Correlati

- [Strumenti ed effetti integrati](/wiki/built-in/)
- [Plugin di terze parti](/wiki/plugins/)
- [Formati di file](/wiki/file-formats/)
- [Scorciatoie](/wiki/shortcuts/)
- [Cosa fa solo LYNX SHARE con le impostazioni di FL](/why/#fl-settings)
- [Impara: i tutorial](/learn/)
