---
source: ab2c2f14eb
---
![La pagina Generale della finestra Impostazioni, con le schede delle pagine in alto](/assets/img/wiki/settings-general.webp)

La finestra Impostazioni ha otto pagine: Audio, MIDI, Generale, Tema, File, LYNX extras, Progetto e Info. Le prime sei appartengono al programma. Progetto e Info appartengono al progetto aperto e vengono salvate con esso. Ogni pagina ha una propria voce in cima al menu `Opzioni`.

## Cambiare un'impostazione

1. Apri il menu `Opzioni` e scegli una pagina, per esempio `Impostazioni generali...`.
2. Clicca un'altra scheda in alto per passare a un'altra pagina.
3. Cambia l'impostazione. Si applica subito, tranne la lingua e il carattere dell'interfaccia, che si applicano al riavvio di LYNX SHARE.

`F10` apre la pagina MIDI e `F11` la pagina Info.

## Audio

`Opzioni › Impostazioni audio...`. Driver, dispositivi, frequenza di campionamento, dimensione del buffer e registrazione. [Configurazione audio](/wiki/audio-setup/) descrive questa pagina. Sotto `Registrazione`, `Formato del file` offre ora anche `WAV a 16 bit (la frequenza di campionamento del dispositivo)`.

## MIDI

![La pagina MIDI: dispositivi di ingresso, dispositivi di uscita con sincronia master e sincronia esterna](/assets/img/wiki/settings-midi.webp)

`Opzioni › Impostazioni MIDI...`

| Impostazione | Cosa fa |
|---|---|
| Dispositivi di ingresso | Ogni dispositivo può essere disattivato, ascoltare un solo canale MIDI e suonare un canale fisso del Channel Rack invece di quelli selezionati |
| `Tipo di controller` | Per ogni dispositivo di ingresso: `Rileva automaticamente`, `Controller generico` o un profilo di controller, come Mackie Control. La riga accanto indica la porta che riceve le luci e il display |
| `Controller MPE` | In fondo alla lista dei canali MIDI di un dispositivo. Ogni nota arriva con il proprio bend, timbro e pressione. Un secondo riquadro imposta i canali membro |
| `Aggiorna dispositivi` | Cerca di nuovo i dispositivi e rilegge i tuoi profili di controller |
| `Cartella dei profili controller` | Apre la cartella per i tuoi profili di controller (`*.lynxctl`) |
| `Invia sincronia master` | Invia il clock MIDI con Song Position Pointer, o MIDI Time Code, a un dispositivo di uscita. `Offset (ms)` lo invia più tardi o prima |
| `Invia all notes off` | Invia all notes off al dispositivo all'inizializzazione e dopo un'esportazione |
| `Sincronia a clock esterno` | LYNX SHARE segue il clock MIDI dell'ingresso scelto: tempo, start, continue, stop e posizione |
| `Velocity` | Per le note dai dispositivi MIDI: come suonata, morbida, dura o fissa |
| `Canale della modalità performance` | Le note su questo canale lanciano le clip della Playlist in modalità performance |
| `Salto a marker del brano` | Le note su questo canale spostano il brano a un marker invece di suonare |
| `Pickup (modalità takeover)` | Un controller muove un parametro solo dopo aver raggiunto il suo valore attuale |
| `Il pedale controlla il rilascio delle note` | Attivo per impostazione predefinita: i pedali di sustain e sostenuto trattengono le note. Disattivato, il pedale arriva ai plugin |
| `Usa un controller di gioco` | Un controller da gioco comanda il trasporto e si può collegare come un controller MIDI. Disattivato per impostazione predefinita |
| Collegamenti | I collegamenti ai controller di questo progetto, poi quelli globali, con `Rimuovi collegamento` e `Rimuovi tutti` |

## Generale

`Opzioni › Impostazioni generali...`

