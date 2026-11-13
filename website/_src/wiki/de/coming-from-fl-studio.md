---
source: 3bf7e0c0ab
---
LYNX SHARE behält die Arbeitsweise, die du kennst: ein Channel Rack, Patterns, eine Playlist und einen Mixer, auf denselben Tasten. Es öffnet `.flp`-Projekte direkt und kann auch eines zurückschreiben.

LYNX SHARE ist ein eigenständiges Programm. Es ist weder mit Image-Line verbunden noch von Image-Line unterstützt oder gesponsert, und es enthält keinen Code von Image-Line. FL Studio ist eine eingetragene Marke von Image-Line Software.

## Ein .flp-Projekt öffnen

1. Wähle `Datei › Öffnen...` (`Ctrl+O`) und such die `.flp` aus.
2. LYNX SHARE löst die Plugins und Samples des Projekts auf und öffnet es dann. Der Titel zeigt „(imported from FL Studio)“.
3. Wähle `Hilfe › Last FL Studio import report`, um zu lesen, was angenähert, weggelassen oder nicht gefunden wurde.
4. Wähle `Hilfe › Fehlende Dateien...`, um die fehlenden Samples und Plugins zu sehen.
5. Wähle `Datei › Speichern`. Es fragt, wohin die `.lynx`-Datei geschrieben werden soll. Die `.flp` selbst wird nie geschrieben.

## Was wem entspricht

| In FL Studio | In LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Dieselben Namen, auf `F5`, `F6`, `F7` und `F9` |
| Plugin-Auswahl | `F8` |
| Browser | `Alt+F8` |
| Pattern-/Song-Modus | `L` |
| Rückgängig | `Ctrl+Z` macht rückgängig, oder wiederholt nach einem Rückgängig. `Ctrl+Alt+Z` macht rückwärts rückgängig |
| Neue Version speichern | `Ctrl+Alt+S` |
| Exportieren | `Ctrl+R`, MP3 auf `Ctrl+Shift+R`, MIDI auf `Ctrl+Shift+M` |
| Mixer-Inserts, Sends und Sidechain | Dasselbe, mit 10 Effekt-Slots pro Insert |
| Fruity Wrapper | Plugins von Drittanbietern werden direkt gehostet, siehe [Plugins von Drittanbietern](/wiki/plugins/) |
| Native Generatoren und Effekte | Laufen auf Engines, die LYNX SHARE nachgebaut hat, siehe unten |
| `.flp`-Projektdatei | `.lynx`-Projektdatei |

`Hilfe › Tastenkürzel` listet jede Taste auf.

## Was importiert wird

- Kanäle, Samples, Patterns, Slide- und Porta-Noten, jedes Arrangement, Marker, Taktartwechsel, Automation und Controller-Verknüpfungen.
- Der Mixer: Routing, Pegel, Sidechains, Spur-EQ und native Effekte mit ihren gespeicherten Einstellungen.
- VST3-, VST2- und CLAP-Plugins von Drittanbietern in Fruity Wrapper: ersetzt durch dasselbe Plugin, das auf deinem Computer installiert ist, mit seinem gespeicherten Zustand.

## Native Plugins von FL Studio

Die eigenen Generatoren und Effekte von FL Studio werden nicht geladen. LYNX SHARE spielt sie auf nachgebauten Engines ab, und jede hat eine Qualitätsstufe:

| Stufe | Bedeutung |
|---|---|
| Exact | Bitgenau identisch mit dem Original in jedem getesteten Zustand |
| Near-exact | Bitgenau identisch in den meisten Zuständen, der Rest weicht unterhalb von etwa -100 dB ab |
| Close | Derselbe Algorithmus, in fast allen getesteten Zuständen innerhalb von -60 dB des Originals |
| Approximate | Die gespeicherten Einstellungen werden auf einen eingebauten Synth oder Effekt von LYNX SHARE abgebildet |
| Placeholder | Der Zustand bleibt erhalten, und das Plugin ist stumm oder reicht das Audio durch |

