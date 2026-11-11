---
source: e767f4482b
---
![Die Playlist mit Clips auf Spuren](/assets/img/playlist.webp)

In der Playlist werden aus Patterns ein Song. Sie enthält auf ihren Spuren drei Arten von Clips: Pattern-Clips, Audioclips und Automationsclips. Mit `F5` blendest du sie ein oder aus, mit `Enter` maximierst du sie oder stellst sie wieder her.

Der Transport hat zwei Modi. Der Pattern-Modus spielt das aktuelle Pattern in einer Schleife. Der Song-Modus spielt die Playlist. Mit `L` wechselst du.

## Ein Pattern im Song platzieren

1. Drücke `F5`, um die Playlist anzuzeigen.
2. Klicke im Bereich Auswahl auf ein Pattern. Die Auswahl listet die Patterns, das Audio und die Automation des Projekts auf, und was du anklickst, wird die Clip-Quelle.
3. Drücke `P` für den Stift.
4. Klicke auf eine Spur, um den Clip zu platzieren. Halte die Taste gedrückt und ziehe, um ihn zu positionieren.
5. Drücke `B` für den Pinsel und ziehe entlang einer Spur, um Clips lückenlos aneinanderzureihen.
6. Drücke `L` für den Song-Modus, dann `Space` zum Abspielen.

Audiodateien kannst du aus dem [Browser](/wiki/browser/) auf die Playlist ziehen.

## Werkzeuge

![Die Werkzeugleiste der Playlist mit dem Arrangement-Feld, den Werkzeugen und dem Raster-Feld](/assets/img/wiki/playlist-tools.webp)

Die Buchstaben funktionieren, solange das Tastaturklavier der Schreibtastatur (`Ctrl+T`) aus ist.

| Werkzeug | Taste | Was es bewirkt |
|---|---|---|
| Stift | `P` | Klick platziert den Clip der Auswahl |
| Pinsel | `B` | Ziehen malt Clips lückenlos aneinander |
| Radierer | `D` | Klicken oder Ziehen löscht |
| Stumm | `T` | Klicken oder Ziehen schaltet Clips stumm oder wieder laut |
| Messer | `C` | Eine Linie ziehen schneidet quer durch Clips, ein Klick schneidet an dieser Stelle |
| Lasso | `E` | Ziehen zeichnet einen Auswahlrahmen. `Shift` fügt hinzu |
| Zoom | `Z` | Klick zoomt hinein, einen Bereich ziehen zoomt darauf, Rechtsklick zoomt heraus |
| Abhören | `Y` | Klicken oder Ziehen spielt ab dieser Stelle |
| Slip | `S` | Ziehen verschiebt den Inhalt des Clips innerhalb des Clips |

## Mit Clips arbeiten

- Ziehe einen Clip, um ihn zu verschieben. `Shift`+Ziehen erzeugt eine Kopie.
- Ziehe am Ende eines Clips, um seine Größe zu ändern. Ziehen am Anfang kürzt ihn, während der Inhalt an seinem Platz bleibt.
- Rechtsklick löscht einen Clip. `Ctrl`+Ziehen wählt aus.
- Doppelklicke einen Clip, um ihn zu öffnen.
- Die Taste oben links an einem Clip öffnet das Clipmenü (eigenständig machen, umbenennen und färben, zerteilen, und bei Audio: Tonhöhe, ans Tempo anpassen, umkehren).
- Halte beim Ziehen `Alt` gedrückt, um das Raster zu ignorieren.
- Audioclips haben Fade-Griffe und eine Clip-Lautstärke, die du nach oben oder unten ziehst.
- `Shift+M` schaltet Stretch um: Wird ein Audioclip dann in der Größe geändert, wird das Audio gedehnt, statt abgeschnitten.

## Spuren

Im Kopf einer Spur wählt ein Klick aus, `Ctrl`+Klick fügt hinzu oder entfernt, `Shift`+Klick wählt einen Bereich, und Rechtsklick öffnet das Spurmenü. `Shift`+Mausrad schiebt die Spur nach oben oder unten. Das Lämpchen der Spur schaltet sie stumm (Klick) oder solo (`Ctrl`+Klick oder Rechtsklick). Ziehe die Trennlinie zwischen zwei Spuren, um eine in der Größe zu ändern, oder `Shift`+Ziehen für alle.

