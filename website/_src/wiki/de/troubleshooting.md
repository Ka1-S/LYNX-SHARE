---
source: a3748d4a2a
---
Jede Überschrift unten ist ein Problem. Steht deines nicht dabei, melde es wie am Ende beschrieben.

## Das Programm starten

### Windows: SmartScreen warnt beim ersten Start

Die ausführbare Datei ist noch nicht codesigniert, SmartScreen warnt deshalb möglicherweise beim ersten Ausführen.

1. Klicke auf `More info`.
2. Klicke auf `Run anyway`.

Der Installer braucht keine Administratorrechte.

### macOS: „kann nicht geöffnet werden“

Die App ist von Apple nicht notarisiert und hat keine Developer-ID-Signatur, der erste Start braucht daher einen zusätzlichen Schritt.

1. Ziehe LYNX SHARE nach `Applications`.
2. Klicke mit rechts (Control-Klick) darauf und wähle `Öffnen`.
3. Klicke noch einmal auf `Öffnen`.

Versuche unter macOS 15 Sequoia oder neuer, es einmal zu öffnen, gehe dann zu `System Settings › Privacy & Security` und klicke auf `Open Anyway`. Oder, im Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS fragt beim ersten Aufnehmen nach dem Zugriff auf das Mikrofon.

### Linux: Das AppImage startet nicht

LYNX SHARE braucht ein x86_64-System mit glibc 2.35 oder neuer (Ubuntu 22.04, Debian 12, Fedora 36, aktuelles Arch oder Manjaro) und X11 oder XWayland.

1. Mache die Datei ausführbar und führe sie aus, mit den beiden Befehlen unten.
2. Startet sie immer noch nicht, installiere libfuse2. Das Paket heißt `libfuse2` unter Ubuntu 22.04, `libfuse2t64` unter Ubuntu 24.04, `fuse2` unter Arch und `fuse-libs` unter Fedora.
3. Wenn du es lieber nicht installieren möchtest, lade stattdessen das `.tar.gz` herunter, entpacke es und führe `./install.sh` aus.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` installiert LYNX SHARE für deinen Benutzer unter `~/.local`, mit Menüeintrag. `./install.sh --uninstall` entfernt es.

## Ton

### Kein Ton, oder das falsche Audiogerät

1. Öffne `Optionen › Audioeinstellungen...`.
2. Wähle einen Treiber.
3. Wähle ein Ausgabegerät.

| System | Treiber |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO und der eingebaute LYNX-ASIO-Treiber |
| macOS | Core Audio |
| Linux | PipeWire (nativ), JACK und ALSA. Beim ersten Start öffnet LYNX SHARE den ersten davon, der Geräte hat |

ASIO ist ein Treibermodell von Windows und gibt es nur unter Windows. `Optionen › Einrichtungsassistent (Sprache, Audio, Plugin-Ordner)...` testet das Audiogerät ebenfalls. Siehe [Audio einrichten](/wiki/audio-setup/).

### Linux: Der Ton bricht ab

Der Audio-Thread sollte mit Echtzeit-Priorität laufen.

1. Öffne `Optionen › Audioeinstellungen...` und lies `Status`. Dort steht, ob der Thread Echtzeit-Priorität bekommen hat.
2. Wenn nicht, installiere rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` oder `sudo pacman -S rtkit`) oder füge dich mit einem Echtzeit-Limit zur Gruppe `Audio` hinzu, wie bei JACK-Setups.
3. Melde dich ab und wieder an.

