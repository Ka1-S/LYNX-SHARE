---
source: 841fdd7d67
---
![La pagina Generale della finestra Impostazioni, con le sette schede delle pagine in alto](/assets/img/wiki/settings-general.webp)

La finestra Impostazioni ha sette pagine: Audio, MIDI, Generale, Tema, File, Progetto e Info. Le prime cinque appartengono al programma. Progetto e Info appartengono al progetto aperto e vengono salvate con esso. Ogni pagina ha una propria voce in cima al menu `Opzioni`.

## Cambiare un'impostazione

1. Apri il menu `Opzioni` e scegli una pagina, per esempio `Impostazioni generali...`.
2. Clicca un'altra scheda in alto per passare a un'altra pagina.
3. Cambia l'impostazione. Si applica subito, tranne la lingua e il carattere dell'interfaccia, che si applicano al riavvio di LYNX SHARE.

`F10` apre la pagina MIDI e `F11` la pagina Info.

## Audio

`Opzioni › Impostazioni audio...`. Driver, dispositivi, frequenza di campionamento, dimensione del buffer e registrazione. [Configurazione audio](/wiki/audio-setup/) descrive questa pagina.

## MIDI

![La pagina MIDI: dispositivi di ingresso, dispositivi di uscita con sincronia master e sincronia esterna](/assets/img/wiki/settings-midi.webp)

`Opzioni › Impostazioni MIDI...`

| Impostazione | Cosa fa |
|---|---|
| Dispositivi di ingresso | Ogni dispositivo può essere disattivato, ascoltare un solo canale MIDI e suonare un canale fisso del Channel Rack invece di quelli selezionati |
| `Aggiorna dispositivi` | Cerca di nuovo i dispositivi |
| `Invia sincronia master` | Invia il clock MIDI con Song Position Pointer, o MIDI Time Code, a un dispositivo di uscita. `Offset (ms)` lo invia più tardi o prima |
| `Invia all notes off` | Invia all notes off al dispositivo all'inizializzazione e dopo un'esportazione |
| `Sincronia a clock esterno` | LYNX SHARE segue il clock MIDI dell'ingresso scelto: tempo, start, continue, stop e posizione |
| `Velocity` | Per le note dai dispositivi MIDI: come suonata, morbida, dura o fissa |
| `Canale della modalità performance` | Le note su questo canale lanciano le clip della Playlist in modalità performance |
| `Salto a marker del brano` | Le note su questo canale spostano il brano a un marker invece di suonare |
| `Pickup (modalità takeover)` | Un controller muove un parametro solo dopo aver raggiunto il suo valore attuale |
| Collegamenti | I collegamenti ai controller di questo progetto, poi quelli globali, con `Rimuovi collegamento` e `Rimuovi tutti` |

## Generale

`Opzioni › Impostazioni generali...`

| Impostazione | Cosa fa |
|---|---|
| `Stile del layout` | Flusso a pattern (predefinito) o flusso a lancio di clip |
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
| `Livelli massimi di annullamento` | Da 25 a 1000 |
| Backup | Ogni quanto viene salvata una copia di backup, quante se ne conservano per progetto, e `Apri cartella dei backup` |
| `Esegui i plugin in un processo separato` | Un plugin che va in crash non trascina giù LYNX SHARE. Disattivato per impostazione predefinita |
| `Sostituisci i plugin mancanti con equivalenti LYNX SHARE` | Attivo per impostazione predefinita. Il progetto conserva il plugin originale |
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

## Correlati

- [Configurazione audio](/wiki/audio-setup/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Scorciatoie](/wiki/shortcuts/)
- [Plugin](/wiki/plugins/)
- [Layout](/wiki/layouts/)
