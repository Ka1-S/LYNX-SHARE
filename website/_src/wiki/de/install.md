---
source: 8a120ab7de
---
Jedes Release hat Builds für Windows, macOS und Linux auf der [Releases-Seite](https://github.com/Ka1-S/LYNX-SHARE/releases). Die Dateien werden von GitHub Actions aus dem getaggten Quellcode gebaut und mit ihrem SHA-256 in `SHA256SUMS.txt` aufgelistet. Die [Download](/download/)-Seite verlinkt die aktuellen.

## Unter Windows installieren

Windows 10 oder 11, 64 Bit.

1. Lade `LYNX-<version>-setup-x64.exe` herunter.
2. Führe sie aus. Sie installiert nur für deinen Benutzer und braucht keine Administratorrechte.
3. SmartScreen warnt möglicherweise, weil die ausführbare Datei noch nicht codesigniert ist. Wähle `More info` und dann `Run anyway`.
4. Starte LYNX SHARE über das Startmenü oder die Desktop-Verknüpfung.

Der Installer macht LYNX SHARE zum Programm, das `.lynx`-Projekte öffnet. Für `.flp`-Dateien bietet er LYNX SHARE unter *Öffnen mit* an, ohne den Standard von FL Studio zu ändern.

Es gibt zwei weitere Wege:

| Weg | Datei oder Ort | Was er bewirkt |
|---|---|---|
| Portables Zip | `LYNX-<version>-windows-x64.zip` | Irgendwo entpacken und `LYNX.exe` ausführen. Es wird nichts installiert. |
| Microsoft Store | [LYNX SHARE im Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Der Store installiert es und hält es aktuell. |

## Unter macOS installieren

macOS 11 Big Sur oder neuer. Eine universelle `LYNX-<version>-macos-universal.dmg` deckt Intel und Apple Silicon ab.

1. Öffne die `.dmg` und ziehe LYNX SHARE in *Programme*.
2. Die App ist von Apple nicht notarisiert und hat keine Developer-ID-Signatur, der erste Start braucht daher einen zusätzlichen Schritt. Klicke in *Programme* mit rechts (oder mit gedrückter Control-Taste) auf LYNX SHARE, wähle *Öffnen* und dann noch einmal *Öffnen*.
3. Versuche unter macOS 15 Sequoia oder neuer, es einmal zu öffnen, gehe dann zu *Systemeinstellungen › Datenschutz & Sicherheit* und klicke auf *Trotzdem öffnen*.

Die Alternative im Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS fragt beim ersten Aufnehmen nach dem Zugriff auf das Mikrofon.

## Unter Linux installieren

x86_64 mit glibc 2.35 oder neuer (Ubuntu 22.04+, Debian 12+, Fedora 36+, aktuelles Arch oder Manjaro). LYNX SHARE braucht X11 oder XWayland. Audio läuft über PipeWire, JACK oder ALSA, und dafür muss nichts zusätzlich installiert werden.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Manche Distributionen brauchen libfuse2, um AppImages auszuführen (`libfuse2` unter Ubuntu 22.04, `libfuse2t64` unter 24.04, `fuse2` unter Arch, `fuse-libs` unter Fedora). Beim ersten Start bietet der Einrichtungsassistent an, LYNX SHARE ins Anwendungsmenü aufzunehmen und `.lynx`-Dateien damit zu öffnen. Das kannst du später unter `Optionen › Allgemeine Einstellungen...` umstellen oder im Terminal mit `./LYNX-*.AppImage --menu-entry on` (oder `aus`).

Wenn du libfuse2 lieber nicht installieren möchtest, nimm `LYNX-<version>-linux-x86_64.tar.gz`: entpacken und `./install.sh` ausführen. Das installiert LYNX SHARE für deinen Benutzer unter `~/.local`, mit Menüeintrag, Symbol und dem Dateityp `.lynx`.

## Wo die Einstellungen liegen

| System | Einstellungsordner |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (oder unter `$XDG_CONFIG_HOME`) |

Der Ordner enthält deine Optionen, die gewählte Sprache (`language.txt`), deine Tastenkürzel (`keys.json`), deine Plugin-Ordner (`plugin-folders.txt`) und die Liste der durchsuchten Plugins (`plugins.xml`).

## Aktualisieren

Beim Start fragt LYNX SHARE bei GitHub die neueste Versionsnummer ab. Über dich wird nichts gesendet. Ist eine neue Version erschienen, zeigt eine Leiste `LYNX SHARE <version> is available` mit einer Taste `Neuigkeiten` und einer Update-Taste. `Hilfe › Nach Updates suchen...` fragt jederzeit nach.

| Build | So aktualisiert er sich |
|---|---|
| Windows-Installer | `Aktualisieren` lädt den neuen Installer herunter, fragt nach ungespeicherten Änderungen, installiert und startet neu |
| Portables Windows-Zip, macOS | `Aktualisieren` lädt die neue Datei herunter und zeigt sie in ihrem Ordner |
| Linux-AppImage | `Aktualisieren` aktualisiert die AppImage-Datei an Ort und Stelle. Gear Lever und AppImageUpdate können dasselbe von außen |
| Linux-`.tar.gz` | `Herunterladen...` öffnet die Download-Seite |
| Microsoft Store | Der Store aktualisiert es. Dieser Build hat keine eigene Update-Prüfung |

Um die Prüfung abzuschalten, öffne `Optionen › Allgemeine Einstellungen...` und entferne den Haken bei `Automatisch nach Updates suchen`.

## Entfernen

- Windows-Installer: Deinstalliere LYNX SHARE in der Liste *Apps & Features* von Windows.
- Portables Zip: Lösche den Ordner, in den du entpackt hast.
- Linux-`.tar.gz`: Führe `./install.sh --uninstall` aus.

## Gut zu wissen

- Die Windows-Builds sind unsigniert, bis der Antrag des Projekts auf Codesignierung angenommen ist. Die macOS-App hat nur eine Ad-hoc-Signatur. Linux-Pakete sind nicht signiert.
- Um einen Download zu prüfen, vergleiche seinen SHA-256 mit der Zeile in `SHA256SUMS.txt` auf der Release-Seite.
- Der Installer, das Zip, die `.dmg` und das `.tar.gz` enthalten das Demo-Projekt `Demo.lynx`.
- Wenn du die Update-Leiste mit ihrer Schließen-Taste schließt, wird diese Version übersprungen.

## Verwandt

- [Hier starten](/wiki/getting-started/)
- [Audio einrichten](/wiki/audio-setup/)
- [Fehlerbehebung](/wiki/troubleshooting/)
- [Download](/download/)
- [Updates](/updates/)