## Lineal, Loop und Marker

- Klicke auf das Lineal, um die Song-Position zu setzen.
- Ziehen mit rechter Maustaste, `Ctrl`+Ziehen oder Doppelklick-Ziehen auf dem Lineal erzeugt eine Zeitauswahl, die zugleich der Loop ist. `Shift`+Ziehen verschiebt sie.
- `Ctrl+Ins` fügt die Länge der Zeitauswahl über alle Spuren ein, `Ctrl+Del` entfernt sie.
- `Alt+T` fügt einen Marker hinzu. Klicke auf einen Marker, um dorthin zu springen.
- `Ctrl`+Mausrad zoomt, `Alt`+Mausrad ändert die Spurhöhe.

## Automation

Klicke mit rechts auf einen Regler oder Fader und wähle `Automationsclip erstellen`. Der Clip erscheint in der Playlist. Mit dem Stift ziehst du seine Punkte, und ein Klick auf die Kurve fügt einen hinzu. Automationsspuren unter den Spuren sind optional und standardmäßig aus. Siehe [Automation](/wiki/automation/).

## Arrangements

Das Arrangement-Feld in der Werkzeugleiste schaltet zwischen Arrangements um. Ein Projekt kann mehrere haben.

## Tasten

| Befehl | Taste | Was sie bewirkt |
|---|---|---|
| Alles auswählen | `Ctrl+A` | Wählt jeden Clip |
| Kopieren, Ausschneiden, Einfügen | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiert, schneidet aus oder fügt Clips ein |
| Duplizieren | `Ctrl+B` | Dupliziert die Auswahl |
| Löschen | `Del` | Löscht die gewählten Clips |
| Löschen mit Nachrücken | `Shift+Del` | Löscht die gewählten Clips und schließt die Lücken |
| Ripple-Bearbeitung | `Shift+S` | Schaltet die Ripple-Bearbeitung ein oder aus |
| Stummschalten, Laut | `Alt+M` / `Shift+Alt+M` | Schaltet die gewählten Clips stumm oder wieder laut |
| Eigenständig machen | `Ctrl+Shift+V` | Macht die gewählten Clips eigenständig |
| Zusammenführen | `Ctrl+G` | Führt Pattern-Clips zusammen |
| Stretch | `Shift+M` | Schaltet Stretch für das Ändern der Größe von Audioclips um |
| Zeit einfügen | `Ctrl+Ins` | Fügt die Länge der Zeitauswahl über alle Spuren ein |
| Zeit löschen | `Ctrl+Del` | Entfernt die Zeitauswahl über alle Spuren |
| Marker hinzufügen | `Alt+T` | Fügt einen Marker hinzu |
| Zoom | `PageUp` / `PageDown` | Zoomt hinein oder heraus |
| Auf Inhalt zoomen | `Shift+4` | Zoomt so, dass alles zu sehen ist |
| Maximieren | `Enter` | Maximiert die Playlist oder stellt sie wieder her |

## Gut zu wissen

- Die Playlist hörst du nur im Song-Modus. Im Pattern-Modus spielt `Space` das aktuelle Pattern, egal was auf den Spuren liegt.
- `Ansicht › Layout-Stil › Clip-Start-Arbeitsablauf` fügt neben der Playlist ein Raster aus Clips und Szenen hinzu. Es ist optional. Siehe [Layouts](/wiki/layouts/).
- Liegen mehrere überlappende Automationsclips auf einem Regler, kann die Wahl, welcher davon spielt, von der in FL Studio abweichen (Release Notes 0.1.25).
- Ein Tempo, das ein verknüpfter MIDI-Controller verstellt, ändert die Größe von Audioclips nicht.
- Unter Linux braucht Video FFmpeg.

## Verwandt

- [Channel Rack](/wiki/channel-rack/)
- [Automation](/wiki/automation/)
- [Aufnahme](/wiki/recording/)
- [Export](/wiki/exporting/)
- [Layouts](/wiki/layouts/)
