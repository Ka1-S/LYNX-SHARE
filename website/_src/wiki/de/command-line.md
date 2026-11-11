---
source: a5c1d056c8
---
LYNX SHARE läuft auch, ohne sein Fenster zu öffnen. Eine Option rendert ein Projekt zu Audio, andere wandeln es in ein FL-Studio-Projekt oder ein Ableton-Live-Set um, und mehrere gibt es zum Testen. Die eigenen Prüfungen des Projekts nutzen dieselben Optionen.

Das Programm heißt `LYNX.exe` unter Windows und `LYNX` unter macOS und Linux. Unter macOS lautet der volle Pfad `/Applications/LYNX.app/Contents/MacOS/LYNX`. Die Optionen sind auf jedem System gleich.

## Ein Projekt in eine Datei rendern

1. Öffne ein Terminal in dem Ordner, der das Programm enthält. Unter Windows ist das der Installationsordner oder der Ordner, in den du das Zip entpackt hast.
2. Führe `LYNX.exe --export project.flp out.flac` aus, mit deinem eigenen Projekt und deiner eigenen Ausgabedatei.
3. Warte, bis sich das Programm beendet. Es öffnet sich kein Fenster.
4. Lies `out.flac.txt`, die neben der Ausgabe geschrieben wird. Bei einem importierten Projekt listet sie auf, was umgewandelt, angenähert und ausgelassen wurde und welche Plugins gefunden wurden oder fehlen.

## Ein Projekt öffnen

```
LYNX.exe project.lynx
```

Startet LYNX SHARE mit Fenster und öffnet die Datei. Akzeptiert werden `.lynx`, `.flp`, `.als`, `.flm` und `.mid`. Ein relativer Pfad wird vom aktuellen Ordner aus gelesen.

```
LYNX.exe --no-plugins project.lynx
```

Öffnet ein `.lynx`-Projekt, ohne seine Fremd-Plugins zu laden, wie es `Datei › Ohne Plugins von Drittanbietern öffnen...` tut. Jedes Plugin behält seine Einstellungen. Nutze es für ein Projekt, dessen Öffnen ein Plugin verhindert.

## Optionen für den Alltag

| Option | Was sie bewirkt |
|---|---|
| `--export <project> <out>` | Rendert den ganzen Song samt Ausklang, wie es das Export-Fenster tut. Die Endung von `<out>` wählt das Format: `.wav` (24 Bit), `.flac`, `.ogg` oder `.mp3` (320 kbps) |
| `--export ... --pattern` | Rendert nur das aktuelle Pattern |
| `--export ... --insert N` | Rendert nur den Ausgang von Mixer-Insert N |
| `--export ... --rate N` | Rendert mit einer anderen Samplerate, in Hz |
| `--stems <project> <base.wav>` | Schreibt eine WAV pro Mixer-Insert, das Kanäle hat: `base - 1.wav`, `base - 2.wav` und so weiter |
| `--export-flp <project> <out.flp>` | Schreibt ein FL-Studio-Projekt. Mit `out.zip` schreibt es ein gezipptes Loop-Paket |
| `--export-flp ... --substitute` | Tauscht Plugins, die es nur in LYNX gibt, gegen das nächstliegende FL-Plugin aus, statt sie zu Audio zu rendern |
| `--export-als <project> <folder>` | Schreibt ein Ableton-Live-Set nach `<folder>/<name> Project/<name>.als`, für Live 12 |
| `--export-als ... --live11` | Schreibt das Set für Live 11 |
| `--export-bundle <project> <folder>` | Schreibt das Paket von `Datei › Exportieren › Für andere Apps...` in den Ordner |
| `--export-bundle ... --zip` | Zippt den Ordner. Er wird durch `<folder>.zip` ersetzt |
| `--export-bundle ... --no-mixdown` | Lässt den Mixdown aus dem Paket weg |
| `--devices <report.txt>` | Listet die Audiotreiber-Typen und Geräte dieses Computers auf |
| `--showcase <out.lynx>` | Schreibt das eigene Demo-Projekt von LYNX SHARE |
| `--lang <id>` | Nutzt für diesen Lauf eine andere Oberflächensprache, zum Beispiel `--lang pt-BR` |
| `--menu-entry on` oder `aus` | Nur Linux-AppImage: nimmt LYNX SHARE ins Anwendungsmenü auf oder entfernt es |

