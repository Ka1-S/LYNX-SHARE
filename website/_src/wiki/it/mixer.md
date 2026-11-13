---
source: b6e9ef0dd0
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
- `Del`, o `Delete` nel menu dell'insert, elimina gli insert selezionati. LYNX SHARE chiede prima conferma. Spunta `Ricorda la mia scelta` per saltare la domanda la volta successiva.

Spostare, inserire o eliminare insert mantiene mandate, sidechain e automazione sul loro insert.

La sezione `Mixer` del menu dell'insert e `Opzioni › Mixer` hanno altri due comandi. `Disponi gli insert come la Playlist` sposta all'inizio gli insert collegati alle tracce della Playlist, nell'ordine della Playlist. Con `Mantieni i collegamenti agli insert eliminati, in grigio (LYNX)` attivo, le mandate che andavano in un insert eliminato restano, in grigio, così puoi ricollegarle.

## Mute e solo

Clicca la spia del mute per mettere un insert in mute. Clic destro o `Ctrl`+clic per il solo. `S` mette in solo gli insert selezionati e `Alt+S` li mette in solo insieme agli insert che li alimentano.

Un insert silenziato continua a muovere il suo meter. Nei nuovi progetti e nei progetti FL Studio importati continua anche a eseguire i suoi effetti, e vengono tagliate solo la sua uscita verso il Master e le sue mandate. Per impedire a un insert silenziato di elaborare, attiva `Gli insert silenziati non elaborano (LYNX, risparmia CPU; questo progetto)` nella sezione `Mixer` del menu dell'insert o in `Opzioni › Mixer`. Il suo meter si ferma e i suoi effetti ripartono da zero quando lo riattivi.

## Effetti

Il pannello degli effetti mostra i dieci slot dell'insert selezionato.

- Trascina uno slot, o usa la rotellina su di esso, per spostarlo su o giù.
- Clic destro su uno slot per le sue opzioni (sostituisci, sposta, elimina).
- Mentre il menu di uno slot è aperto, `Del` elimina il plugin in quello slot.
- `Raggruppa in un rack di effetti audio` nel menu dello slot mette l'effetto in un nuovo rack, e `Raggruppa tutto in un rack di effetti audio` lo fa per l'intera catena. `Separa` rimette negli slot gli effetti di un rack a una sola catena.
- L'interruttore nell'intestazione del pannello attiva o disattiva tutti gli effetti dell'insert.
- Puoi trascinare un effetto dal Database dei plugin del [Browser](/wiki/browser/) su un insert o su uno slot effetto.
- `Voce pulita` nel menu dell'insert mette sull'insert una catena pronta per ripulire la voce.

## Edison in uno slot

Edison, l'editor e registratore audio, può stare in qualsiasi slot effetto, anche più d'uno per insert. La sua finestra è l'editor audio con una riga di controlli di registrazione.

