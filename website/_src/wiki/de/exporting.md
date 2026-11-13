---
source: da5e41c322
---
![Das Export-Fenster mit gewähltem WAV: Bereich, Ausklang und Dateien links, Format rechts, Mix-Optionen darunter](/assets/img/wiki/export.webp)

Alles, was LYNX SHARE verlässt, läuft über `Datei › Exportieren`. Die Audioformate (WAV, MP3, OGG und FLAC) teilen sich ein Export-Fenster. MIDI, FL-Studio-Projekte, Ableton-Live-Sets und gezippte Projekte haben eigene Einträge im selben Menü.

## Den Song als WAV exportieren

1. Wähle `Datei › Exportieren › Wave-Datei...`.
2. Lass `Bereich` auf `Song (Playlist)` und `Dateien` auf `Eine Datei (der Mix)`.
3. Wähle eine `Bittiefe`: 16 bits, 24 bits oder 32 bits (float).
4. Setze `Ausklang` auf `Rest ausklingen lassen`, damit Hall und Release nach dem letzten Takt ausklingen.
5. Klicke auf `Exportieren...`, wähle Namen und Ordner und warte auf das Rendern.
6. Ist es fertig, meldet LYNX SHARE, wohin die Datei gegangen ist, und ihren Spitzenpegel.

`Ctrl+R` öffnet das Fenster mit dem Format, das du zuletzt exportiert hast. `Ctrl+Shift+R` öffnet es auf MP3.

## Was gerendert wird

| Einstellung | Was sie tut |
|---|---|
| `Bereich` | `Song (Playlist)`, `Aktuelles Pattern` oder `Playlist-Auswahl` |
| `Wiedergabe` | Wie oft der Bereich in der Datei abgespielt wird, von `Einmal` bis 16-mal |
| `Ausklang` | `Rest abschneiden` hört genau am Ende auf. `Rest ausklingen lassen` lässt den Klang ausklingen. `Rest umbrechen` mischt den Ausklang an den Anfang, für Loops |
| `Max. Ausklang` | 3, 6, 12 oder 30 Sekunden. Der Ausklang endet früher, wenn der Klang verklungen ist |
| `Dateien` | Eine Datei oder Stems (siehe unten) |

## Ausgabeformat

Die vier Buttons wählen das Format. `Auch speichern` schreibt aus demselben Render weitere Formate, mit demselben Namen.

| Format | Optionen |
|---|---|
| WAV | `Bittiefe`: 16 bits, 24 bits oder 32 bits (float). `Dither`: `Aus`, `TPDF` oder `TPDF + Noise-Shaping` |
| MP3 | `Bitrate`, konstant |
| OGG | Bitrate |
| FLAC | `Bittiefe`, `Kompression` von `0 (am schnellsten)` bis `8 (am kleinsten)`, `Dither`. Auf jeder Stufe verlustfrei |

`Samplerate` und `Kanäle` gelten für jedes Format. `Kanäle` ist `Stereo`, `Mono (beide Seiten gemischt)`, `Mono (nur links)` oder `Mono (nur rechts)`.

## Mix-Optionen

| Einstellung | Was sie tut |
|---|---|
| `Normalisieren (Peak auf 0 dB)` | Hebt oder senkt die ganze Datei so, dass ihre lauteste Spitze Vollaussteuerung erreicht |
| `Master-Effekte` | Aus: Die Effekte des Master-Inserts werden umgangen |
| `Insert-Effekte` | Aus: Die Effekte der anderen Inserts werden umgangen |
| `HQ für alle Plugins` | Plugins nutzen ihre Render-Qualität. Aus: Der Export nutzt die Wiedergabequalität |
| `Plugin-Aufwärmen` | Plugins von Drittanbietern verarbeiten einen Moment Stille, bevor der Export startet |
| `Maximale Polyphonie deaktivieren` | Hebt das Stimmenlimit der Kanäle auf, damit keine Note abgeschnitten wird |
| `Stille des Latenzausgleichs entfernen` | Entfernt die Stille, die der Latenzausgleich der Plugins am Anfang erzeugt |
| `Tempoinformation`, `Loop-Marker`, `Playlist-Marker` | Nur WAV: speichern das Tempo, den Loop und die Marker der Playlist in der Datei |
| `Datei(en) nach Abschluss anzeigen` | Öffnet den Dateimanager beim Ergebnis |

## Stems und Spuren

![Das Export-Fenster auf FLAC, mit einer Datei pro Mixer-Insert, insgesamt neun Dateien](/assets/img/wiki/export-flac-stems.webp)

