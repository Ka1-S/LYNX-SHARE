---
source: a73763934f
---
![Il Piano Roll](/assets/img/pianoroll.webp)

Il Piano Roll mostra le note di un canale nel pattern corrente, con una tastiera a sinistra e una corsia per i valori delle singole note in basso. Premi `F7` per mostrarlo o nasconderlo, oppure scegli `Piano Roll` nel menu di un canale nel [Channel Rack](/wiki/channel-rack/).

Il riquadro del canale nella barra degli strumenti sceglie quale canale modifichi. I canali che hanno note in questo pattern sono contrassegnati. La rotellina sul riquadro scorre i canali con note, `Ctrl`+rotellina tutti.

## Scrivere qualche nota

1. Nel Channel Rack, clicca con il tasto destro sul nome di un canale e scegli `Piano Roll`.
2. Premi `P` per la Matita.
3. Clicca nella griglia per aggiungere una nota. Tieni premuto il tasto e trascina per posizionarla.
4. Trascina la fine della nota per allungarla o accorciarla.
5. Clic destro su una nota per eliminarla.
6. Premi `Space` per sentire il pattern.

## Strumenti

![La barra degli strumenti del Piano Roll con il riquadro del canale, gli strumenti e il riquadro dello snap](/assets/img/wiki/pianoroll-tools.webp)

Ogni strumento ha un tasto a lettera. Le lettere funzionano mentre il pianoforte sulla tastiera del PC (`Ctrl+T`) è disattivato.

| Strumento | Tasto | Cosa fa |
|---|---|---|
| Matita | `P` | Il clic aggiunge una nota, il trascinamento la posiziona |
| Pennello | `B` | Il trascinamento dipinge una serie di note. Con `Shift`, una alla volta |
| Pennello batteria | `N` | Un colpo per ogni cella della griglia sul tasto sotto il mouse |
| Gomma | `D` | Clic o trascinamento elimina |
| Mute | `T` | Clic o trascinamento mette le note in mute o le riattiva |
| Taglierino | `C` | Trascina una linea per tagliare attraverso le note, il clic taglia in quel punto |
| Lazo | `E` | Il trascinamento disegna un riquadro di selezione. `Shift` aggiunge |
| Zoom | `Z` | Il clic ingrandisce, trascina un intervallo per ingrandire su di esso, clic destro riduce |
| Ascolta | `Y` | Clic o trascinamento suona le note sotto il cursore |

## Lavorare con le note

- Trascina una nota per spostarla. `Shift`+trascina ne fa una copia.
- Trascina la fine di una nota per ridimensionarla. Si può trascinare anche l'inizio, con la fine che resta ferma.
- Il clic destro elimina una nota, con qualsiasi strumento.
- Fai doppio clic su una nota per aprirne le proprietà.
- `Alt`+rotellina su una nota ne cambia la velocity.
- `Ctrl`+trascina seleziona. `Ctrl`+clic aggiunge una nota alla selezione o la toglie.
- Tieni premuto `Alt` mentre trascini per ignorare lo snap. `Backspace` disattiva e attiva lo snap.
- Con più note selezionate, la maniglia di stretch le allunga o le comprime nel tempo.

## Tastiera e righello

Clicca un tasto sulla tastiera per sentirlo. Più a destra sul tasto è più forte. `Ctrl`+clic su un tasto ne seleziona le note. Il trascinamento con il tasto centrale sulla tastiera rende i tasti più alti o più bassi. Lo fa anche la rotella dello zoom verticale, il piccolo quadrato alla fine della barra di scorrimento del tempo: trascinala su o giù, oppure usa la rotella del mouse sopra di essa.

`Vista › Stile della tastiera` nel menu del Piano roll sceglie tra `Tasti piatti` e `Tasti di pianoforte (linee tra i tasti bianchi)`, che disegna una tastiera tradizionale.

Clicca il righello per impostare la posizione. `Ctrl`+trascina, trascina con il tasto destro o doppio clic e trascina sul righello selezionano le note in un intervallo di tempo. `Ctrl`+rotellina fa lo zoom e `Shift`+rotellina scorre.

## Barra degli strumenti

| Impostazione | Cosa fa |
|---|---|
| Snap | "Principale" segue lo snap della barra degli strumenti principale, oppure ne scegli uno per questa finestra. |
| Accordo | Cliccando si crea l'accordo scelto, con la nota cliccata come fondamentale. `Shift+N` torna alle note singole. La stessa lista è `Timbro accordo` nel menu del Piano roll. |
| Scala | Evidenzia le note di una scala. Un pulsante separato aggancia alla scala scelta le note nuove e spostate, e il MIDI in ingresso. Le note spostate si muovono allora per gradi della scala, così un accordo resta nella scala, e `Up` e `Down` spostano di un grado della scala. |
| Slide (`S`) | Le nuove note sono note slide. La nota che sta suonando scivola verso di esse (glide 808). |
| Note fantasma (`Alt+V`) | Mostra dietro alle tue le note degli altri canali. |
| Colore nota | Le nuove note ricevono questo colore. Ce ne sono 16. |
| Segui | La vista scorre con il cursore di riproduzione mentre il pattern suona. |
| Riquadro della corsia | Sceglie cosa modifica la corsia in basso: Velocity, Pan, Intonazione fine, Release, Mod X, Mod Y, Chance o Velocity range. |

