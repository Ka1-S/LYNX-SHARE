---
source: 532b2eee04
---
`File › Apri...` (`Ctrl+O`) apre i progetti `.lynx` di LYNX SHARE e anche i file `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx` e `.mid`. Un progetto di un altro programma è un'importazione: diventa un nuovo progetto LYNX SHARE non salvato, e il file originale non viene mai scritto.

## Aprire un progetto di un altro programma

1. Scegli `File › Apri...` e seleziona il file `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip` o di modulo tracker (`.mod`, `.xm`, `.s3m`, `.it`).
2. Aspetta mentre LYNX SHARE cerca i plugin e i sample del progetto.
3. Se qualcosa non è stato trovato, si apre la finestra `File mancanti`. Indicale dove sono i tuoi sample, oppure chiudila.
4. La barra del titolo ora mostra da dove viene il progetto, per esempio "(importato da FL Studio)".
5. Scegli `File › Salva`. LYNX SHARE chiede dove scrivere un file `.lynx`.

## Cosa viene portato

| Formato | Convertito | Non convertito |
|---|---|---|
| `.flp` (FL Studio) | Canali, sample, pattern, note slide e porta, ogni arrangiamento, marker, cambi di indicazione di tempo, automazione, collegamenti ai controller, il Mixer con l'instradamento, sidechain, EQ di traccia ed effetti nativi | I pacchetti FLEX sono cifrati e non vengono mai aperti. Un canale FLEX riceve una patch di synth scelta in base al nome del suo preset |
| `.als` (Live 11 e 12) | Tracce MIDI, audio, di gruppo e di ritorno, tempo, indicazione di tempo, locator, il loop, volume, pan, mute, solo e mandate con la loro automazione, clip di sessione e scene, marker di warp, rack e Drum Rack, e molti dispositivi propri di Live (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable e altri) come quelli di LYNX SHARE | I dispositivi di Live senza equivalente in LYNX SHARE (Amp, Cabinet, Beat Repeat, Vocoder e altri) vengono scartati ed elencati |
| `.flm` (FL Studio Mobile) | Tempo, indicazione di tempo, un insert del mixer per canale, una traccia della Playlist per corsia, pattern, clip audio con offset e pitch, drum kit, DW Sampler e programmi `.dwp`, lo Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth e SuperSaw, gli effetti, l'automazione e i set di accordi | Gli altri generatori di FLM suonano con il synth integrato |
| `.dawproject` (Bitwig Studio, Studio One, Cubase e altre app) | Tracce, cartelle, clip, note, audio con i suoi marker di warp, automazione, tempo, marker, il mixer con i send, scene nella griglia di clip e plugin VST3, VST2 e CLAP con il loro stato. L'EQ, il compressore, il gate e il limiter del formato diventano quelli di LYNX SHARE | Gli effetti di note, le espressioni per nota e i dispositivi propri dell'altra app sono elencati nel report di importazione |
| `.band` (GarageBand 10, GarageBand per iOS) | Tracce, regioni MIDI e audio, tempo, indicazione di tempo, tonalità, sezioni come marker, livello, pan e mute, come tracce del flusso semplice. Strumenti Sampler creati in GarageBand per iOS | Le tracce di strumento suonano con il synth e la batteria integrati. Le regioni di Drummer, le cartelle di take e i cambi di tempo vengono elencati. I progetti di GarageBand 11 e precedenti non sono leggibili |
| `.blx` (BandLab) | Tracce, regioni MIDI e audio con i loro loop, tempo, indicazione di tempo, tonalità, volume, pan e mute, automazione di volume e pan, e gli effetti comuni, come tracce del flusso semplice | Le tracce di strumento suonano con il synth e la batteria integrati. Gli altri effetti e il preset Mastering vengono lasciati fuori ed elencati. Testato solo su progetti inventati |
| `.mid` | Note, indicazioni di tempo e ogni cambio di tempo | Un file MIDI non contiene suoni: i canali ricevono il synth di LYNX SHARE o un canale MIDI Out |
| `.mod`, `.xm`, `.s3m`, `.it` (moduli tracker) | Gli strumenti come canali Sampler con i loro sample, i pattern, la lista d'ordine come Playlist, velocità e tempo, volume delle note, pan, ritardo e taglio; i cambi di volume e di pan come eventi del canale (MOD e XM), e slide, vibrato, arpeggi e inviluppi di intonazione come curve di intonazione delle note | Gli altri effetti e gli inviluppi degli strumenti sono conteggiati nel report di importazione. `Vista › Tracker` modifica le note alla maniera di un tracker |
| `.zip` | Il pacchetto di loop zippato di FL Studio, o un progetto esportato con i suoi sample: estratto in una cartella a parte, poi si apre il progetto al suo interno con i suoi sample | |

