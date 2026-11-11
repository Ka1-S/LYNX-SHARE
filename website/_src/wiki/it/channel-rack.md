---
source: f08a3da66d
---
![Il Channel Rack con le righe di step](/assets/img/rack.webp)

Il Channel Rack elenca ogni strumento (canale) del progetto, uno per riga, e mostra gli step del pattern corrente. Premi `F6` per mostrarlo o nasconderlo. Anche il clic destro sul pulsante Pattern nel trasporto lo mostra.

## Fare un pattern di batteria

1. Premi `F6` per mostrare il Channel Rack.
2. Fai clic sulla casella `+ aggiungi canale` sotto l'ultima riga e scegli uno strumento dal menu. Puoi anche fare doppio clic su un sample nel [Browser](/wiki/browser/) per ottenere un nuovo canale sampler.
3. Fai clic sugli step nella riga del canale per attivarli. Trascina per disegnare una serie di step.
4. Fai clic destro su uno step per cancellarlo.
5. Premi `Space` per riprodurre. In modalità Pattern il pattern va in loop.
6. Trascina la manopola del volume della riga per impostare il livello.

## Programmare gli step

- Fai clic su uno step per attivarlo. Trascinando si disegna una serie di step. Se il trascinamento parte da uno step acceso, cancella invece.
- Il clic destro o il trascinamento con il tasto destro cancella.
- Se fai clic dopo la fine del pattern, aggiungi uno step e il pattern si allunga.
- Una riga le cui note non sono semplici step (altri tasti, note più lunghe, slide) mostra una miniatura delle note. Un clic la apre nel [Piano Roll](/wiki/piano-roll/).
- Puoi rilasciare file audio sul rack per creare canali.

Il menu del canale (clic destro sul nome del canale) contiene `Riempi ogni 2 step`, `Riempi ogni 4 step`, `Riempi ogni 8 step`, `Riempimento avanzato`, e `Ruota a sinistra` e `Ruota a destra`.

## La corsia del grafico

![La corsia del grafico aperta sotto il canale Kick, con una barra di velocity per ogni step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` mostra la corsia del grafico sotto il canale selezionato. I pulsanti alla sua sinistra scelgono cosa modifica: Velocity, Pan, Release, Intonazione fine, Mod X o Mod Y. Trascinando si imposta un valore per step, il trascinamento con il tasto destro disegna una rampa e `Alt`+clic reimposta uno step.

## Le parti di una riga

| Parte | Cosa fa |
|---|---|
| Spia del mute | Il clic silenzia. `Ctrl`+clic o clic destro mette in solo. |
| Manopole di pan e volume | Trascina o usa la rotella. |
| Casella di instradamento | Trascinando o con la rotella scegli l'insert del Mixer. Il doppio clic mostra quell'insert nel Mixer. Il clic destro prende il prossimo insert libero. |
| Pulsante del nome | Il clic apre o chiude l'editor del canale. `Alt`+clic lascia aperti gli altri editor. `Ctrl`+clic ascolta. Trascinando su o giù si riordina. Il clic destro apre il menu del canale. |
| Selettore | Il clic seleziona un solo canale. `Ctrl`+clic o clic destro aggiunge o toglie. `Shift`+clic seleziona un intervallo. Il doppio clic seleziona tutto. |

## Pattern

Il rack modifica sempre il pattern corrente. Il selettore di pattern del rack ne mostra il nome: fai clic sul nome per l'elenco, oppure usa le frecce. Anche `Shift`+rotella sul titolo del rack cambia il pattern.

La casella della durata del pattern nel rack imposta la durata in battute (trascinala o usa la rotella). Fai doppio clic per una durata automatica ricavata dalle note.

La manopola dello swing nella barra degli strumenti principale vale per tutti i canali. `Mix dello swing` nel menu del canale imposta quanto un canale la segue.

| Comando | Tasto | Cosa fa |
|---|---|---|
| Pattern successivo | `Numpad +` | Rende corrente il pattern successivo |
| Pattern precedente | `Numpad -` | Rende corrente il pattern precedente |
| Prossimo pattern vuoto | `F4` | Va al prossimo pattern vuoto e chiede un nome |
| Rinomina e colora | `F2` | Rinomina e colora il pattern corrente |

## Suonare note dalla tastiera del PC

`Ctrl+T` trasforma la tastiera del PC in un pianoforte: da `Z` a `M` e da `Q` a `P` suonano i canali selezionati. Finché è attivo, le scorciatoie a lettera singola degli editor sono in pausa.

## Tasti

Funzionano mentre il Channel Rack ha il focus.

| Comando | Tasto | Cosa fa |
|---|---|---|
| Seleziona canale | `Up` / `Down` | Seleziona il canale sopra o sotto |
| Estendi la selezione | `Shift+Up` / `Shift+Down` | Aggiunge alla selezione il canale sopra o sotto |
| Sposta canali | `Alt+Up` / `Alt+Down` | Sposta su o giù i canali selezionati |
| Seleziona tutto | `Ctrl+A` | Seleziona tutti i canali |
| Taglia, copia, incolla | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Taglia, copia o incolla gli step dei canali selezionati |
| Ruota gli step | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Ruota gli step dei canali selezionati |
| Clona | `Alt+C` | Clona i canali selezionati |
| Elimina | `Alt+Del` | Elimina i canali selezionati |
| Raggruppa | `Alt+G` | Mette i canali selezionati in un nuovo gruppo di filtro |
| Instrada al Mixer | `Ctrl+L` | Manda i canali selezionati a insert del Mixer liberi |
| Mute | `1` a `0` | Silenzia o riattiva i canali da 1 a 10 |
| Solo | `Ctrl+1` a `Ctrl+0` | Mette in solo i canali da 1 a 10 |
| Corsia del grafico | `Ctrl+K` | Mostra o nasconde la corsia del grafico |

## Altre voci del menu del canale

`Piano Roll`, `Rinomina e colora`, `Sostituisci` (cambia lo strumento), `Clona`, `Delete`, `Arpeggiatore`, `Punto di loop`, `Scrivi MIDI in`, `Ordina canali`, `Seleziona canali inutilizzati` e `Impostazioni del canale`.

## Da sapere

- Solo la casella `+ aggiungi canale` apre il menu di aggiunta.
- Il rack non ha un pattern proprio. Cambiare il pattern corrente in qualsiasi punto cambia ciò che mostra il rack.
- Una riga che mostra una miniatura di note non si può modificare come step. Usa il Piano Roll.
- I tasti numerici arrivano solo ai primi dieci canali.
- I tasti della tabella richiedono che il rack abbia il focus. Fai prima clic sul rack.

## Correlati

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Scorciatoie](/wiki/shortcuts/)
