---
source: 1b54c6493f
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
- Das Menü eines Audioclips folgt dem Layout von FL Studio in zwei Spalten. Die linke Spalte hat den Abschnitt `Audioclip` (`Vorschau`, `Stumm`, `Umbenennen und färben...`, `Farbe ändern...`, `Zufällige Farbe`, `Symbol ändern...`, `Quellkanal auswählen`, `Kanaleinstellungen`, `Eigenständig machen`, `Alle ähnlichen Clips auswählen`, `Delete`, `Eigenschaften...`) und den Abschnitt `Region` (`Region auswählen`, `Zerhacken`). Die rechte Spalte hat `Sample` (`Als Sample eindeutig machen`, `Sample bearbeiten`, `Stems aus Sample extrahieren`, `Tempo erkennen`, `An Tempo anpassen`), `Automation` (`Automatisieren › Panning` oder `Lautstärke`, `Crossfade mit`) und `Fades` (`Fades zurücksetzen`). Eigene Einträge von LYNX SHARE, etwa `Hier teilen`, `Rückwärts`, `Normalisieren` und `In MIDI umwandeln`, stehen zuletzt unter einer Linie.
- `Automatisieren › Panning` oder `Lautstärke` erzeugt auf ihrer Spur einen Automationsclip, der so lang ist wie der Audioclip, für das Pan oder die Lautstärke der Levels-Einstellung des Kanals.
- Halte beim Ziehen `Alt` gedrückt, um das Raster zu ignorieren.
- Audioclips haben Fade-Griffe und eine Clip-Lautstärke, die du nach oben oder unten ziehst. Klicke mit der rechten Maustaste auf einen Fade-Griff für sein Menü mit `Crossfade erstellen`. Klicke mit der rechten Maustaste auf den Lautstärke-Griff für `Gain zurücksetzen` und `Normalisieren`.
- `Shift+M` schaltet Stretch um: Wird ein Audioclip dann in der Größe geändert, wird das Audio gedehnt, statt abgeschnitten.
- `Shift+Left` und `Shift+Right` verschieben die gewählten Clips um einen Raster-Schritt, `Shift+Up` und `Shift+Down` um eine Spur. `Alt+Left` und `Alt+Right` verschieben sie um einen Bildschirmpixel bei der aktuellen Zoomstufe, ohne Raster. Ist kein Clip gewählt, bewegen sich alle Clips.
- `Als Sample eindeutig machen` im Menü eines Audioclips gibt dem Clip eine neue Datei mit dem, was er gespielt hat, wobei Tonhöhe, Stretch und Reverse schon im Audio stecken.
- Ein Audioclip mit Warp-Markern zeigt sie als kleine Fähnchen an seiner Oberkante. Ziehe ein Fähnchen zur Seite, um den Marker zu verschieben.

## Die Menüs Edit und Select

Das Menü der Playlist hat ein Untermenü `Bearbeiten` und eines `Auswählen`. `Bearbeiten` enthält die Clip-Befehle: Ausschneiden, Kopieren und Einfügen, `Nach links verschieben`, `Nach rechts verschieben`, `Nach oben verschieben`, `Nach unten verschieben`, `Eigenständig machen`, `Als Sample eindeutig machen`, `Leerraum einfügen`, `Leerraum löschen`, die Befehle zum Zusammenführen und die Fade-Optionen. `Auswählen` hat `Nach ausgewählter Quelle auswählen` (`Shift+C`), `Auswahl umkehren` (`Shift+I`), `Auswahl aufheben` (`Ctrl+D`), die stummen und die überlappenden Clips sowie `Magisches Lasso`. Ist `Magisches Lasso` an, bewege die Maus ohne gedrückte Taste in einer Schleife um Clips, um sie zu wählen.

## Spuren

Im Kopf einer Spur wählt ein Klick aus, `Ctrl`+Klick fügt hinzu oder entfernt, `Shift`+Klick wählt einen Bereich, und Rechtsklick öffnet das Spurmenü. `Shift`+Mausrad schiebt die Spur nach oben oder unten. Das Lämpchen der Spur schaltet sie stumm (Klick) oder solo (`Ctrl`+Klick oder Rechtsklick). Ziehe die Trennlinie zwischen zwei Spuren, um eine in der Größe zu ändern, oder `Shift`+Ziehen für alle.

