---
source: a6d9063ec3
---
LYNX SHARE ist eine kostenlose Open-Source-DAW für Windows, macOS und Linux. Sie arbeitet mit Patterns: Du programmierst sie im Channel Rack und im Piano Roll, arrangierst sie in der Playlist und mischst sie im Mixer. Sie öffnet FL-Studio-Projekte (`.flp`) und Ableton-Live-Sets (`.als`) und speichert im eigenen Format `.lynx`.

Es ist eine Alpha. Rechne mit Fehlern und speichere oft.

## Zum Laufen bringen

1. Prüfe in der Tabelle unten, ob dein System unterstützt wird.
2. Lade es herunter und installiere es. [Installation](/wiki/install/) hat die Schritte für jedes System.
3. Starte es. Ein kurzer Einrichtungsassistent wählt die Sprache, testet den Audioausgang und listet die Plugin-Ordner auf, die danach im Hintergrund durchsucht werden. Du kannst ihn überspringen und später über `Optionen › Einrichtungsassistent (Sprache, Audio, Plugin-Ordner)...` wieder öffnen.
4. Wenn du nichts hörst, gehe zu [Audio einrichten](/wiki/audio-setup/).
5. Lies [Rundgang durch das Fenster](/wiki/tour/) und mache dann [Dein erster Song](/wiki/first-song/).

## Was du brauchst

| System | Voraussetzung |
|---|---|
| Windows | Windows 10 oder 11, 64 Bit |
| macOS | macOS 11 Big Sur oder neuer, Intel oder Apple Silicon |
| Linux | x86_64 mit glibc 2.35 oder neuer (Ubuntu 22.04+, Debian 12+, Fedora 36+, aktuelles Arch oder Manjaro), X11 oder XWayland |

Du brauchst kein Audio-Interface. LYNX SHARE spielt über das Soundgerät, das dein System ohnehin benutzt. FL Studio brauchst du ebenfalls nicht. Ist es auf demselben Computer installiert, findet LYNX SHARE dessen Samples und nutzt für `.flp`-Projekte dieselben Plugin-Dateien.

Plugins von Drittanbietern sind optional. LYNX SHARE hat eigene Instrumente und Effekte, die im [Katalog](/instruments-and-effects/) aufgelistet sind.

## Deine Arbeit speichern

`Ctrl+S` speichert, `Ctrl+Shift+S` speichert unter neuem Namen, und `Ctrl+Alt+S` speichert eine neue Version (Song_2, Song_3 und so weiter). Es gibt eine automatische Sicherung, erreichbar über `Datei › Ordner der automatischen Sicherung öffnen` und `Datei › Zur letzten Sicherung zurückkehren...`.

`Datei › Neu aus Vorlage` bietet vier Ausgangspunkte: Leer, Basic beat, Trap with 808 und House.

## Das Demo-Projekt

Der Windows-Installer und das Zip, die macOS-`.dmg` und das Linux-`.tar.gz` enthalten ein Demo-Projekt, `Demo.lynx`. Öffne es mit `Datei › Öffnen...` (`Ctrl+O`). Unter Windows liegt es im Installationsordner, neben `LYNX.exe`.

Du kannst es auch in einem Terminal erzeugen. Siehe [Kommandozeile](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Wie es weitergeht

| Du möchtest | Lies |
|---|---|
| Jedes Fenster kennenlernen | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Ein Projekt aus einem anderen Programm öffnen | [Umstieg von FL Studio](/wiki/coming-from-fl-studio/), [Umstieg von Ableton Live](/wiki/coming-from-ableton/) |
| Deine VST-, CLAP-, LV2- oder AU-Plugins nutzen | [Plugins](/wiki/plugins/) |
| Eine Taste nachschlagen | [Tastenkürzel](/wiki/shortcuts/) |
| Ein Problem lösen | [Fehlerbehebung](/wiki/troubleshooting/) und die [FAQ](/faq/) |

## Gut zu wissen

- Die Programmdatei heißt `LYNX.exe` unter Windows und `LYNX` unter macOS und Linux. Der Einstellungsordner heißt ebenfalls `LYNX`.
- Unter Windows und macOS kann der erste Start eine Systemwarnung zeigen, weil der Windows-Build noch nicht codesigniert und der macOS-Build nicht notarisiert ist. [Installation](/wiki/install/) erklärt, was du anklicken musst.
- Auf einem System, dessen Sprache LYNX SHARE nicht hat, ist die Oberfläche standardmäßig Englisch. Du kannst das unter `Optionen › Allgemeine Einstellungen...` ändern. Eine neue Sprache gilt nach einem Neustart.
- FLEX-Packs in FL-Studio-Projekten sind verschlüsselt und werden nie geöffnet. Diese Kanäle bekommen einen Synth-Patch, der nach dem Preset-Namen gewählt wird.

## Verwandt

- [Installation](/wiki/install/)
- [Audio einrichten](/wiki/audio-setup/)
- [Rundgang durch das Fenster](/wiki/tour/)
- [Dein erster Song](/wiki/first-song/)
- [Download](/download/)
