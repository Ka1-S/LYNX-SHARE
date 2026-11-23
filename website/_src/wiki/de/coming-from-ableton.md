---
source: 37920663db
---
![Das Clip-Start-Layout: ein Clip-Raster mit Szenen neben der Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE startet im Pattern-Arbeitsablauf: ein Channel Rack, Patterns und eine Playlist. Der Clip-Start-Arbeitsablauf ist optional und fügt neben der Playlist ein Clip-Raster mit Szenen hinzu. LYNX SHARE liest und schreibt `.als`-Sets. Es hat keine Verbindung zu Ableton.

Diese Seite ist auch für Nutzer von Bitwig Studio: Das Clip-Raster funktioniert genauso, und die DAWproject-Dateien (`.dawproject`) von Bitwig öffnen sich mit `Datei › Öffnen...` und werden von `Datei › Exportieren › DAWproject (.dawproject)...` geschrieben.

## Deine ersten Schritte

1. **Wähle das Layout.** Beim ersten Start von LYNX SHARE fragt eine Willkommenskarte, woher du kommst. Klicke auf `Ableton Live oder Bitwig`: Das Clip-Start-Layout öffnet sich. Später wechselt `Ansicht › Layout-Stil › Clip-Start-Arbeitsablauf` jederzeit dorthin.
2. **Fülle einen Slot.** Ziehe ein Sample oder ein Pattern aus dem Browser in einen Slot, oder klicke mit rechts auf einen leeren Slot und wähle `Pattern hier einfügen`.
3. **Starte.** Klicke auf das Dreieck links von einem Clip, oder auf das Dreieck einer Szene in der Spalte `Szenen`, um die ganze Zeile zu starten. Oder öffne eines deiner `.als`-Sets (unten).

Was anders ist als in Live:

- Die Playlist ist das Arrangement, und ihre Spuren sind die Spalten des Rasters. Eine Spur, die einen Clip gestartet hat, bleibt im Raster, bis du `Zurück zur Playlist` drückst.
- Außerhalb des Rasters gelten die Tasten von FL Studio. `Ctrl+Z` macht einen Schritt rückgängig, und noch einmal gedrückt wiederholt es ihn.
- Nur einige der eigenen Geräte von Live werden umgewandelt (siehe Gut zu wissen). Plugins von Drittanbietern kommen mit ihrem Zustand mit.
- Das Pattern-Layout, die Arbeitsweise von FL Studio mit Channel Rack und Patterns, arbeitet am selben Projekt.

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
| Eine MIDI-Spur pro Kanal, mit den Noten als MIDI-Clips | Die eigenen Instrumente und Effekte von LYNX SHARE, außer Helix, das zu Wavetable wird. Ihre Spuren und Noten bleiben erhalten |
| Audiospuren mit Stretch, Transponierung, Gain, Fades und Warp-Modus | Automation von Effektparametern |
| Gruppen, Returns und Sends | Routen von Insert zu Insert |
| VST2- und VST3-Plugins mit ihrem Zustand | |
| Marker sowie Automation von Lautstärke, Pan und Tempo | |
| Das Clip-Raster als Session-Clips und Szenen, Racks und Drum Racks | |

Ein Bericht nach dem Export listet auf, was ausgelassen wurde.

## Gut zu wissen

- Viele der eigenen Devices von Live kommen als die von LYNX SHARE herein, mit ihren Einstellungen und ihrer Automation: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable und Meld. Die übrigen entfallen und stehen im Importbericht.
- Instrument- und Audio-Effect-Racks kommen mit ihren Ketten, Zonen und Makros, und ein Drum Rack wird zum Drum Rack von LYNX SHARE. Der Arpeggiator von Live wird zum Arpeggiator des Kanals. Pitch, Velocity, Chord, Note Length und MIDI-Effect-Racks werden in die Noten geschrieben.
- Der Importbericht listet auf, was ausgelassen wurde.
- In der Device-Ansicht zeigt ein Rack die Devices seiner Ketten direkt an, und der Macro-Map-Modus (die `Map`-Buttons des Racks) gibt einen Device-Parameter an ein Makro, wie in Live.
- Der Session-Mixer hat den Solo-/Cue-Schalter von Live, mit einem `Cue Out` für deine Kopfhörer und einer `Cue-Lautstärke`.
- Live kürzt Audioclips, die sich in einem exportierten Set überlappen.
- Warp-Marker aus einem Live-Set bleiben beim Import erhalten und werden beim Export zurückgeschrieben.

## Verwandt

- [Playlist](/wiki/playlist/)
- [Layouts](/wiki/layouts/)
- [Projekte importieren](/wiki/importing-projects/)
- [Dateiformate](/wiki/file-formats/)
- [Zwei Arbeitsabläufe in einem Programm](/why/#workflows)
- [Lernen: Anleitungen](/learn/)
