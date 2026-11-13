---
source: e15b0e3ba5
---
![Il Clip-launch workflow: una griglia di clip e scene a sinistra, il Selettore e la Playlist a destra](/assets/img/cliplaunch.webp)

`Vista › Stile del layout` ha tre voci: `Flusso a pattern (predefinito)`, `Flusso a lancio di clip` e `Flusso semplice`. La stessa scelta è in `Opzioni › Impostazioni generali`, sotto Flusso di lavoro. La scelta si mantiene quando chiudi il programma.

Il flusso a pattern è il Channel Rack, i pattern e la Playlist. Il flusso a lancio di clip aggiunge una griglia di clip con scene accanto alla Playlist. I pattern e il Channel Rack restano disponibili.

## Lanciare le clip nella griglia

1. Scegli `Vista › Stile del layout › Flusso a lancio di clip`. La griglia di clip compare accanto alla Playlist.
2. Clicca con il tasto destro su uno slot vuoto, scegli `Metti un pattern qui` e seleziona un pattern. Il doppio clic su uno slot vuoto ci mette il pattern corrente.
3. Clicca il triangolo a sinistra della clip per lanciarla. Se il trasporto è fermo, parte.
4. Clicca il triangolo di una scena, nella colonna `Scene`, per lanciare ogni clip di quella riga.
5. Clicca `Ferma tutte le clip` per fermarle.
6. Clicca `Torna alla Playlist` per far suonare di nuovo alle tracce le loro clip della Playlist.

## Cosa cambia sullo schermo

- La griglia di clip è mostrata accanto alla Playlist. Le tracce della Playlist sono le colonne e le scene sono le righe.
- La barra del titolo della Playlist riceve un selettore `Entrambi`, `Griglia`, `Brano`. `Griglia` mostra solo la griglia, `Brano` solo la Playlist.
- La prima volta il Channel Rack è nascosto. `F6` o il suo pulsante nella barra degli strumenti lo mostra.
- Le corsie di automazione della Playlist sono attivate.
- Il layout delle finestre è memorizzato per stile, quindi ogni stile torna come l'hai lasciato.
- Sotto la griglia, il pulsante `Mixer` mostra o nasconde il Session mixer, come in Ableton Live: `Ingresso/Uscita` (`Audio da`, `Monitor`, `Audio a`), `Mandate`, `Ritardo traccia`, e il volume di ogni traccia con il suo activator, solo, pulsante di arm e meter, più la strip `Principale`.
- `Groove`, sotto la griglia, mostra il Groove Pool. `Applica il groove alle note` scrive un groove nelle note di una clip MIDI.
- I pulsanti `MIDI` e `Tonalità` avviano MIDI Map Mode e Key Map Mode. Vedi [Controller MIDI](/wiki/midi-controllers/).

## La griglia

Una clip lanciata non parte subito. Aspetta il successivo punto di quantizzazione di lancio, e il suo triangolo lampeggia nel frattempo. Il riquadro in fondo alla griglia imposta la quantizzazione di lancio globale, che usano tutte le clip impostate su `Globale`. Anche uno stop aspetta allo stesso modo.

Clicca con il tasto destro su una clip per le sue impostazioni.

| Impostazione | Cosa fa |
|---|---|
| `Quantizzazione di lancio` | Quando parte questa clip: da `Globale`, `Nessuno`, `8 battute` fino a `1/32` |
| `Modalità di lancio` | `Trigger`, `Gate`, `Alterna` o `Ripetizioni` |
| `Legato (mantieni la posizione)` | La clip prende la posizione di riproduzione della clip che suonava prima su quella traccia |
| `Loop` | Attivo: la clip si ripete. Disattivo: suona una volta, poi la traccia si ferma |
| `Azione successiva` | Cosa succede dopo la clip: due azioni con una probabilità ciascuna, e il tempo dopo il quale agiscono |
| `Delete` | Svuota lo slot |

Clicca con il tasto destro su uno slot vuoto per `Pulsante di stop (una scena ferma questa traccia qui)`. Cliccare il quadrato a sinistra di uno slot vuoto ferma quella traccia. Clicca con il tasto destro su una scena per `Inserisci scena`, `Aggiungi scena sotto`, `Elimina scena`, `Rinomina scena...` e il tempo della scena.

