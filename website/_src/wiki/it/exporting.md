---
source: b05790879f
---
![La finestra di esportazione con WAV selezionato: intervallo, coda e file a sinistra, formato a destra, opzioni del mix sotto](/assets/img/wiki/export.webp)

Tutto ciò che esce da LYNX SHARE passa da `File › Esporta`. I formati audio (WAV, MP3, OGG, FLAC, AIFF e M4A) condividono un'unica finestra di esportazione. MIDI, progetti FL Studio, set di Ableton Live, file DAWproject e progetti zippati hanno voci proprie nello stesso menu.

## Esportare il brano come WAV

1. Scegli `File › Esporta › File Wave...`.
2. Lascia `Estensione` su `Brano (Playlist)` e `File` su `Un file (il mix)`.
3. Scegli un `Profondità di bit`: 16 bit, 24 bit o 32 bit (float).
4. Imposta `Coda` su `Lascia il resto`, così riverberi e release si spengono dopo l'ultima battuta.
5. Clicca `Esporta...`, scegli un nome e una cartella e aspetta il render.
6. Alla fine LYNX SHARE ti dice dove è finito il file e il suo livello di picco.

`Ctrl+R` apre la finestra con il formato che hai esportato per ultimo. `Ctrl+Shift+R` la apre su MP3.

## Cosa renderizzare

| Impostazione | Cosa fa |
|---|---|
| `Estensione` | `Brano (Playlist)`, `Pattern corrente` o `Selezione della Playlist` |
| `Riproduci` | Quante volte l'intervallo suona nel file, da `Una volta` a 16 volte |
| `Coda` | `Taglia il resto` si ferma esattamente alla fine. `Lascia il resto` lascia spegnere il suono. `Riavvolgi il resto` mixa la coda all'inizio, per i loop |
| `Coda massima` | 3, 6, 12 o 30 secondi. La coda si ferma prima se il suono si è già spento |
| `File` | Un file, oppure gli stem (vedi sotto) |

## Formato di uscita

I pulsanti scelgono il formato. M4A viene offerto solo dove il sistema ha un encoder AAC: su Windows e macOS, non su Linux. `Salva anche` scrive altri formati dallo stesso render, con lo stesso nome.

| Formato | Opzioni |
|---|---|
| WAV | `Profondità di bit`: 16 bit, 24 bit o 32 bit (float). `Dither`: `Off`, `TPDF` o `TPDF + noise shaping` |
| MP3 | `Bitrate`, costante |
| OGG | Bitrate |
| FLAC | `Profondità di bit`, `Compressione` da `0 (più veloce)` a `8 (più piccolo)`, `Dither`. Senza perdita a ogni livello |
| AIFF | `Profondità di bit`: 16 bit o 24 bit. `Dither` |
| M4A | `Bitrate`: 96, 128, 160 o 192 kbps. AAC, scritto dall'encoder del sistema |

`Frequenza` e `Canali` valgono per ogni formato. `Canali` è `Stereo`, `Mono (entrambi i lati miscelati)`, `Mono (solo sinistro)` o `Mono (solo destro)`.

## Opzioni del mix

| Impostazione | Cosa fa |
|---|---|
| `Normalizza (picco a 0 dB)` | Alza o abbassa tutto il file in modo che il picco più forte raggiunga il fondo scala |
| `Effetti del Master` | Disattivo: gli effetti dell'insert Master sono in bypass |
| `Effetti degli insert` | Disattivo: gli effetti degli altri insert sono in bypass |
| `HQ per tutti i plugin` | I plugin usano la loro qualità di render. Disattivo: l'esportazione usa la qualità di riproduzione |
| `Riscaldamento dei plugin` | I plugin di terze parti elaborano un attimo di silenzio prima che l'esportazione inizi |
| `Disattiva la polifonia massima` | Toglie il limite di voci dei canali, così nessuna nota viene tagliata |
| `Rifila il silenzio della compensazione del ritardo` | Rimuove il silenzio che la compensazione della latenza dei plugin aggiunge all'inizio |
| `Informazioni di tempo`, `Marcatori di loop`, `Marcatori della playlist` | Solo WAV: salvano nel file il tempo, il loop e i marker della Playlist |
| `Mostra i file al termine` | Apre il file manager sul risultato |

## Stem e tracce

![La finestra di esportazione impostata su FLAC con un file per insert del Mixer, nove file in tutto](/assets/img/wiki/export-flac-stems.webp)

`File` ha tre modalità stem:

- `Uno per insert del Mixer (stem)`: un file per ogni insert che riceve suono.
- `Uno per canale (stem)`: un file per ogni canale che suona.
- `Uno per canale, solo strumenti (senza mandate né bus)`.

Spunta `Anche il mix completo` per avere il mix accanto agli stem. La riga in basso mostra quanti file verranno scritti.

Quando il brano ha delle sezioni nella corsia delle sezioni, `Un file per sezione` lo taglia alle sezioni e dà a ogni file il nome della sua sezione. Con gli stem, ogni sezione ha il proprio set.

`File › Esporta › Tutte le tracce della Playlist` e `Tutti i gruppi di tracce della Playlist` scrivono un file per ogni traccia della Playlist o per ogni gruppo. Nel menu del Mixer, `Registrazione su disco › Renderizza le tracce armate in file WAV...` (`Alt+R`) apre la finestra di esportazione solo per gli insert armati.

## Altre voci del menu Esporta