I plugin di terze parti VST2, VST3 e CLAP in un `.flp` o in un `.dawproject`, e i plugin VST2 e VST3 in un `.als`, si caricano con il loro stato salvato se lo stesso plugin è installato sul tuo computer.

## I plugin propri di FL Studio

I generatori e gli effetti di FL Studio non vengono caricati. LYNX SHARE ne legge le impostazioni salvate e le suona su motori che ha ricreato. Ogni motore ha un livello di qualità, da `Exact` (identico bit per bit all'originale in ogni stato provato) fino a `Placeholder` (le impostazioni vengono conservate, ma il plugin è muto o lascia passare l'audio). I livelli sono spiegati in [Se vieni da FL Studio](/wiki/coming-from-fl-studio/).

Aprire un `.flp` funziona senza FL Studio installato. Alcuni motori hanno bisogno di onde, sample o impulsi che vengono letti dalla tua installazione di FL Studio durante l'esecuzione.

Un `.flp` che usa il "FL Studio Mobile Rack" di FL Studio (il "Save as FLP" di FL Studio Mobile ne scrive uno per ogni traccia di strumento) suona il suo strumento e i suoi effetti sugli stessi motori di un `.flm`, e così anche l'effetto "FL Studio Mobile Rack FX". Un rack con un drum kit o più strumenti suona ancora con il synth integrato.

## Suoni di fabbrica di FL Studio Mobile

I suoni di fabbrica che usa un `.flm` (drum kit, programmi DirectWave, SoundFont, le onde di GMSynth) appartengono a FL Studio Mobile e non vengono mai distribuiti con LYNX SHARE. LYNX SHARE li cerca nella tua copia della cartella `Resources` dell'app: aggiungila in `Impostazioni › File › Risorse di FL Studio Mobile` (funziona anche scegliendo la cartella sopra di essa, o la cartella di un'installazione di FL Studio). L'installazione di FL Studio ne contiene solo una parte, e viene cercata dopo le tue cartelle. I file ancora mancanti sono elencati nella finestra File mancanti.

## Sample mancanti

![La finestra File mancanti nella scheda Samples: file trovati in verde, mancanti in ambra, con Search folder, Locate e Skip](/assets/img/wiki/missing-files.webp)

La scheda `Campioni` elenca ogni sample che non è stato trovato, con il suo percorso salvato e ciò che lo usa.

| Comando | Cosa fa |
|---|---|
| `Cerca nella cartella...` | Cerca in una cartella e nelle sue sottocartelle tutti i sample mancanti |
| `Individua...` | Chiede dove si trova un sample. Anche gli altri trovati nella stessa cartella vengono recuperati |
| `Salta` | Lascia fuori il sample. Il suo canale resta muto |

`Applica` usa i file che sono stati trovati.

La scheda `Trovati automaticamente` elenca i file che LYNX SHARE ha trovato da solo in un'altra cartella. Salva il progetto per conservare le nuove posizioni.

## Plugin mancanti

LYNX SHARE cerca ogni plugin di terze parti per identificatore, poi per file, poi per nome. Un progetto salvato con il VST2 si apre con il VST3 dello stesso prodotto e viceversa, ma con le impostazioni predefinite.

