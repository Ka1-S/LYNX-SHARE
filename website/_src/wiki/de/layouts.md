---
source: b4f9b5e040
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

Das Slot-Menü hat außerdem zwei Optionen für das ganze Raster: `Spuren beim Stoppen des Transports an die Playlist zurückgeben` und `Folgeaktionen im gesamten Raster`.

| Befehl | Taste | Was er tut |
|---|---|---|
| Bewegen | Pfeiltasten | Wählt einen anderen Slot |
| Clip starten | `Enter` | Startet den gewählten Clip |
| Szene starten | `Ctrl+Enter` | Startet die gewählte Szene |
| Clip löschen | `Del` | Leert den gewählten Slot |

## Ableton-Live-Sets

Öffnest du ein Live-Set, kommen dessen Session-Clips und Szenen mit ihren Start-Einstellungen ins Clip-Raster. `Datei › Exportieren › Ableton-Live-Set (.als)` schreibt das Raster als Session-Clips zurück. Unter `Optionen › Allgemeine Einstellungen` gibt es `Session-Clips aus Live-Sets`, wo `Playlist, nach dem Arrangement` sie stattdessen in die Playlist legt.

## Zurückwechseln

Wähle `Ansicht › Layout-Stil › Pattern-Arbeitsablauf (Standard)`. Das Fensterlayout, das du im Pattern-Arbeitsablauf hattest, kommt zurück, und die Automationsspuren gehen auf ihre frühere Einstellung zurück. Die Clips im Raster bleiben im Projekt.

## Gut zu wissen

- Eine Spur, die einen Clip gestartet hat, bleibt in der Playlist stumm, bis du `Zurück zur Playlist` drückst. Die Option `Spuren beim Stoppen des Transports an die Playlist zurückgeben` erledigt das bei jedem Stopp für dich.
- In 0.1.25 wird ein Slot über sein Menü oder per Doppelklick gefüllt. Einen Clip oder eine Datei in einen Slot zu ziehen geht nicht.
- Follow Actions gibt es für Clips, nicht für Szenen.
- Automationsspuren, die du im Clip-Start-Arbeitsablauf von Hand eingeschaltet hast, bleiben beim Zurückwechseln nicht erhalten.
- Ist der Pattern-Arbeitsablauf gewählt, wird vom Raster nichts geladen.

## Siehe auch

- [Playlist](/wiki/playlist/)
- [Umstieg von Ableton Live](/wiki/coming-from-ableton/)
- [Projekte importieren](/wiki/importing-projects/)
- [Was nur LYNX kann: Workflows](/why/#workflows)

Der dritte Eintrag ist ein Fenster für einen ersten Song. Siehe [Einfaches Layout](/wiki/simple-layout/).
