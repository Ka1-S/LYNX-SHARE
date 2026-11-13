---
source: 094cbefd18
---
![La finestra Registrazione chiede cosa registrare: audio, note e automazione, tutto, oppure audio nell'editor audio](/assets/img/wiki/record-dialog.webp)

Il pulsante di registrazione nel trasporto, o il tasto `R`, attiva la registrazione. La prima volta una finestra chiede cosa vuoi registrare. L'audio va nella [Playlist](/wiki/playlist/) come clip audio. Le note vanno nel pattern corrente. I controlli che muovi diventano automazione.

## Registrare un microfono o uno strumento

1. Apri `Opzioni › Impostazioni audio...` e scegli un dispositivo di ingresso.
2. Apri il [Mixer](/wiki/mixer/) (`F9`) e seleziona un insert vuoto.
3. Clicca il riquadro di ingresso sopra gli slot effetto dell'insert e scegli un ingresso. Le coppie stereo vengono per prime, i canali singoli sono contrassegnati da `(mono)`.
4. Clicca il pulsante di armo della registrazione accanto al riquadro di ingresso.
5. Premi `R` e scegli `Audio, nella Playlist come clip audio`.
6. Premi play se la riproduzione non è partita da sola, e suona.
7. Premi stop. La take è una clip audio nella Playlist.

`Shift+R` esegue i passi da 4 a 6 con un solo tasto: arma l'insert selezionato, registra solo audio e avvia la riproduzione. Premilo di nuovo per fermare.

Quando scegli un ingresso per un insert che registra più di quell'ingresso (vengono registrati anche gli altri canali che gli inviano), LYNX SHARE chiede, come fa FL Studio, se passare a `Solo ingresso esterno`. Rispondi `Sì` per registrare solo l'ingresso su quell'insert.

## Registrazione audio con un clic

`Strumenti › Registrazione audio con un clic` avvia una take in un solo passo, come in FL Studio. Funziona sull'insert del Mixer selezionato. Con il Master o nulla selezionato, prende il primo insert con un ingresso, oppure un insert libero.

| Comando | Cosa fa |
|---|---|
| `Nell'editor/registratore audio Edison` | Mette Edison sull'insert, impostato per registrare quando parte la riproduzione, e avvia la riproduzione. Vedi [Mixer](/wiki/mixer/) |
| `Nella playlist come clip audio` | Passa alla modalità Song, arma l'insert, chiede il suo ingresso se non ne ha, e avvia registrazione e riproduzione. Gli insert già armati registrano così come sono |

## Cosa offre la finestra Registrazione

| Scelta | Cosa fa |
|---|---|
| `Audio, nella Playlist come clip audio` | Arma l'insert del Mixer selezionato e chiede il suo ingresso se non ne ha |
| `Note e automazione` | Registra le note da una tastiera MIDI o dalla tastiera del PC, e i controlli che muovi |
| `Tutto` | Note, automazione e audio dagli insert armati |
| `Audio, nell'editor audio` | Registra l'ingresso in un nuovo editor audio mentre il brano suona |

Spunta `Non chiedere più` per mantenere la scelta. Dopo, clic destro sul pulsante di registrazione per il filtro di registrazione: `Automazione`, `Nota`, `Audio`, `Clip` e `Chiedi cosa registrare all'avvio della registrazione` per riportare la finestra.

## Il riquadro di ingresso di un insert

![Il Mixer con l'insert 3 armato e il suo riquadro di ingresso impostato sugli ingressi 1 e 2](/assets/img/wiki/mixer-input.webp)

| Impostazione | Cosa fa |
|---|---|
| `Sorgente dell'ingresso audio` | L'ingresso del dispositivo che questo insert suona e registra, oppure `(nessuno)` |
| `Registra` | Dove viene prelevata la take: `Solo ingresso esterno`, `Ingresso esterno e del mixer`, `Dopo gli effetti`, `Dopo l'EQ`, `Dopo livello e panning` o `Dopo la traccia` |
| `Registra anche l'ingresso secco come seconda take (LYNX)` | Conserva l'ingresso non elaborato accanto a una take elaborata |
| `Monitora l'ingresso esterno` | `Off`, `Quando armato`, `Auto (quando armato, non durante la riproduzione)` o `On` |
| `Protezione registrazione (non può essere armato)` | Protegge l'insert dall'essere armato per sbaglio |

## Registrare le note

Le note arrivano da una tastiera MIDI, dalla tastiera del PC (`Ctrl+T`) o dalla tastiera touch, e vanno ai canali selezionati nel pattern corrente. Vedi [Controller MIDI](/wiki/midi-controllers/).

- `Unisci note registrate` (`Ctrl+B`) decide tra overdub e sostituzione. Attivo, le nuove note si uniscono a quelle del pattern. Disattivo, le vecchie note di un canale registrato vengono cancellate.
- `Quantizza le note registrate sullo snap globale` è nelle opzioni di registrazione.
- `Inserimento a passi` (`Ctrl+E`) funziona da fermo: ogni nota che suoni entra nel pattern un passo di snap dopo l'altro.
- `Cattura MIDI` (`Ctrl+Shift+Alt+R`) mette nel pattern le note che hai appena suonato, anche se la registrazione era disattivata.
- Il pedale di sustain (CC 64) e il pedale di sostenuto (CC 66) trattengono le note che suoni, e una nota registrata dura finché il pedale non si alza. Un canale Layer passa il pedale ai suoi canali. Con `Il pedale controlla il rilascio delle note` disattivato in `Opzioni › Impostazioni MIDI...`, il pedale arriva invece ai plugin come controller.
- In modalità Song, le note registrate in una clip di pattern più lunga del suo pattern restano dove le hai suonate, e il pattern si allunga. La take non viene ripiegata sulle prime battute del pattern. Con `Le clip di pattern allungate ripetono il loro pattern (LYNX)` attivo, nel loop entrano solo le note suonate sulle battute che avevano già note.

I controlli che muovi durante la registrazione vengono scritti come clip di automazione in modalità Brano e come eventi del pattern in modalità Pattern. Vedi [Automazione](/wiki/automation/).

## Opzioni di registrazione

Le opzioni sono in `Opzioni › Registrazione` e nel menu del clic destro del pulsante di registrazione.

| Comando | Tasto | Cosa fa |
|---|---|---|
| `Conteggio` | `Ctrl+P` | Off, 1 battuta, 2 battute o 4 battute di metronomo prima dell'inizio della registrazione. `Ctrl+P` passa tra off e la durata che hai scelto per ultima. La scelta viene conservata alla chiusura del programma; una nuova installazione parte con 1 battuta, come FL Studio |
| `Metronomo` | `Ctrl+M` | Click durante la riproduzione |
| `Attendi la prima nota per iniziare` | `Ctrl+I` | La registrazione aspetta finché non suoni |
| `Registrazione in loop` | nessuno | La registrazione va in loop invece di proseguire |
| `Audio: punch in/out sulla regione del loop` | nessuno | Suona attraverso la regione del loop e conserva solo l'audio al suo interno |
| `Audio: pre-roll` e `post-roll` | nessuno | Off, 1, 2 o 4 battute suonate prima del punch-in e dopo il punch-out |
| `La registrazione avvia la riproduzione` | nessuno | Attivare la registrazione preme anche play |
| `Disarma gli insert quando la registrazione si ferma` | nessuno | Gli insert armati vengono rilasciati dopo ogni take |

`Bassa latenza in registrazione` si trova nel menu del tasto destro del pulsante di registrazione e nel menu dell'insert del Mixer sotto `Registrazione su disco`. È disattivato per impostazione predefinita. Finché un insert con un ingresso è armato, o il suo monitoraggio è su `On`, gli effetti che aggiungono latenza sul suo percorso verso il Master vengono bypassati. Cambia solo ciò che senti: un insert mantiene tutti i suoi effetti quando la take è prelevata dopo di essi, per esempio con `Dopo gli effetti`. Gli effetti tornano quando l'insert viene disarmato.

`Compensazione della latenza` è nello stesso menu `Registrazione su disco` ed è attivo per impostazione predefinita, come in FL Studio. Sposta ogni take indietro del tempo di andata e ritorno del dispositivo audio, del valore `Regolazione fine (campioni)` delle impostazioni audio e del ritardo di compensazione dei plugin. Disattivalo per tenere le take dove sono state registrate.

`Strumenti › Annulla registrazione` butta via ciò che il passaggio di registrazione corrente ha scritto (le sue note, l'automazione e le take audio) e disattiva la registrazione. La riproduzione continua e gli insert armati restano armati.

## Registrare negli slot delle clip

Nel [flusso Clip-launch](/wiki/layouts/), le take possono andare nella griglia di clip invece che nella Playlist, come in Ableton Live.

- Arma una traccia con il suo pulsante di arm nel Session mixer. I suoi slot vuoti mostrano allora un pulsante di registrazione rotondo al posto del quadrato di stop. Fai clic su uno per registrare in quello slot.
- `Registrazione di sessione`, sotto la griglia, registra le tracce armate nella scena selezionata. Mentre le loro clip suonano, vengono aggiunte nuove note (overdub).
- `Durata di registrazione fissa`, nel menu di uno slot vuoto, termina una registrazione in uno slot dopo 1 o più battute, e la clip continua a suonare. `Off` registra finché non fermi.

## Registrazione in loop e take

Con `Registrazione in loop` attivo, la registrazione fa il loop sulla regione del loop in modalità Brano, o sul pattern in modalità Pattern. Con l'opzione disattivata, la registrazione prosegue dritta.

Ogni passaggio di una registrazione audio in loop finisce sulla successiva traccia libera della Playlist, e le tracce formano un gruppo. `Unisci note registrate` decide cosa suona dopo che ti fermi: disattivo mette in mute ogni passaggio, attivo li suona tutti. Clic destro su una traccia del gruppo e scegli `Crea un comp da queste take` per costruire una sola take dai passaggi. `Dopo la registrazione in loop, suona solo l'ultimo passaggio` nelle opzioni di registrazione torna a una traccia sola.

## Dove vengono salvate le registrazioni

- Un progetto salvato conserva le sue take in `Audio/Recorded`, accanto al file di progetto.
- Un progetto non salvato registra in `LYNX/Recordings` nella tua cartella Musica.
- Le take sono file WAV alla frequenza di campionamento del dispositivo audio, a 16 bit, 24 bit o 32 bit float. Il formato (`Formato del file`) e i nomi delle take si impostano sotto `Registrazione` in `Opzioni › Impostazioni audio...`.
- `Apri cartella delle registrazioni` è nel menu del clic destro del pulsante di registrazione. `File › Elimina registrazioni inutilizzate e salva...` rimuove le take che il progetto non usa più.

## Da sapere

- Un insert armato senza ingresso non registra nulla. LYNX SHARE te lo dice e indica il riquadro di ingresso.
- La registrazione si rifiuta di partire quando il disco ha meno di 30 secondi di spazio, e avvisa sotto i 10 minuti.
- `Opzioni › Registra l'audio in ingresso` registra i primi due canali di ingresso in un nuovo insert quando nulla è armato.
- Su macOS il sistema chiede l'accesso al microfono la prima volta che registri.
- Se LYNX SHARE si chiude durante la registrazione, al successivo avvio viene offerto di mettere nella Playlist le take recuperate.

## Correlati

- [Configurazione audio](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automazione](/wiki/automation/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
