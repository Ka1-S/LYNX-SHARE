---
source: 8df354b49e
---
![Der Browser links mit seinen drei Reitern, dem Suchfeld, dem Ordnerbaum und der Vorschauleiste](/assets/img/wiki/browser-tree.webp)

Der Browser listet deine Sample-Ordner, den Inhalt des aktuellen Projekts und die Plugins auf, die LYNX SHARE kennt. Mit `Alt+F8` blendest du ihn ein oder aus. `F8` allein öffnet die Plugin-Auswahl, eine eigene Vollbildansicht aller Instrumente und Effekte.

## Ein Sample ins Projekt laden

1. Drücke `Alt+F8`, um den Browser anzuzeigen.
2. Klicke im Reiter `ALLE` auf einen Ordner, um ihn zu öffnen.
3. Klicke auf eine Audiodatei, um sie anzuhören. Mit `Up` und `Down` gehst du durch die Dateien und hörst jede vor.
4. Doppelklicke die Datei, um einen neuen Kanal mit dem Sample im [Channel Rack](/wiki/channel-rack/) zu erhalten.
5. Oder ziehe die Datei auf die [Playlist](/wiki/playlist/), um einen Audioclip zu erhalten.

## Reiter

| Reiter | Zeigt |
|---|---|
| ALL | Das aktuelle Projekt, die Plugin-Datenbank und jeden Sample-Ordner |
| PROJEKT | Nur das, was das aktuelle Projekt verwendet: Patterns, Effekte, Generatoren, Automation, Audiodateien |
| FAVORITEN | Die Dateien und Ordner, die du mit einem Stern markiert hast (Rechtsklick, dann `Favorit`) |

## Durchsuchen und vorhören

- Klicke auf einen Ordner, um ihn zu öffnen oder zu schließen. Mit Rechtsklick öffnest du ein Menü (alle zuklappen, im Dateimanager anzeigen).
- Klicke auf eine Audiodatei, um sie anzuhören. Die Vorschauleiste unter der Liste zeigt ihre Wellenform. Klicke auf die Leiste, um die Datei noch einmal zu hören, oder ziehe von ihr, als wäre sie die Datei selbst.
- Das Feld über der Vorschauleiste zeigt Datum, Größe und Pfad der gewählten Datei.

## Ein Sample verwenden

| Aktion | Was sie bewirkt |
|---|---|
| Doppelklick | Neuer Kanal mit dem Sample |
| Auf das Channel Rack ziehen | Neuer Kanal, oder auf eine Zeile ziehen, um es dort zu laden |
| Auf die Playlist ziehen | Audioclip |
| `Enter` | Lädt die Datei in den gewählten Kanal |
| `Shift+Up` / `Shift+Down` | Springt zur vorherigen oder nächsten Datei und lädt sie in den gewählten Kanal |

Mit der letzten Aktion kannst du verschiedene Samples direkt an Ort und Stelle probehören, während das Pattern läuft.

## Andere Dateitypen

- **Projekt**: Doppelklick öffnet es.
- **MIDI-Datei**: Doppelklick öffnet sie als Projekt.
- **SoundFont oder SFZ**: Klick hört sie vor, Doppelklick erzeugt einen LYNX-Soundfont-Kanal (oder lädt sie in den gewählten).
- **Mixer-Preset**: Doppelklick lädt es auf die gewählten Mixer-Inserts, oder ziehe es auf ein Insert.
- **Effekt-Preset**: Ziehe es auf ein Mixer-Insert.

## Plugin-Datenbank

Der Eintrag Plugin-Datenbank listet die eingebauten Instrumente und Effekte von LYNX SHARE und die installierten Plugins. `Shift+F8` springt dorthin.