| Impostazione | Cosa fa |
|---|---|
| `Stile del layout` | Flusso a pattern (predefinito), flusso a lancio di clip o flusso semplice |
| `Modello predefinito`, `Progetto all'avvio` | Il modello che si apre con `File › Nuovo` e cosa si apre all'avvio: `Progetto vuoto`, `Modello predefinito` o `Ultimo progetto usato`. `File › Salva come modello...` aggiunge i tuoi |
| `Clip di sessione dei set di Live` | Dove vanno le clip di sessione di un set di Live importato: la griglia di clip, o la Playlist dopo l'arrangiamento |
| `Lingua`, `Carattere dell'interfaccia` | Applicati al prossimo avvio |
| `Dimensione dell'interfaccia`, `Densità dei menu` | Scala dell'intera interfaccia, e menu compatti o comodi |
| `Forme d'onda` | Pulito (il colore della clip) o colori spettrali, a scelta con i canali stereo separati |
| `Nomi delle note`, `Tasti neri` | Nomi inglesi, germanici o in solfeggio, con diesis o bemolle |
| `Stile dei plugin` | L'aspetto delle finestre dei plugin integrati: automatico, o uno stile per tutti i plugin |
| `Animazioni`, `Frequenza di animazione` | Le animazioni dell'interfaccia, e quanto spesso si muovono il cursore di riproduzione e gli indicatori |
| `Disegna con la GPU (Direct2D)` | Disattivala se il driver grafico va in crash o lo schermo sfarfalla |
| Avvio | Schermata iniziale, suono di avvio e ricerca di nuovi plugin in background a ogni avvio |
| Modifica | Tastiera del PC come pianoforte, mantenere aperti gli editor dei canali, ricordare il tempo di ricerca, mettere in loop le clip di pattern allungate |
| `Separa tutti i nuovi plugin` | Attivo: la finestra di un plugin che aggiungi si apre separata, sopra i pannelli. Disattivato (predefinito): si apre agganciata nella finestra principale |
| `Pittura monofonica nel piano roll` | Attivo: lo strumento Paint sostituisce le note sotto ogni nuova nota, e Shift dipinge sopra di esse. Disattivato: il contrario |
| `Seleziona automaticamente i moduli collegati` | Dare il focus alla finestra di un plugin seleziona la sua traccia del Mixer. Attivo per impostazione predefinita |
| `Disattiva per impostazione predefinita la priorità della tastiera di digitazione` | Per i plugin che aggiungi da quel momento. Disattivato per impostazione predefinita |
| `Livelli massimi di annullamento` | Da 25 a 1000 |
| `Gestisci messaggi` | Sotto `Suggerimenti e avvisi`. I messaggi spuntati vengono mostrati. Togli la spunta a uno per saltare la sua domanda |
| Backup | Ogni quanto viene salvata una copia di backup, quante se ne conservano per progetto, e `Apri cartella dei backup` |
| `Esegui i plugin in un processo separato` | Un plugin che va in crash non trascina giù LYNX SHARE. Disattivato per impostazione predefinita |
| `Sostituisci i plugin mancanti con equivalenti LYNX SHARE` | Attivo per impostazione predefinita. Quando un progetto usa un plugin di terze parti che non è installato, si carica il sostituto di LYNX SHARE con impostazioni corrispondenti dove ne ha uno. Il progetto conserva il plugin originale e lo usa di nuovo una volta installato |
| `Limita il numero di editor di plugin aperti` | Restano aperte al massimo 25 finestre di editor di plugin. Attivo per impostazione predefinita |
| `Report di crash` | Chiedi dopo un crash o un blocco (predefinito), invia sempre o non inviare mai |
| `Cerca aggiornamenti automaticamente` | La ricerca di aggiornamenti all'avvio, con `Cerca ora` |

## Tema

`Opzioni › Tema (colori, sfondo)...`