Le clip si possono tagliare, copiare, incollare e duplicare (`Ctrl+D`) dal loro menu, e trascinare tra gli slot e da o verso la Playlist. Un pattern o un sample trascinato dal Browser, o dei file audio rilasciati dall'esterno, riempiono lo slot su cui cadono. Il menu di una scena ha anche `Duplica` e `Cattura e inserisci scena`, che mette le clip in riproduzione in questo momento in una nuova scena. `Vista › Tempo e indicazione di tempo delle scene` mostra un tempo e un tempo in chiave per ogni scena.

Il menu dello slot ha anche due opzioni per l'intera griglia: `Restituisci le tracce alla Playlist quando il trasporto si ferma` e `Azioni successive su tutta la griglia`.

| Comando | Tasto | Cosa fa |
|---|---|---|
| Spostarsi | Tasti freccia | Seleziona un altro slot |
| Lanciare una clip | `Enter` | Lancia la clip selezionata |
| Lanciare una scena | `Ctrl+Enter` | Lancia la scena selezionata |
| Eliminare una clip | `Del` | Svuota lo slot selezionato |
| Draw Mode | `B` | Un trascinamento in una corsia di automazione disegna gradini larghi quanto la griglia, oppure a mano libera con lo snap disattivato o `Alt` premuto, come in Ableton Live. Anche in `Opzioni` |

## Set di Ableton Live

Quando apri un set di Live, le sue clip Session e le sue scene vanno nella griglia di clip, con le loro impostazioni di lancio. `File › Esporta › Set di Ableton Live (.als)` riscrive la griglia come clip Session. `Opzioni › Impostazioni generali` ha `Clip di sessione dei set di Live`, dove `Playlist, dopo l'arrangiamento` le mette invece nella Playlist.

## Il flusso Simple

`Vista › Stile del layout › Flusso semplice` è una sola finestra con le tracce e la timeline, per un primo brano. `Vista › Pannello della traccia` mostra sotto la timeline un pannello per la traccia selezionata, con le schede `Effetti`, `Strumento`, `Tastiera` e `Editor`. In questo flusso, il menu `Aiuto` ha `Tour della prima canzone` e `Schermata di benvenuto...`, che offre un primo brano in sette passi, un brano dimostrativo o un progetto vuoto. Vedi [Layout Simple](/wiki/simple-layout/).

`Testo e note` e `Abbinamento al riferimento...` nel menu `Vista`, e `Voce pulita` nel menu di un insert del Mixer, ci sono in ogni layout. Nel flusso Simple, `Voce pulita` è anche nel menu di una traccia.

## Tornare indietro

Scegli `Vista › Stile del layout › Flusso a pattern (predefinito)`. Il layout delle finestre che avevi nel flusso a pattern torna, e le corsie di automazione tornano all'impostazione che avevano prima. Le clip nella griglia restano nel progetto.

## Da sapere

- Una traccia che ha lanciato una clip resta muta nella Playlist finché non premi `Torna alla Playlist`. L'opzione `Restituisci le tracce alla Playlist quando il trasporto si ferma` lo fa per te a ogni stop.
- I nuovi tasti della griglia (`B`, `Q`, `Ctrl+M`, `Ctrl+K` e altri) agiscono così solo nel flusso Clip-launch. Nel flusso Pattern mantengono il significato di FL Studio.
- Una scena in 6/8 suona una battuta di tre movimenti, perché il metro conta i quarti.
- Le azioni successive esistono per le clip, non per le scene.
- Le corsie di automazione attivate a mano mentre sei nel flusso a lancio di clip non vengono mantenute quando torni indietro.
- Con il flusso a pattern selezionato, della griglia non viene caricato nulla.

## Correlati

- [Playlist](/wiki/playlist/)
- [Se vieni da Ableton Live](/wiki/coming-from-ableton/)
- [Importare progetti](/wiki/importing-projects/)
- [Cosa fa solo LYNX: i flussi di lavoro](/why/#workflows)

La terza voce è una sola finestra per un primo brano. Vedi [Layout semplice](/wiki/simple-layout/).
