---
source: 358890815a
---
`Datei › Öffnen...` (`Ctrl+O`) öffnet die eigenen `.lynx`-Projekte von LYNX SHARE und außerdem `.flp`-, `.als`-, `.flm`- und `.mid`-Dateien. Ein Projekt aus einem anderen Programm ist ein Import: Es wird zu einem neuen, ungespeicherten LYNX-SHARE-Projekt, und die Originaldatei wird nie beschrieben.

## Ein Projekt aus einem anderen Programm öffnen

1. Wähle `Datei › Öffnen...` und such die `.flp`-, `.als`-, `.flm`- oder `.mid`-Datei aus.
2. Warte, während LYNX SHARE nach den Plugins und Samples des Projekts sucht.
3. Wurde etwas nicht gefunden, öffnet sich das Fenster `Fehlende Dateien`. Verweise es auf deine Samples oder schließe es.
4. Die Titelleiste zeigt nun, woher das Projekt stammt, zum Beispiel „(imported from FL Studio)“.
5. Wähle `Datei › Speichern`. LYNX SHARE fragt, wohin eine `.lynx`-Datei geschrieben werden soll.

## Was mitkommt

| Format | Umgewandelt | Nicht umgewandelt |
|---|---|---|
| `.flp` (FL Studio) | Kanäle, Samples, Patterns, Slide- und Porta-Noten, jedes Arrangement, Marker, Taktartwechsel, Automation, Controller-Verknüpfungen, der Mixer mit Routing, Sidechains, Spur-EQ und native Effekte | FLEX-Packs sind verschlüsselt und werden nie geöffnet. Ein FLEX-Kanal bekommt einen Synth-Patch, der nach seinem Preset-Namen gewählt wird |
| `.als` (Live 11 und 12) | MIDI-, Audio-, Gruppen- und Return-Spuren, Tempo, Taktart, Locators, der Loop, Lautstärke, Pan, Mute, Solo und Sends mit ihrer Automation, Session-Clips und Szenen, Warp-Marker, Racks und Drum Racks sowie viele der eigenen Devices von Live (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift und mehr) als die von LYNX SHARE | Devices von Live ohne Entsprechung in LYNX SHARE (Hybrid Reverb, Grain Delay, Amp, Roar, FM-Operator-Patches und andere) entfallen und werden aufgelistet. MIDI-Effect-Racks werden nicht umgewandelt |
| `.flm` (FL Studio Mobile) | Tempo, Taktart, ein Mixer-Insert pro Kanal, eine Playlist-Spur pro Spur, Patterns, Audioclips mit Offset und Tonhöhe, Drum-Kits, DW Sampler und `.dwp`-Programme, der Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth und SuperSaw, die Effekte, die Automation und die Akkordsets | Andere FLM-Generatoren spielen den eingebauten Synth |
| `.mid` | Noten, Taktarten und jeder Tempowechsel | Eine MIDI-Datei enthält keine Klänge: Kanäle bekommen den LYNX-SHARE-Synth oder einen MIDI-Out-Kanal |

Plugins von Drittanbietern (VST2, VST3 und CLAP in einer `.flp`, VST2 und VST3 in einer `.als`) laden mit ihrem gespeicherten Zustand, wenn dasselbe Plugin auf deinem Computer installiert ist.

## Die eigenen Plugins von FL Studio

Die Generatoren und Effekte von FL Studio werden nicht geladen. LYNX SHARE liest ihre gespeicherten Einstellungen und spielt sie auf nachgebauten Engines ab. Jede Engine hat eine Qualitätsstufe, von `Exact` (bitidentisch mit dem Original bei jedem getesteten Zustand) bis `Placeholder` (die Einstellungen bleiben erhalten, aber das Plugin ist stumm oder reicht Audio durch). Die Stufen sind unter [Umstieg von FL Studio](/wiki/coming-from-fl-studio/) erklärt.

Eine `.flp` zu öffnen funktioniert auch ohne installiertes FL Studio. Einige Engines brauchen Waves, Samples oder Impulse, die zur Laufzeit aus deiner eigenen FL-Studio-Installation gelesen werden.

## Fehlende Samples

![Das Fenster Fehlende Dateien im Reiter Samples: gefundene Dateien grün, fehlende bernsteinfarben, mit Search folder, Locate und Skip](/assets/img/wiki/missing-files.webp)

Der Reiter `Samples` listet jedes Sample auf, das nicht gefunden wurde, mit seinem gespeicherten Pfad und dem, was es verwendet.

