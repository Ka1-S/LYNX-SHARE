---
source: a79e43f031
---
LYNX SHARE salva i progetti come file `.lynx`. Apre anche i progetti di altri programmi, carica i comuni file audio ed esporta audio, MIDI e progetti per altri programmi. La tabella elenca ogni formato della 0.1.26 alpha.

Un progetto aperto dal formato di un altro programma è un'importazione. `File › Salva` chiede allora dove scrivere un file `.lynx`, e il file originale non viene mai modificato.

## Aprire un progetto di un altro programma

1. Scegli `File › Apri...` (`Ctrl+O`).
2. Seleziona un file `.flp`, `.als`, `.flm`, `.mid` o `.zip`.
3. Per un `.flp`, scegli `Aiuto › Last FL Studio import report` per leggere cosa è stato approssimato o tralasciato.
4. Scegli `File › Salva` e seleziona dove scrivere il file `.lynx`.

## Formati

| Formato | Apri | Salva / Esporta | Note |
|---|---|---|---|
| `.lynx` | `File › Apri...` | `File › Salva`, con salvataggio automatico | Il formato di progetto di LYNX SHARE (JSON) |
| `.zip` | `File › Apri...` lo estrae in una cartella e apre il progetto al suo interno | `File › Esporta › Progetto con i sample (.zip)...` | Apre anche il pacchetto di loop zippato di FL Studio |
| `.lynxpack` | Si apre dopo un riepilogo. Una parte viene unita al progetto dell'host | `File › Sessione online › Invia progetto...` | Un progetto o una parte inviati in una [sessione online](/wiki/online-sessions/). Le parti con plugin di terze parti viaggiano come render audio più le impostazioni del plugin |
| `.flp` | `File › Apri...` | `File › Esporta › Progetto FL Studio...`, un progetto FL Studio 2026 o un pacchetto di loop zippato | I plugin nativi suonano su motori ricreati con diversi gradi di esattezza. I pacchetti FLEX non vengono mai aperti. In esportazione, gli strumenti e gli effetti solo LYNX vengono renderizzati in audio o sostituiti con il plugin FL più vicino, e i modulatori vengono tralasciati. Vedi [Se vieni da FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `File › Apri...` | No | Progetto di FL Studio Mobile. Parziale: gli strumenti diversi da MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, lo Slicer e DirectWave suonano con il synth integrato |
| `.als` | `File › Apri...`, set di Live 11 e 12 | `File › Esporta › Set di Ableton Live (.als)...`, per Live 12 o Live 11 | In importazione, i dispositivi di Live senza equivalente in LYNX SHARE vengono scartati. In esportazione, gli strumenti e gli effetti propri di LYNX SHARE non vengono convertiti, e l'automazione dei parametri degli effetti e i collegamenti da insert a insert vengono scartati. Vedi [Se vieni da Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `File › Apri...` crea un nuovo progetto. `File › Importa › File MIDI...` e il Piano Roll importano in quello corrente | `File › Esporta › File MIDI...` (`Ctrl+Shift+M`) | |
| `.wav` | Come sample o come clip audio | `File › Esporta › File Wave...`, 16 bit, 24 bit o 32 bit float | Dither, informazioni di tempo, marcatori di loop e marcatori della Playlist opzionali |
| `.mp3` | Come sample o come clip audio | `File › Esporta › File MP3...` | Bitrate costante |
| `.ogg` | Come sample o come clip audio | `File › Esporta › File OGG...` | |
| `.flac` | Come sample o come clip audio | `File › Esporta › File FLAC...`, compressione da 0 a 8 | Senza perdita a ogni livello |
| `.aif`, `.aiff` | Come sample o come clip audio | No | |
| `.wv` | Come sample | No | Sample WavPack, come distribuiti nei pacchetti di FL Studio |
| `.sf2`, `.sf3` | Suonati da LYNX Soundfont | No | SoundFont |
| `.fst` | Dal Browser | No | Preset di un plugin nativo di FL Studio. Solo per i plugin per cui LYNX SHARE ha un motore |
| `.fsc` | Piano Roll ed editor degli eventi | Piano Roll ed editor degli eventi | File di partitura e automazione di FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Carica file di preset...` nel menu dei preset di un plugin ospitato | `Salva preset...` e `Esporta file di preset...` | Preset dei plugin di terze parti. `.fxp` e `.fxb` sono per i plugin VST2 e `.vstpreset` per i plugin VST3, e solo mentre il plugin gira dentro LYNX SHARE |
| File video | `File › Importa › File video...` mette un video nella Playlist | `Scrivi anche un video con questo mix` nella finestra di esportazione | L'importazione usa i decoder di sistema su Windows e macOS, e richiede FFmpeg su Linux. L'esportazione richiede FFmpeg |

## Altre esportazioni

| Comando | Cosa fa |
|---|---|
| Opzione `File` della finestra di esportazione | Stem: un file per ogni insert del Mixer, per ogni canale, o per ogni canale con solo gli strumenti |
| `File › Esporta › Tutte le tracce della Playlist` | Un file per ogni traccia della Playlist |
| `File › Esporta › Tutti i gruppi di tracce della Playlist` | Un file per ogni gruppo di tracce |
| `File › Esporta › Masterizza...` | Renderizza il progetto e lo masterizza a un obiettivo di loudness, sul tuo computer |
| `File › Esporta › Ossa del progetto...` | Scrive in una cartella preset e partiture di canali ed effetti |

La finestra di esportazione renderizza il brano, il pattern corrente o la selezione della Playlist.

## Dalla riga di comando

Su Windows il programma è `LYNX.exe`. Su macOS e Linux è `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` e `--export-flp` accettano come input anche un `.als`.

## Da sapere

- I file audio più lunghi di 30 secondi vengono letti in streaming da una cache su disco.
- Un `.flp`, `.als` o `.flm` non viene mai scritto quando salvi. I comandi di esportazione scrivono file nuovi.
- I file `.wv` sono di sola lettura.
- Uno `.zip` viene prima estratto in una cartella, poi si apre il progetto al suo interno.

## Correlati

- [Importare progetti](/wiki/importing-projects/)
- [Esportazione](/wiki/exporting/)
- [Riga di comando](/wiki/command-line/)
- [Se vieni da FL Studio](/wiki/coming-from-fl-studio/)
- [Se vieni da Ableton Live](/wiki/coming-from-ableton/)
