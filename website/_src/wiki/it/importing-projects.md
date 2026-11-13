---
source: 358890815a
---
`File › Apri...` (`Ctrl+O`) apre i progetti `.lynx` di LYNX SHARE e anche i file `.flp`, `.als`, `.flm` e `.mid`. Un progetto di un altro programma è un'importazione: diventa un nuovo progetto LYNX SHARE non salvato, e il file originale non viene mai scritto.

## Aprire un progetto di un altro programma

1. Scegli `File › Apri...` e seleziona il file `.flp`, `.als`, `.flm` o `.mid`.
2. Aspetta mentre LYNX SHARE cerca i plugin e i sample del progetto.
3. Se qualcosa non è stato trovato, si apre la finestra `File mancanti`. Indicale dove sono i tuoi sample, oppure chiudila.
4. La barra del titolo ora mostra da dove viene il progetto, per esempio "(importato da FL Studio)".
5. Scegli `File › Salva`. LYNX SHARE chiede dove scrivere un file `.lynx`.

## Cosa viene portato

| Formato | Convertito | Non convertito |
|---|---|---|
| `.flp` (FL Studio) | Canali, sample, pattern, note slide e porta, ogni arrangiamento, marker, cambi di indicazione di tempo, automazione, collegamenti ai controller, il Mixer con l'instradamento, sidechain, EQ di traccia ed effetti nativi | I pacchetti FLEX sono cifrati e non vengono mai aperti. Un canale FLEX riceve una patch di synth scelta in base al nome del suo preset |
| `.als` (Live 11 e 12) | Tracce MIDI, audio, di gruppo e di ritorno, tempo, indicazione di tempo, locator, il loop, volume, pan, mute, solo e mandate con la loro automazione, clip di sessione e scene, marker di warp, rack e Drum Rack, e molti dispositivi propri di Live (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift e altri) come quelli di LYNX SHARE | I dispositivi di Live senza equivalente in LYNX SHARE (Hybrid Reverb, Grain Delay, Amp, Roar, patch FM di Operator e altri) vengono scartati ed elencati. I MIDI Effect Rack non vengono convertiti |
| `.flm` (FL Studio Mobile) | Tempo, indicazione di tempo, un insert del mixer per canale, una traccia della Playlist per corsia, pattern, clip audio con offset e pitch, drum kit, DW Sampler e programmi `.dwp`, lo Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth e SuperSaw, gli effetti, l'automazione e i set di accordi | Gli altri generatori di FLM suonano con il synth integrato |
| `.mid` | Note, indicazioni di tempo e ogni cambio di tempo | Un file MIDI non contiene suoni: i canali ricevono il synth di LYNX SHARE o un canale MIDI Out |

I plugin di terze parti VST2, VST3 e CLAP in un `.flp`, e i plugin VST2 e VST3 in un `.als`, si caricano con il loro stato salvato se lo stesso plugin è installato sul tuo computer.

## I plugin propri di FL Studio

I generatori e gli effetti di FL Studio non vengono caricati. LYNX SHARE ne legge le impostazioni salvate e le suona su motori che ha ricreato. Ogni motore ha un livello di qualità, da `Exact` (identico bit per bit all'originale in ogni stato provato) fino a `Placeholder` (le impostazioni vengono conservate, ma il plugin è muto o lascia passare l'audio). I livelli sono spiegati in [Se vieni da FL Studio](/wiki/coming-from-fl-studio/).

Aprire un `.flp` funziona senza FL Studio installato. Alcuni motori hanno bisogno di onde, sample o impulsi che vengono letti dalla tua installazione di FL Studio durante l'esecuzione.

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

## Da sapere

- `Aiuto › File mancanti...` riapre la finestra più tardi. `Aiuto › Last FL Studio import report` elenca ciò che è stato approssimato, lasciato fuori o non installato.
- Le clip di sessione di un set di Live vanno nella griglia di clip del layout a lancio di clip. `Clip di sessione dei set di Live` in `Opzioni › Impostazioni generali...` le mette invece sulla Playlist.

## Correlati

- [Formati di file](/wiki/file-formats/)
- [Se vieni da FL Studio](/wiki/coming-from-fl-studio/)
- [Se vieni da Ableton Live](/wiki/coming-from-ableton/)
- [Plugin](/wiki/plugins/)
- [Esportazione](/wiki/exporting/)
