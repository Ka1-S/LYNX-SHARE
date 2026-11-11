---
source: b96330a448
---
![L'editor di automazione con una curva che sale in sei battute](/assets/img/wiki/automation.webp)

Una clip di automazione è una curva nella [Playlist](/wiki/playlist/) che muove un controllo mentre il brano suona: il volume di un canale, un fader del Mixer, un parametro di un plugin, il tempo. La clip ha una finestra di editor tutta sua, l'editor di automazione.

Ci sono altri tre modi per muovere un controllo. Gli eventi sono valori memorizzati dentro un pattern. Un collegamento fa pilotare il controllo da una manopola MIDI o da un controller interno. Un modulatore fa oscillare una manopola attorno al suo valore.

## Automatizzare una manopola

1. Sposta il cursore di riproduzione dove deve iniziare l'automazione, oppure seleziona un intervallo di tempo nella Playlist.
2. Clicca con il tasto destro sulla manopola o sul fader e scegli `Crea clip di automazione`.
3. La clip finisce sulla prima traccia vuota della Playlist e si apre l'editor di automazione. La curva parte come una linea piatta al valore attuale del controllo.
4. Clicca nell'editor per aggiungere un punto e trascinalo al suo posto.
5. Tieni premuto `Alt` e trascina un segmento in su o in giù per curvarlo.
6. Riproduci il brano in modalità Brano. Il controllo segue la curva.

Altri modi per ottenere la stessa clip:

- `Aggiungi › Automazione per l'ultimo parametro modificato` crea una clip per il controllo che hai mosso per ultimo.
- L'indicatore dell'ultimo parametro modificato e `Strumenti › Ultimo modificato` aprono lo stesso menu del clic destro sul controllo.
- Per un plugin di terze parti, apri il menu del canale nel Channel Rack o il menu dello slot effetto nel Mixer e usa `Automatizza parametro del plugin`. Accanto c'è `Automatizza controller MIDI (CC, aftertouch, pitch bend)`.

Per riaprire l'editor, fai doppio clic sulla clip, oppure clic destro e scegli `Modifica punti...`. `Modifica clip di automazione...` nel menu del controllo stesso fa la stessa cosa.

## L'editor di automazione

| Comando | Tasto | Cosa fa |
|---|---|---|
| Nuovo punto | Clic o clic destro, poi trascina | Aggiunge un punto e lo sposta finché tieni premuto il tasto |
| Spostare un punto | Trascina | `Shift` mantiene il suo tempo, `Ctrl` mantiene il suo valore, `Alt` ignora lo snap |
| Curvare un segmento | `Alt`+trascina in su o in giù | Cambia la tensione del segmento sotto il mouse |
| Maniglia di tensione | Trascina in su o in giù | Curva il segmento. `Ctrl` è più fine, il clic destro lo rende dritto |
| Punto sulla curva | `Shift`+clic destro | Aggiunge un punto sulla linea |
| Menu del punto | Clic destro su un punto | `Digita valore...`, `Copia valore`, `Incolla valore`, `Elimina punto` e `Curva intera` per scalare, ribaltare o raddrizzare tutto |
| Zoom | `Ctrl`+rotellina | Ingrandisce o riduce la vista |
| Scorrimento | Trascina con il tasto centrale | Sposta la vista |

## Modalità LFO

![L'editor di automazione in modalità LFO, con un'onda triangolare e le manopole SPD, TNS, SKEW, PW e AMNT](/assets/img/wiki/automation-lfo.webp)

Attiva `LFO` in alto nell'editor. Ora una forma d'onda modella il valore della clip, e i punti che disegni impostano l'ampiezza dell'LFO invece del valore stesso.

| Impostazione | Cosa fa |
|---|---|
| `SPD` | La durata di un ciclo, in beat |
| `TNS` | Tensione dell'onda |
| `SKEW` | Asimmetria dell'onda |
| `PW` | Larghezza d'impulso |
| `AMNT` | Quanto l'LFO sposta il valore |
| `MUL` | Attivo: l'LFO viene moltiplicato per il livello dell'inviluppo. Disattivo: viene sommato |

## Eventi in un pattern

![L'editor degli eventi con eventi di volume disegnati come barre](/assets/img/wiki/eventeditor.webp)

Gli eventi appartengono a un pattern e suonano ovunque suoni quel pattern. Clicca con il tasto destro su un controllo e scegli `Modifica gli eventi nel pattern` per aprire per lui l'editor degli eventi.

- `Disegna` imposta i valori mentre trascini, e il trascinamento con il tasto destro cancella. `Linea` disegna una rampa dritta. `Seleziona` segna un intervallo di tempo per gli strumenti in basso.
- `Snap` va da 1/2 step a 1 battuta.
- `Scala`, `Offset` e `Randomizza` modificano gli eventi della selezione, o dell'intero pattern.
- `LFO` scrive un'onda sinusoidale, triangolare o quadra da 1 beat a 4 battute.
- `Cancella` rimuove gli eventi.
- `In automazione...` trasforma gli eventi in una clip di automazione e li rimuove dal pattern.
- `File` apre e salva gli eventi come file `.fsc` e importa un file MIDI.

`Inizializza il brano con questa posizione`, nello stesso menu del clic destro, memorizza il valore a cui il controllo viene riportato ogni volta che il brano parte.

## Registrare i tuoi movimenti

Attiva la registrazione con `Automazione` spuntato nel filtro di registrazione, premi play e muovi il controllo. In modalità Brano i movimenti diventano una clip di automazione. In modalità Pattern diventano eventi nel pattern corrente. Vedi [Registrazione](/wiki/recording/).

## Collegamenti e modulatori

- `Collega a un controller MIDI (muovi una manopola sul controller)` è trattato in [Controller MIDI](/wiki/midi-controllers/).
- `Collega a un controller interno` permette a un Envelope, a un Formula o a un Peak Controller del progetto di pilotare il controllo. Aggiungine prima uno.
- `Modula` collega un modulatore, un LFO o una sorgente casuale che muove una manopola di LYNX SHARE attorno al suo valore. `Strumenti › Modulatori...` li elenca.

## Corsie di automazione

Le corsie sono disattivate per impostazione predefinita. Attiva `Corsie di automazione` nel menu della Playlist. Ogni intestazione di traccia ha allora un pulsante che aggiunge una corsia sotto la traccia, e il menu del controllo guadagna `Automatizza in corsia`. In una corsia, clicca per aggiungere un punto o trascinalo, clic destro per eliminarne uno, e `Alt`+trascina per curvare la linea. Il menu della corsia ha `In clip di automazione`.

## Da sapere

- Una nuova clip parte dalla selezione temporale se c'è, altrimenti dal cursore di riproduzione in modalità Brano. Disattiva `Crea le clip di automazione alla posizione di riproduzione` in `Opzioni › Impostazioni generali...` per partire dall'inizio del brano.
- Più clip sullo stesso controllo suonano ciascuna al proprio posto. Prima della sua prima clip, un controllo mantiene il proprio valore.
- All'inizio di una clip il controllo raggiunge il nuovo valore in circa 8 ms. FL Studio ci arriva scivolando, quindi un progetto importato può differire leggermente in quei punti.
- Dove le clip sullo stesso controllo si sovrappongono, quella scelta da LYNX SHARE può differire da quella scelta da FL Studio.
- I modulatori funzionano solo sulle manopole di LYNX SHARE e vengono lasciati fuori quando esporti in `.flp`.

## Correlati

- [Playlist](/wiki/playlist/)
- [Registrazione](/wiki/recording/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Scorciatoie](/wiki/shortcuts/)
