---
source: a3748d4a2a
---
Ogni titolo qui sotto è un problema. Se il tuo non c'è, segnalalo come descritto alla fine.

## All'avvio del programma

### Windows: SmartScreen avvisa al primo avvio

L'eseguibile non è ancora firmato, quindi SmartScreen può avvisare la prima volta che lo esegui.

1. Fai clic su `More info`.
2. Fai clic su `Run anyway`.

L'installer non richiede diritti di amministratore.

### macOS: "non può essere aperta"

L'app non è notarizzata da Apple e non ha una firma Developer ID, quindi il primo avvio richiede un passaggio in più.

1. Trascina LYNX SHARE in `Applications`.
2. Fai clic destro (Control-clic) su di essa e scegli `Apri`.
3. Fai di nuovo clic su `Apri`.

Su macOS 15 Sequoia o successivo, prova ad aprirla una volta, poi vai in `System Settings › Privacy & Security` e fai clic su `Open Anyway`. Oppure, da Terminale:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS chiede l'accesso al microfono la prima volta che registri.

### Linux: l'AppImage non parte

LYNX SHARE richiede un sistema x86_64 con glibc 2.35 o successiva (Ubuntu 22.04, Debian 12, Fedora 36, Arch o Manjaro aggiornati) e X11 o XWayland.

1. Rendi il file eseguibile ed eseguilo, con i due comandi qui sotto.
2. Se ancora non parte, installa libfuse2. Il pacchetto è `libfuse2` su Ubuntu 22.04, `libfuse2t64` su Ubuntu 24.04, `fuse2` su Arch e `fuse-libs` su Fedora.
3. Se preferisci non installarlo, scarica invece il `.tar.gz`, estrailo ed esegui `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` installa LYNX SHARE per il tuo utente sotto `~/.local` con una voce di menu. `./install.sh --uninstall` la rimuove.

## Suono

### Nessun suono, o dispositivo audio sbagliato

1. Apri `Opzioni › Impostazioni audio...`.
2. Scegli un driver.
3. Scegli un dispositivo di uscita.

| Sistema | Driver |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO e il driver ASIO LYNX integrato |
| macOS | Core Audio |
| Linux | PipeWire (nativo), JACK e ALSA. Al primo avvio LYNX SHARE apre il primo di questi che ha dispositivi |

ASIO è un modello di driver di Windows ed esiste solo su Windows. Anche `Opzioni › Configurazione guidata (lingua, audio, cartelle dei plugin)...` prova il dispositivo audio. Vedi [Configurazione audio](/wiki/audio-setup/).

### Linux: il suono si interrompe

Il thread audio dovrebbe girare con priorità real-time.

1. Apri `Opzioni › Impostazioni audio...` e leggi `Stato`. Mostra se il thread ha ottenuto la priorità real-time.
2. Se non l'ha ottenuta, installa rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` o `sudo pacman -S rtkit`), oppure aggiungiti al gruppo `audio` con un limite real-time, come nelle configurazioni JACK.
3. Esci e rientra nella sessione.

I dettagli sono in [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

## Plugin

### Un plugin va in crash

Eseguilo nel suo processo, così si ferma solo il plugin.

1. Apri `Opzioni › Impostazioni generali...`.
2. Attiva `Esegui i plugin in un processo separato (protegge LYNX SHARE dai crash)`.
3. Riapri il progetto.

Un plugin che è andato in crash o ha smesso di rispondere nel suo processo viene silenziato. Salva il progetto e riaprilo per caricare di nuovo il plugin. Vedi [Plugin di terze parti](/wiki/plugins/).

### Un plugin non è nell'elenco

1. Scegli `Opzioni › Cerca plugin`.
2. Se ancora manca, apri `Opzioni › Gestione plugin...`, fai clic su `Cartelle dei plugin...` e aggiungi la cartella del plugin.
3. Guarda la scheda `Non riusciti`. Un file di plugin che non supera la scansione viene saltato. Su Windows il motivo è in `%APPDATA%\LYNX\scan-failures.log`.
4. Fai clic su `Scansiona tutto...` per riprovare i file falliti.

### Un progetto usa un plugin che non è installato

Un effetto viene sostituito da un effetto LYNX SHARE con lo stesso ruolo, e le sue impostazioni restano nel progetto. `Aiuto › File mancanti...` elenca ciò che manca. Vedi [Strumenti ed effetti integrati](/wiki/built-in/).

### Il video non funziona su Linux

Le tracce video e `Export with video` richiedono FFmpeg. LYNX SHARE dice come installarlo.

## Report di crash

![La domanda sul report di crash mostrata al prossimo avvio, con Send, Show what will be sent e Don't send](/assets/img/wiki/crash-report.webp)

I report di crash sono opt-in. Dopo un crash o un blocco, il prossimo avvio chiede se inviare un report.

| Comando | Cosa fa |
|---|---|
| `Mostra cosa verrà inviato` | Mostra il testo esatto |
| `Segnala invece su GitHub` | Apre il modulo delle issue di GitHub, già compilato. I file li alleghi tu |
| `Apri la cartella con il report` | Mostra i file del report sul tuo computer |
| `Include file names (projects, samples)` | Se è spento, viene inviata solo l'estensione di ogni file |
| `Invia sempre senza chiedere` | Invia i report futuri senza fare la domanda |

`Report di crash` in `Opzioni › Impostazioni generali...` imposta `Ask` (il valore predefinito), `Invia sempre` o `Non inviare mai`.

Un report contiene la versione di LYNX SHARE, il sistema operativo, la CPU, le schede grafiche e i loro driver, la dimensione dello schermo, il driver e il dispositivo audio, il file del crash o del blocco, gli ultimi 60 passaggi della sessione e i nomi e le versioni dei plugin di terze parti che erano caricati.

Non contiene audio, progetti, sample, il nome del tuo computer o il tuo nome utente. Il server non memorizza alcun indirizzo IP. Gli id inviati sono elencati in `crash-reports\sent.log` nella cartella delle impostazioni. Per far cancellare un report, apri una issue con il suo id. Il testo completo è nell'[informativa sulla privacy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Segnalare un problema

![La finestra Segnala un problema: una casella di descrizione, un indirizzo e-mail facoltativo e il pulsante Send](/assets/img/wiki/report-problem.webp)

1. Scegli `Aiuto › Segnala un problema...`.
2. Descrivi cosa è successo. L'indirizzo e-mail è facoltativo, per quando vuoi una risposta.
3. Lascia selezionato `Allega i file di crash / blocco più recenti e i passaggi di questa sessione` se il problema era un crash.
4. Fai clic su `Mostra cosa verrà inviato` per controllare il testo.
5. Fai clic su `Invia`. Prima di allora non viene inviato nulla.

Puoi anche aprire una issue su [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Da sapere

- Dopo un crash, le copie di backup del tuo progetto sono nella cartella dei salvataggi automatici di LYNX SHARE.
- Quando un plugin ha partecipato al crash, la domanda propone di eseguire da ora in poi quel plugin in un processo separato.
- La cartella delle impostazioni è `%APPDATA%\LYNX` su Windows, `~/Library/Application Support/LYNX` su macOS e `~/.config/LYNX` su Linux.

## Correlati

- [Configurazione audio](/wiki/audio-setup/)
- [Plugin di terze parti](/wiki/plugins/)
- [Installazione](/wiki/install/)
- [FAQ](/faq/)
- [Protezione dai crash](/why/#crash)
