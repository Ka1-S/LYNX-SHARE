---
source: 5efc5c8254
---
![La finestra Gestisci scorciatoie: un elenco di azioni con ricerca e i tasti di ciascuna](/assets/img/wiki/shortcuts.webp)

Questi sono i tasti predefiniti nella 0.1.25 alpha. `Aiuto › Scorciatoie` (`F1`) mostra l'elenco delle scorciatoie dentro l'app. `Opzioni › Gestisci scorciatoie...` apre l'editor mostrato nell'immagine, dove le cambi.

> Su macOS l'app mostra i tasti con i nomi del Mac: Ctrl appare come ⌘ e Alt come ⌥.

## Cambiare una scorciatoia

1. Scegli `Opzioni › Gestisci scorciatoie...`.
2. Scrivi un'azione o un tasto, come `Ctrl+L`, nella casella di ricerca. L'elenco accanto limita la ricerca a un solo ambito, e `Solo modificate` mostra quelle che hai cambiato.
3. Fai clic sull'azione.
4. Fai clic su `Cambia` per sostituirne i tasti, o su `Nuova scorciatoia` per dargliene una seconda, poi premi i nuovi tasti. `Esc` annulla.
5. Se i tasti sono già usati nello stesso ambito, un messaggio indica l'altra azione e ti permette di spostarli.

`Rimuovi` cancella i tasti dell'azione. `Ripristina predefiniti` ripristina un'azione e `Ripristina tutto...` le ripristina tutte. Le tue modifiche vengono salvate in `keys.json` nella cartella delle impostazioni (`%APPDATA%\LYNX` su Windows, `~/Library/Application Support/LYNX` su macOS, `~/.config/LYNX` su Linux). `Apri keys.json` apre quel file.

## Trasporto e registrazione

| Tasto | Cosa fa |
|---|---|
| `Space` | Riproduce, oppure ferma e torna al punto da cui è partita la riproduzione |
| `Ctrl+Space` | Riproduce o mette in pausa |
| `Shift+Space` | Riproduce, oppure ferma all'inizio del brano o del pattern |
| `L` | Passa dalla modalità Pattern alla modalità Brano e viceversa |
| `R` | Registrazione attiva o disattiva |
| `Shift+R` | Registra subito l'audio: arma l'insert selezionato e registra |
| `Ctrl+M` | Metronomo attivo o disattivo |
| `Ctrl+P` | Conteggio prima della registrazione attivo o disattivo |
| `Ctrl+I` | La registrazione aspetta la prima nota, attivo o disattivo |
| `Ctrl+B` | Unisci note registrate attivo o disattivo |
| `Ctrl+E` | Inserimento a passi attivo o disattivo |
| `Ctrl+H` | Ferma il suono (all notes off) |
| `Ctrl+T` | Tastiera del PC come pianoforte attiva o disattiva |
| `Alt+Numpad *` / `Alt+Numpad /` | Salta al marker del brano successivo o precedente |

Alcuni di questi tasti fanno altro dentro un editor che ha il focus. Nel Piano Roll, `Ctrl+M` importa un file MIDI e `Ctrl+B` duplica. Nella Playlist, `Ctrl+P` è la modalità performance.

## File e modifica

| Tasto | Cosa fa |
|---|---|
| `Ctrl+N` | Nuovo progetto |
| `Ctrl+O` | Apri progetto |
| `Ctrl+S` | Salva |
| `Ctrl+Shift+S` | Salva come |
| `Ctrl+Alt+S` | Salva nuova versione |
| `Ctrl+R` | Esporta audio |
| `Ctrl+Shift+R` | Esporta un file MP3 |
| `Ctrl+Shift+M` | Esporta un file MIDI |
| `Ctrl+Z` | Annulla. Premuto di nuovo, ripete quel passaggio |
| `Ctrl+Alt+Z` | Annulla all'indietro, passaggio dopo passaggio |
| `Ctrl+Y` o `Ctrl+Shift+Z` | Ripeti |
| `Ctrl+Alt+H` | Cronologia annullamenti |
| `Ctrl+F` | Cerca nel Browser |

## Finestre

| Tasto | Cosa fa |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Selettore plugin |
| `Shift+F8` | Database dei plugin (nel Browser) |
| `Enter` | Ingrandisce o ripristina la Playlist |
| `F10` | Impostazioni MIDI |
| `F11` | Informazioni sul progetto |
| `F12` | Chiude tutte le finestre (plugin, impostazioni del canale, strumenti) |
| `Alt+F12` | Chiude tutte le finestre dei plugin |
| `Ctrl+F12` | Chiude tutte le finestre tranne quella con il focus |
| `Alt+F7` | Tastiera touch |
| `Ctrl+F5` | Tempo tapper |
| `F1` | Elenco delle scorciatoie |

