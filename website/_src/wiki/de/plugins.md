---
source: be0fc22373
---
![Der untere Teil des Plugin-Managers mit dem Hinweis, dass jede Plugin-Datei in einem separaten Prozess geprüft wird](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE hostet Instrument- und Effekt-Plugins anderer Hersteller. Es findet sie, indem es die Plugin-Ordner durchsucht, und `Optionen › Plugin-Manager...` zeigt, was es gefunden hat. Die eigenen Instrumente und Effekte von LYNX SHARE sind unter [Eingebaute Instrumente und Effekte](/wiki/built-in/) beschrieben.

## Ein Plugin suchen und hinzufügen

1. Wähle `Optionen › Nach Plugins suchen`. Der Fortschritt erscheint in der Hinweisleiste, und du kannst weiterarbeiten.
2. Liegt das Plugin in einem eigenen Ordner, öffne `Optionen › Plugin-Manager...`, klicke auf `Plugin-Ordner...`, füge den Ordner hinzu und klicke dann auf `Neue Plugins finden`.
3. Drücke `F8` für die Plugin-Auswahl und tippe einen Teil des Plugin-Namens.
4. Doppelklicke es oder drücke `Enter`. Ein Instrument wird ein neuer Kanal. Ein Effekt kommt in den gewählten Mixer-Insert.

Beim ersten Start listet der Einrichtungsassistent die Plugin-Ordner auf und durchsucht sie im Hintergrund. `Optionen › Einrichtungsassistent (Sprache, Audio, Plugin-Ordner)...` öffnet ihn erneut. LYNX SHARE sucht außerdem einige Sekunden nach jedem Start nach neuen Plugins.

## Formate pro System

| System | Gehostete Formate |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2 wird unter macOS nicht gehostet. Unter Linux laufen Windows-Plugins über [yabridge](https://github.com/robbert-vdh/yabridge) und Wine, die du selbst installierst: Füge den Ordner, in den yabridge synchronisiert, als Plugin-Ordner hinzu.

## Der Plugin-Manager

| Regler | Was er bewirkt |
|---|---|
| Suchfeld | Filtert nach Name, Hersteller, Format oder Datei |
| `Alle`, `Instrumente`, `Effekte`, `Favoriten`, `Fehlerhaft` | Wählen, welche Plugins aufgelistet werden |
| Stern-Spalte | Markiert einen Favoriten |
| Spalte `Bits` | 32, 64 oder 32+64 (Windows) |
| Spalte `Status` | `OK`, `Datei fehlt` oder `Ladefehler` |
| `Neue Plugins finden` | Sucht nach Dateien, die noch nicht in der Liste stehen |
| `Alle neu suchen...` | Vergisst die fehlgeschlagenen Dateien und prüft jede Datei erneut |
| `Plugin-Ordner...` | Bearbeitet die Ordner, die durchsucht werden |
| `Plugin-Datei hinzufügen...` | Fügt eine einzelne Plugin-Datei hinzu |

Jede Plugin-Datei wird in einem separaten Prozess geprüft. Eine Datei, die abstürzt oder 30 Sekunden lang hängt, wird als fehlgeschlagen markiert und später übersprungen. Unter Windows stehen die fehlgeschlagenen Dateien mit dem Grund in `%APPDATA%\LYNX\scan-failures.log`.

Die Standardordner werden durchsucht, dazu die, die du hinzufügst:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` und `Steinberg\VSTPlugins` unter Program Files sowie die Ordner, die FL Studio nutzt, wenn es installiert ist.
- macOS: `VST3`, `Components` und `CLAP` in `~/Library/Audio/Plug-Ins` und `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` und dieselben Ordner unter `/usr/lib` und `/usr/local/lib`.

## Angedockte und abgelöste Plugin-Fenster

Der Editor eines Plugins öffnet sich wie die anderen Panels im Hauptfenster. `Abgelöst` im Menü des Plugin-Fensters gibt dem Plugin ein eigenes Fenster, und `Im Vordergrund anheften` im selben Menü hält ein abgelöstes Fenster über den anderen Fenstern von LYNX SHARE.

`Alle neuen Plugins ablösen` unter `Optionen › Allgemeine Einstellungen...` öffnet jedes neu hinzugefügte Plugin abgelöst. `Tools › Alle Plugin-Editoren umschalten auf` setzt `Angedockt` oder `Abgelöst` für alle Plugins des Projekts.

## Plugins in einem separaten Prozess ausführen

Standardmäßig läuft ein Plugin innerhalb von LYNX SHARE, ein abstürzendes Plugin kann also das Programm mitreißen. Als Option läuft jedes Plugin in einem eigenen Prozess.

1. Öffne `Optionen › Allgemeine Einstellungen...`.
2. Schalte `Plugins in einem separaten Prozess ausführen (schützt LYNX SHARE vor Abstürzen)` ein.
3. Öffne das Projekt erneut, damit es auch für bereits geöffnete Plugins gilt.

Stürzt ein solches Plugin ab oder antwortet nicht mehr, wird es stummgeschaltet, eine Meldung erscheint, und LYNX SHARE läuft weiter. `Neu laden` in dieser Meldung startet das Plugin mit den Einstellungen des letzten Speicherstands oder Backups neu, ohne das Projekt erneut zu öffnen. Der Editor des Plugins öffnet sich in einem eigenen Fenster und lässt sich weder andocken noch anheften.

Um das nur für ein einzelnes Plugin zu tun, nimm `In separatem Prozess ausführen` oder `Always run <plugin> in a separate process` im Wrapper-Menü des Plugin-Fensters. Stürzt der Editor eines Plugins ab, bietet die Meldung, die dann erscheint, auch das zweite an.

Das funktioniert unter Windows und Linux, und seit 0.1.25 auch unter macOS.

Ein Plugin, das in LYNX SHARE abstürzt, wird abgeschaltet, und auch seine Meldung bietet `Neu laden` an. Später nutzt du `Abgestürztes Plugin neu laden` im Menü des Kanals oder im Menü des Effekt-Slots.

## 32-Bit-Plugins (nur Windows)

Unter Windows laufen 32-Bit-VST2- und -VST3-Plugins über eine Bridge (`LYNX-bridge32.exe`). Die Bridge fügt keine Latenz hinzu. Ein Plugin, das darin abstürzt, hält von selbst an, während LYNX SHARE weiterspielt. Der Editor eines gebridgten Plugins öffnet sich in einem eigenen Fenster und lässt sich weder andocken noch anheften. Sind von einem Plugin sowohl ein 32-Bit- als auch ein 64-Bit-Build installiert, wählt `Bitbreite` unter `Wrapper-Einstellungen` im Plugin-Fenster, welcher geladen wird.

## Plugin-Leistungsmonitor

![Der Plugin-Leistungsmonitor: eine Zeile pro Instrument und Effekt mit ihrem Anteil an der Verarbeitungszeit](/assets/img/wiki/plugin-monitor.webp)

`Tools › Plugin-Leistungsmonitor...` listet jedes Instrument und jeden Effekt mit der Verarbeitungszeit auf, die sie brauchen: aktuell, im Durchschnitt und in der Spitze. 100 % bedeutet, dass ein Prozessorkern die ganze Dauer des Audios über ausgelastet ist.

| Einstellung | Was sie bewirkt |
|---|---|
| `Suchen` | Filtert nach Plugin, Kanal oder Insert |
| `Nur Plugins` | Blendet die eigenen Instrumente und Effekte von LYNX SHARE aus |
| `Einfrieren` | Hält die Aktualisierung der Zahlen an |
| `Bei Wiedergabe zurücksetzen` | Beginnt Durchschnitte und Spitzen bei jedem Wiedergabestart neu |
| `Reset` | Löscht Durchschnitte und Spitzen sofort |

## Die Ausgänge eines Instruments an getrennte Inserts senden

Ein Instrument-Plugin mit mehreren Ausgängen kann jeden Ausgang auf einem eigenen Mixer-Insert spielen. Klicke im Fenster des Plugins auf `Wrapper-Einstellungen` und dann auf den Tab `Verarbeitung`. Unter `Verbindungen` hat jeder Ausgang ein Licht, das ihn ein- oder ausschaltet, und eine Zahl: auf dem wievielten Insert nach dem eigenen Insert des Kanals er spielt. `Ausgänge automatisch zuordnen` gibt jedem Ausgang das nächste Insert, und `Reset` stellt die Standardwerte wieder her.

## Der Ordner Plugin database

Die Ordner der Plugin database im [Browser](/wiki/browser/), der Plugin-Auswahl und der Plugin-Menüs sind ein Ordner auf der Festplatte: `LYNX/Plugin database` in deinem Ordner Dokumente. Um ihn zu öffnen, klicke im Browser mit der rechten Maustaste auf `Plugin-Datenbank` und wähle `Öffnen`.

- `Effekte` und `Generatoren` enthalten pro Plugin eine `.lynxplugin`-Datei. Lege Ordner und Unterordner an (bis zu 4 Ebenen) und verschiebe die Dateien dorthin.
- Benenne eine Datei um, um das Plugin in den Menüs umzubenennen. Lösche eine Datei, um das Plugin aus den Menüs zu nehmen. Es wird dadurch nicht deinstalliert.
- `Installiert` hat einen Eintrag für jedes Plugin. Kopiere einen davon nach `Effekte` oder `Generatoren`, um ein Plugin zurückzuholen.

## Ein Projekt ohne seine Plugins öffnen

`Datei › Ohne Plugins von Drittanbietern öffnen...` öffnet ein LYNX-SHARE-Projekt mit abgeschalteten Fremd-Plugins, für ein Projekt, das sich nicht öffnen lässt oder beim Laden abstürzt. Die gespeicherten Einstellungen der Plugins bleiben erhalten. `Datei › Plugins wieder einschalten...` schaltet sie wieder ein.

## Wenn ein Plugin nicht installiert ist

LYNX SHARE sucht dasselbe Plugin zuerst anhand seiner Kennung, dann anhand der Datei, dann anhand des Namens. Ein Projekt, das mit dem VST2 gespeichert wurde, öffnet sich mit dem VST3 desselben Produkts und umgekehrt, dann spielt das Plugin aber mit seinen Standardeinstellungen.

Ist ein Effekt gar nicht installiert, übernimmt ein LYNX-SHARE-Effekt mit derselben Aufgabe seinen Slot, und das Original bleibt im Projekt. Lassen sich die gespeicherten Einstellungen des Plugins lesen, startet der Ersatz mit passenden Einstellungen. Der Schalter ist `Fehlende Plugins durch LYNX-SHARE-Entsprechungen ersetzen` unter `Optionen › Allgemeine Einstellungen...`. Siehe [Eingebaute Instrumente und Effekte](/wiki/built-in/). `Hilfe › Fehlende Dateien...` listet die Plugins auf, die ersetzt wurden.

## Gut zu wissen

- Das Fenster eines gehosteten Plugins hat eine Preset-Leiste: die eigenen Programme des Plugins, deine Presets, `Preset speichern...`, `Preset-Datei laden...` und `Preset-Datei exportieren...`. `.fxp`-, `.fxb`- und `.vstpreset`-Dateien lassen sich nur in ein Plugin laden, das innerhalb von LYNX SHARE läuft, nicht in einem separaten Prozess.
- Der Monitor misst nur, solange sein Fenster offen ist.
- Solange das Fenster eines Plugins den Fokus hat, spielt eine Taste, die das Plugin nicht nutzt, das Tastatur-Klavier. Ist unter Windows der Tastatur-Button in der Kopfzeile des Fensters aus, verarbeitet LYNX SHARE Tasten vor dem Plugin, auch Tastenkürzel.
- Das eigene Parametermenü eines VST3-Plugins beginnt mit Einträgen von LYNX SHARE, etwa `Automationsclip erstellen`. Siehe [Automation](/wiki/automation/).
- `MPE` unter `MIDI` in den `Wrapper-Einstellungen` eines Plugins sendet jede Note auf einem eigenen MIDI-Kanal. Siehe [MIDI-Controller](/wiki/midi-controllers/).
- Fehlende Instrumente werden noch nicht ersetzt. Ein fehlendes Instrument-Plugin wird zu einem eingebauten Synth, der nach dem Kanalnamen gewählt wird.

## Verwandt

- [Eingebaute Instrumente und Effekte](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Fehlerbehebung](/wiki/troubleshooting/)
