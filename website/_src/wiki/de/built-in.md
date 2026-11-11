---
source: 7ca5e689db
---
![Die Schnellauswahl: Instrumente links, Effekte rechts, jeweils als eingebaut oder als eingebaute FL-Engine gekennzeichnet](/assets/img/wiki/quickadd.webp)

LYNX SHARE bringt eigene Instrumente und Effekte mit, damit ein Projekt ohne Plugins von Drittanbietern abspielt. Es gibt drei Arten: Originale von LYNX SHARE (ein Synth, ein Sampler, ein SoundFont-Player, Drums, EQs, Kompressoren, Limiter, Hallgeräte, Delays), Engines, die die Einstellungen der nativen Plugins von FL Studio abspielen, und Effekte, die für bekannte Plugins von Drittanbietern einspringen.

Jedes davon hat im [Katalog](/instruments-and-effects/) eine eigene Seite. `Hilfe › Integrierte Plugins (Online-Liste)...` öffnet die Liste mit dem Status jeder Engine.

## Ein Instrument und einen Effekt hinzufügen

1. Drücke `F6`, um das Channel Rack anzuzeigen.
2. Klicke auf das Feld `+ Kanal hinzufügen` unter der letzten Zeile und wähle ein Instrument. Das Menü gruppiert sie nach Kategorie.
3. Drücke `F9`, um den Mixer anzuzeigen, und wähle das Insert, über das der Kanal spielt.
4. Klicke im Effektbereich auf einen leeren Slot und wähle einen Effekt. Sein Fenster öffnet sich.
5. Klicke auf den Namen des Slots, um dieses Fenster wieder auszublenden oder einzublenden.

## Die Plugin-Auswahl

Drücke `F8` für die Plugin-Auswahl. Sie füllt den Bildschirm und zeigt jeden Generator und jeden Effekt als Bild seines eigenen Editors, mit den Kategorien als Reiter am unteren Rand.

| Befehl | Taste | Was sie bewirkt |
|---|---|---|
| Suche | Tippen | Blendet die Plugins ab, die nicht passen |
| Durch die Plugins gehen | `Up` / `Down` | Bewegt die Markierung |
| Hinzufügen | `Enter`, Doppelklick oder Rechtsklick | Ein Generator wird ein neuer Kanal. Ein Effekt kommt in den gewählten Mixer-Insert |
| Ein Bild ziehen | | Auf das Channel Rack für einen neuen Kanal, auf einen Kanal, um dessen Instrument zu ersetzen, oder auf ein Mixer-Insert oder -Slot |
| Schließen | `Esc` oder `F8` | Schließt die Auswahl, ohne etwas hinzuzufügen |

Die Bilder der eigenen Plugins von LYNX SHARE zeichnet LYNX SHARE im Hintergrund. Ein Plugin von Drittanbietern zeigt eine schlichte Kachel mit dem Namen seines Herstellers, bis sein Editor einmal geöffnet wurde.

`Bearbeiten › Schnellauswahl...` öffnet stattdessen eine schlichte Liste: Instrumente links, Effekte rechts, gefiltert beim Tippen. `Tab` wechselt die Listen und `Enter` fügt den markierten Eintrag hinzu. Die Plugin-Datenbank des [Browsers](/wiki/browser/) (`Shift+F8`) listet dieselben Plugins als Baum.

![Die Plugin-Datenbank des Browsers mit geöffnetem Ordner der FL-Engines](/assets/img/wiki/browser-plugins.webp)

## Presets

Das Fenster eines eingebauten Effekts hat in der Kopfzeile eine Preset-Leiste.

| Regler | Was er bewirkt |
|---|---|
| Pfeiltasten | Vorheriges und nächstes Preset |
| Preset-Name | Öffnet das Menü: `LYNX-SHARE-Presets`, `Eigene Presets`, `FL-Studio-Presets`, wo es sie gibt, `Preset speichern...` und `Preset-Ordner anzeigen` |
| `A` / `B` | Zwei Einstellungen zum Vergleichen. Die Taste daneben kopiert die, die du hörst, auf die andere |

