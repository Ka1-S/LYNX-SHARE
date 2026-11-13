---
source: d5cc625221
---
![Das Clip-Start-Layout: ein Clip-Raster mit Szenen neben der Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE startet im Pattern-Arbeitsablauf: ein Channel Rack, Patterns und eine Playlist. Der Clip-Start-Arbeitsablauf ist optional und fügt neben der Playlist ein Clip-Raster mit Szenen hinzu. LYNX SHARE liest und schreibt `.als`-Sets. Es hat keine Verbindung zu Ableton.

## Ein Live-Set öffnen

1. Wähle `Datei › Öffnen...` (`Ctrl+O`) und such die `.als` aus. Sets, die mit Live 11 und Live 12 gespeichert wurden, werden gelesen.
2. Wähle `Ansicht › Layout-Stil › Clip-Start-Arbeitsablauf`, um mit dem Clip-Raster zu arbeiten. Dorthin kommen die Session-Clips und Szenen des Sets.
3. Mit dem Schalter `Grid / Both / Song` in der Titelleiste der Playlist zeigst du das Raster, die Playlist oder beides.
4. Wähle `Datei › Speichern`. Es fragt, wohin eine `.lynx`-Datei geschrieben werden soll. Die `.als` bleibt unverändert.

`Ansicht › Layout-Stil › Pattern-Arbeitsablauf (Standard)` führt zurück. Dieselbe Auswahl gibt es unter `Optionen › Allgemeine Einstellungen...`. Patterns und das Channel Rack stehen in beiden Layouts zur Verfügung.

## Was wohin gehört

| In Live | In LYNX SHARE |
|---|---|
| Session-Ansicht | Das Clip-Raster des Clip-Start-Arbeitsablaufs |
| Arrangement-Ansicht | Die Playlist |
| Spuren | Playlist-Spuren, die die Spalten des Clip-Rasters sind |
| Szenen | Szenen, die Zeilen des Clip-Rasters. Eine Szene kann beim Start das Tempo setzen |
| Start-Quantisierung | `Global`, `Keine`, 8 Takte bis 1 Takt oder ein Notenwert von 1/2 bis 1/32, auf Wunsch pro Clip |
| Startmodi | `Trigger`, `Gate`, `Umschalten` und `Wiederholen` |
| Follow Actions | Action A und Action B mit je einer Wahrscheinlichkeit |
| Clip-Stopp-Tasten | Ein leerer Slot kann eine Stopp-Taste haben |
| Zurück zum Arrangement | `Zurück zur Playlist` |
| Warp-Modi | `Schläge`, `Töne`, `Textur` und `Komplex` unter `Kanaleinstellungen › Time-Stretch` |
| Simpler, Sampler und Drum Racks | Sampler von LYNX SHARE |
| `.als`-Set | `.lynx`-Projektdatei |

## Das Clip-Raster

Ein Slot enthält einen Pattern-Clip oder einen Audioclip.

- Eine Spur spielt immer nur einen Clip. Ein gestarteter Clip ersetzt den laufenden.
- Ein Start wartet auf den nächsten Schritt der Start-Quantisierung.
- `Legato` startet den neuen Clip an der Position, die der ersetzte Clip erreicht hatte. `Loop` gilt pro Clip.
- Szenen lassen sich einfügen, löschen und umbenennen.
- Eine Spur, die einen Clip gestartet hat, bleibt in der Playlist stumm, bis du `Zurück zur Playlist` drückst. Eine Option gibt die Spuren zurück, wenn der Transport stoppt.

Follow Actions laufen nach einer Anzahl von Takten oder Durchgängen des Clips. Die Aktionen sind `Keine Aktion`, `Stopp`, `Erneut abspielen`, `Vorherige`, `Weiter`, `Erste`, `Letzter`, `Beliebige` und `Sonstige`. Ein Schalter aktiviert oder deaktiviert Follow Actions für das ganze Raster.

## Was aus einer .als importiert wird

- MIDI-, Audio-, Gruppen- und Return-Spuren, Tempo, Taktart, Locators, der Loop, Mixer-Einstellungen (Lautstärke, Pan, Mute, Solo, Sends) und deren Automation.
- VST2- und VST3-Plugins mit ihrem Zustand, wenn dasselbe Plugin installiert ist. Siehe [Plugins von Drittanbietern](/wiki/plugins/).
- Session-Clips und Szenen, ins Clip-Raster, mit Startmodus, Quantisierung, Legato, Loop, Follow Actions und Stopp-Tasten. Um sie stattdessen auf die Playlist zu legen, einen Abschnitt pro Szene, ändere `Session-Clips aus Live-Sets` unter `Optionen › Allgemeine Einstellungen...`.
- Warp-Marker.

## Eine .als exportieren

1. Wähle `Datei › Exportieren › Ableton-Live-Set (.als)...`.
2. Wähle unter `Speichern für` entweder `Live 12` oder `Live 11 (öffnet auch in Live 12)`.
3. Unter `Samples` kopierst du sie in den Projektordner des Sets oder verweist dorthin, wo sie liegen.
4. Wähle einen Ordner. Das Set wird als `<name> Project/<name>.als` gespeichert.

| Geschrieben | Nicht geschrieben |
|---|---|
| Eine MIDI-Spur pro Kanal, mit den Noten als MIDI-Clips | Die eigenen Instrumente und Effekte von LYNX SHARE. Ihre Spuren und Noten bleiben erhalten |
| Audiospuren mit Stretch, Transponierung, Gain, Fades und Warp-Modus | Automation von Effektparametern |
| Gruppen, Returns und Sends | Routen von Insert zu Insert |
| VST2- und VST3-Plugins mit ihrem Zustand | |
| Marker sowie Automation von Lautstärke, Pan und Tempo | |
| Das Clip-Raster als Session-Clips und Szenen, Racks und Drum Racks | |

Ein Bericht nach dem Export listet auf, was ausgelassen wurde.

## Gut zu wissen

- Viele der eigenen Devices von Live kommen als die von LYNX SHARE herein, mit ihren Einstellungen und ihrer Automation: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift und Operator (ohne FM). Die übrigen entfallen und stehen im Importbericht.
- Instrument- und Audio-Effect-Racks kommen mit ihren Ketten, Zonen und Makros, und ein Drum Rack wird zum Drum Rack von LYNX SHARE. Die MIDI-Effect-Racks von Live werden noch nicht umgewandelt und stehen im Importbericht.
- Der Importbericht listet auf, was ausgelassen wurde.
- Live kürzt Audioclips, die sich in einem exportierten Set überlappen.
- Warp-Marker aus einem Live-Set bleiben beim Import erhalten und werden beim Export zurückgeschrieben.

## Verwandt

- [Playlist](/wiki/playlist/)
- [Layouts](/wiki/layouts/)
- [Projekte importieren](/wiki/importing-projects/)
- [Dateiformate](/wiki/file-formats/)
- [Zwei Arbeitsabläufe in einem Programm](/why/#workflows)