`--export`, `--stems` und `--export-flp` nehmen als Projekt eine `.lynx`, `.flp` oder `.als`. `--export-als` nimmt auch `.flm`. `--export-bundle` nimmt eine `.lynx`, `.flp` oder `.als`.

Beispiele:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --export-bundle song.lynx out-folder --zip
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Optionen zum Testen

Sie dienen dazu, LYNX SHARE selbst zu prüfen. In einem Fehlerbericht sind sie nützlich.

| Option | Was sie bewirkt |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Führt den Selbsttest ohne Audiogerät aus. Der Bericht endet mit `result=OK`, wenn er besteht |
| `--snapshot <prefix> [project]` | Zeichnet die Oberfläche und ihre Fenster in PNG-Dateien, die nach dem Präfix benannt sind. So entstehen die Bilder in diesem Wiki |
| `--render <project> <out.wav> <seconds>` | Rendert so viele Sekunden des aktuellen Patterns in eine 24-Bit-WAV |
| `--plugintest <plugin> <report.txt>` | Lädt und testet ein VST3- oder CLAP-Plugin |
| `--sandboxtest <plugin> <report.txt>` | Testet ein Plugin in einem separaten Prozess, auch ein hängendes |
| `--scantest <folder> <report.txt>` | Durchsucht einen Ordner nach Plugins, in separaten Prozessen |
| `--fldb <report.txt> [project.flp]` | Meldet die hier gefundene FL-Studio-Installation, ihre Plugin-Datenbank und die Plugin-Treffer eines Projekts |
| `--devicetest <type> <device> <report.txt>` | Öffnet einen Audiotreiber für eine Sekunde Stille und meldet das Ergebnis |
| `--uiperf <project> <report.txt>` | Misst die Zeichenzeit von Mixer, Playlist und Channel Rack |

Beispiele:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Gut zu wissen

- Ergebnisse gehen in Dateien, nicht ins Terminal. `--export` schreibt `<out>.txt`, `--export-flp` schreibt `<out.flp>.txt`, `--export-als` schreibt `export-als.txt` in den Ordner, `--export-bundle` schreibt `export-bundle.txt` in den Ordner und `--stems` schreibt `<base.wav>.txt`.
- Das Programm liest die Standardeingabe. In einer Shell-Schleife hängst du an jeden Aufruf `< /dev/null` an.
- `--snapshot` zeichnet mit 1280x720, sofern die Umgebungsvariable `LYNX_SNAPSHOT_SIZE` nichts anderes sagt, zum Beispiel `1920x1080`.
- `--lang` nimmt die IDs der Sprachen, die unter `Optionen › Allgemeine Einstellungen...` aufgelistet sind, etwa `en`, `pt-BR`, `de`, `es`, `fr` oder `ja`. Eine unbekannte ID wird ignoriert.
- `--export` nutzt denselben Render-Weg wie das Export-Fenster, aber keine der übrigen Optionen des Fensters. Für Dither, Bittiefe oder Normalisieren nimmst du das Fenster.
- Der Quellcode hat noch viele weitere Testoptionen. Sie sind intern und können sich zwischen Versionen ändern.

## Verwandt

- [Export](/wiki/exporting/)
- [Dateiformate](/wiki/file-formats/)
- [Aus dem Quellcode bauen](/wiki/build-from-source/)
- [Fehlerbehebung](/wiki/troubleshooting/)