Der Preset-Name und die Einstellungen A und B werden mit dem Projekt gespeichert.

## Was „öffnet die Einstellungen von FL“ heißt

Die nativen Plugins von FL Studio werden nie geladen. LYNX SHARE hat für jedes eine eigene Engine, und diese Engine liest die Einstellungen, die das FL-Plugin gespeichert hat. Das funktioniert an zwei Stellen:

- Ein `.flp`-Projekt. Jeder FL-Generator und jeder FL-Effekt öffnet sich auf seiner LYNX-SHARE-Engine mit den gespeicherten Einstellungen, und seine Regler bleiben bearbeitbar und automatisierbar.
- Ein `.fst`-Preset. Doppelklicke im Browser ein Synth-Preset, um einen neuen Kanal zu erhalten, oder ziehe ein Effekt-Preset auf ein Mixer-Insert. Klicke mit rechts auf einen belegten Effekt-Slot für `FL-Presets`; das listet die Presets, die in deiner FL-Studio-Installation gefunden wurden.

Wie nah jede Engine am Original ist, steht pro Engine, von Exact bis Placeholder. Die Stufen sind unter [Umstieg von FL Studio](/wiki/coming-from-fl-studio/) erklärt.

## Engines, die deine FL-Studio-Installation brauchen

LYNX SHARE enthält keinen Code, kein Audio und keine Wellendaten von Image-Line. Einige Engines brauchen die Waves, Samples, Impulse oder Tabellen, die zum Original-Plugin gehören, und lesen sie zur Laufzeit aus deiner eigenen FL-Studio-Installation.

| Typ | Engines |
|---|---|
| Instrumente | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Effekte | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Werden die Dateien nicht gefunden, kann das Hinzufügen einer solchen Engine mit der Meldung scheitern, dass die Datendateien einer FL-Studio-Installation nicht gefunden oder nicht gelesen werden konnten.

## Platzhalter für Plugins von Drittanbietern

Verwendet ein Projekt einen Effekt eines Drittanbieters, der nicht installiert ist, übernimmt ein LYNX-SHARE-Effekt mit derselben Aufgabe seinen Slot: ein Hall für einen Hall, ein Delay für ein Delay.

- Der Slot zeigt den Namen des Original-Plugins mit einem LYNX-SHARE-Abzeichen.
- Wo sich der gespeicherte Zustand lesen lässt, werden seine Einstellungen übersetzt. Sonst startet der Platzhalter mit einem Standard-Preset.
- Das Original-Plugin und seine Einstellungen bleiben im Projekt. Sobald das Plugin installiert ist, öffnet sich das Projekt wieder damit.
- Klicke mit rechts auf einen Platzhalter-Slot und wähle `Alle LYNX-SHARE-Ersatz-Plugins dieses Projekts umgehen`, um das Projekt ohne sie zu hören.

Der Schalter heißt `Fehlende Plugins durch LYNX-SHARE-Entsprechungen ersetzen` unter `Optionen › Allgemeine Einstellungen...` und ist standardmäßig an. In einem neuen Projekt stehen dieselben Effekte im Menü des leeren Slots unter `LYNX-SHARE-Entsprechungen`.

## Gut zu wissen

- Fehlende Instrumente werden noch nicht ersetzt. Ein fehlendes Instrument-Plugin wird zu einem eingebauten Synth, der nach dem Kanalnamen gewählt wird.
- Die Automation der Parameter des Original-Plugins bleibt erhalten, steuert den Platzhalter aber nicht.
- FLEX-Packs sind verschlüsselt und werden nie geöffnet. Ein FLEX-Kanal bekommt einen Synth-Patch, der nach seinem Preset-Namen gewählt wird.
- Ein Insert hat 10 Effekt-Slots.

## Verwandt

- [Katalog der Instrumente und Effekte](/instruments-and-effects/)
- [Plugins von Drittanbietern](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Umstieg von FL Studio](/wiki/coming-from-fl-studio/)