Quando un effetto non è installato, un effetto LYNX SHARE con lo stesso ruolo ne prende lo slot: un reverb per un reverb, un compressore per un compressore.

- Lo slot mostra il nome del plugin originale con un badge LYNX SHARE.
- Dove lo stato salvato si può leggere, le impostazioni vengono tradotte. Altrimenti il sostituto parte da un preset predefinito. La tabella dei prodotti, dei loro sostituti e di cosa viene tradotto per ciascuno è in [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- L'automazione dei parametri dell'originale viene conservata ma non pilota il sostituto.
- Il riferimento e lo stato del plugin originale restano nel progetto. Quando il plugin viene installato, il progetto si apre di nuovo con quello.
- `Bypassa tutti i sostituti LYNX SHARE di questo progetto`, nel menu del clic destro di uno slot sostituito, li disattiva tutti insieme.

L'interruttore è `Sostituisci i plugin mancanti con equivalenti LYNX SHARE` in `Opzioni › Impostazioni generali...`, attivo di default. Se è spento, lo slot di un effetto mancante viene tolto dal mixer. Uno strumento mancante non viene sostituito. Diventa un synth integrato con un preset scelto in base al nome del canale.

Per riavere il plugin vero, installalo, scegli `Opzioni › Cerca plugin`, poi apri di nuovo il progetto.

## File MIDI

![La finestra Importa dati MIDI: tracce, canali, tipo di canale e opzioni](/assets/img/wiki/midi-import-dialog.webp)

`File › Apri...` crea un nuovo progetto da un file MIDI. `File › Importa › File MIDI...` apre prima la finestra `Importa dati MIDI`, dove scegli cosa portare dentro.

| Impostazione | Cosa fa |
|---|---|
| `Quali tracce importare?` | Tutte le tracce, oppure una |
| `Quali canali importare?` | Fai clic su un canale MIDI per escluderlo. Il clic destro tiene solo quello |
| `Tipo di canale` | `Synth di LYNX SHARE` o `MIDI Out` |
| `Inizia un nuovo progetto` | Inizia un progetto vuoto. Spento: aggiunge a quello aperto |
| `Crea un canale per traccia` | Spento: un canale per canale MIDI |
| `Imposta tracce del Mixer per i nuovi canali` | Instrada i nuovi canali a insert del mixer consecutivi |
| `Importa indicazioni di tempo` | L'indicazione di tempo del file diventa quella del progetto |

`Ctrl+M` nel [Piano Roll](/wiki/piano-roll/) importa un file MIDI nel canale corrente.

## Importare tracce da un altro progetto

`File › Importa › Tracce da un altro progetto...` porta le tracce di un altro progetto in quello aperto, senza chiuderlo. Legge tutti i formati che legge `File › Apri...`.

1. Scegli il progetto. Una finestra elenca le sue tracce della Playlist e ciò che contiene ciascuna.
2. Spunta le tracce che vuoi.
3. Scegli se portare anche `Con gli insert del mixer e gli effetti` e `Con l'automazione`, e `Posizionale a` l'inizio del brano o l'inizio della selezione temporale.
4. Clicca per importare. Le nuove tracce, i canali, i pattern e gli insert arrivano come un unico passo, quindi un solo annulla li toglie tutti.

## Da sapere

- `Aiuto › File mancanti...` riapre la finestra più tardi. `Aiuto › Ultimo report di importazione del progetto` elenca ciò che è stato approssimato, lasciato fuori o non installato.
- Le clip di sessione di un set di Live vanno nella griglia di clip del layout a lancio di clip. `Clip di sessione dei set di Live` in `Opzioni › Impostazioni generali...` le mette invece sulla Playlist.

## Correlati

- [Formati di file](/wiki/file-formats/)
- [Se vieni da FL Studio](/wiki/coming-from-fl-studio/)
- [Se vieni da Ableton Live](/wiki/coming-from-ableton/)
- [Plugin](/wiki/plugins/)
- [Esportazione](/wiki/exporting/)