| Impostazione | Cosa fa |
|---|---|
| Riquadro del tema | Sceglie un preset. `Importa...` ed `Esporta...` leggono e scrivono file `.lynxtheme`, `Salva preset...` conserva il tema corrente, `Predefinito` lo ripristina |
| `Colore principale`, `Sfondo`, `Area di lavoro` | I tre colori di un tema. Il colore dell'area di lavoro può seguire automaticamente lo sfondo |
| Immagine dell'area di lavoro | Un PNG, JPEG o GIF dietro le finestre, con `Adattamento immagine`, `Opacità dell'immagine` e `Attenua` |
| Suono di avvio | Un tuo suono di 30 secondi o meno, oppure quello di LYNX SHARE |

## File

`Opzioni › Impostazioni dei file (cartelle di ricerca)...`

| Impostazione | Cosa fa |
|---|---|
| Cartelle di ricerca per i sample mancanti | La finestra File mancanti cerca da sola in queste cartelle e nelle loro sottocartelle |
| Cartelle del Browser | Mostrate nell'elenco cartelle del Browser, e cercate anche per i sample mancanti |
| Cartelle dei plugin | Scansionate in cerca di plugin da `Opzioni › Cerca plugin` |

## LYNX extras

`Opzioni › Extra di LYNX...`. Le aggiunte proprie di LYNX SHARE in un unico posto. Tutte sono disattivate per impostazione predefinita. Ogni interruttore è la stessa impostazione del menu o della pagina che la sua riga indica.

| Impostazione | Cosa fa |
|---|---|
| `Misuratore di loudness accanto al Master (LYNX)` | Loudness a breve termine e integrata (LUFS) e true peak del Master, con un pulsante Reset |
| `Mostra solo gli insert usati` | Nasconde gli insert del Mixer che nessuno usa |
| `Mantieni i collegamenti agli insert eliminati, in grigio (LYNX)` | Eliminare un insert mantiene i collegamenti che andavano in esso, così si possono ricollegare |
| `I gruppi del Mixer seguono la Playlist (LYNX)` | I gruppi del Mixer rispecchiano i gruppi di tracce della Playlist |
| `Corsie di automazione` | L'automazione può stare in corsie sotto una traccia della Playlist |
| `Righello: battute del brano (LYNX)` | Il righello del Piano roll mostra i numeri di battuta del punto del brano in cui suona il pattern |
| `Mostra il valore di un controllo accanto al mouse al passaggio (LYNX)` | Il valore compare accanto al cursore, non solo nella barra dei suggerimenti |
| `Dopo la registrazione in loop, suona solo l'ultimo passaggio` | I passaggi precedenti restano sotto, silenziati |
| `W mostra o nasconde la finestra del dispositivo selezionato` | Un tasto per la Device view |
| `Selezionare un dispositivo seleziona il suo insert nel Mixer` | Il Mixer segue il dispositivo scelto nella Device view |
| `Usa un controller di gioco` | Lo stesso interruttore della pagina MIDI |

## Progetto

![La pagina Progetto: impostazioni di tempo, groove e intonazione, timebase e opzioni della Playlist](/assets/img/wiki/settings-project.webp)

`Opzioni › Impostazioni del progetto (tempo, indicazione di tempo)...`

| Impostazione | Cosa fa |
|---|---|
| `Impostazioni di tempo` | Divisione del tempo (beat per battuta, step per beat) o un'indicazione di tempo tradizionale (numeratore e denominatore) |
| `Tempo`, `Beat per battuta` | Il tempo del progetto e i beat in una battuta |
| `Swing`, `Intonazione master` | La quantità di swing globale e l'intonazione master in centesimi |
| `Dissolvenze incrociate automatiche` | Le clip audio sovrapposte su una traccia fanno una dissolvenza incrociata sulla sovrapposizione |
| `Dissolvenza del comp` | La durata della dissolvenza incrociata usata quando si fa il comp delle take, in millisecondi |
| `Riproduci le note troncate nelle clip` | Una nota che inizia prima di una clip di pattern suona dall'inizio della clip |
| `Declick rapido per i gruppi di taglio` | Rampa più veloce quando i gruppi di taglio tagliano le note |
| `Legge di panoramica` | Circolare (centro a 0 dB, +3 dB sul lato verso cui si fa il pan) o Triangolare (il lato più forte mantiene il suo livello). Per gli insert del Mixer e i canali Sampler |

## Info

`Opzioni › Informazioni sul progetto...`. Titolo, autore, genere, link web e commenti del progetto, e `Mostra questa pagina all'apertura del progetto`. In fondo compaiono la data di creazione del progetto e il tempo di lavoro.

## Da sapere

- La timebase è fissa a 960 tick per semiminima. I progetti di FL Studio vengono convertiti a essa.
- I cambi di indicazione di tempo lungo il brano non sono nella pagina Progetto. Aggiungi un marker di indicazione di tempo sul righello della Playlist.
- `Esegui i plugin in un processo separato` si applica ai plugin aperti da quel momento in poi. Riapri il progetto per spostare gli altri.
- Le impostazioni degli aggiornamenti mancano nella versione del Microsoft Store, perché è lo Store ad aggiornarla.
- Le scorciatoie sono in `Opzioni › Gestisci scorciatoie...`, non in una pagina delle Impostazioni.
- Alcuni interruttori sono nei menu, non in questa finestra. `Bassa latenza in registrazione` è nel menu delle opzioni di registrazione accanto al pulsante di registrazione e nel menu `Registrazione su disco` del Mixer. `Stile della tastiera` è nel menu View del Piano roll.
- Il database dei plugin è una cartella, `Database dei plugin`, nella cartella `LYNX` dei tuoi documenti. Puoi organizzarla nel file manager.

## Correlati

- [Configurazione audio](/wiki/audio-setup/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Scorciatoie](/wiki/shortcuts/)
- [Plugin](/wiki/plugins/)
- [Layout](/wiki/layouts/)
