---
source: 5291e1e4ad
---
![Das Piano Roll](/assets/img/pianoroll.webp)

Das Piano Roll zeigt die Noten eines Kanals im aktuellen Pattern, mit einer Klaviatur links und einer Spur für Werte pro Note unten. Mit `F7` zeigst du es an oder blendest es aus, oder du wählst `Piano Roll` im Menü eines Kanals im [Channel Rack](/wiki/channel-rack/).

Das Kanalfeld in der Werkzeugleiste wählt, welchen Kanal du bearbeitest. Kanäle mit Noten in diesem Pattern sind markiert. Das Mausrad über dem Feld schaltet durch die Kanäle mit Noten, `Ctrl`+Mausrad durch alle.

## Ein paar Noten schreiben

1. Klicke im Channel Rack mit rechts auf den Namen eines Kanals und wähle `Piano Roll`.
2. Drücke `P` für den Stift.
3. Klicke ins Raster, um eine Note zu setzen. Halte die Taste gedrückt und ziehe, um sie zu positionieren.
4. Ziehe das Ende der Note, um sie länger oder kürzer zu machen.
5. Klicke mit rechts auf eine Note, um sie zu löschen.
6. Drücke `Space`, um das Pattern zu hören.

## Werkzeuge

![Die Werkzeugleiste des Piano Roll mit Kanalfeld, Werkzeugen und Rasterfeld](/assets/img/wiki/pianoroll-tools.webp)

Jedes Werkzeug hat eine Buchstabentaste. Die Buchstaben funktionieren, solange das Tastatur-Klavier (`Ctrl+T`) aus ist.

| Werkzeug | Taste | Was es tut |
|---|---|---|
| Stift | `P` | Klick setzt eine Note, Ziehen positioniert sie |
| Pinsel | `B` | Ziehen malt eine Reihe von Noten. Mit `Shift` eine nach der anderen |
| Drum-Pinsel | `N` | Ein Schlag pro Rasterzelle auf der Taste unter der Maus |
| Radierer | `D` | Klicken oder Ziehen löscht |
| Stumm | `T` | Klicken oder Ziehen schaltet Noten stumm oder hebt es auf |
| Messer | `C` | Eine Linie ziehen schneidet quer durch Noten, ein Klick schneidet an diesem Punkt |
| Lasso | `E` | Ziehen zeichnet einen Auswahlrahmen. `Shift` fügt hinzu |
| Zoom | `Z` | Klick zoomt hinein, einen Bereich ziehen zoomt darauf, Rechtsklick zoomt heraus |
| Abhören | `Y` | Klicken oder Ziehen spielt die Noten unter dem Cursor |

## Mit Noten arbeiten

- Ziehe eine Note, um sie zu verschieben. `Shift`+Ziehen macht eine Kopie.
- Ziehe das Ende einer Note, um ihre Größe zu ändern. Auch der Anfang lässt sich ziehen, wobei das Ende an seinem Platz bleibt.
- Rechtsklick löscht eine Note, in jedem Werkzeug.
- Doppelklicke eine Note, um ihre Eigenschaften zu öffnen.
- `Alt`+Mausrad über einer Note ändert ihre Velocity.
- `Ctrl`+Ziehen wählt aus. `Ctrl`+Klick fügt eine Note zur Auswahl hinzu oder nimmt sie heraus.
- Halte beim Ziehen `Alt`, um das Raster zu ignorieren. `Backspace` schaltet das Raster aus und ein.
- Sind mehrere Noten gewählt, dehnt oder staucht der Stretch-Griff sie in der Zeit.

## Klaviatur und Lineal

Klicke auf eine Taste der Klaviatur, um sie zu hören. Weiter rechts auf der Taste ist lauter. `Ctrl`+Klick auf eine Taste wählt ihre Noten. Mit der mittleren Taste auf der Klaviatur ziehen macht die Tasten höher oder niedriger.

Klicke auf das Lineal, um die Position zu setzen. `Ctrl`+Ziehen, Ziehen mit rechts oder Doppelklick-Ziehen auf dem Lineal wählt die Noten in einem Zeitbereich. `Ctrl`+Mausrad zoomt und `Shift`+Mausrad scrollt.