- Doppelklicke ein Instrument, um es als neuen Kanal hinzuzufügen, oder ziehe es auf das Channel Rack.
- Doppelklicke einen Effekt, um ihn zum gewählten [Mixer](/wiki/mixer/)-Insert hinzuzufügen, oder ziehe ihn auf ein Insert oder einen Effekt-Slot.
- `F2` benennt das gewählte Plugin um.
- Klicke mit rechts auf ein Plugin und wähle `In Ordner verschieben`, um Plugins in Ordner zu sortieren. Das Slot-Menü des Mixers und das Hinzufügen-Menü des Channel Racks listen dann jeden obersten Ordner auf.

## Der Reiter PROJEKT

Die hier gelisteten Patterns kannst du anklicken, um sie zum aktuellen Pattern zu machen, doppelklicken, um sie im [Piano Roll](/wiki/piano-roll/) zu öffnen, oder in die Playlist ziehen. Der Abschnitt des aktuellen Projekts listet auch den Rückgängig-Verlauf: Klicke auf einen Schritt, um dorthin zurückzugehen.

## Suche

Drücke `Ctrl+F`, um den Cursor ins Suchfeld zu setzen. Gesucht wird im gewählten Ordner (oder in allen Ordnern) und darunter. Im Reiter PROJEKT durchsucht sie die Elemente des Projekts.

Alle Wörter müssen passen. Das Feld versteht außerdem `OR`, ein Minuszeichen zum Ausschließen eines Worts und Platzhalter mit `*`:

```
kick -loop
snare OR clap
hat*.wav
```

Die Ordner werden im Hintergrund indiziert. `Suchindex aktualisieren` im Menü des Browsers baut den Index neu auf.

## Deine eigenen Ordner

Mit `Ordner hinzufügen` in der Ordnerliste des Browsers fügst du einen Ordner mit Samples, Projekten oder SoundFonts hinzu. Unter `Optionen › Dateieinstellungen (Suchordner)` findest du die Suchordner.

## Tasten

Die Pfeiltasten, `Enter` und `F2` funktionieren nur, wenn der Browser den Fokus hat.

| Befehl | Taste | Was sie bewirkt |
|---|---|---|
| Browser anzeigen | `Alt+F8` | Blendet den Browser ein oder aus |
| Plugin-Auswahl | `F8` | Öffnet die Plugin-Auswahl im Vollbild |
| Plugin-Datenbank | `Shift+F8` | Springt zum Eintrag Plugin-Datenbank |
| Suche | `Ctrl+F` | Setzt den Cursor ins Suchfeld |
| Weitergehen und vorhören | `Up` / `Down` | Wählt die vorherige oder nächste Datei und spielt sie ab |
| Ordner schließen | `Left` | Schließt einen Ordner oder springt zu dessen übergeordnetem Ordner |
| Ordner öffnen | `Right` | Öffnet einen Ordner |
| Laden | `Enter` | Lädt die Datei in den gewählten Kanal |
| Weitergehen und laden | `Shift+Up` / `Shift+Down` | Springt zur vorherigen oder nächsten Datei und lädt sie in den gewählten Kanal |
| Plugin umbenennen | `F2` | Benennt das gewählte Plugin in der Plugin-Datenbank um |

## Gut zu wissen

- Einen Ordner aus der Liste zu entfernen, löscht ihn nicht von der Festplatte.
- `F8` und `Alt+F8` sind zwei verschiedene Dinge. `F8` ist die Plugin-Auswahl, `Alt+F8` ist der Browser.
- Suchergebnisse stammen aus einem Index, der im Hintergrund aufgebaut wird. `Suchindex aktualisieren` im Menü des Browsers baut ihn neu auf.
- `Enter` lädt die Datei in den gewählten Kanal. Ein Doppelklick erzeugt einen neuen Kanal.

## Verwandt

- [Channel Rack](/wiki/channel-rack/)
- [Plugins von Drittanbietern](/wiki/plugins/)
- [Dateiformate](/wiki/file-formats/)
- [Rückgängig und Verlauf](/wiki/undo-history/)
- [Instrumente und Effekte](/instruments-and-effects/)
