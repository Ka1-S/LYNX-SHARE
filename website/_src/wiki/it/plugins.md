---
source: be0fc22373
---
![La parte bassa della Gestione plugin, con la nota che ogni file di plugin viene esaminato in un processo separato](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE ospita plugin di strumenti ed effetti di altri produttori. Li trova scansionando le cartelle dei plugin, e `Opzioni › Gestione plugin...` mostra cosa ha trovato. Gli strumenti e gli effetti propri di LYNX SHARE sono descritti in [Strumenti ed effetti integrati](/wiki/built-in/).

## Cercare e aggiungere un plugin

1. Scegli `Opzioni › Cerca plugin`. L'avanzamento appare nella barra dei suggerimenti e puoi continuare a lavorare.
2. Se il plugin è in una cartella a parte, apri `Opzioni › Gestione plugin...`, fai clic su `Cartelle dei plugin...`, aggiungi la cartella, poi fai clic su `Trova nuovi plugin`.
3. Premi `F8` per il selettore plugin e scrivi una parte del nome del plugin.
4. Fai doppio clic su di esso o premi `Enter`. Uno strumento diventa un nuovo canale. Un effetto va nell'insert del Mixer selezionato.

Al primo avvio, la configurazione guidata elenca le cartelle dei plugin e le scansiona in background. `Opzioni › Configurazione guidata (lingua, audio, cartelle dei plugin)...` la riapre. LYNX SHARE cerca nuovi plugin anche alcuni secondi dopo ogni avvio.

## Formati per sistema

| Sistema | Formati ospitati |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

Il VST2 non è ospitato su macOS. Su Linux, i plugin per Windows funzionano con [yabridge](https://github.com/robbert-vdh/yabridge) e Wine, che installi tu: aggiungi la cartella con cui yabridge si sincronizza come cartella dei plugin.

## La Gestione plugin

| Controllo | Cosa fa |
|---|---|
| Casella di ricerca | Filtra per nome, produttore, formato o file |
| `Tutti`, `Strumenti`, `Effetti`, `Preferiti`, `Non riusciti` | Scelgono quali plugin vengono elencati |
| Colonna con la stella | Segna un preferito |
| Colonna `Bit` | 32, 64 o 32+64 (Windows) |
| Colonna `Stato` | `OK`, `Non trovato` o `Non caricato` |
| `Trova nuovi plugin` | Cerca i file che non sono ancora nell'elenco |
| `Scansiona tutto...` | Dimentica i file falliti ed esamina di nuovo ogni file |
| `Cartelle dei plugin...` | Modifica le cartelle che vengono scansionate |
| `Aggiungi plugin...` | Aggiunge un singolo file di plugin |

Ogni file di plugin viene esaminato in un processo separato. Un file che va in crash o si blocca per 30 secondi viene segnato come fallito e saltato in seguito. Su Windows i file falliti sono elencati con il motivo in `%APPDATA%\LYNX\scan-failures.log`.

Vengono scansionate le cartelle standard, più quelle che aggiungi:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` e `Steinberg\VSTPlugins` sotto Program Files, e le cartelle che usa FL Studio se è installato.
- macOS: `VST3`, `Components` e `CLAP` in `~/Library/Audio/Plug-Ins` e `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` e le stesse cartelle sotto `/usr/lib` e `/usr/local/lib`.

## Finestre dei plugin agganciate e separate

L'editor di un plugin si apre dentro la finestra principale, come gli altri pannelli. `Separato` nel menu della finestra del plugin dà a quel plugin una finestra propria, e `Fissa in primo piano` nello stesso menu tiene una finestra separata sopra le altre finestre di LYNX SHARE.

`Separa tutti i nuovi plugin` in `Opzioni › Impostazioni generali...` apre separato ogni plugin che aggiungi. `Strumenti › Passa tutti gli editor dei plugin a` imposta `Agganciato` o `Separato` per tutti i plugin del progetto.

## Eseguire i plugin in un processo separato

Di default un plugin gira dentro LYNX SHARE, quindi un plugin che va in crash può far cadere il programma. Come opzione, ogni plugin gira nel suo processo.

1. Apri `Opzioni › Impostazioni generali...`.
2. Attiva `Esegui i plugin in un processo separato (protegge LYNX SHARE dai crash)`.
3. Riapri il progetto per applicarlo ai plugin già aperti.

Se un plugin così va in crash o smette di rispondere, viene silenziato, appare un messaggio e LYNX SHARE continua a funzionare. `Ricarica` in quel messaggio riavvia il plugin con le impostazioni dell'ultimo salvataggio o backup, senza riaprire il progetto. L'editor del plugin si apre nella sua finestra e non può essere agganciato né fissato.

Per farlo solo per un plugin, usa `Esegui in un processo separato` o `Always run <plugin> in a separate process` nel menu del wrapper della finestra del plugin. Quando l'editor di un plugin va in crash, il messaggio che appare propone anche la seconda.

Funziona su Windows e Linux, e su macOS dalla 0.1.25.

Un plugin che va in crash dentro LYNX SHARE viene disattivato, e anche il suo messaggio offre `Ricarica`. In seguito usa `Ricarica il plugin bloccato` nel menu del canale o nel menu dello slot dell'effetto.

## Plugin a 32 bit (solo Windows)

Su Windows, i plugin VST2 e VST3 a 32 bit girano attraverso un bridge (`LYNX-bridge32.exe`). Il bridge non aggiunge latenza. Un plugin che va in crash al suo interno si ferma da solo mentre LYNX SHARE continua a suonare. L'editor di un plugin con bridge si apre nella sua finestra e non può essere agganciato né fissato. Quando sono installate sia una build a 32 bit sia una a 64 bit di un plugin, `Dimensione bit` in `Impostazioni del wrapper` della finestra del plugin sceglie quale caricare.

## Monitor delle prestazioni dei plugin

![Il Monitor delle prestazioni dei plugin: una riga per ogni strumento ed effetto con la sua quota di tempo di elaborazione](/assets/img/wiki/plugin-monitor.webp)

`Strumenti › Monitor delle prestazioni dei plugin...` elenca ogni strumento ed effetto con il tempo di elaborazione che richiede: adesso, in media e al picco. Il 100% significa un core del processore occupato per tutta la durata dell'audio.

| Impostazione | Cosa fa |
|---|---|
| `Trova` | Filtra per plugin, canale o insert |
| `Solo plugin` | Nasconde gli strumenti e gli effetti propri di LYNX SHARE |
| `Blocca` | Smette di aggiornare i numeri |
| `Azzera alla riproduzione` | Riparte con medie e picchi a ogni avvio della riproduzione |
| `Ripristina` | Azzera subito medie e picchi |

## Inviare le uscite di uno strumento a insert separati

Un plugin strumento con più uscite può suonare ciascuna su un proprio insert del Mixer. Nella finestra del plugin clicca `Impostazioni del wrapper`, poi la scheda `Elaborazione`. Sotto `Connessioni`, ogni uscita ha una luce che la attiva o la disattiva e un numero: su quanti insert dopo l'insert del canale suona. `Mappa uscite automaticamente` assegna a ogni uscita l'insert successivo, e `Ripristina` torna ai valori predefiniti.

## La cartella Plugin database

Le cartelle del Plugin database del [Browser](/wiki/browser/), del selettore plugin e dei menu dei plugin sono una cartella su disco: `LYNX/Plugin database` nella cartella Documenti. Per aprirla, clicca con il tasto destro su `Database dei plugin` nel Browser e scegli `Apri`.

- `Effetti` e `Generatori` contengono un file `.lynxplugin` per ogni plugin. Crea cartelle e sottocartelle (fino a 4 livelli) e sposta i file al loro interno.
- Rinomina un file per rinominare il plugin nei menu. Elimina un file per togliere il plugin dai menu. Il plugin non viene disinstallato.
- `Installati` ha una voce per ogni plugin. Copiane una in `Effetti` o `Generatori` per riportare un plugin.

## Aprire un progetto senza i suoi plugin

`File › Apri senza plugin di terze parti...` apre un progetto LYNX SHARE con ogni plugin di terze parti disattivato, per un progetto che non si apre o va in crash durante il caricamento. Le impostazioni salvate dei plugin vengono conservate. `File › Riattiva i plugin...` li riattiva.

## Quando un plugin non è installato

LYNX SHARE cerca prima lo stesso plugin per identificatore, poi per file, poi per nome. Un progetto salvato con il VST2 si apre con il VST3 dello stesso prodotto e viceversa, ma allora il plugin suona con le impostazioni predefinite.

Se un effetto non è installato affatto, un effetto LYNX SHARE con lo stesso ruolo ne prende lo slot e l'originale resta nel progetto. Dove le impostazioni salvate del plugin si possono leggere, il sostituto parte con impostazioni corrispondenti. L'opzione è `Sostituisci i plugin mancanti con equivalenti LYNX SHARE` in `Opzioni › Impostazioni generali...`. Vedi [Strumenti ed effetti integrati](/wiki/built-in/). `Aiuto › File mancanti...` elenca i plugin che sono stati sostituiti.

## Da sapere

- La finestra di un plugin ospitato ha una barra dei preset: i programmi propri del plugin, i tuoi preset, `Salva preset...`, `Carica file di preset...` e `Esporta file di preset...`. I file `.fxp`, `.fxb` e `.vstpreset` si caricano solo in un plugin che gira dentro LYNX SHARE, non in un processo separato.
- Il monitor misura solo mentre la sua finestra è aperta.
- Quando la finestra di un plugin ha il focus, un tasto che il plugin non usa suona il pianoforte sulla tastiera del PC. Su Windows, con il pulsante della tastiera nell'intestazione della finestra disattivato, LYNX SHARE gestisce i tasti prima del plugin, scorciatoie comprese.
- Il menu dei parametri di un plugin VST3 comincia con le voci di LYNX SHARE, come `Crea clip di automazione`. Vedi [Automazione](/wiki/automation/).
- `MPE` sotto `MIDI` in `Impostazioni del wrapper` di un plugin manda ogni nota sul proprio canale MIDI. Vedi [Controller MIDI](/wiki/midi-controllers/).
- Gli strumenti mancanti non vengono ancora sostituiti. Un plugin strumento mancante diventa un synth integrato scelto in base al nome del canale.

## Correlati

- [Strumenti ed effetti integrati](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