## Pattern

| Tasto | Cosa fa |
|---|---|
| `Numpad +` / `Numpad -` | Pattern successivo, precedente |
| `Numpad 1` a `Numpad 9` | Seleziona il pattern da 1 a 9 |
| `F4` | Prossimo pattern vuoto, e gli dà un nome |
| `Ctrl+F4` | Prossimo pattern vuoto, senza dare un nome |
| `Shift+F4` | Primo pattern vuoto, e gli dà un nome |
| `F2` | Rinomina e colora il pattern |
| `Ctrl+Shift+C` | Clona il pattern |
| `Ctrl+Shift+Ins` | Inserisce un pattern vuoto prima di quello corrente |
| `Ctrl+Shift+Del` | Elimina il pattern |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Sposta il pattern su, giù |

## Channel Rack

| Tasto | Cosa fa |
|---|---|
| `Up` / `Down` | Seleziona il canale sopra, sotto |
| `Shift+Up` / `Shift+Down` | Estende la selezione |
| `Alt+Up` / `Alt+Down` | Sposta i canali selezionati |
| `Ctrl+A` | Seleziona tutti i canali |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Taglia, copia, incolla gli step dei canali selezionati |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Ruota gli step a sinistra, a destra |
| `Alt+C` | Clona i canali selezionati |
| `Alt+Del` | Elimina i canali selezionati |
| `Alt+G` | Raggruppa i canali selezionati |
| `Ctrl+L` | Manda i canali selezionati a insert del Mixer liberi |
| `Ctrl+K` | Corsie del grafico degli step attive o disattive |
| `1` a `0` | Silenzia o riattiva i canali da 1 a 10 |
| `Ctrl+1` a `Ctrl+0` | Mette in solo i canali da 1 a 10 |

## Piano Roll

| Tasto | Cosa fa |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Matita, Pennello, Pennello batteria, Gomma, Mute, Taglierino, Lazo, Zoom, Ascolta |
| `S` | Note slide attive o disattive (nuove note) |
| `G` / `K` | Canale precedente, successivo |
| `H` / `J` | Canale precedente, successivo con note |
| `Ctrl+A` | Seleziona tutto |
| `Ctrl+D` o `Esc` | Deseleziona |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, taglia, incolla |
| `Ctrl+B` | Duplica |
| `Del` | Elimina la selezione |
| `Up` / `Down` | Sposta la selezione di un semitono |
| `Ctrl+Up` / `Ctrl+Down` | Sposta la selezione di un'ottava |
| `Left` / `Right` | Sposta la selezione di uno step della griglia |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Ruota le note all'interno della battuta |
| `Backspace` | Snap disattivato o attivato |
| `Ctrl+Q` | Quantizzazione rapida (inizi e durate) |
| `Shift+Q` | Quantizza gli inizi |
| `Ctrl+U` | Spezza sulla griglia |
| `Ctrl+G` | Unisci note adiacenti |
| `Ctrl+L` | Legato |
| `Shift+D` | Accorcia le note alla griglia |
| `Ctrl+P` | Porta attiva o disattiva per la selezione |
| `Alt+M` / `Shift+Alt+M` | Silenzia, riattiva la selezione |
| `Alt+V` | Note fantasma attive o disattive |
| `Shift+I` | Inverte la selezione |
| `Shift+G` / `Alt+G` | Raggruppa, separa le note selezionate |
| `Alt+C` | Colora le note selezionate con il colore della nota |
| `Shift+C` | Seleziona le note dello stesso colore |
| `Shift+N` | Timbro accordo: nessuno (note singole) |
| `Ctrl+Ins` / `Ctrl+Del` | Inserisce, elimina spazio (la selezione temporale) |
| `Ctrl+M` | Importa un file MIDI |
| `Ctrl+Shift+M` | Esporta come file MIDI |
| `PageUp` / `PageDown` | Ingrandisce, riduce con lo zoom |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom su 1, 4, 16 battute |
| `Shift+4` | Zoom per mostrare tutto |
| `Shift+5` | Zoom sulla selezione |
| `Shift+0` | Centra la vista sul cursore di riproduzione |

### Strumenti per le note

Ogni tasto apre la finestra di uno strumento per le note nel Piano Roll.

| Tasto | Cosa fa |
|---|---|
| `Alt+A` | Arpeggia |
| `Alt+S` | Strum |
| `Alt+Y` | Capovolgi |
| `Alt+R` | Randomizza |
| `Alt+X` | Scala i livelli |
| `Alt+K` | Limita a un intervallo |
| `Alt+Q` | Quantizza |
| `Alt+U` | Spezza |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Articola |
| `Alt+E` | Riff machine |
| `Alt+P` | Genera progressione di accordi |