| Befehl | Was er bewirkt |
|---|---|
| `Ordner durchsuchen...` | Durchsucht einen Ordner und seine Unterordner nach allen fehlenden Samples |
| `Suchen...` | Fragt, wo ein Sample liegt. Weitere im selben Ordner gefundene werden gleich mit übernommen |
| `Überspringen` | Lässt das Sample weg. Sein Kanal bleibt stumm |

`Anwenden` verwendet die Dateien, die gefunden wurden.

Der Reiter `Automatisch gefunden` listet Dateien auf, die LYNX SHARE selbst in einem anderen Ordner gefunden hat. Speichere das Projekt, um die neuen Orte zu behalten.

## Fehlende Plugins

LYNX SHARE sucht jedes Plugin eines Drittanbieters zuerst anhand seiner Kennung, dann anhand seiner Datei, dann anhand seines Namens. Ein Projekt, das mit dem VST2 gespeichert wurde, öffnet sich mit dem VST3 desselben Produkts und umgekehrt, dann aber mit Standardeinstellungen.

Ist ein Effekt nicht installiert, übernimmt ein LYNX-SHARE-Effekt mit derselben Aufgabe den Slot: ein Hall für einen Hall, ein Kompressor für einen Kompressor.

- Der Slot zeigt den Namen des Original-Plugins mit einem LYNX-SHARE-Abzeichen.
- Wo sich der gespeicherte Zustand lesen lässt, werden die Einstellungen übersetzt. Sonst startet der Ersatz mit einem Standard-Preset. Die Tabelle der Produkte, ihrer Ersatzeffekte und dessen, was jeweils übersetzt wird, steht in [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- Die Automation der Parameter des Originals bleibt erhalten, steuert den Ersatz aber nicht.
- Die Referenz und der Zustand des Original-Plugins bleiben im Projekt. Sobald das Plugin installiert ist, öffnet sich das Projekt wieder damit.
- `Alle LYNX-SHARE-Ersatz-Plugins dieses Projekts umgehen` im Rechtsklickmenü eines Ersatz-Slots schaltet sie alle auf einmal ab.

Der Schalter heißt `Fehlende Plugins durch LYNX-SHARE-Entsprechungen ersetzen` unter `Optionen › Allgemeine Einstellungen...` und ist standardmäßig an. Ist er aus, wird der Slot eines fehlenden Effekts im Mixer weggelassen. Ein fehlendes Instrument wird nicht ersetzt. Es wird zu einem eingebauten Synth mit einem Preset, das nach dem Kanalnamen gewählt wird.

Um das echte Plugin zurückzubekommen, installiere es, wähle `Optionen › Nach Plugins suchen` und öffne das Projekt dann erneut.

## MIDI-Dateien

![Das Fenster Import MIDI data: Spuren, Kanäle, Kanaltyp und Optionen](/assets/img/wiki/midi-import-dialog.webp)

`Datei › Öffnen...` macht aus einer MIDI-Datei ein neues Projekt. `Datei › Importieren › MIDI-Datei...` öffnet zuerst das Fenster `MIDI-Daten importieren`, in dem du wählst, was hereinkommt.

| Einstellung | Was sie bewirkt |
|---|---|
| `Welche Spuren importieren?` | Alle Spuren oder eine |
| `Welche Kanäle importieren?` | Klicke auf einen MIDI-Kanal, um ihn wegzulassen. Rechtsklick behält nur diesen |
| `Kanaltyp` | `LYNX-SHARE-Synth` oder `MIDI Out` |
| `Neues Projekt beginnen` | Beginnt ein leeres Projekt. Aus: fügt zum geöffneten hinzu |
| `Einen Kanal pro Spur erstellen` | Aus: ein Kanal pro MIDI-Kanal |
| `Mixer-Inserts für neue Kanäle festlegen` | Routet die neuen Kanäle auf aufeinanderfolgende Mixer-Inserts |
| `Taktarten importieren` | Die Taktart der Datei wird die des Projekts |

`Ctrl+M` im [Piano Roll](/wiki/piano-roll/) importiert eine MIDI-Datei in den aktuellen Kanal.

## Gut zu wissen

- `Hilfe › Fehlende Dateien...` öffnet das Fenster später erneut. `Hilfe › Last FL Studio import report` listet auf, was angenähert, ausgelassen oder nicht installiert war.
- Session-Clips eines Live-Sets kommen ins Clip-Raster des Clip-Start-Layouts. `Session-Clips aus Live-Sets` unter `Optionen › Allgemeine Einstellungen...` legt sie stattdessen auf die Playlist.

## Verwandt

- [Dateiformate](/wiki/file-formats/)
- [Umstieg von FL Studio](/wiki/coming-from-fl-studio/)
- [Umstieg von Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Export](/wiki/exporting/)
