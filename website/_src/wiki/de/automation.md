---
source: b96330a448
---
![Der Automationseditor mit einer Kurve, die über sechs Takte ansteigt](/assets/img/wiki/automation.webp)

Ein Automationsclip ist eine Kurve in der [Playlist](/wiki/playlist/), die einen Regler bewegt, während der Song läuft: die Lautstärke eines Kanals, einen Mixer-Fader, einen Plugin-Parameter, das Tempo. Der Clip hat ein eigenes Editorfenster, den Automation-Editor.

Es gibt drei weitere Wege, einen Regler zu bewegen. Events sind Werte, die in einem Pattern gespeichert sind. Eine Verknüpfung lässt einen MIDI-Regler oder einen internen Controller den Regler steuern. Ein Modulator lässt einen Regler um seinen eigenen Wert schwanken.

## Einen Regler automatisieren

1. Setze den Abspielcursor dorthin, wo die Automation beginnen soll, oder wähle in der Playlist einen Zeitbereich aus.
2. Klicke mit rechts auf den Regler oder Fader und wähle `Automationsclip erstellen`.
3. Der Clip landet auf der ersten leeren Playlist-Spur, und der Automationseditor öffnet sich. Die Kurve beginnt als flache Linie auf dem aktuellen Wert des Reglers.
4. Klicke in den Editor, um einen Punkt zu setzen, und ziehe ihn an seinen Platz.
5. Halte `Alt` und ziehe ein Segment nach oben oder unten, um es zu biegen.
6. Spiele den Song im Song-Modus ab. Der Regler folgt der Kurve.

Weitere Wege zum selben Clip:

- `Hinzufügen › Automation für zuletzt geänderten Parameter` erzeugt einen Clip für den Regler, den du zuletzt bewegt hast.
- Die Anzeige des zuletzt bewegten Reglers und `Tools › Zuletzt geändert` öffnen dasselbe Menü wie ein Rechtsklick auf den Regler.
- Bei einem Plugin von Drittanbietern öffnest du das Menü des Kanals im Channel Rack oder das Menü des Effekt-Slots im Mixer und nimmst `Plugin-Parameter automatisieren`. `MIDI-Controller automatisieren (CC, Aftertouch, Pitchbend)` steht gleich daneben.

Um den Editor erneut zu öffnen, doppelklicke den Clip oder klicke mit rechts darauf und wähle `Punkte bearbeiten...`. `Automationsclip bearbeiten...` im eigenen Menü des Reglers macht dasselbe.

## Der Automationseditor

| Befehl | Taste | Was er tut |
|---|---|---|
| Neuer Punkt | Klick oder Rechtsklick, dann ziehen | Setzt einen Punkt und bewegt ihn, solange du die Taste hältst |
| Punkt verschieben | Ziehen | `Shift` behält seine Zeit, `Ctrl` behält seinen Wert, `Alt` ignoriert das Raster |
| Segment biegen | `Alt`+nach oben oder unten ziehen | Ändert die Spannung des Segments unter der Maus |
| Spannungsgriff | Nach oben oder unten ziehen | Biegt das Segment. `Ctrl` ist feiner, ein Rechtsklick macht es gerade |
| Punkt auf der Kurve | `Shift`+Rechtsklick | Setzt einen Punkt auf die Linie |
| Punktmenü | Rechtsklick auf einen Punkt | `Wert eingeben...`, `Wert kopieren`, `Wert einfügen`, `Punkt löschen` und `Ganze Kurve`, um alles zu skalieren, zu spiegeln oder zu glätten |
| Zoom | `Ctrl`+Mausrad | Zoomt die Ansicht |
| Scrollen | Mit der mittleren Taste ziehen | Verschiebt die Ansicht |

## LFO-Modus

![Der Automationseditor im LFO-Modus, mit einer Dreieckswelle und den Reglern SPD, TNS, SKEW, PW und AMNT](/assets/img/wiki/automation-lfo.webp)

Hake oben im Editor `LFO` an. Jetzt formt eine Wellenform den Wert des Clips, und die Punkte, die du zeichnest, legen die Amplitude des LFO fest statt des Werts selbst.

| Einstellung | Was sie tut |
|---|---|
| `SPD` | Die Länge eines Zyklus in Schlägen |
| `TNS` | Spannung der Welle |
| `SKEW` | Schiefe der Welle |
| `PW` | Pulsbreite |
| `AMNT` | Wie weit der LFO den Wert bewegt |
| `MUL` | An: Der LFO wird mit dem Pegel der Hüllkurve multipliziert. Aus: Er wird addiert |

