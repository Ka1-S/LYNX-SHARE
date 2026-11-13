---
source: 1b54c6493f
---
![La Playlist con clip sulle tracce](/assets/img/playlist.webp)

La Playlist è dove i pattern diventano un brano. Contiene tre tipi di clip sulle sue tracce: clip di pattern, clip audio e clip di automazione. Premi `F5` per mostrarla o nasconderla, e `Enter` per ingrandirla o ripristinarla.

Il trasporto ha due modalità. La modalità Pattern riproduce il pattern corrente in loop. La modalità Brano riproduce la Playlist. Premi `L` per passare dall'una all'altra.

## Piazzare un pattern nel brano

1. Premi `F5` per mostrare la Playlist.
2. Nel Pannello di selezione, fai clic su un pattern. Il Selettore elenca i pattern, l'audio e l'automazione del progetto, e quello su cui fai clic diventa la sorgente delle clip.
3. Premi `P` per la Matita.
4. Fai clic su una traccia per piazzare la clip. Tieni premuto il pulsante e trascina per posizionarla.
5. Premi `B` per il Pennello e trascina lungo una traccia per dipingere clip una dopo l'altra.
6. Premi `L` per la modalità Brano, poi `Space` per riprodurre.

I file audio si possono trascinare dal [Browser](/wiki/browser/) sulla Playlist.

## Strumenti

