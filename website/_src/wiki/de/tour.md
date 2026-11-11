---
source: edf15b371e
---
![Das Hauptfenster mit dem Demo-Projekt: Werkzeugleiste oben, Browser links, dahinter die Playlist und darüber das schwebende Channel Rack](/assets/img/wiki/desktop.webp)

LYNX SHARE hat ein Hauptfenster. Oben läuft eine Werkzeugleiste entlang. Darunter liegt eine Arbeitsfläche mit fünf Fenstern: dem Browser, der Playlist, dem Channel Rack, dem Piano Roll und dem Mixer. Beim ersten Start liegt der Browser links, die Playlist füllt den Rest, und das Channel Rack schwebt darüber. Das Piano Roll und der Mixer sind verborgen, bis du sie aufrufst.

## Sich zurechtfinden

1. Zeige mit der Maus auf ein beliebiges Bedienelement. Die Hinweisleiste unter den Menüs sagt, was es tut und welche Maustasten und Tasten es annimmt.
2. Drücke `F6`, um das Channel Rack auszublenden, und noch einmal `F6`, um es einzublenden.
3. Drücke `F9`, um den Mixer anzuzeigen, dann `F7` für das Piano Roll.
4. Ziehe ein Fenster an seiner Titelleiste, um es zu verschieben. Doppelklicke den Titel, um es zu maximieren, und noch einmal, um es wiederherzustellen.
5. Wähle `Ansicht › Layout zurücksetzen`, um alles zurückzusetzen.

## Die Werkzeugleiste

![Die Werkzeugleiste: Menüs und Hinweisleiste, Transport, Master-Regler, Aufnahmeoptionen, Raster und Pattern, Fensterknöpfe, Dateiknöpfe, Akkordanzeige, Pegelanzeigen und Ausgangsmonitor](/assets/img/wiki/toolbar-1920.webp)

Von links nach rechts, mit dem Standard-Layout:

| Gruppe | Was sie enthält |
|---|---|
| Menü und Hinweisleiste | Die Menüs `Datei`, `Bearbeiten`, `Hinzufügen`, `Patterns`, `Ansicht`, `Optionen`, `Tools` und `Hilfe`, darunter die Hinweisleiste. Immer als Erstes |
| Transport | Die Modus-Tasten `Pattern` und `Song`, Wiedergabe, Stopp und Aufnahme, das Tempo in BPM und die Position als Takt, Schlag und Step |
| Master-Lautstärke und Tonhöhe | Der Lautstärkeregler `MASTER` und der Regler `PITCH`, in Cent |
| Aufnahmeoptionen | Metronom, Einzähler, Loop-Aufnahme, Schritteingabe und die anderen Aufnahmeschalter |
| Raster und Pattern | Das Haupt-Raster (standardmäßig `Linie (folgt dem Zoom)`), das aktuelle Pattern, eine Taste für ein neues Pattern und das Pattern-Menü |
| Fenster-Tastenkürzel | Eine Taste pro Fenster und die Plugin-Auswahl |
| Datei und Rückgängig | Neu, Öffnen, Speichern, Export, Rückgängig und Wiederholen |
| Akkorderkennung | Nennt den Akkord, der auf einem MIDI-Gerät oder der Schreibtastatur gespielt wird |
| CPU, Speicher und Polyphonie | Audio-CPU-Last, belegter Speicher und die Zahl der Stimmen |
| Ausgangsmonitor | Ein Oszilloskop des Ausgangs. Ein Klick schaltet durch seine anderen Modi |

Eine Gruppe namens `Multilink zu Controllern` gibt es auch, sie ist standardmäßig verborgen.

`Optionen › Werkzeugleiste` ändert die Werkzeugleiste: `Werkzeugleiste bearbeiten (Panels ziehen, um sie zu verschieben, hinzuzufügen oder zu entfernen)`, `Trennlinien`, `Panels der Werkzeugleiste ausblenden`, `Presets`, ein Schalter für jede Gruppe und `Symbolgröße` (klein, mittel oder groß).

## Transport

