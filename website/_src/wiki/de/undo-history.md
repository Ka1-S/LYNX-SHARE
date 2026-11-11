---
source: 9f36c60cc7
---
![Das Fenster Rückgängig-Verlauf: sechs Schritte von Start bis Add notes, mit Mute: Kick als aktuellem](/assets/img/wiki/history.webp)

Jede Änderung am Projekt ist ein Schritt im Rückgängig-Verlauf: eine Note, die du hinzufügst, ein Clip, den du verschiebst, ein Fader, eine Routing-Änderung, das Tempo. `Bearbeiten › Rückgängig-Verlauf...` (`Ctrl+Alt+H`) zeigt die Schritte als Liste. Unabhängig davon speichert LYNX SHARE Sicherungskopien des Projekts, während du arbeitest.

## Zu einem früheren Schritt zurückgehen

1. Wähle `Bearbeiten › Rückgängig-Verlauf...` oder drücke `Ctrl+Alt+H`.
2. Die Liste zeigt die Schritte, der neueste steht unten. Die erste Zeile, `Start`, ist das Projekt, wie es geöffnet wurde.
3. Klicke auf einen Schritt. Das Projekt geht auf den Zustand direkt danach zurück, und die späteren Schritte werden grau.
4. Klicke auf einen grauen Schritt, um wieder vorwärts zu gehen.
5. Mache eine neue Änderung, um vom gewählten Schritt aus weiterzuarbeiten. Die grauen Schritte sind dann weg.

## Die Rückgängig-Tasten

`Ctrl+Z` funktioniert nicht wie in den meisten Programmen. Es macht den letzten Schritt rückgängig, und noch einmal gedrückt stellt es ihn wieder her, es wechselt also zwischen den letzten beiden Zuständen. Um weiter zurückzugehen, nimm `Ctrl+Alt+Z`.

| Befehl | Taste | Was er tut |
|---|---|---|
| `Rückgängig` | `Ctrl+Z` | Macht den letzten Schritt rückgängig. Nach einem Rückgängig stellt es den nächsten Schritt wieder her, und der Menüeintrag lautet `Rückgängig (Wiederholen)` |
| `Rückwärts rückgängig` | `Ctrl+Alt+Z` | Geht immer zurück, ein Schritt pro Druck |
| `Wiederholen` | `Ctrl+Y` oder `Ctrl+Shift+Z` | Geht vorwärts |
| `Rückgängig-Verlauf...` | `Ctrl+Alt+H` | Öffnet die Liste |

Nach jedem Rückgängig oder Wiederholen nennt die Hinweisleiste den Schritt und zeigt die Ebene, zum Beispiel `Level 2/3`.

Wenn du das übliche Verhalten bevorzugst, schalte unter `Optionen › Allgemeine Einstellungen...` `Alternativer Rückgängig-Modus (klassisches Rückgängig statt Rückgängig/Wiederholen auf demselben Kürzel)` ein. `Ctrl+Z` geht dann immer zurück.

## Was ein Schritt ist und was nicht

- Änderungen am Projekt sind Schritte: Noten, Patterns, Clips, Kanäle, Mixer-Pegel und Routing, Effekte, Automation, Tempo, Stummschaltungen.
- Eine Aufnahme ist ein Schritt. Ebenso das Zusammenführen eines Parts in einer [Online-Session](/wiki/online-sessions/) und `Comp aus diesen Takes erstellen`.
- Was das Projekt nicht verändert, ist kein Schritt: Zoomen, Scrollen, Fenster verschieben und die eigenen Einstellungen des Programms.
- Speichern und Exportieren werden nicht rückgängig gemacht. Die Dateien, die sie geschrieben haben, bleiben.
- Die eigenen Plugin-Fenster von LYNX SHARE haben neben den Einstellungen A und B ein eigenes Rückgängig und Wiederholen für die Änderungen dieses Plugins.

## Einstellungen

Beide stehen unter `Optionen › Allgemeine Einstellungen...`.

| Einstellung | Was sie tut |
|---|---|
| `Maximale Rückgängig-Schritte` | Wie viele Schritte behalten werden: 25, 50, 100, 200, 500 oder 1000. Standard ist 100 |
| `Alternativer Rückgängig-Modus` | Macht `Ctrl+Z` zu einem klassischen Rückgängig |

## Backups

LYNX SHARE speichert standardmäßig alle 2 Minuten eine Sicherungskopie des geöffneten Projekts und behält die 10 neuesten pro Projekt. Ein Backup ist eine normale `.lynx`-Datei im Ordner `autosave` innerhalb des Einstellungsordners, benannt nach dem Projekt mit `(autosaved on ...)` sowie Datum und Uhrzeit.

Jedes Mal, wenn du über eine bestehende Projektdatei speicherst, wird die ersetzte Version in denselben Ordner kopiert, mit `(overwritten on ...)` im Namen.

| Befehl | Was er tut |
|---|---|
| `Datei › Zur letzten Sicherung zurückkehren...` | Ersetzt das geöffnete Projekt nach Rückfrage durch sein neuestes Backup. Die Projektdatei selbst ändert sich erst, wenn du speicherst |
| `Datei › Ordner der automatischen Sicherung öffnen` | Zeigt die Backups im Dateimanager |
| `Datei › Neue Version speichern` (`Ctrl+Alt+S`) | Speichert als `Song_2`, `Song_3` und so weiter, neben der aktuellen Datei |

Der Bereich `Sicherung` unter `Optionen › Allgemeine Einstellungen...` hat diese Einstellungen:

| Einstellung | Was sie tut |
|---|---|
| `Sicherungskopie speichern` | `Nie` oder ein Intervall in Minuten |
| `Aufbewahrte Sicherungen pro Projekt` | 1, 3, 5, 10, 20 oder 50 |
| `Sicherungen warten, solange der Song läuft oder auf einem MIDI-Keyboard gespielt wird` | Hält ein Backup zurück, damit es dich nicht unterbricht |
| `Längste Pause zwischen Backups` | 5 bis 60 Minuten. Nach dieser Zeit wird auch während des Spielens ein Backup angelegt |

Ist die letzte Sitzung unerwartet beendet worden, bietet der nächste Start an, das neueste Backup dieser Sitzung zu öffnen.

## Gut zu wissen

- Der Verlauf wird nicht mit dem Projekt gespeichert. Ein Projekt zu öffnen oder ein neues zu beginnen leert ihn.
- Ist der Verlauf voll, fallen die ältesten Schritte weg.
- In einer Live-Online-Session holen Rückgängig und Wiederholen nur deine eigenen Änderungen zurück, nie die anderer.
- Steht `Sicherungskopie speichern` auf `Nie`, bleiben die Kopien, die beim Überspeichern einer Datei entstehen, trotzdem erhalten.
- Der Einstellungsordner ist `%APPDATA%\LYNX` unter Windows, `~/Library/Application Support/LYNX` unter macOS und `~/.config/LYNX` unter Linux.

## Siehe auch

- [Tastenkürzel](/wiki/shortcuts/)
- [Einstellungen](/wiki/settings/)
- [Dateiformate](/wiki/file-formats/)
- [Fehlerbehebung](/wiki/troubleshooting/)
- [Was nur LYNX SHARE kann: Absturzsicherheit](/why/#crash)