![La barra degli strumenti della Playlist con la casella dell'arrangiamento, gli strumenti e la casella dello snap](/assets/img/wiki/playlist-tools.webp)

Le lettere funzionano mentre il pianoforte sulla tastiera del PC (`Ctrl+T`) è spento.

| Strumento | Tasto | Cosa fa |
|---|---|---|
| Matita | `P` | Il clic piazza la clip del Selettore |
| Pennello | `B` | Trascinando dipinge clip una dopo l'altra |
| Gomma | `D` | Il clic o il trascinamento elimina |
| Mute | `T` | Il clic o il trascinamento silenzia o riattiva le clip |
| Taglierino | `C` | Trascina una linea per tagliare attraverso le clip, il clic taglia in quel punto |
| Lazo | `E` | Trascinando disegna un riquadro di selezione. `Shift` aggiunge |
| Zoom | `Z` | Il clic ingrandisce, trascinare un intervallo ingrandisce su di esso, il clic destro riduce |
| Ascolta | `Y` | Il clic o il trascinamento riproduce da quel punto |
| Slip | `S` | Trascinando fa scorrere il contenuto della clip dentro la clip |

## Lavorare con le clip

- Trascina una clip per spostarla. `Shift`+trascina ne fa una copia.
- Trascina la fine di una clip per ridimensionarla. Trascinare l'inizio la rifila mentre il contenuto resta al suo posto.
- Il clic destro elimina una clip. `Ctrl`+trascina seleziona.
- Fai doppio clic su una clip per aprirla.
- Il pulsante in alto a sinistra di una clip apre il menu della clip (rendi unica, rinomina e colora, spezza e, per l'audio: pitch, adatta al tempo, reverse).
- Il menu di una clip audio segue il layout di FL Studio su due colonne. La colonna sinistra ha la sezione `Clip audio` (`Anteprima`, `Silenziato`, `Rinomina e colora...`, `Cambia colore...`, `Colore casuale`, `Cambia icona...`, `Seleziona canale di origine`, `Impostazioni del canale`, `Rendi unico`, `Seleziona tutte le clip simili`, `Delete`, `Proprietà...`) e la sezione `Regione` (`Seleziona regione`, `Spezza`). La colonna destra ha `Sample` (`Rendi unico come campione`, `Modifica campione`, `Estrai stem dal sample`, `Rileva il tempo`, `Adatta al tempo`), `Automazione` (`Automatizza › Panning` o `Volume`, `Dissolvenza incrociata con`) e `Dissolvenze` (`Reimposta dissolvenze`). Le voci proprie di LYNX SHARE, come `Dividi qui`, `Reverse`, `Normalizza` e `Converti in MIDI`, vengono per ultime, sotto una linea.
- `Automatizza › Panning` o `Volume` crea una clip di automazione lunga quanto la clip audio, sulla sua traccia, per il pan o il volume della regolazione Levels del canale.
- Tieni premuto `Alt` mentre trascini per ignorare lo snap.
- Le clip audio hanno maniglie di fade e un gain della clip che trascini su o giù. Clicca con il tasto destro su una maniglia di fade per il suo menu, con `Crea dissolvenza incrociata`. Clicca con il tasto destro sulla maniglia del gain per `Azzera gain` e `Normalizza`.
- `Shift+M` attiva Stretch: ridimensionare una clip audio allora allunga l'audio invece di tagliarlo.
- `Shift+Left` e `Shift+Right` spostano le clip selezionate di uno step di snap, `Shift+Up` e `Shift+Down` di una traccia. `Alt+Left` e `Alt+Right` le spostano di un pixel dello schermo allo zoom corrente, senza snap. Se nessuna clip è selezionata, si spostano tutte le clip.
- `Rendi unico come campione` nel menu di una clip audio dà alla clip un nuovo file che contiene ciò che ha suonato, con intonazione, stretch e reverse già nell'audio.
- Una clip audio con warp marker li mostra come piccole bandiere sul bordo superiore. Trascina una bandiera di lato per spostare il marker.

## Menu Edit e Select

Il menu della Playlist ha un sottomenu `Modifica` e uno `Seleziona`. `Modifica` contiene i comandi sulle clip: taglia, copia e incolla, `Sposta a sinistra`, `Sposta a destra`, `Sposta su`, `Sposta giù`, `Rendi unico`, `Rendi unico come campione`, `Inserisci spazio`, `Elimina spazio`, i comandi di unione e le opzioni di fade. `Seleziona` ha `Seleziona per sorgente selezionata` (`Shift+C`), `Inverti selezione` (`Shift+I`), `Deseleziona` (`Ctrl+D`), le clip mute e quelle sovrapposte, e `Lazo magico`. Con `Lazo magico` attivo, muovi il mouse in un giro intorno alle clip, senza premere un pulsante, per selezionarle.

## Tracce

Nell'intestazione di una traccia, il clic seleziona, `Ctrl`+clic aggiunge o toglie, `Shift`+clic seleziona un intervallo e il clic destro apre il menu della traccia. `Shift`+rotella sposta la traccia su o giù. La spia della traccia la silenzia (clic) o la mette in solo (`Ctrl`+clic o clic destro). Trascina il divisore tra due tracce per ridimensionarne una, oppure `Shift`+trascina per ridimensionarle tutte.

`Vista` nel menu della Playlist sceglie cosa mostrano le intestazioni delle tracce: `Mostra i controlli sulle tracce audio`, `Mostra i livelli sulle tracce audio` e `Mostra i livelli sulle tracce strumento`.

`Modalità traccia` nel menu della traccia rende una traccia `Non assegnata`, una `Traccia audio` o una `Traccia strumento`, come in FL Studio. `Traccia audio` elenca prima `Nuova/e traccia/e del mixer` e poi ogni insert del Mixer per numero e nome. La scelta vale per tutte le tracce selezionate. `Traccia strumento` ha `Usa un canale esistente`, `Altri plugin...` e l'elenco degli strumenti del Channel rack.

Il `+` in alto a sinistra nelle intestazioni delle tracce apre le stesse scelte per una nuova traccia: `Aggiungi traccia audio`, `Altri plugin...` e gli strumenti. Un clic destro su `+` aggiunge subito una traccia audio.

## Pattern Blocks

Con `Blocchi di pattern (progetto attuale)` attivo nel menu della Playlist, una sezione sotto le tracce ha una riga per pattern. Lì la Matita mette un blocco e lo trascina lungo la sua riga, il Pennello dipinge blocchi e la Gomma li elimina. Il clic destro cancella con qualsiasi strumento.

## Righello, loop e marker

- Fai clic sul righello per impostare la posizione del brano.
- Il trascinamento con il tasto destro, `Ctrl`+trascina o doppio clic e trascina sul righello crea una selezione temporale, che è anche il loop. `Shift`+trascina la sposta.
- `Ctrl+Ins` inserisce la durata della selezione temporale su tutte le tracce, `Ctrl+Del` la rimuove.
- `Alt+T` aggiunge un marker. Fai clic su un marker per saltarci.
- `Ctrl`+rotella ingrandisce, `Alt`+rotella cambia l'altezza delle tracce.

## Automazione

Fai clic destro su una manopola o un fader e scegli `Crea clip di automazione`. La clip appare sulla Playlist. Con la Matita, trascina i suoi punti e fai clic sulla curva per aggiungerne uno. Le corsie di automazione sotto le tracce sono facoltative e spente di default. Vedi [Automazione](/wiki/automation/).

Clic destro su un punto per il suo menu. La sua lista `Modalità` ha i 13 modi di curva di FL Studio per il segmento che finisce in quel punto: `Smussa`, `Hold`, tre curve singole, tre curve doppie, `Mezza sinusoide`, `Scalini`, `Scalini morbidi`, `Impulso` e `Onda`. Un nuovo punto prende il modo usato per ultimo. I modi vengono letti e scritti nei file .flp.

## Arrangiamenti

La casella dell'arrangiamento nella barra degli strumenti passa da un arrangiamento all'altro. Un progetto può averne diversi.

## Tasti

| Comando | Tasto | Cosa fa |
|---|---|---|
| Seleziona tutto | `Ctrl+A` | Seleziona ogni clip |
| Copia, taglia, incolla | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, taglia o incolla clip |
| Duplica | `Ctrl+B` | Duplica la selezione |
| Elimina | `Del` | Elimina le clip selezionate |
| Elimina con ripple | `Shift+Del` | Elimina le clip selezionate e chiude gli spazi |
| Modifica ripple | `Shift+S` | Attiva o disattiva la modifica ripple |
| Mute, riattiva | `Alt+M` / `Shift+Alt+M` | Silenzia o riattiva le clip selezionate |
| Spostare le clip | `Shift+Left` / `Shift+Right` | Sposta le clip selezionate di uno step di snap |
| Spostare su un'altra traccia | `Shift+Up` / `Shift+Down` | Sposta le clip selezionate di una traccia |
| Spostamento fine delle clip | `Alt+Left` / `Alt+Right` | Sposta le clip selezionate di un pixel dello schermo, senza snap |
| Rendi unico | `Ctrl+Shift+V` | Rende uniche le clip selezionate |
| Unisci | `Ctrl+G` | Unisce le clip di pattern |
| Stretch | `Shift+M` | Attiva Stretch per ridimensionare le clip audio |
| Inserisci tempo | `Ctrl+Ins` | Inserisce la durata della selezione temporale su tutte le tracce |
| Elimina tempo | `Ctrl+Del` | Rimuove la selezione temporale su tutte le tracce |
| Aggiungi marker | `Alt+T` | Aggiunge un marker |
| Zoom | `PageUp` / `PageDown` | Ingrandisce o riduce |
| Adatta con lo zoom | `Shift+4` | Zooma per mostrare tutto |
| Ingrandisci | `Enter` | Ingrandisce o ripristina la Playlist |

## Da sapere

- La Playlist si sente solo in modalità Brano. In modalità Pattern, `Space` riproduce il pattern corrente, qualunque cosa ci sia sulle tracce.
- `Vista › Stile del layout › Flusso a lancio di clip` aggiunge una griglia di clip e scene accanto alla Playlist. È facoltativo. Vedi [Layout](/wiki/layouts/).
- Con più clip di automazione sovrapposte su un solo controllo, la scelta di quale suoni può differire da quella di FL Studio (note di rilascio 0.1.25).
- Come in FL Studio, un pattern suona da una sola clip alla volta. Quando parte una clip successiva dello stesso pattern, su qualsiasi traccia, la clip precedente non avvia più note da lì in poi. Le clip di pattern diversi suonano insieme. Questo vale nei nuovi progetti e nei progetti di FL Studio. Un progetto .lynx salvato prima della 0.1.27 continua a suonare ogni clip per intero.
- Un tempo spostato da un controller MIDI collegato non ridimensiona le clip audio.
- Su Linux, il video richiede FFmpeg.

## Correlati

- [Channel Rack](/wiki/channel-rack/)
- [Automazione](/wiki/automation/)
- [Registrazione](/wiki/recording/)
- [Esportazione](/wiki/exporting/)
- [Layout](/wiki/layouts/)