| Bedienelement | Taste | Was es bewirkt |
|---|---|---|
| Pattern-Modus | `L` | Spielt das aktuelle Pattern in einer Schleife. Rechtsklick auf die Taste zeigt das Channel Rack |
| Song-Modus | `L` | Spielt das Arrangement der Playlist. Rechtsklick auf die Taste zeigt die Playlist |
| Wiedergabe | `Space` | Spielt ab. Noch einmal gedrückt, stoppt es und geht zurück zum Startpunkt. `Ctrl+Space` pausiert |
| Stopp | | Stoppt und springt zum Anfang. Doppelklick sendet zusätzlich All Notes Off |
| Aufnahme | `R` | Aufnahme ein oder aus. Siehe [Aufnahme](/wiki/recording/) |

## Die Hinweisleiste

In Ruhe steht in der Hinweisleiste `Point at anything for help`, dazu `F1` für die Tastenkürzel-Übersicht, `F5` bis `F9` für die Fenster und `Space` für Wiedergabe und Stopp. Über einem Bedienelement nennt sie dessen Namen und listet auf, was Ziehen, `Ctrl`, `Alt` und Rechtsklick dort bewirken. Lange Aufgaben wie die Plugin-Suche im Hintergrund melden darin ihren Fortschritt.

## Die fünf Fenster

| Fenster | Taste | Wofür es da ist |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Pattern-, Audio- und Automationsclips zu einem Song arrangieren |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Eine Zeile pro Instrument, mit den Steps des aktuellen Patterns |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Die Noten eines Kanals im aktuellen Pattern |
| [Mixer](/wiki/mixer/) | `F9` | Inserts mit Fadern, Effekten und Routing |
| [Browser](/wiki/browser/) | `Alt+F8` | Samples, Presets, der Inhalt des Projekts und die Plugin-Datenbank |

Jede Taste zeigt das Fenster und gibt ihm den Fokus oder blendet es aus, wenn es den Fokus schon hat. `F8` allein öffnet die Plugin-Auswahl, die keines der fünf Fenster ist. Die Fensterknöpfe der Werkzeugleiste tun dasselbe wie die Tasten. Ein Rechtsklick auf einen zeigt dieses Fenster maximiert. `Enter` maximiert die Playlist oder stellt sie wieder her.

## Wie sich Fenster bewegen und andocken

Jedes Fenster hat eine Titelleiste. Links davon sitzt eine Menütaste mit den eigenen Optionen des Fensters, dann `Abgelöst (eigenes Fenster)`, `Maximiert` und `Schließen`. Rechts sitzen die Tasten zum Maximieren und Schließen.

- Ziehe die Titelleiste, um ein Fenster zu verschieben. Es rastet an den anderen Fenstern und an den Rändern ein. Halte `Alt` gedrückt, um ohne Einrasten zu verschieben.
- Ziehe an einem Rand, um die Größe zu ändern.
- `Abgelöst (eigenes Fenster)` macht das Fenster zu einem eigenen Systemfenster, etwa für einen zweiten Monitor. Ein abgelöstes Fenster hat eine Taste `Andocken`, die es zurücksetzt. `Ansicht › Abgelöste Panels (eigenes Fenster)` listet alle fünf auf.
- `Ansicht › Panels außerhalb des Bildschirms zurückholen` holt ein Fenster zurück, das außerhalb der Arbeitsfläche gelandet ist.
- `Ansicht › Layout speichern unter...` speichert die Anordnung unter einem Namen, und `Ansicht › Layout laden` holt eine zurück.

Der Browser ist der Hintergrund der Arbeitsfläche: Er bleibt seitlich angedockt, und die anderen Fenster liegen immer darüber.

## Gut zu wissen

- Solange du nichts verschoben, in der Größe geändert oder abgelöst hast, nehmen die Fenster ihre Standardplätze wieder ein, sobald sich die Größe des Hauptfensters ändert. Danach wird deine Anordnung gemerkt und skaliert mit dem Fenster.
- `F12` schließt die Fenster für Plugins, Kanaleinstellungen und Werkzeuge. Die fünf Hauptfenster schließt es nicht.
- Solange das Tastaturklavier der Schreibtastatur an ist (`Ctrl+T`), spielen Buchstabentasten Noten, und die meisten Ein-Buchstaben-Kürzel ruhen.
- `Ansicht › Layout-Stil` schaltet zwischen dem Pattern-Arbeitsablauf (Standard) und dem Clip-Start-Arbeitsablauf um, der neben der Playlist ein Clip-Raster hinzufügt.

## Verwandt

- [Dein erster Song](/wiki/first-song/)
- [Layouts](/wiki/layouts/)
- [Tastenkürzel](/wiki/shortcuts/)
- [Einstellungen](/wiki/settings/)