Die Einzelheiten stehen in [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

## Plugins

### Ein Plugin stürzt ab

Führe es in einem eigenen Prozess aus, damit nur das Plugin anhält.

1. Öffne `Optionen › Allgemeine Einstellungen...`.
2. Schalte `Plugins in einem separaten Prozess ausführen (schützt LYNX SHARE vor Abstürzen)` ein.
3. Öffne das Projekt erneut.

Ein Plugin, das in seinem eigenen Prozess abgestürzt ist oder nicht mehr antwortet, wird stummgeschaltet. Speichere das Projekt und öffne es erneut, um das Plugin wieder zu laden. Siehe [Plugins von Drittanbietern](/wiki/plugins/).

### Ein Plugin steht nicht in der Liste

1. Wähle `Optionen › Nach Plugins suchen`.
2. Fehlt es immer noch, öffne `Optionen › Plugin-Manager...`, klicke auf `Plugin-Ordner...` und füge den Ordner des Plugins hinzu.
3. Sieh dir den Reiter `Fehlerhaft` an. Eine Plugin-Datei, die bei der Suche scheitert, wird übersprungen. Unter Windows steht der Grund in `%APPDATA%\LYNX\scan-failures.log`.
4. Klicke auf `Alle neu suchen...`, um die fehlgeschlagenen Dateien erneut zu versuchen.

### Ein Projekt nutzt ein Plugin, das nicht installiert ist

Ein Effekt wird durch einen LYNX-SHARE-Effekt mit derselben Aufgabe ersetzt, und seine Einstellungen bleiben im Projekt erhalten. `Hilfe › Fehlende Dateien...` listet auf, was fehlt. Siehe [Eingebaute Instrumente und Effekte](/wiki/built-in/).

### Video funktioniert unter Linux nicht

Videospuren und `Export with video` brauchen FFmpeg. LYNX SHARE sagt, wie du es installierst.

## Absturzberichte

![Die Frage nach dem Absturzbericht beim nächsten Start, mit Send, Show what will be sent und Don't send](/assets/img/wiki/crash-report.webp)

Absturzberichte sind freiwillig. Nach einem Absturz oder Einfrieren fragt der nächste Start, ob ein Bericht gesendet werden soll.

| Befehl | Was er bewirkt |
|---|---|
| `Anzeigen, was gesendet wird` | Zeigt den genauen Text |
| `Stattdessen auf GitHub melden` | Öffnet das Issue-Formular von GitHub, vorausgefüllt. Die Dateien hängst du selbst an |
| `Ordner mit dem Bericht öffnen` | Zeigt die Berichtsdateien auf deinem Computer |
| `Include file names (projects, samples)` | Aus: Es wird nur die Endung jeder Datei gesendet |
| `Immer ohne Nachfrage senden` | Sendet künftige Berichte ohne die Frage |

`Absturzberichte` unter `Optionen › Allgemeine Einstellungen...` stellt `Ask` (Standard), `Immer senden` oder `Nie senden` ein.

Ein Bericht enthält die Version von LYNX SHARE, das Betriebssystem, die CPU, die Grafikkarten und ihre Treiber, die Bildschirmgröße, den Audiotreiber und das Audiogerät, die Absturz- oder Einfrierdatei, die letzten 60 Schritte der Sitzung sowie die Namen und Versionen der geladenen Plugins von Drittanbietern.

Er enthält kein Audio, keine Projekte, keine Samples, nicht den Namen deines Computers und nicht deinen Benutzernamen. Der Server speichert keine IP-Adresse. Gesendete IDs stehen in `crash-reports\sent.log` im Einstellungsordner. Damit ein Bericht gelöscht wird, eröffne ein Issue mit seiner ID. Den vollständigen Text findest du in der [Datenschutzerklärung](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Ein Problem melden

![Das Fenster Report a problem: ein Beschreibungsfeld, eine optionale E-Mail-Adresse und die Taste Send](/assets/img/wiki/report-problem.webp)

1. Wähle `Hilfe › Problem melden...`.
2. Beschreibe, was passiert ist. Die E-Mail-Adresse ist optional, für den Fall, dass du eine Antwort möchtest.
3. Lass `Neueste Absturz-/Hänger-Dateien und die Schritte dieser Sitzung anhängen` angehakt, wenn das Problem ein Absturz war.
4. Klicke auf `Anzeigen, was gesendet wird`, um den Text zu prüfen.
5. Klicke auf `Senden`. Vorher wird nichts gesendet.

Du kannst auch ein Issue unter [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues) eröffnen.

## Gut zu wissen

- Nach einem Absturz liegen Sicherungskopien deines Projekts im Autosave-Ordner von LYNX SHARE.
- War ein Plugin am Absturz beteiligt, bietet die Frage an, dieses Plugin ab jetzt in einem separaten Prozess auszuführen.
- Der Einstellungsordner ist `%APPDATA%\LYNX` unter Windows, `~/Library/Application Support/LYNX` unter macOS und `~/.config/LYNX` unter Linux.

## Verwandt

- [Audio einrichten](/wiki/audio-setup/)
- [Plugins von Drittanbietern](/wiki/plugins/)
- [Installation](/wiki/install/)
- [FAQ](/faq/)
- [Absturzschutz](/why/#crash)
