---
source: 45c1275215
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

## Eseguire i plugin in un processo separato

Di default un plugin gira dentro LYNX SHARE, quindi un plugin che va in crash può far cadere il programma. Come opzione, ogni plugin gira nel suo processo.

1. Apri `Opzioni › Impostazioni generali...`.
2. Attiva `Esegui i plugin in un processo separato (protegge LYNX SHARE dai crash)`.
3. Riapri il progetto per applicarlo ai plugin già aperti.

Se un plugin così va in crash o smette di rispondere, viene silenziato, appare un messaggio e LYNX SHARE continua a funzionare. Salva il progetto e riaprilo per caricare di nuovo il plugin. L'editor del plugin si apre nella sua finestra.

Per farlo solo per un plugin, usa `Esegui in un processo separato` o `Always run <plugin> in a separate process` nel menu del wrapper della finestra del plugin. Quando l'editor di un plugin va in crash, il messaggio che appare propone anche la seconda.

Funziona su Windows e Linux, e su macOS dalla 0.1.25.

## Plugin a 32 bit (solo Windows)

Su Windows, i plugin VST2 e VST3 a 32 bit girano attraverso un bridge (`LYNX-bridge32.exe`). Il bridge non aggiunge latenza. Un plugin che va in crash al suo interno si ferma da solo mentre LYNX SHARE continua a suonare. L'editor di un plugin con bridge si apre nella sua finestra. Quando sono installate sia una build a 32 bit sia una a 64 bit di un plugin, `Dimensione bit` in `Impostazioni del wrapper` della finestra del plugin sceglie quale caricare.

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

## Quando un plugin non è installato

LYNX SHARE cerca prima lo stesso plugin per identificatore, poi per file, poi per nome. Un progetto salvato con il VST2 si apre con il VST3 dello stesso prodotto e viceversa, ma allora il plugin suona con le impostazioni predefinite.

Se un effetto non è installato affatto, un effetto LYNX SHARE con lo stesso ruolo ne prende lo slot e l'originale resta nel progetto. Vedi [Strumenti ed effetti integrati](/wiki/built-in/). `Aiuto › File mancanti...` elenca i plugin che sono stati sostituiti.

## Da sapere

- La finestra di un plugin ospitato ha una barra dei preset: i programmi propri del plugin, i tuoi preset, `Salva preset...`, `Carica file di preset...` e `Esporta file di preset...`. I file `.fxp`, `.fxb` e `.vstpreset` si caricano solo in un plugin che gira dentro LYNX SHARE, non in un processo separato.
- Il monitor misura solo mentre la sua finestra è aperta.
- Gli strumenti mancanti non vengono ancora sostituiti. Un plugin strumento mancante diventa un synth integrato scelto in base al nome del canale.

## Correlati

- [Strumenti ed effetti integrati](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