## Playlist

| Tasto | Cosa fa |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Matita, Pennello, Gomma, Mute, Taglierino, Lazo, Zoom, Ascolta, Slip |
| `Ctrl+A` | Seleziona tutto |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, taglia, incolla |
| `Ctrl+B` | Duplica |
| `Del` | Elimina le clip selezionate |
| `Shift+Del` | Elimina le clip selezionate e chiude gli spazi |
| `Shift+S` | Modifica ripple attiva o disattiva |
| `Backspace` | Snap disattivato o attivato |
| `Alt+M` / `Shift+Alt+M` | Silenzia, riattiva le clip selezionate |
| `Shift+M` | Stretch dell'audio quando si ridimensionano le clip audio, attivo o disattivo |
| `Ctrl+Shift+V` | Rende uniche le clip selezionate |
| `Ctrl+G` | Unisce le clip di pattern |
| `Ctrl+Shift+G` | Unisce le clip di pattern simili |
| `Ctrl+Alt+G` | Unisce le clip di automazione |
| `Ctrl+Shift+B` | Clona le tracce della Playlist selezionate |
| `Ctrl+Q` | Quantizza gli inizi delle clip |
| `7` / `8` | Abbassa, alza di un semitono le clip audio selezionate |
| `9` | Reverse delle clip audio selezionate |
| `Ctrl+Ins` / `Ctrl+Del` | Inserisce, rimuove la durata della selezione temporale su tutte le tracce |
| `Ctrl+Left` / `Ctrl+Right` | Seleziona il tempo precedente, successivo |
| `Ctrl+Enter` | Seleziona il tempo attorno alla selezione |
| `Alt+T` | Aggiunge un marker temporale |
| `Ctrl+T` | Aggiunge un marker automaticamente |
| `Shift+Alt+T` | Aggiunge un cambio di indicazione di tempo |
| `Shift+T` | Piazza il marker di loop |
| `Ctrl+Alt+C` | Consolida la selezione della Playlist |
| `Ctrl+P` | Modalità performance attiva o disattiva |
| `PageUp` / `PageDown` | Ingrandisce, riduce con lo zoom |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom su 1, 4, 16 battute |
| `Shift+4` | Zoom per mostrare tutto |
| `Shift+5` | Zoom sulla selezione |
| `Shift+0` | Centra la vista sul cursore di riproduzione |

## Mixer

| Tasto | Cosa fa |
|---|---|
| `Alt+Left` / `Alt+Right` | Sposta gli insert selezionati a sinistra, a destra |
| `F2` | Rinomina e colora l'insert selezionato |
| `S` | Mette in solo gli insert selezionati |
| `Alt+S` | Mette in solo gli insert selezionati con gli insert che li alimentano |
| `Ctrl+L` | Collega i canali selezionati all'insert selezionato |
| `Ctrl+Shift+L` | Collega i canali selezionati agli insert a partire da quello selezionato |
| `Alt+L` | Seleziona i canali collegati all'insert selezionato |
| `Del` | Elimina il plugin nello slot di effetto selezionato |
| `Ctrl+E` | Apre l'editor audio |
| `Shift+E` | Apre l'editor audio e registra |
| `Alt+R` | Renderizza gli insert armati in file wave |
| `Shift+Alt+R` | Renderizza gli insert selezionati in file wave |

## Browser

| Tasto | Cosa fa |
|---|---|
| `Up` / `Down` | File precedente, successivo (con anteprima) |
| `Shift+Up` / `Shift+Down` | File precedente, successivo nel canale selezionato |
| `Enter` | Carica il file nel canale selezionato |
| `Left` | Chiude la cartella, o va alla cartella superiore |
| `Right` | Apre la cartella |
| `F2` | Rinomina il plugin selezionato (Database dei plugin) |

## Da sapere

- Mentre il pianoforte sulla tastiera del PC è attivo, i suoi tasti suonano note e la maggior parte delle scorciatoie a lettera singola è in pausa (`L` funziona ancora). Disattivalo con `Ctrl+T`.
- Una scorciatoia vale ovunque oppure in una sola finestra. Quella che appartiene a una finestra prevale sugli stessi tasti validi ovunque, finché quella finestra ha il focus.
- Alcune azioni non hanno un tasto predefinito, per esempio la palette di aggiunta rapida. Assegnagliene uno nell'editor.

## Correlati

- [Un giro della finestra](/wiki/tour/)
- [Impostazioni](/wiki/settings/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Se vieni da FL Studio](/wiki/coming-from-fl-studio/)
