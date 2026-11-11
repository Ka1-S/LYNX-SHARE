---
source: b2de12e24d
---
![Il Mixer](/assets/img/mixer.webp)

Ogni canale suona attraverso un insert del Mixer. Ogni insert ha un fader, il pan, mute e solo, un indicatore di picco, dieci slot effetto con una quantità dry/wet ciascuno e un EQ integrato. Tutti gli insert finiscono nel Master, a meno che tu non li instradi altrove. Premi `F9` per mostrare o nascondere il Mixer.

Un progetto può avere fino a 127 insert. Il Mixer ha una striscia di aggiunta per un nuovo insert. Il riquadro nella barra del titolo del Mixer sceglie uno degli otto layout delle strisce, da `Compatto` a `Extra grande`.

## Mettere un effetto su un canale

1. Nel [Channel Rack](/wiki/channel-rack/), seleziona il canale e premi `Ctrl+L`. Il canale va su un insert libero, che ne prende il nome e il colore.
2. Premi `F9` per mostrare il Mixer e clicca la striscia di quell'insert.
3. Nel pannello degli effetti, clicca uno slot vuoto e scegli un effetto.
4. Clicca il nome dello slot per mostrare o nascondere la finestra dell'effetto.
5. Clicca la spia dello slot per mettere l'effetto in bypass e confrontare.

## Inviare un canale a un insert

Nel Channel Rack, trascina il riquadro di instradamento del canale (o usa la rotellina su di esso) per scegliere il numero dell'insert. Il doppio clic sul riquadro mostra quell'insert nel Mixer.

`Ctrl+L` nel Channel Rack funziona su più canali selezionati insieme. Ognuno va su un proprio insert libero.

Con il Mixer in primo piano, `Ctrl+L` collega i canali selezionati all'insert selezionato e `Ctrl+Shift+L` li collega a insert consecutivi a partire da quello.

## Selezionare e spostare gli insert

- Clicca una striscia per selezionarla.
- `Ctrl`+clic, o il trascinamento sulle strisce, seleziona un intervallo. `Ctrl+Shift`+clic ne aggiunge o ne toglie una.
- Trascina una striscia di lato, oppure premi `Alt+Left` e `Alt+Right`, per spostare gli insert selezionati.
- Clic destro su una striscia per il menu dell'insert. `F2` rinomina e colora l'insert selezionato.

Spostare, inserire o eliminare insert mantiene mandate, sidechain e automazione sul loro insert.

## Mute e solo

Clicca la spia del mute per mettere un insert in mute. Clic destro o `Ctrl`+clic per il solo. `S` mette in solo gli insert selezionati e `Alt+S` li mette in solo insieme agli insert che li alimentano.

## Effetti

Il pannello degli effetti mostra i dieci slot dell'insert selezionato.

- Trascina uno slot, o usa la rotellina su di esso, per spostarlo su o giù.
- Clic destro su uno slot per le sue opzioni (sostituisci, sposta, elimina).
- `Del` elimina il plugin nello slot selezionato.
- L'interruttore nell'intestazione del pannello attiva o disattiva tutti gli effetti dell'insert.
- Puoi trascinare un effetto dal Database dei plugin del [Browser](/wiki/browser/) su un insert o su uno slot effetto.

## EQ dell'insert

Ogni insert ha il proprio EQ. Trascina una banda per impostarne frequenza e livello, usa la rotellina per la larghezza e fai doppio clic su una banda per riportarla a 0 dB. Clic destro sul titolo dell'EQ per metterlo in bypass. Se vuoi un Mixer senza EQ integrato, i nuovi progetti possono essere impostati per partire con un mixer pulito.

## Instradamento, mandate e sidechain

![Il Mixer con l'insert Kick selezionato, le sue mandate disegnate come cavi sotto le strisce e il pannello degli effetti a destra](/assets/img/wiki/mixer-sends.webp)

Seleziona l'insert che vuoi instradare. Ogni altra striscia ha allora un interruttore di instradamento:

- Clicca l'interruttore su un'altra striscia per inviare lì l'insert selezionato. Trascinare dopo averlo attivato imposta il livello della mandata.
- `Ctrl`+clic instrada solo su quell'insert.
- `Shift`+clic crea una mandata sidechain. `Ctrl+Shift`+clic la crea solo sidechain.
- Il clic destro apre le stesse scelte come menu.

Gli effetti con un ingresso laterale, come un compressore, hanno un selettore `Sidechain da` che elenca gli insert da ascoltare. Sceglierne uno crea l'instradamento per te.

## Gruppi e fader VCA

I gruppi del Mixer si richiudono con il loro chevron (o `Alt`+clic per tutti i gruppi). Un fader VCA muove il livello degli insert che ne fanno parte, mentre questi mantengono i propri fader. Creane uno dal menu dell'insert: sotto Gruppo, scegli `VCA › Nuovo VCA dai selezionati`.

## Registrazione

Ogni insert ha un pulsante di armo per la registrazione. Con un insert armato, Rec e Play registrano il suo ingresso audio in una clip della Playlist. `Shift+R` arma l'insert selezionato e avvia la registrazione in un solo passaggio. Vedi [Registrazione](/wiki/recording/).

## Tasti

`Ctrl+L` e `Ctrl+Shift+L` hanno questo significato solo mentre il Mixer ha il focus.

| Comando | Tasto | Cosa fa |
|---|---|---|
| Collegare i canali | `Ctrl+L` | Collega i canali selezionati all'insert selezionato |
| Collegare a insert consecutivi | `Ctrl+Shift+L` | Collega i canali selezionati a insert a partire da quello selezionato |
| Sposta insert | `Alt+Left` / `Alt+Right` | Sposta gli insert selezionati |
| Rinominare e colorare | `F2` | Rinomina e colora l'insert selezionato |
| Solo | `S` | Mette in solo gli insert selezionati |
| Solo con le sorgenti | `Alt+S` | Mette in solo gli insert selezionati e gli insert che li alimentano |
| Eliminare un effetto | `Del` | Elimina il plugin nello slot selezionato |
| Armare e registrare | `Shift+R` | Arma l'insert selezionato e avvia la registrazione |

## Da sapere

- `Renderizza in audio` (un insert bounciato in una clip audio) suona circa 5 dB più forte dell'insert che sostituisce. È un problema noto nelle note di rilascio della 0.1.25.
- `Ctrl+L` fa cose diverse nel Channel Rack e nel Mixer. Nel rack sceglie insert liberi. Nel Mixer usa l'insert selezionato.
- Gli interruttori di instradamento compaiono sulle altre strisce, non su quella selezionata.
- Il limite è 127 insert per progetto e dieci slot effetto per insert.

## Correlati

- [Channel Rack](/wiki/channel-rack/)
- [Registrazione](/wiki/recording/)
- [Automazione](/wiki/automation/)
- [Plugin di terze parti](/wiki/plugins/)
- [Strumenti ed effetti](/instruments-and-effects/)
