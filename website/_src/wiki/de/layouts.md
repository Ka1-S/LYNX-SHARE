---
source: e15b0e3ba5
---
![Der Clip-Start-Arbeitsablauf: links ein Raster aus Clips und Szenen, rechts die Auswahl und die Playlist](/assets/img/cliplaunch.webp)

`Ansicht › Layout-Stil` hat drei Einträge: `Pattern-Arbeitsablauf (Standard)`, `Clip-Start-Arbeitsablauf` und `Einfacher Arbeitsablauf`. Dieselbe Wahl gibt es unter `Optionen › Allgemeine Einstellungen`, im Bereich Arbeitsablauf. Die Wahl bleibt erhalten, wenn du das Programm schließt.

Der Pattern-Arbeitsablauf besteht aus Channel Rack, Patterns und Playlist. Der Clip-Start-Arbeitsablauf ergänzt neben der Playlist ein Clip-Raster mit Szenen. Patterns und das Channel Rack bleiben verfügbar.

## Clips im Raster starten

1. Wähle `Ansicht › Layout-Stil › Clip-Start-Arbeitsablauf`. Das Clip-Raster erscheint neben der Playlist.
2. Klicke mit rechts auf einen leeren Slot, wähle `Pattern hier einfügen` und such ein Pattern aus. Ein Doppelklick auf einen leeren Slot legt das aktuelle Pattern dorthin.
3. Klicke auf das Dreieck links vom Clip, um ihn zu starten. Steht der Transport still, startet er.
4. Klicke in der Spalte `Szenen` auf das Dreieck einer Szene, um jeden Clip in dieser Zeile zu starten.
5. Klicke auf `Alle Clips stoppen`, um sie anzuhalten.
6. Klicke auf `Zurück zur Playlist`, damit die Spuren wieder ihre Playlist-Clips spielen.

## Was sich auf dem Bildschirm ändert

- Das Clip-Raster wird neben der Playlist angezeigt. Playlist-Spuren sind die Spalten, Szenen die Zeilen.
- Die Titelleiste der Playlist bekommt einen Schalter `Beide`, `Raster`, `Song`. `Raster` zeigt nur das Raster, `Song` nur die Playlist.
- Beim ersten Mal ist das Channel Rack ausgeblendet. `F6` oder sein Button in der Werkzeugleiste zeigt es.
- Die Automationsspuren der Playlist sind eingeschaltet.
- Das Fensterlayout wird pro Stil gespeichert, sodass jeder Stil so zurückkommt, wie du ihn verlassen hast.
- Unter dem Raster zeigt oder verbirgt die Schaltfläche `Mixer` den Session-Mixer, wie in Ableton Live: `Ein/Aus` (`Audio von`, `Monitor`, `Audio an`), `Sends`, `Spurverzögerung` und pro Spur die Lautstärke mit ihrem Aktivator, Solo, Aufnahme-Taste und Meter, dazu den Streifen `Haupt`.
- `Grooves` unter dem Raster zeigt den Groove Pool. `Groove auf Noten anwenden` schreibt einen Groove in die Noten eines MIDI-Clips.
- Die Schaltflächen `MIDI` und `Tonart` starten den MIDI Map Mode und den Key Map Mode. Siehe [MIDI-Controller](/wiki/midi-controllers/).

## Das Raster

Ein gestarteter Clip beginnt nicht sofort. Er wartet auf den nächsten Punkt der Start-Quantisierung, und sein Dreieck blinkt bis dahin. Das Feld unten im Raster legt die globale Start-Quantisierung fest, die jeder Clip mit `Global` nutzt. Ein Stopp wartet auf dieselbe Weise.

Klicke mit rechts auf einen Clip für seine Einstellungen.

| Einstellung | Was sie tut |
|---|---|
| `Start-Quantisierung` | Wann dieser Clip startet: `Global`, `Keine`, `8 Takte` bis hinunter zu `1/32` |
| `Startmodus` | `Trigger`, `Gate`, `Umschalten` oder `Wiederholen` |
| `Legato (Position beibehalten)` | Der Clip übernimmt die Abspielposition des Clips, der vorher auf dieser Spur lief |
| `Loop` | An: Der Clip wiederholt sich. Aus: Er spielt einmal, dann hält die Spur an |
| `Folgeaktion` | Was nach dem Clip geschieht: zwei Aktionen mit je einer Wahrscheinlichkeit und die Zeit, nach der sie greifen |
| `Delete` | Leert den Slot |

Klicke mit rechts auf einen leeren Slot für `Stopp-Taste (eine Szene stoppt diese Spur hier)`. Ein Klick auf das Quadrat links von einem leeren Slot hält diese Spur an. Klicke mit rechts auf eine Szene für `Szene einfügen`, `Szene darunter hinzufügen`, `Szene löschen`, `Szene umbenennen...` und das Tempo der Szene.