## Chance e velocity range

Ogni nota ha una Chance e un Velocity range. La Chance è la probabilità che la nota suoni a ogni passaggio. Il Velocity range lascia variare la velocity della nota tra la sua velocity e quella velocity più il range. Scegli `Probabilità` o `Intervallo di velocity (LYNX)` nel riquadro della corsia per modificarli. `Strumenti › Probabilità (LYNX)` nel menu del Piano roll raggruppa le note selezionate in modo che suonino tutte o ne suoni una sola, e ha `Reimposta probabilità e intervallo di velocity`.

## Strumenti per le note

Il menu `Strumenti` del Piano roll apre gli strumenti per le note di FL Studio come finestre di dialogo. Lavorano sulle note selezionate, o su tutte le note quando non ce n'è nessuna selezionata. Alcuni di essi:

| Strumento | Tasto | Cosa fa |
|---|---|---|
| Limit | `Alt+K` | Sposta le note per ottave in un intervallo di tasti, poi le limita. Sotto `Restrizione di tonalità / scala` mantiene le note su una tonica e uno degli accordi o delle scale di FL Studio, con `Snap` impostato su `Sopra`, `Sotto` o `Alterna`. `Riporta in alto / in basso` e `Trasponi` impostano dove vanno le note fuori intervallo |
| Flip | `Alt+Y` | `Capovolgi orizzontalmente` specchia le note nel tempo. Con `Mantieni i tempi di inizio`, attivo all'inizio, gli istanti di inizio vengono specchiati e le note mantengono la loro lunghezza. `Capovolgi verticalmente` specchia i tasti attorno al centro del loro intervallo |
| Randomise | `Alt+R` | Variazioni casuali di velocity, pan, intonazione fine, timing e lunghezza, e di Mod X, Mod Y e release |
| Scale levels | `Alt+X` | Scala il livello di una nota dall'inizio alla fine della selezione, con `Offset`, `Centro` (il valore attorno a cui scala) e `Tensione` |
| LFO | `Alt+O` | Scrive la forma di un LFO in un livello di nota. Clic destro su uno slider `Periodo` per un elenco da 1 step a 512 step |

## Strumenti MIDI e script

`Strumenti › Strumenti MIDI...` nel menu del Piano roll apre un pannello con Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp e Velocity Shaper.

La colonna `Script` del menu `Strumenti` elenca gli script del Piano roll: quelli propri di LYNX SHARE e i file `.pyscript`, compresi quelli nelle cartelle degli script di FL Studio se FL Studio è installato. `Apri la cartella degli script...` apre la cartella per i tuoi script, e `Esegui di nuovo l'ultimo script` ripete l'ultimo.

## Tasti

| Comando | Tasto | Cosa fa |
|---|---|---|
| Seleziona tutto | `Ctrl+A` | Seleziona ogni nota |
| Deseleziona | `Ctrl+D` o `Esc` | Cancella la selezione |
| Copia, taglia, incolla | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, taglia o incolla le note |
| Duplica | `Ctrl+B` | Duplica la selezione |
| Elimina | `Del` | Elimina la selezione |
| Trasponi | `Up` / `Down` | Sposta la selezione di un semitono |
| Trasponi di un'ottava | `Ctrl+Up` / `Ctrl+Down` | Sposta la selezione di un'ottava |
| Sposta di poco | `Left` / `Right` | Sposta la selezione di un passo di griglia |
| Quantizzazione rapida | `Ctrl+Q` | Quantizza inizi e durate |
| Quantizza gli inizi | `Shift+Q` | Quantizza solo gli inizi |
| Spezza | `Ctrl+U` | Spezza le note sulla griglia |
| Glue | `Ctrl+G` | Unisce le note che si toccano |
| Legato | `Ctrl+L` | Fa durare ogni nota fino alla successiva |
| Mute, riattiva | `Alt+M` / `Shift+Alt+M` | Mette in mute o riattiva la selezione |
| Importa MIDI | `Ctrl+M` | Importa un file MIDI in questo canale |
| Esporta MIDI | `Ctrl+Shift+M` | Esporta il pattern come file MIDI |
| Zoom | `PageUp` / `PageDown` | Ingrandisce o riduce |

Gli strumenti per le note (Arpeggia, Strum, Capovolgi, Randomizza, Quantizza, Spezza e altri) aprono finestre di dialogo. I loro tasti sono nella pagina [Scorciatoie](/wiki/shortcuts/).

## Da sapere

- Il Piano Roll mostra un canale in un pattern. Cambia il pattern corrente e mostra le note di quel pattern.
- Le lettere degli strumenti non fanno nulla mentre il pianoforte sulla tastiera del PC (`Ctrl+T`) è attivo.
- Il clic destro elimina con ogni strumento, quindi non serve passare alla Gomma per una sola nota.
- `S` qui è l'interruttore Slide. Nella Playlist la stessa lettera è lo strumento Slip.
- Le note che non sono semplici step (altri tasti, note più lunghe, slide) fanno mostrare alla riga del canale nel Channel Rack una miniatura invece dei pulsanti step.

## Correlati

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Scorciatoie](/wiki/shortcuts/)
- [Formati di file](/wiki/file-formats/)
