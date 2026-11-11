---
source: edf15b371e
---
![La finestra principale con il progetto demo: barra degli strumenti in alto, Browser a sinistra, Playlist dietro e Channel Rack che le fluttua sopra](/assets/img/wiki/desktop.webp)

LYNX SHARE ha una sola finestra principale. Una barra degli strumenti corre lungo il bordo superiore. Sotto c'è un'area di lavoro che contiene cinque finestre: il Browser, la Playlist, il Channel Rack, il Piano Roll e il Mixer. A un avvio nuovo il Browser è a sinistra, la Playlist riempie il resto e il Channel Rack le fluttua sopra. Il Piano Roll e il Mixer sono nascosti finché non li richiami.

## Orientarsi

1. Punta il mouse su un controllo qualsiasi. La barra dei suggerimenti sotto i menu dice cosa fa e quali pulsanti del mouse e tasti accetta.
2. Premi `F6` per nascondere il Channel Rack, e di nuovo `F6` per mostrarlo.
3. Premi `F9` per mostrare il Mixer, poi `F7` per il Piano Roll.
4. Trascina una finestra dalla sua barra del titolo per spostarla. Fai doppio clic sul titolo per ingrandirla, e di nuovo per ripristinarla.
5. Scegli `Vista › Ripristina layout` per rimettere tutto a posto.

## La barra degli strumenti

![La barra degli strumenti: menu e barra dei suggerimenti, trasporto, manopole master, opzioni di registrazione, snap e pattern, pulsanti delle finestre, pulsanti dei file, visualizzazione degli accordi, meter e monitor di uscita](/assets/img/wiki/toolbar-1920.webp)

Da sinistra a destra, con il layout predefinito:

| Gruppo | Cosa contiene |
|---|---|
| Menu e barra dei suggerimenti | I menu `File`, `Modifica`, `Aggiungi`, `Pattern`, `Vista`, `Opzioni`, `Strumenti` e `Aiuto`, con la barra dei suggerimenti sotto. Sempre per primo |
| Trasporto | I pulsanti di modalità `Pattern` e `Brano`, Riproduci, Stop e Registra, il tempo in BPM e la posizione come battuta, beat e step |
| Volume e pitch master | La manopola del volume `MASTER` e la manopola `PITCH`, in centesimi |
| Opzioni di registrazione | Metronomo, conteggio, registrazione in loop, inserimento a passi e gli altri interruttori di registrazione |
| Snap e pattern | Lo snap principale (`Linea (segue lo zoom)` di default), il pattern corrente, un pulsante per un nuovo pattern e il menu dei pattern |
| Scorciatoie delle finestre | Un pulsante per finestra, e il selettore plugin |
| File e annulla | Nuovo, Apri, Salva, Esporta, Annulla e Ripeti |
| Rilevamento accordi | Dà il nome all'accordo che si sta suonando su un dispositivo MIDI o sulla tastiera del PC |
| CPU, memoria e polifonia | Carico di CPU dell'audio, memoria in uso e numero di voci |
| Monitor di uscita | Un oscilloscopio dell'uscita. Un clic passa alle sue altre modalità |

Esiste anche un gruppo chiamato `Multilink ai controller`, nascosto di default.

`Opzioni › Barra degli strumenti` cambia la barra degli strumenti: `Modifica la barra degli strumenti (trascina i pannelli per spostarli, aggiungerli o rimuoverli)`, `Separatori`, `Nascondi i pannelli della barra degli strumenti`, `Preset`, un interruttore per ogni gruppo e `Dim. icone` (piccola, media o grande).

## Trasporto

| Controllo | Tasto | Cosa fa |
|---|---|---|
| Modalità Pattern | `L` | Riproduce il pattern corrente in loop. Clic destro sul pulsante per mostrare il Channel Rack |
| Modalità Brano | `L` | Riproduce l'arrangiamento della Playlist. Clic destro sul pulsante per mostrare la Playlist |
| Riproduci | `Space` | Riproduce. Premuto di nuovo, si ferma e torna dov'era partito. `Ctrl+Space` mette in pausa |
| Stop | | Si ferma e torna all'inizio. Il doppio clic invia anche all notes off |
| Registra | `R` | Registrazione attiva o disattiva. Vedi [Registrazione](/wiki/recording/) |

## La barra dei suggerimenti

A riposo la barra dei suggerimenti dice `Point at anything for help`, con `F1` per il foglio delle scorciatoie, da `F5` a `F9` per le finestre e `Space` per riprodurre e fermare. Sopra un controllo ne dice il nome ed elenca cosa fanno lì il trascinamento, `Ctrl`, `Alt` e il clic destro. I lavori lunghi come la scansione dei plugin in background vi riportano il loro avanzamento.

## Le cinque finestre

| Finestra | Tasto | A cosa serve |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Arrangiare clip di pattern, audio e automazione in un brano |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Una riga per strumento, con gli step del pattern corrente |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Le note di un canale nel pattern corrente |
| [Mixer](/wiki/mixer/) | `F9` | Insert con fader, effetti e instradamento |
| [Browser](/wiki/browser/) | `Alt+F8` | Sample, preset, il contenuto del progetto e il database dei plugin |

Ogni tasto mostra la finestra e le dà il focus, oppure la nasconde se ce l'ha già. `F8` da solo apre il selettore plugin, che non è una delle cinque. I pulsanti delle finestre nella barra degli strumenti fanno lo stesso dei tasti. Il clic destro su uno mostra quella finestra ingrandita. `Enter` ingrandisce o ripristina la Playlist.

## Come si muovono e si agganciano le finestre

Ogni finestra ha una barra del titolo. Alla sua sinistra c'è un pulsante di menu con le opzioni proprie della finestra, poi `Separato (finestra propria)`, `Ingrandito` e `Chiudi`. Alla sua destra ci sono i pulsanti di ingrandimento e di chiusura.

- Trascina la barra del titolo per spostare una finestra. Si aggancia alle altre finestre e ai bordi. Tieni premuto `Alt` per spostare senza agganciare.
- Trascina un bordo per ridimensionare.
- `Separato (finestra propria)` trasforma la finestra in una finestra di sistema separata, per un secondo monitor. Una finestra separata ha un pulsante `Aggancia` che la rimette a posto. `Vista › Pannelli separati (finestra propria)` le elenca tutte e cinque.
- `Vista › Riporta i pannelli fuori schermo` recupera una finestra finita fuori dall'area di lavoro.
- `Vista › Salva layout con nome...` memorizza la disposizione con un nome, e `Vista › Carica layout` ne ripristina una.

Il Browser è lo sfondo dell'area di lavoro: resta agganciato al lato e le altre finestre stanno sempre sopra di esso.

## Da sapere

- Finché non sposti, ridimensioni o separi qualcosa, le finestre riprendono le posizioni predefinite ogni volta che la finestra principale cambia dimensione. Dopo, la tua disposizione viene ricordata e si adatta alla finestra.
- `F12` chiude le finestre dei plugin, delle impostazioni del canale e degli strumenti. Non chiude le cinque finestre principali.
- Mentre il pianoforte sulla tastiera del PC è attivo (`Ctrl+T`), i tasti lettera suonano note e la maggior parte delle scorciatoie a lettera singola è in pausa.
- `Vista › Stile del layout` passa dal flusso a pattern (predefinito) al flusso a lancio di clip, che aggiunge una griglia di clip accanto alla Playlist.

## Correlati

- [La tua prima canzone](/wiki/first-song/)
- [Layout](/wiki/layouts/)
- [Scorciatoie](/wiki/shortcuts/)
- [Impostazioni](/wiki/settings/)
