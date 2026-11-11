---
source: a06504f3a0
---
![La pagina MIDI delle impostazioni: dispositivi di ingresso, dispositivi di uscita con sincronia master e sincronia esterna](/assets/img/wiki/settings-midi.webp)

LYNX SHARE riceve note e movimenti dei controller da qualsiasi dispositivo di ingresso MIDI. I dispositivi si configurano nella pagina MIDI delle impostazioni: `Opzioni › Impostazioni MIDI...`, oppure `F10`. Senza hardware, anche la tastiera del PC e la tastiera touch suonano note.

## Collegare una tastiera

1. Collega la tastiera e apri `Opzioni › Impostazioni MIDI...` (`F10`).
2. Trovala sotto `Dispositivi di ingresso` e spuntala. Se non è elencata, clicca `Aggiorna dispositivi`.
3. Lascia i due riquadri su `Qualsiasi canale MIDI` e `Suona i canali selezionati`.
4. Chiudi le impostazioni e seleziona un canale nel [Channel Rack](/wiki/channel-rack/).
5. Suona. Il canale selezionato suona.

## Dispositivi di ingresso

| Impostazione | Cosa fa |
|---|---|
| Casella da spuntare | Attiva o disattiva il dispositivo |
| `Qualsiasi canale MIDI` | Ascolta ogni canale MIDI, oppure un solo canale. `Controller MPE` è alla fine di questa lista |
| `Suona i canali selezionati` | Le note vanno ai canali selezionati nel Channel Rack, oppure sempre a un canale fisso |
| `Velocity` | `Come suonato`, `Morbida (un tocco leggero suona più forte)`, `Dura (richiede un tocco più forte)` o `Fissa (ogni nota alla velocity massima)` |
| `Canale della modalità performance` | Le note su questo canale MIDI lanciano le clip della Playlist mentre la modalità performance è attiva |
| `Salto a marker del brano` | Le note su questo canale MIDI spostano il brano a un marker invece di suonare |
| `Tipo di controller` | `Rileva automaticamente`, `Controller generico` o un profilo di controller |
| `Il pedale controlla il rilascio delle note` | Attivo per impostazione predefinita. Un pedale di sustain (CC 64) o di sostenuto (CC 66) trattiene le note. Disattivato, il pedale arriva ai plugin come controller |

## Collegare una manopola a un controllo

1. Clicca con il tasto destro sulla manopola o sul fader in LYNX SHARE.
2. Scegli `Collega a un controller MIDI (muovi una manopola sul controller)`.
3. Muovi la manopola o il fader sul tuo hardware. Il collegamento è fatto.

Il collegamento viene salvato con il progetto. Per rimuoverlo, clicca di nuovo con il tasto destro sul controllo e scegli `Scollega controller`. La voce di menu mostra il numero CC in uso.

| Comando | Tasto | Cosa fa |
|---|---|---|
| `Override global link...` | nessuno | Crea un collegamento che vale in ogni progetto. Viene mantenuto nelle impostazioni, non nel progetto |
| `Rimuovi collegamento globale` | nessuno | Lo rimuove di nuovo |
| `Multilink ai controller` | `Ctrl+J` | Finché è attivo, i controlli che muovi vengono collegati, in ordine, ai controlli hardware che muovi subito dopo. Si trova nel menu `Opzioni` |
| `Pickup (modalità takeover)` | nessuno | Nella pagina MIDI. Un controller muove un parametro solo dopo aver raggiunto il suo valore attuale, così niente salta |

La pagina MIDI elenca i collegamenti sotto `Collegamenti (progetto, globali)`. Un dispositivo il cui `Tipo di controller` è `Controller generico` fa solo ciò che colleghi a mano.

I parametri di un plugin di terze parti si raggiungono con `Automatizza parametro del plugin` nel menu del canale o nel menu dello slot effetto. Ogni parametro lì ha le stesse voci di collegamento. Vedi [Automazione](/wiki/automation/).

## Profili di controller

Un profilo di controller configura i pulsanti del trasporto, i fader, le manopole, i pad, le luci e il display di un dispositivo, senza nulla da collegare.

1. Apri `Opzioni › Impostazioni MIDI...` (`F10`) e spunta il dispositivo sotto `Dispositivi di ingresso`.
2. Lascia il suo `Tipo di controller` su `Rileva automaticamente`. Il nome tra parentesi è il profilo trovato dal nome della porta del dispositivo.
3. Se mostra `Controller generico`, scegli tu il profilo dalla lista.
4. Leggi la riga accanto al riquadro. `Feedback to` indica la porta di uscita che riceve le luci, i fader motorizzati e il testo del display.

| Profilo | Cosa mappa |
|---|---|
| `Mackie Control Universal` | Fader ai volumi degli insert e al Master. V-Pot al pan, ai parametri del plugin del canale selezionato o ai volumi del Channel rack. Arm, solo, mute e select per ogni strip, banchi di fader, play, stop, record, rewind, forward, modalità Pattern o Song, metronomo, undo e la jog wheel. Il display mostra nomi e valori |
| `Mackie Control Extender (XT)` | Lo stesso per altre 8 strip. Le unità condividono un solo banco, nell'ordine della lista `Dispositivi di ingresso` |
| `Novation Launchkey MK3 (DAW mode)` | Pot a pan, volume o parametri dei plugin, fader al volume, i pulsanti dei fader selezionano un insert. Pulsanti di banco, play, stop, record, loop, metronomo e undo. I pad Session sono i 16 step del canale selezionato. Usalo sulla seconda porta della tastiera |
| `Akai APC Key 25 mk2` | Manopole a volume, pan o parametri dei plugin. Le due file di pad in alto sono i 16 step del canale selezionato e le tre sotto suonano note. I pulsanti delle tracce selezionano un insert. Stop, play e record. Usalo sulla seconda porta |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | La mappa Mackie Control, sulla porta MCU/HUI della tastiera |
| `Korg nanoKONTROL2 (Mackie Control mode)` | La mappa Mackie Control. Non viene rilevato: avvia l'unità in una modalità Mackie Control e scegli questo tipo a mano |