| Comando | Tasto | Cosa fa |
|---|---|---|
| `File MIDI...` | `Ctrl+Shift+M` | Scrive le note come file MIDI standard (`.mid`) |
| `Masterizza...` | `Ctrl+Alt+M` | Renderizza il progetto e lo masterizza a un obiettivo di loudness in LUFS, sul tuo computer |
| `Pacchetto di loop zippato...` | nessuno | Il progetto e ogni sample che usa in un unico `.zip`, come il pacchetto di loop zippato di FL Studio |
| `File di dati del progetto...` | nessuno | Copia tutti i file usati dal progetto in una cartella che scegli; il progetto continua a usare i propri file |
| `Progetto FL Studio...` | nessuno | Un `.flp`, o un pacchetto di loop zippato con ogni sample |
| `Set di Ableton Live (.als)...` | nessuno | Un set per Live 12 o Live 11 |
| `DAWproject (.dawproject)...` | nessuno | Un progetto per Bitwig Studio, Studio One, Cubase e altre app che leggono DAWproject |
| `Per altre app...` | nessuno | Una cartella con stem, file MIDI, un set di Live, un progetto FL Studio, il mixdown e un README, zippata se vuoi |
| `Ossa del progetto...` | nessuno | Preset e punteggi di canali ed effetti, in una cartella |

## Esportare verso FL Studio, Ableton Live e DAWproject

Il `.flp` è un progetto di FL Studio 2026. Sampler, clip audio, generatori ed effetti propri di FL Studio, plugin di terze parti, pattern, Playlist, Mixer e automazione passano con le loro impostazioni. Per le cose che FL Studio non ha, scegli tra `Renderizzarli in audio (suona uguale)` e `Usare il plugin FL più vicino (resta modificabile, può suonare diverso)`. Con la seconda scelta gli strumenti solo LYNX vengono tralasciati. I modulatori che seguono la posizione del brano diventano clip di automazione: un LFO sincronizzato diventa l'LFO di clip di FL Studio, e step, sorgenti casuali e macro diventano punti. Gli inseguitori, gli inviluppi, il keytrack e la velocity vengono tralasciati. Un report elenca cosa è stato renderizzato, sostituito o tralasciato. I dettagli sono in [Se vieni da FL Studio](/wiki/coming-from-fl-studio/).

Il `.als` contiene una traccia MIDI per canale, tracce audio, gruppi, return, mandate, rack e Drum Rack, plugin VST2 e VST3 con il loro stato, marker e automazione di volume, pan e tempo. Gli strumenti e gli effetti propri di LYNX SHARE non vengono convertiti, anche se le loro tracce e le loro note si conservano. Helix è l'eccezione: diventa il Wavetable di Live. L'automazione dei parametri degli effetti e i collegamenti da insert a insert vengono scartati. I dettagli sono in [Se vieni da Ableton Live](/wiki/coming-from-ableton/).

Il `.dawproject` contiene tracce, cartelle, clip, note, audio con i suoi marker di warp, automazione, tempo, marker, il mixer con i send e plugin VST3, VST2 e CLAP con il loro stato. Gli strumenti e gli effetti propri di LYNX SHARE vengono scritti in modo che LYNX SHARE possa riaprirli. La griglia di clip, gli altri arrangiamenti e alcuni dettagli per nota vengono lasciati fuori, e un report li elenca.

`File › Salva una copia come progetto FL Studio Mobile (.flm)...` scrive un `.flm` per FL Studio Mobile e copia i sample in una cartella `<name> Samples` accanto. Prima di salvare, una finestra elenca ciò che la copia perde, come per `File › Salva una copia per un LYNX precedente...`. Il `.flm` mantiene le note e i pattern (FL Studio Mobile ha un pattern per traccia), il tempo, i tempi per battuta, la tonalità e il set di accordi, i livelli, il panning, il mute e il solo del mixer e gli invii ai bus, gli strumenti propri di FL Studio Mobile (MiniSynth, SuperSaw, GMSynth e Transistor Bass, e il MiniSynth e il Transistor Bass di FL Studio), i sampler di batteria come drum kit, i Layer come rack con più strumenti, ogni effetto Pocket (FLM) con il suo mix e le clip audio con livello, panning, intonazione e inversione. Scrive anche il DW Sampler (con le sue zone) e 3x Osc, e clip di automazione del livello e del panning del mixer, degli effetti Pocket (FLM) e dei valori propri del DW Sampler e di GMSynth. Gli altri strumenti (per ora anche lo Slicer e il SoundFont Player di FL Studio Mobile) mantengono le loro note sul MiniSynth di FL Studio Mobile, mentre gli effetti senza modulo FLM e l'automazione di altri parametri, come le manopole proprie dei synth e i plugin di terze parti, restano fuori; tutto è elencato.

## Da sapere

- `Dither` è disattivato per MP3, OGG, M4A e WAV a 32 bit, e le opzioni di tempo e marker sono disattivate per ogni formato tranne WAV.
- Con `Selezione della Playlist` e nessuna selezione nella Playlist, viene renderizzato tutto il brano. La finestra ti avvisa.
- Se l'insert Master è in mute, la finestra avvisa che il file sarà silenzioso.
- `Scrivi anche un video con questo mix` compare solo quando il progetto ha una traccia video, e richiede FFmpeg.
- `Renderizza attraverso l'hardware in tempo reale` compare solo quando il progetto ha un insert hardware attivo. Quell'esportazione dura quanto il brano.
- Render to audio, dove un insert viene bounciato in una clip audio, in questa versione suona circa 5 dB più forte dell'insert che sostituisce.

## Correlati

- [Formati di file](/wiki/file-formats/)
- [Riga di comando](/wiki/command-line/)
- [Se vieni da FL Studio](/wiki/coming-from-fl-studio/)
- [Se vieni da Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