## Werkzeugleiste

| Einstellung | Was sie tut |
|---|---|
| Raster | „Main“ folgt dem Raster der Haupt-Werkzeugleiste, oder du wählst eines für dieses Fenster. |
| Akkord | Ein Klick erzeugt den gewählten Akkord, mit der angeklickten Note als Grundton. `Shift+N` geht zurück zu einzelnen Noten. |
| Skala | Hebt die Noten einer Skala hervor. Ein eigener Button rastet neue und verschobene Noten sowie eingehendes MIDI auf die gewählte Skala ein. |
| Slide (`S`) | Neue Noten sind Slide-Noten. Die gerade klingende Note gleitet zu ihnen (808-Glide). |
| Ghost-Notes (`Alt+V`) | Zeigt die Noten der anderen Kanäle hinter deinen. |
| Notenfarbe | Neue Noten bekommen diese Farbe. Es gibt 16. |
| Folgen | Die Ansicht scrollt mit dem Abspielcursor, während das Pattern spielt. |
| Spurfeld | Wählt, was die untere Spur bearbeitet: Velocity, Pan oder Feinstimmung. |

## Tasten

| Befehl | Taste | Was er tut |
|---|---|---|
| Alles auswählen | `Ctrl+A` | Wählt jede Note |
| Auswahl aufheben | `Ctrl+D` oder `Esc` | Hebt die Auswahl auf |
| Kopieren, Ausschneiden, Einfügen | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiert, schneidet aus oder fügt Noten ein |
| Duplizieren | `Ctrl+B` | Dupliziert die Auswahl |
| Löschen | `Del` | Löscht die Auswahl |
| Transponieren | `Up` / `Down` | Verschiebt die Auswahl um einen Halbton |
| Oktave transponieren | `Ctrl+Up` / `Ctrl+Down` | Verschiebt die Auswahl um eine Oktave |
| Verschieben | `Left` / `Right` | Verschiebt die Auswahl um einen Rasterschritt |
| Schnell quantisieren | `Ctrl+Q` | Quantisiert Anfänge und Längen |
| Starts quantisieren | `Shift+Q` | Quantisiert nur die Anfänge |
| Zerhacken | `Ctrl+U` | Zerhackt die Noten am Raster |
| Verbinden | `Ctrl+G` | Verbindet sich berührende Noten |
| Legato | `Ctrl+L` | Lässt jede Note bis zur nächsten dauern |
| Stumm, Stummschaltung aufheben | `Alt+M` / `Shift+Alt+M` | Schaltet die Auswahl stumm oder hebt es auf |
| MIDI importieren | `Ctrl+M` | Importiert eine MIDI-Datei in diesen Kanal |
| MIDI exportieren | `Ctrl+Shift+M` | Exportiert das Pattern als MIDI-Datei |
| Zoom | `PageUp` / `PageDown` | Zoomt hinein oder heraus |

Die Notenwerkzeuge (Arpeggieren, Strum, Spiegeln, Zufällig, Quantisieren, Zerhacken und andere) öffnen Dialoge. Ihre Tasten stehen auf der Seite [Tastenkürzel](/wiki/shortcuts/).

## Gut zu wissen

- Das Piano Roll zeigt einen Kanal in einem Pattern. Wechselst du das aktuelle Pattern, zeigt es dessen Noten.
- Die Werkzeugbuchstaben tun nichts, solange das Tastatur-Klavier (`Ctrl+T`) an ist.
- Rechtsklick löscht mit jedem Werkzeug, du musst also für eine Note nicht zum Radierer wechseln.
- `S` ist hier der Slide-Schalter. In der Playlist ist derselbe Buchstabe das Slip-Werkzeug.
- Noten, die keine einfachen Steps sind (andere Tonhöhen, längere Noten, Slides), lassen die Zeile des Kanals im Channel Rack eine Miniatur statt Step-Buttons zeigen.

## Siehe auch

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Tastenkürzel](/wiki/shortcuts/)
- [Dateiformate](/wiki/file-formats/)