Le manopole e i fader che inviano una posizione usano il soft takeover: muovono un controllo solo dopo aver raggiunto il suo valore. I controlli che hai collegato a mano mantengono i loro collegamenti. `Cartella dei profili controller` apre la cartella per i tuoi profili (`*.lynxctl`), e `Aggiorna dispositivi` li rilegge.

I profili integrati sono stati scritti dalla documentazione di ciascun produttore. Non sono ancora stati provati sull'hardware.

## Controller MPE

L'MPE è a un primo stadio. Un controller MPE suona ogni nota con il proprio pitch bend, timbro (CC 74) e pressione.

1. Nella pagina MIDI, apri il riquadro del dispositivo che riporta `Qualsiasi canale MIDI` e scegli `Controller MPE`.
2. Nel riquadro che compare accanto, scegli i canali membro, da `Canale 2` a `Channels 2-16`.
3. Suona. Per registrare, vedi [Registrazione](/wiki/recording/).

- Una nota registrata conserva un solo valore di ciascuno: il suo bend come intonazione fine, il suo timbro come Mod X e la sua pressione più forte come Mod Y. Non è una curva.
- Tra gli strumenti integrati, il Sampler e il synth integrato seguono questi valori.
- Un plugin di terze parti riceve l'MPE quando `MPE` è attivo sotto `MIDI` in `Impostazioni del wrapper` della finestra del plugin. Ogni nota esce allora sul proprio canale MIDI, da 2 a 16.
- Non è ancora stato provato con un vero controller MPE.

## La tastiera del PC

`Ctrl+T`, oppure `Opzioni › Tastiera del PC come pianoforte`, trasforma la tastiera del computer in un pianoforte. Le file da `Z` a `M` e da `Q` a `P` suonano i canali selezionati.

- Finché è attiva, i tasti di lettere e numeri sono note. I tasti strumento a lettera singola del Piano Roll e della Playlist funzionano solo quando è disattivata.
- Suona sempre alla velocity del Piano Roll. L'impostazione `Velocity` della pagina MIDI è per i dispositivi MIDI.
- Lo stesso interruttore è in `Opzioni › Impostazioni generali...`.

## La tastiera touch

![La tastiera touch: pulsanti di ottava, dimensione dei tasti, modalità velocity e una fila di tasti di pianoforte da C4](/assets/img/wiki/touch-keyboard.webp)

`Vista › Tastiera touch` (`Alt+F7`) apre un pianoforte che suoni con il mouse o con uno schermo touch. Suona il canale selezionato nel Channel Rack e ne mostra il nome a destra.

| Impostazione | Cosa fa |
|---|---|
| `-` e `+` | Un'ottava più giù o più su |
| Dimensione dei tasti | `Tasti piccoli`, `Tasti medi` o `Tasti grandi` |
| `Velocity: dove tocchi` | Vicino alla parte alta di un tasto suona piano, vicino alla parte bassa forte |
| `Velocity: fissa` | Ogni nota alla velocity impostata con lo slider |
| `Nomi delle note` | Mostra i nomi sui tasti |

## Sincronia MIDI

`Dispositivi di uscita` nella pagina MIDI invia il trasporto all'hardware e ad altro software. Spunta `Invia sincronia master` per un dispositivo e scegli `MIDI clock` (con Start, Continue, Stop e Song Position Pointer) o MIDI Time Code. `Offset (ms)` invia la sincronia più tardi con un valore positivo e prima con un valore negativo. `Invia all notes off` invia quel messaggio al dispositivo all'inizializzazione e dopo un'esportazione.

`Sincronia a clock esterno` funziona al contrario: LYNX SHARE segue il clock MIDI dell'ingresso che scegli, con il suo tempo, Start, Continue, Stop e Song Position Pointer. `Offset della sincronia esterna (millisecondi)` fa suonare LYNX SHARE più tardi con un valore positivo e prima con un valore negativo. `Stato` mostra se un clock è in funzione.

## Da sapere

- Scegliere un ingresso per `Sincronia a clock esterno` attiva quel dispositivo sotto `Dispositivi di ingresso`.
- Un tempo mosso da un controller MIDI collegato non ridimensiona le clip audio. Digita o imposta invece il tempo finale.
- Per registrare ciò che suoni, vedi [Registrazione](/wiki/recording/). Le note in ingresso possono essere agganciate a una scala nel [Piano Roll](/wiki/piano-roll/).
- MIDI CC, aftertouch di canale e pitch bend inviati a un plugin si possono automatizzare con `Automatizza controller MIDI (CC, aftertouch, pitch bend)`.
- Una nota trattenuta da un pedale viene registrata fino al momento in cui il pedale si alza.
- Una clip di automazione non riprende un controllo mentre lo muovi da un controller. Il controllo segue di nuovo la sua automazione alla successiva partenza della riproduzione.

## Correlati

- [Registrazione](/wiki/recording/)
- [Automazione](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Impostazioni](/wiki/settings/)
- [Scorciatoie](/wiki/shortcuts/)