Die Stufe jeder Engine und das, was ihr noch fehlt, steht in [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Siehe auch [Eingebaute Instrumente und Effekte](/wiki/built-in/).

## Wofür du deine eigene FL-Studio-Installation brauchst

Eine `.flp` zu öffnen funktioniert auch ohne installiertes FL Studio. Eine Installation auf demselben Computer bringt zwei Dinge dazu:

- Die Werks-Samples von FL Studio und seine Plugin-Datenbank, damit Samples und Plugins ohne Scan gefunden werden.
- Die eingebauten Wellenformen, Samples, Impulse und Tabellen, die manche Engines brauchen (FL Keys, Soundgoodizer, Maximus und die anderen, die in PLUGINS.md mit einem Kreuz markiert sind). Sie werden zur Laufzeit aus deiner Installation gelesen und nicht mit LYNX SHARE ausgeliefert.

Unter Linux sucht LYNX SHARE nach einem Windows-FL-Studio unter Wine. Unter macOS wird nach dem App-Bundle gesucht, das ist aber ungetestet.

## Zurück nach .flp exportieren

1. Wähle `Datei › Exportieren › FL Studio-Projekt...`.
2. Nimm `FL Studio-Projekt (.flp)` oder `Gezipptes Loop-Paket (.zip, mit allen Samples)`.
3. Wähle, was mit Dingen geschieht, die FL Studio nicht hat: in Audio rendern (klingt gleich) oder das nächstliegende FL-Plugin verwenden (bleibt editierbar, kann anders klingen, und Instrumente nur für LYNX werden weggelassen).

Die Datei ist ein FL-Studio-2026-Projekt. Sampler, Audioclips, die eigenen Generatoren und Effekte von FL, Plugins von Drittanbietern (in Fruity Wrapper), Patterns, die Playlist, der Mixer und die Automation gehen mit ihren Einstellungen hinüber. Ein Bericht listet, was exakt hinüberging und was gerendert, ersetzt oder weggelassen wurde.

## FL Studio Mobile (.flm)

`Datei › Öffnen...` nimmt auch `.flm`-Projekte. Der Import ist unvollständig.

- Importiert: Tempo, Taktart, ein Mixer-Insert pro FLM-Kanal, eine Playlist-Spur pro Spur, Patterns (jeder Loop-Durchlauf als Clip), Audioclips mit Versatz und Tonhöhe, Drum-Kits, DW-Sampler- und `.dwp`-Programme sowie der Slicer (auf den eigenen Engines von FLM), die Instrumente MiniSynth, Transistor Bass, 3x Osc, GMSynth und SuperSaw, die Effekte von FLM (als Pocket-Effekte), die Automation von Pegel, Panorama, Stummschaltung, Solo, Pitch Bend, Sends und Instrumentparametern sowie die Akkordsets und der Tonart-Locker.
- Nicht importiert: die anderen Instrumente von FLM, die den eingebauten Synth spielen.
- Samples werden neben der `.flm` gesucht und in der Kopie der FLM-Ressourcen von FL Studio. Eine `.dwp`, die nicht gefunden wird, spielt einen Ersatz.

## Gut zu wissen

- FLEX-Packs sind verschlüsselt und werden nie geöffnet. Ein FLEX-Kanal bekommt einen Synth-Patch, der nach seinem Preset-Namen gewählt wird.
- Modulatoren werden bei einem `.flp`-Export weggelassen, und der Exportbericht sagt das.
- Der Importbericht listet, was vom eingebauten Synth angenähert wurde, was keine Entsprechung hatte, welche Plugins gefunden wurden oder nicht installiert sind, welche Effekte weggelassen oder nur teilweise nachgebildet wurden und wie viele Samples fehlen.
- Ein Effekt von Drittanbietern, der nicht installiert ist, wird durch einen LYNX-SHARE-Effekt mit derselben Aufgabe ersetzt. Das Original bleibt im Projekt.

## Siehe auch

- [Eingebaute Instrumente und Effekte](/wiki/built-in/)
- [Plugins von Drittanbietern](/wiki/plugins/)
- [Dateiformate](/wiki/file-formats/)
- [Tastenkürzel](/wiki/shortcuts/)
- [Was nur LYNX SHARE mit FL-Einstellungen kann](/why/#fl-settings)