- `Apri editor audio` (`Ctrl+E`) nel menu dell'insert carica Edison nel primo slot vuoto dell'insert e lo apre. `Apri l'editor audio e registra` (`Shift+E`) lo carica pronto a registrare quando il brano è in riproduzione.
- Le modalità di registrazione sono `Ora`, `All'ingresso` (quella predefinita: aspetta che l'ingresso superi una soglia), `Ingresso` e `In riproduzione`. `Accoda` mette una nuova registrazione dopo l'audio già presente. La durata massima della registrazione è `Senza limite` oppure 1, 3, 5, 10, 30 o 60 minuti: un nuovo slot parte da 30, `Apri l'editor audio e registra` imposta 5.
- `Sync` suona l'audio di Edison al tempo del brano mentre il brano è in riproduzione.
- Edison continua a registrare dopo che hai chiuso la sua finestra.
- `Strumenti › Macro › Rimuovi tutte le istanze di Edison` svuota ogni slot Edison del progetto, dopo una domanda.
- Gli slot Edison nei progetti di FL Studio (.flp) si aprono con il loro audio.

## EQ dell'insert

Ogni insert ha il proprio EQ. Trascina una banda per impostarne frequenza e livello, usa la rotellina per la larghezza e fai doppio clic su una banda per riportarla a 0 dB. Clic destro sul titolo dell'EQ per metterlo in bypass. Se vuoi un Mixer senza EQ integrato, i nuovi progetti possono essere impostati per partire con un mixer pulito.

## Instradamento, mandate e sidechain

![Il Mixer con l'insert Kick selezionato, le sue mandate disegnate come cavi sotto le strisce e il pannello degli effetti a destra](/assets/img/wiki/mixer-sends.webp)

Seleziona l'insert che vuoi instradare. Ogni altra striscia ha allora un interruttore di instradamento:

- Clicca l'interruttore su un'altra striscia per inviare lì l'insert selezionato. Trascinare dopo averlo attivato imposta il livello della mandata.
- `Ctrl`+clic instrada solo su quell'insert.
- `Shift`+clic crea una mandata sidechain. `Ctrl+Shift`+clic la crea solo sidechain.
- Il clic destro apre le stesse scelte come menu.
- Per una mandata pre-fader, clicca con il tasto destro sull'interruttore di instradamento o sulla manopola del livello della mandata e spunta `Mandata pre-fader (il fader di questa traccia non la modifica)`. La mandata viene allora contrassegnata `PRE`.

Gli effetti con un ingresso laterale, come un compressore, hanno un selettore `Sidechain da` che elenca gli insert da ascoltare. Sceglierne uno crea l'instradamento per te.

## Gruppi e fader VCA

I gruppi del Mixer si richiudono con il loro chevron (o `Alt`+clic per tutti i gruppi). Un fader VCA muove il livello degli insert che ne fanno parte, mentre questi mantengono i propri fader. Creane uno dal menu dell'insert: sotto Gruppo, scegli `VCA › Nuovo VCA dai selezionati`.

## Registrazione

Ogni insert ha un pulsante di armo per la registrazione. Con un insert armato, Rec e Play registrano il suo ingresso audio in una clip della Playlist. `Shift+R` arma l'insert selezionato e avvia la registrazione in un solo passaggio. Vedi [Registrazione](/wiki/recording/).

Quando scegli un ingresso esterno per un insert che registra anche altro audio, LYNX SHARE chiede se passarlo a `Solo ingresso esterno`, così la take contiene solo l'ingresso. `Mixer › Registrazione su disco › Compensazione della latenza` nel menu dell'insert è attivo per impostazione predefinita. Se lo disattivi, le take registrate vengono collocate senza alcuna compensazione della latenza.

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
| Eliminare gli insert | `Del` | Elimina gli insert selezionati, dopo una domanda |
| Armare e registrare | `Shift+R` | Arma l'insert selezionato e avvia la registrazione |
| Open audio editor | `Ctrl+E` | Carica Edison nel primo slot vuoto dell'insert e lo apre |
| Open audio editor and record | `Shift+E` | Lo stesso, pronto a registrare quando il brano è in riproduzione |

## Da sapere

- `Misuratore di loudness accanto al Master (LYNX)`, in `Opzioni › Mixer`, mostra la loudness a breve termine e integrata (LUFS) e il true peak dell'uscita del Master, con un pulsante `Ripristina`.
- Un plugin strumento con più uscite può suonare ciascuna uscita su un proprio insert. Vedi [Plugin di terze parti](/wiki/plugins/).
- `Ctrl+L` fa cose diverse nel Channel Rack e nel Mixer. Nel rack sceglie insert liberi. Nel Mixer usa l'insert selezionato.
- Gli interruttori di instradamento compaiono sulle altre strisce, non su quella selezionata.
- Il limite è 127 insert per progetto e dieci slot effetto per insert.
- La registrazione di Edison è conservata come file WAV nella cartella del progetto, non dentro il file del progetto.
- Il Session mixer sotto la griglia di clip appartiene solo al flusso Clip-launch. Mostra gli stessi insert di questo Mixer. Vedi [Layout](/wiki/layouts/).

## Correlati

- [Channel Rack](/wiki/channel-rack/)
- [Registrazione](/wiki/recording/)
- [Automazione](/wiki/automation/)
- [Plugin di terze parti](/wiki/plugins/)
- [Strumenti ed effetti](/instruments-and-effects/)