`Dateien` hat drei Stem-Modi:

- `Je Mixer-Insert (Stems)`: eine Datei für jedes Insert, das Ton bekommt.
- `Eine pro Kanal (Stems)`: eine Datei für jeden Kanal, der spielt.
- `Eine pro Kanal, nur Instrumente (ohne Sends oder Busse)`.

Hake `Auch den Gesamtmix` an, um den Mix neben den Stems zu bekommen. Die Zeile unten zeigt, wie viele Dateien geschrieben werden.

`Datei › Exportieren › Alle Playlist-Spuren` und `Alle Playlist-Spurgruppen` schreiben eine Datei pro Playlist-Spur oder pro Gruppe. Im Menü des Mixers öffnet `Aufnahme auf Festplatte › Aufnahmebereite Inserts in WAV-Datei(en) rendern...` (`Alt+R`) das Export-Fenster nur für die scharfgeschalteten Inserts.

## Weitere Einträge des Menüs Exportieren

| Befehl | Taste | Was er tut |
|---|---|---|
| `MIDI-Datei...` | `Ctrl+Shift+M` | Schreibt die Noten als Standard-MIDI-Datei (`.mid`) |
| `Mastern...` | `Ctrl+Alt+M` | Rendert das Projekt und mastert es auf einen Lautheitswert in LUFS, auf deinem Computer |
| `Projekt mit Samples (.zip)...` | keine | Das Projekt und seine Samples in einem Archiv |
| `FL Studio-Projekt...` | keine | Eine `.flp` oder ein gezipptes Loop-Paket mit jedem Sample |
| `Ableton-Live-Set (.als)...` | keine | Ein Set für Live 12 oder Live 11 |
| `Projektknochen...` | keine | Presets und Bewertungen von Kanälen und Effekten, in einem Ordner |

## Export nach FL Studio und Ableton Live

Die `.flp` ist ein FL-Studio-2026-Projekt. Sampler, Audioclips, die eigenen Generatoren und Effekte von FL Studio, Plugins von Drittanbietern, Patterns, die Playlist, der Mixer und die Automation gehen mit ihren Einstellungen hinüber. Für Dinge, die FL Studio nicht hat, wählst du zwischen `Zu Audio rendern (klingt gleich)` und `Das nächstliegende FL-Plugin verwenden (bleibt bearbeitbar, kann anders klingen)`. Bei der zweiten Wahl werden Instrumente nur für LYNX weggelassen. Modulatoren werden immer weggelassen. Ein Bericht listet, was gerendert, ersetzt oder weggelassen wurde. Details stehen unter [Umstieg von FL Studio](/wiki/coming-from-fl-studio/).

Die `.als` enthält pro Kanal eine MIDI-Spur, Audiospuren, Gruppen, Returns, Sends, Racks und Drum Racks, VST2- und VST3-Plugins mit ihrem Zustand, Marker sowie Automation von Lautstärke, Pan und Tempo. Die eigenen Instrumente und Effekte von LYNX SHARE werden nicht umgewandelt, ihre Spuren und Noten bleiben aber erhalten. Automation von Effektparametern und Routen von Insert zu Insert entfallen. Details stehen unter [Umstieg von Ableton Live](/wiki/coming-from-ableton/).

## Gut zu wissen

- `Dither` ist bei MP3 ausgegraut, und die Tempo- und Marker-Optionen sind bei jedem Format außer WAV ausgegraut.
- Bei `Playlist-Auswahl` ohne Auswahl in der Playlist wird der ganze Song gerendert. Das Fenster warnt dich.
- Ist das Master-Insert stummgeschaltet, warnt das Fenster, dass die Datei stumm sein wird.
- `Auch ein Video mit diesem Mix schreiben` erscheint nur, wenn das Projekt eine Videospur hat, und braucht FFmpeg.
- `In Echtzeit über die Hardware rendern` erscheint nur, wenn das Projekt ein aktives Hardware-Insert hat. Dieser Export dauert so lang wie der Song.
- Beim Rendern in Audio, wenn ein Insert in einen Audioclip gebounct wird, spielt dieser in dieser Version etwa 5 dB lauter als das Insert, das er ersetzt.

## Siehe auch

- [Dateiformate](/wiki/file-formats/)
- [Kommandozeile](/wiki/command-line/)
- [Umstieg von FL Studio](/wiki/coming-from-fl-studio/)
- [Umstieg von Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
