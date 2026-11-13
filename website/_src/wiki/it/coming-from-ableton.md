---
source: d5cc625221
---
![Il layout a lancio di clip: una griglia di clip con le scene accanto alla Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE parte nel flusso a pattern: un Channel Rack, i pattern e una Playlist. Il flusso a lancio di clip è facoltativo e aggiunge una griglia di clip con le scene accanto alla Playlist. LYNX SHARE legge e scrive i set `.als`. Non è affiliato ad Ableton.

## Aprire un set di Live

1. Scegli `File › Apri...` (`Ctrl+O`) e seleziona il file `.als`. Vengono letti i set salvati da Live 11 e Live 12.
2. Scegli `Vista › Stile del layout › Flusso a lancio di clip` per lavorare con la griglia di clip, dove vanno le clip di sessione e le scene del set.
3. Usa l'interruttore `Grid / Both / Song` nella barra del titolo della Playlist per mostrare la griglia, la Playlist o entrambe.
4. Scegli `File › Salva`. Ti chiede dove scrivere un file `.lynx`. Il file `.als` non viene modificato.

`Vista › Stile del layout › Flusso a pattern (predefinito)` torna indietro. La stessa scelta è in `Opzioni › Impostazioni generali...`. I pattern e il Channel Rack restano disponibili in entrambi i layout.

## Cosa corrisponde a cosa

| In Live | In LYNX SHARE |
|---|---|
| Vista Session | La griglia di clip del flusso a lancio di clip |
| Vista Arrangement | La Playlist |
| Tracce | Le tracce della Playlist, che sono le colonne della griglia di clip |
| Scene | Scene, le righe della griglia di clip. Una scena può impostare il tempo quando viene lanciata |
| Quantizzazione di lancio | `Globale`, `Nessuno`, da 8 battute a 1 battuta, o un valore di nota da 1/2 a 1/32, per clip se vuoi |
| Modalità di lancio | `Trigger`, `Gate`, `Alterna` e `Ripetizioni` |
| Azioni successive | Azione A e Azione B con una probabilità per ciascuna |
| Pulsanti di stop delle clip | Uno slot vuoto può avere un pulsante di stop |
| Torna ad Arrangement | `Torna alla Playlist` |
| Modalità di warp | `Beat`, `Toni`, `Texture` e `Complesso` in `Impostazioni del canale › Time stretch` |
| Simpler, Sampler e drum rack | I sampler di LYNX SHARE |
| Set `.als` | File di progetto `.lynx` |

## La griglia di clip

Uno slot contiene una clip di pattern o una clip audio.

- Una traccia suona una clip alla volta. Una clip lanciata sostituisce quella che sta suonando.
- Un lancio aspetta il prossimo step della quantizzazione di lancio.
- `Legato` fa partire la nuova clip dalla posizione raggiunta da quella sostituita. `Loop` si imposta per clip.
- Le scene si possono inserire, eliminare e rinominare.
- Una traccia che ha lanciato una clip resta muta nella Playlist finché non premi `Torna alla Playlist`. Un'opzione restituisce le tracce quando il trasporto si ferma.

Le azioni successive vengono eseguite dopo un certo numero di battute o di passaggi della clip. Le azioni sono `Nessuna azione`, `Stop`, `Riproduci di nuovo`, `Precedente`, `Succ.`, `Primo`, `Ultimo`, `Qualsiasi` e `Altro`. Un interruttore attiva o disattiva le azioni successive per l'intera griglia.

## Cosa si importa da un .als

- Tracce MIDI, audio, di gruppo e di ritorno, tempo, indicazione di tempo, locator, il loop, impostazioni del mixer (volume, pan, mute, solo, mandate) e la loro automazione.
- Plugin VST2 e VST3 con il loro stato, se lo stesso plugin è installato. Vedi [Plugin di terze parti](/wiki/plugins/).
- Clip di sessione e scene, nella griglia di clip, con modalità di lancio, quantizzazione, legato, loop, azioni successive e pulsanti di stop. Per metterle invece sulla Playlist, una sezione per scena, cambia `Clip di sessione dei set di Live` in `Opzioni › Impostazioni generali...`.
- Marker di warp.

## Esportare un .als

1. Scegli `File › Esporta › Set di Ableton Live (.als)...`.
2. In `Salva per`, scegli `Live 12` o `Live 11 (si apre anche in Live 12)`.
3. In `Campioni`, copiali nella cartella di progetto del set oppure fai riferimento a dove si trovano.
4. Scegli una cartella. Il set viene salvato come `<name> Project/<name>.als`.

| Scritto | Non scritto |
|---|---|
| Una traccia MIDI per canale, con le sue note come clip MIDI | Gli strumenti e gli effetti propri di LYNX SHARE. Le loro tracce e note vengono conservate |
| Tracce audio con stretch, trasposizione, gain, dissolvenze e modalità di warp | L'automazione dei parametri degli effetti |
| Gruppi, ritorni e mandate | I collegamenti da insert a insert |
| Plugin VST2 e VST3 con il loro stato | |
| Marker e automazione di volume, pan e tempo | |
| La griglia di clip come clip di sessione e scene, rack e Drum Rack | |

Un report dopo l'esportazione elenca ciò che è stato lasciato fuori.

## Da sapere

- Molti dispositivi propri di Live arrivano come quelli di LYNX SHARE, con le loro impostazioni e la loro automazione: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift e Operator (senza FM). Gli altri vengono scartati ed elencati nel report di importazione.
- I Rack di strumenti e di effetti audio arrivano con le loro catene, zone e macro, e un Drum Rack diventa il Drum Rack di LYNX SHARE. I MIDI Effect Rack di Live non vengono ancora convertiti e sono elencati nel report di importazione.
- Il report di importazione elenca ciò che è stato lasciato fuori.
- Live taglia le clip audio che si sovrappongono in un set esportato.
- I marker di warp di un set di Live vengono conservati all'importazione e riscritti all'esportazione.

## Correlati

- [Playlist](/wiki/playlist/)
- [Layout](/wiki/layouts/)
- [Importare progetti](/wiki/importing-projects/)
- [Formati di file](/wiki/file-formats/)
- [Due flussi di lavoro in un solo programma](/why/#workflows)