## Events in einem Pattern

![Der Event-Editor mit Lautstärke-Events als Balken](/assets/img/wiki/eventeditor.webp)

Events gehören zu einem Pattern und spielen überall dort, wo dieses Pattern spielt. Klicke mit rechts auf einen Regler und wähle `Events im Pattern bearbeiten`, um den Event-Editor dafür zu öffnen.

- `Zeichnen` setzt Werte beim Ziehen, und mit der rechten Taste ziehen löscht. `Linie` zeichnet eine gerade Rampe. `Auswählen` markiert einen Zeitbereich für die Werkzeuge unten.
- `Raster` reicht von 1/2 Step bis 1 Takt.
- `Skala`, `Versatz` und `Zufällig` ändern die Events der Auswahl oder des ganzen Patterns.
- `LFO` schreibt eine Sinus-, Dreiecks- oder Rechteckwelle über 1 Schlag bis 4 Takte.
- `Leeren` entfernt die Events.
- `Zu Automation...` wandelt die Events in einen Automationsclip um und entfernt sie aus dem Pattern.
- `Datei` öffnet und speichert die Events als `.fsc`-Datei und importiert eine MIDI-Datei.

`Song mit dieser Position initialisieren` im selben Rechtsklick-Menü speichert den Wert, auf den der Regler bei jedem Start des Songs zurückgesetzt wird.

## Deine Bewegungen aufnehmen

Schalte die Aufnahme ein, mit angehaktem `Automation` im Aufnahmefilter, drücke Wiedergabe und bewege den Regler. Im Song-Modus werden die Bewegungen zu einem Automationsclip. Im Pattern-Modus werden sie zu Events im aktuellen Pattern. Siehe [Aufnahme](/wiki/recording/).

## Verknüpfungen und Modulatoren

- `Mit MIDI-Controller verknüpfen (einen Regler am Controller bewegen)` wird unter [MIDI-Controller](/wiki/midi-controllers/) behandelt.
- `Mit internem Controller verknüpfen` lässt eine Hüllkurve (Envelope), eine Formel (Formula) oder einen Peak Controller im Projekt den Regler steuern. Füge zuerst einen davon hinzu.
- `Modulieren` hängt einen Modulator an, einen LFO oder eine Zufallsquelle, die einen LYNX-SHARE-Regler um seinen eigenen Wert bewegt. `Tools › Modulatoren...` listet sie auf.

## Automationsspuren

Automationsspuren sind standardmäßig aus. Schalte `Automationsspuren` im Menü der Playlist ein. Jeder Spurkopf hat dann einen Button, der unter der Spur eine Automationsspur hinzufügt, und das Reglermenü bekommt `In Spur automatisieren`. In einer Automationsspur klickst du, um einen Punkt zu setzen, oder ziehst ihn, klickst mit rechts, um einen zu löschen, und ziehst mit `Alt`, um die Linie zu biegen. Das Menü der Automationsspur hat `Zu Automationsclip`.

## Gut zu wissen

- Ein neuer Clip beginnt an der Zeitauswahl, wenn es eine gibt, sonst am Abspielcursor im Song-Modus. Schalte `Automationsclips an der Wiedergabeposition erstellen` unter `Optionen › Allgemeine Einstellungen...` aus, damit er am Anfang des Songs beginnt.
- Mehrere Clips auf einem Regler spielen jeweils an ihrer eigenen Stelle. Vor seinem ersten Clip behält ein Regler seinen eigenen Wert.
- Am Anfang eines Clips erreicht der Regler den neuen Wert in etwa 8 ms. FL Studio gleitet dorthin, deshalb kann ein importiertes Projekt an diesen Stellen leicht abweichen.
- Wo sich Clips auf demselben Regler überlappen, kann der Clip, den LYNX SHARE wählt, ein anderer sein als der, den FL Studio wählt.
- Modulatoren wirken nur auf die eigenen Regler von LYNX SHARE und werden beim Export nach `.flp` weggelassen.

## Siehe auch

- [Playlist](/wiki/playlist/)
- [Aufnahme](/wiki/recording/)
- [MIDI-Controller](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Tastenkürzel](/wiki/shortcuts/)
