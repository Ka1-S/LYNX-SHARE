---
source: 8a120ab7de
---
Ogni versione ha build per Windows, macOS e Linux nella [pagina delle release](https://github.com/Ka1-S/LYNX-SHARE/releases). I file sono costruiti dal sorgente taggato da GitHub Actions ed elencati con il loro SHA-256 in `SHA256SUMS.txt`. La pagina [Download](/download/) rimanda a quelli attuali.

## Installare su Windows

Windows 10 o 11, 64 bit.

1. Scarica `LYNX-<version>-setup-x64.exe`.
2. Eseguilo. Si installa solo per il tuo utente e non richiede diritti di amministratore.
3. SmartScreen può avvisare, perché l'eseguibile non è ancora firmato. Scegli `More info`, poi `Run anyway`.
4. Avvia LYNX SHARE dal menu Start o dal collegamento sul desktop.

L'installer rende LYNX SHARE il programma che apre i progetti `.lynx`. Offre LYNX SHARE sotto *Apri con* per i file `.flp` senza cambiare il programma predefinito di FL Studio.

Ci sono altri due modi:

| Modo | File o posto | Cosa fa |
|---|---|---|
| Zip portatile | `LYNX-<version>-windows-x64.zip` | Estrailo dove vuoi ed esegui `LYNX.exe`. Non si installa nulla. |
| Microsoft Store | [LYNX SHARE sullo Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Lo Store lo installa e lo tiene aggiornato. |

## Installare su macOS

macOS 11 Big Sur o successivo. Un unico `LYNX-<version>-macos-universal.dmg` copre Intel e Apple silicon.

1. Apri il `.dmg` e trascina LYNX SHARE in *Applicazioni*.
2. L'app non è notarizzata da Apple e non ha una firma Developer ID, quindi il primo avvio richiede un passaggio in più. Fai clic destro (o Control-clic) su LYNX SHARE in *Applicazioni*, scegli *Apri*, poi di nuovo *Apri*.
3. Su macOS 15 Sequoia o successivo, prova ad aprirlo una volta, poi vai in *Impostazioni di Sistema › Privacy e sicurezza* e fai clic su *Apri comunque*.

L'alternativa da Terminale:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS chiede l'accesso al microfono la prima volta che registri.

## Installare su Linux

x86_64 con glibc 2.35 o successiva (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch o Manjaro aggiornati). LYNX SHARE richiede X11 o XWayland. L'audio passa da PipeWire, JACK o ALSA, e per questo non serve installare nulla di extra.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Alcune distribuzioni hanno bisogno di libfuse2 per eseguire le AppImage (`libfuse2` su Ubuntu 22.04, `libfuse2t64` su 24.04, `fuse2` su Arch, `fuse-libs` su Fedora). Al primo avvio la configurazione guidata propone di aggiungere LYNX SHARE al menu delle applicazioni e di aprire con esso i file `.lynx`. Puoi cambiarlo più tardi in `Opzioni › Impostazioni generali...`, o da un terminale con `./LYNX-*.AppImage --menu-entry on` (oppure `off`).

Se preferisci non installare libfuse2, usa `LYNX-<version>-linux-x86_64.tar.gz`: estrailo ed esegui `./install.sh`. Questo installa LYNX SHARE per il tuo utente sotto `~/.local` con una voce di menu, un'icona e il tipo di file `.lynx`.

## Dove sono conservate le impostazioni

| Sistema | Cartella delle impostazioni |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (oppure sotto `$XDG_CONFIG_HOME`) |

La cartella contiene le tue opzioni, la lingua scelta (`language.txt`), le tue scorciatoie (`keys.json`), le tue cartelle dei plugin (`plugin-folders.txt`) e l'elenco dei plugin scansionati (`plugins.xml`).

## Aggiornare

All'avvio LYNX SHARE chiede a GitHub il numero dell'ultima versione. Non viene inviato nulla su di te. Quando esce una nuova versione, una barra mostra `LYNX SHARE <version> is available` con un pulsante `Novità` e un pulsante di aggiornamento. `Aiuto › Cerca aggiornamenti...` lo chiede in qualsiasi momento.

| Build | Come si aggiorna |
|---|---|
| Installer per Windows | `Aggiorna` scarica il nuovo installer, chiede delle modifiche non salvate, installa e riavvia |
| Zip portatile per Windows, macOS | `Aggiorna` scarica il nuovo file e lo mostra nella sua cartella |
| AppImage per Linux | `Aggiorna` aggiorna il file AppImage sul posto. Gear Lever e AppImageUpdate possono fare lo stesso dall'esterno |
| `.tar.gz` per Linux | `Scarica...` apre la pagina di download |
| Microsoft Store | Lo Store lo aggiorna. Questa build non ha un controllo degli aggiornamenti proprio |

Per disattivare il controllo, apri `Opzioni › Impostazioni generali...` e deseleziona `Cerca aggiornamenti automaticamente`.

## Rimuovere

- Installer per Windows: disinstalla LYNX SHARE dall'elenco *App e funzionalità* di Windows.
- Zip portatile: elimina la cartella che hai estratto.
- `.tar.gz` per Linux: esegui `./install.sh --uninstall`.

## Da sapere

- Le build per Windows non sono firmate finché la richiesta di firma del codice del progetto non viene accettata. L'app per macOS ha solo una firma ad-hoc. I pacchetti Linux non sono firmati.
- Per verificare un download, confronta il suo SHA-256 con la riga in `SHA256SUMS.txt` nella pagina della release.
- L'installer, lo zip, il `.dmg` e il `.tar.gz` includono il progetto demo `Demo.lynx`.
- Chiudere la barra degli aggiornamenti con il suo pulsante di chiusura salta quella versione.

## Correlati

- [Da qui si parte](/wiki/getting-started/)
- [Configurazione audio](/wiki/audio-setup/)
- [Risoluzione dei problemi](/wiki/troubleshooting/)
- [Download](/download/)
- [Aggiornamenti](/updates/)
