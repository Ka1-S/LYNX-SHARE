---
source: 2e33c00ef4
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

## Cosa offre la finestra Registrazione

| Scelta | Cosa fa |
|---|---|
| `Audio, nella Playlist come clip audio` | Arma l'insert del Mixer selezionato e chiede il suo ingresso se non ne ha |
| `Note e automazione` | Registra le note da una tastiera MIDI o dalla tastiera del PC, e i controlli che muovi |
| `Tutto` | Note, automazione e audio dagli insert armati |
| `Audio, nell'editor audio` | Registra l'ingresso in un nuovo editor audio mentre il brano suona |

Spunta `Non chiedere più` per mantenere la scelta. Dopo, clic destro sul pulsante di registrazione per il filtro di registrazione: `Automazione`, `Note`, `Audio`, `Clip` e `Chiedi cosa registrare all'avvio della registrazione` per riportare la finestra.

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

I controlli che muovi durante la registrazione vengono scritti come clip di automazione in modalità Brano e come eventi del pattern in modalità Pattern. Vedi [Automazione](/wiki/automation/).

## Opzioni di registrazione

Le opzioni sono in `Opzioni › Registrazione` e nel menu del clic destro del pulsante di registrazione.

| Comando | Tasto | Cosa fa |
|---|---|---|
| `Conteggio` | `Ctrl+P` | Off, 1 battuta, 2 battute o 4 battute di metronomo prima dell'inizio della registrazione |
| `Metronomo` | `Ctrl+M` | Click durante la riproduzione |
| `Attendi la prima nota per iniziare` | `Ctrl+I` | La registrazione aspetta finché non suoni |
| `Registrazione in loop` | nessuno | La registrazione va in loop invece di proseguire |
| `Audio: punch in/out sulla regione del loop` | nessuno | Suona attraverso la regione del loop e conserva solo l'audio al suo interno |
| `Audio: pre-roll` e `post-roll` | nessuno | Off, 1, 2 o 4 battute suonate prima del punch-in e dopo il punch-out |
| `La registrazione avvia la riproduzione` | nessuno | Attivare la registrazione preme anche play |
| `Disarma gli insert quando la registrazione si ferma` | nessuno | Gli insert armati vengono rilasciati dopo ogni take |

## Registrazione in loop e take

Con `Registrazione in loop` attivo, la registrazione fa il loop sulla regione del loop in modalità Brano, o sul pattern in modalità Pattern. Con l'opzione disattivata, la registrazione prosegue dritta.

Ogni passaggio di una registrazione audio in loop finisce sulla successiva traccia libera della Playlist, e le tracce formano un gruppo. `Unisci note registrate` decide cosa suona dopo che ti fermi: disattivo mette in mute ogni passaggio, attivo li suona tutti. Clic destro su una traccia del gruppo e scegli `Crea un comp da queste take` per costruire una sola take dai passaggi. `Dopo la registrazione in loop, suona solo l'ultimo passaggio` nelle opzioni di registrazione torna a una traccia sola.

## Dove vengono salvate le registrazioni

- Un progetto salvato conserva le sue take in `Audio/Recorded`, accanto al file di progetto.
- Un progetto non salvato registra in `LYNX/Recordings` nella tua cartella Musica.
- Le take sono file WAV alla frequenza di campionamento del dispositivo audio, a 24 bit o 32 bit float. Il formato e i nomi delle take si impostano sotto `Registrazione` in `Opzioni › Impostazioni audio...`.
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
