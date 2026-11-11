---
source: e767f4482b
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
- Tieni premuto `Alt` mentre trascini per ignorare lo snap.
- Le clip audio hanno maniglie di fade e un gain della clip che trascini su o giù.
- `Shift+M` attiva Stretch: ridimensionare una clip audio allora allunga l'audio invece di tagliarlo.

## Tracce

Nell'intestazione di una traccia, il clic seleziona, `Ctrl`+clic aggiunge o toglie, `Shift`+clic seleziona un intervallo e il clic destro apre il menu della traccia. `Shift`+rotella sposta la traccia su o giù. La spia della traccia la silenzia (clic) o la mette in solo (`Ctrl`+clic o clic destro). Trascina il divisore tra due tracce per ridimensionarne una, oppure `Shift`+trascina per ridimensionarle tutte.

## Righello, loop e marker

- Fai clic sul righello per impostare la posizione del brano.
- Il trascinamento con il tasto destro, `Ctrl`+trascina o doppio clic e trascina sul righello crea una selezione temporale, che è anche il loop. `Shift`+trascina la sposta.
- `Ctrl+Ins` inserisce la durata della selezione temporale su tutte le tracce, `Ctrl+Del` la rimuove.
- `Alt+T` aggiunge un marker. Fai clic su un marker per saltarci.
- `Ctrl`+rotella ingrandisce, `Alt`+rotella cambia l'altezza delle tracce.

## Automazione

Fai clic destro su una manopola o un fader e scegli `Crea clip di automazione`. La clip appare sulla Playlist. Con la Matita, trascina i suoi punti e fai clic sulla curva per aggiungerne uno. Le corsie di automazione sotto le tracce sono facoltative e spente di default. Vedi [Automazione](/wiki/automation/).

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
- Un tempo spostato da un controller MIDI collegato non ridimensiona le clip audio.
- Su Linux, il video richiede FFmpeg.

## Correlati

- [Channel Rack](/wiki/channel-rack/)
- [Automazione](/wiki/automation/)
- [Registrazione](/wiki/recording/)
- [Esportazione](/wiki/exporting/)
- [Layout](/wiki/layouts/)