Clips lassen sich über ihr Menü ausschneiden, kopieren, einfügen und duplizieren (`Ctrl+D`) und zwischen Slots sowie zur oder von der Playlist ziehen. Ein Pattern oder ein Sample, das du aus dem Browser ziehst, oder Audiodateien, die du von außen ablegst, füllen den Slot, auf dem sie landen. Das Menü einer Szene hat außerdem `Duplizieren` und `Szene aufnehmen und einfügen`, das die gerade spielenden Clips in eine neue Szene legt. `Ansicht › Tempo und Taktart der Szenen` zeigt für jede Szene ein Tempo und eine Taktart.

Das Slot-Menü hat außerdem zwei Optionen für das ganze Raster: `Spuren beim Stoppen des Transports an die Playlist zurückgeben` und `Folgeaktionen im gesamten Raster`.

| Befehl | Taste | Was er tut |
|---|---|---|
| Bewegen | Pfeiltasten | Wählt einen anderen Slot |
| Clip starten | `Enter` | Startet den gewählten Clip |
| Szene starten | `Ctrl+Enter` | Startet die gewählte Szene |
| Clip löschen | `Del` | Leert den gewählten Slot |
| Zeichenmodus | `B` | Ein Ziehen in einer Automationsspur zeichnet Stufen in Rasterbreite oder freihändig bei ausgeschaltetem Einrasten oder gehaltener `Alt`-Taste, wie in Ableton Live. Auch unter `Optionen` |

## Ableton-Live-Sets

Öffnest du ein Live-Set, kommen dessen Session-Clips und Szenen mit ihren Start-Einstellungen ins Clip-Raster. `Datei › Exportieren › Ableton-Live-Set (.als)` schreibt das Raster als Session-Clips zurück. Unter `Optionen › Allgemeine Einstellungen` gibt es `Session-Clips aus Live-Sets`, wo `Playlist, nach dem Arrangement` sie stattdessen in die Playlist legt.

## Der Einfache Arbeitsablauf

`Ansicht › Layout-Stil › Einfacher Arbeitsablauf` ist ein Fenster mit den Spuren und der Zeitleiste, für einen ersten Song. `Ansicht › Spur-Dock` zeigt unter der Zeitleiste ein Dock für die gewählte Spur, mit den Tabs `Effekte`, `Instrument`, `Tastatur` und `Editor`. In diesem Arbeitsablauf hat das Menü `Hilfe` die Einträge `Tour zum ersten Song` und `Willkommensbildschirm...`, die einen ersten Song in sieben Schritten, einen Demo-Song oder ein leeres Projekt anbieten. Siehe [Einfaches Layout](/wiki/simple-layout/).

`Text und Notizen` und `Referenzabgleich...` im Menü `Ansicht` sowie `Klare Stimme` im Menü eines Mixer-Inserts gibt es in jedem Layout. Im Einfachen Arbeitsablauf ist `Klare Stimme` auch im Menü einer Spur zu finden.

## Zurückwechseln

Wähle `Ansicht › Layout-Stil › Pattern-Arbeitsablauf (Standard)`. Das Fensterlayout, das du im Pattern-Arbeitsablauf hattest, kommt zurück, und die Automationsspuren gehen auf ihre frühere Einstellung zurück. Die Clips im Raster bleiben im Projekt.

## Gut zu wissen

- Eine Spur, die einen Clip gestartet hat, bleibt in der Playlist stumm, bis du `Zurück zur Playlist` drückst. Die Option `Spuren beim Stoppen des Transports an die Playlist zurückgeben` erledigt das bei jedem Stopp für dich.
- Die neuen Tasten des Rasters (`B`, `Q`, `Ctrl+M`, `Ctrl+K` und andere) wirken nur im Clip-Start-Arbeitsablauf so. Im Pattern-Arbeitsablauf behalten sie die Bedeutung von FL Studio.
- Eine Szene in 6/8 spielt einen Takt aus drei Schlägen, weil der Takt Viertelnoten zählt.
- Follow Actions gibt es für Clips, nicht für Szenen.
- Automationsspuren, die du im Clip-Start-Arbeitsablauf von Hand eingeschaltet hast, bleiben beim Zurückwechseln nicht erhalten.
- Ist der Pattern-Arbeitsablauf gewählt, wird vom Raster nichts geladen.

## Siehe auch

- [Playlist](/wiki/playlist/)
- [Umstieg von Ableton Live](/wiki/coming-from-ableton/)
- [Projekte importieren](/wiki/importing-projects/)
- [Was nur LYNX kann: Workflows](/why/#workflows)

Der dritte Eintrag ist ein Fenster für einen ersten Song. Siehe [Einfaches Layout](/wiki/simple-layout/).