`Ansicht` im Menü der Playlist wählt, was die Spurköpfe zeigen: `Bedienelemente auf Audiospuren anzeigen`, `Pegel auf Audiospuren anzeigen` und `Pegel auf Instrumentenspuren anzeigen`.

`Spurmodus` im Spurmenü macht eine Spur zu `Nicht zugewiesen`, einer `Audiospur` oder einer `Instrumentenspur`, wie in FL Studio. `Audiospur` listet zuerst `Neue Mixerspur(en)` auf und dann jeden Mixer-Insert mit Nummer und Name. Die Wahl gilt für alle gewählten Spuren. `Instrumentenspur` hat `Vorhandenen Kanal verwenden`, `Weitere Plugins...` und die Instrumentenliste des Channel Racks.

Das `+` oben links in den Spurköpfen öffnet dieselben Optionen für eine neue Spur: `Audiospur hinzufügen`, `Weitere Plugins...` und die Instrumente. Ein Rechtsklick auf `+` fügt sofort eine Audiospur hinzu.

## Pattern-Blöcke

Ist `Pattern-Blöcke (aktuelles Projekt)` im Menü der Playlist an, hat ein Bereich unter den Spuren eine Zeile pro Pattern. Dort setzt der Stift einen Block und zieht ihn an seiner Zeile entlang, der Pinsel malt Blöcke und der Radierer löscht sie. Ein Rechtsklick löscht mit jedem Werkzeug.

## Lineal, Loop und Marker

- Klicke auf das Lineal, um die Song-Position zu setzen.
- Ziehen mit rechter Maustaste, `Ctrl`+Ziehen oder Doppelklick-Ziehen auf dem Lineal erzeugt eine Zeitauswahl, die zugleich der Loop ist. `Shift`+Ziehen verschiebt sie.
- `Ctrl+Ins` fügt die Länge der Zeitauswahl über alle Spuren ein, `Ctrl+Del` entfernt sie.
- `Alt+T` fügt einen Marker hinzu. Klicke auf einen Marker, um dorthin zu springen.
- `Ctrl`+Mausrad zoomt, `Alt`+Mausrad ändert die Spurhöhe.

## Automation

Klicke mit rechts auf einen Regler oder Fader und wähle `Automationsclip erstellen`. Der Clip erscheint in der Playlist. Mit dem Stift ziehst du seine Punkte, und ein Klick auf die Kurve fügt einen hinzu. Automationsspuren unter den Spuren sind optional und standardmäßig aus. Siehe [Automation](/wiki/automation/).

Klicke mit rechts auf einen Punkt für sein Menü. Seine Liste `Modus` hat die 13 Kurvenmodi von FL Studio für das Segment, das an diesem Punkt endet: `Glätten`, `Hold`, drei einfache Kurven, drei doppelte Kurven, `Halbsinus`, `Treppe`, `Weiche Treppe`, `Puls` und `Welle`. Ein neuer Punkt übernimmt den zuletzt genutzten Modus. Die Modi werden aus .flp-Dateien gelesen und in sie geschrieben.

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
| Clips verschieben | `Shift+Left` / `Shift+Right` | Verschiebt die gewählten Clips um einen Raster-Schritt |
| Auf eine andere Spur verschieben | `Shift+Up` / `Shift+Down` | Verschiebt die gewählten Clips um eine Spur |
| Clips nudgen | `Alt+Left` / `Alt+Right` | Verschiebt die gewählten Clips um einen Bildschirmpixel, ohne Raster |
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
- Wie in FL Studio spielt ein Pattern immer nur aus einem Clip. Startet ein späterer Clip desselben Patterns, auf irgendeiner Spur, startet der frühere Clip ab dort keine Noten mehr. Clips verschiedener Patterns spielen zusammen. Das gilt in neuen Projekten und in FL-Studio-Projekten. Ein .lynx-Projekt, das vor 0.1.27 gespeichert wurde, spielt weiterhin jeden Clip vollständig.
- Ein Tempo, das ein verknüpfter MIDI-Controller verstellt, ändert die Größe von Audioclips nicht.
- Unter Linux braucht Video FFmpeg.

## Verwandt

- [Channel Rack](/wiki/channel-rack/)
- [Automation](/wiki/automation/)
- [Aufnahme](/wiki/recording/)
- [Export](/wiki/exporting/)
- [Layouts](/wiki/layouts/)
