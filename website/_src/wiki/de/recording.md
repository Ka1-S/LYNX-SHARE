---
source: 094cbefd18
---
![Das Aufnahmefenster fragt, was aufgenommen werden soll: Audio, Noten und Automation, alles, oder Audio in den Audio-Editor](/assets/img/wiki/record-dialog.webp)

Der Aufnahme-Button im Transport oder die Taste `R` schaltet die Aufnahme ein. Beim ersten Mal fragt ein Fenster, was du aufnehmen willst. Audio kommt als Audioclips in die [Playlist](/wiki/playlist/). Noten gehen in das aktuelle Pattern. Die Regler, die du bewegst, werden zu Automation.

## Ein Mikrofon oder ein Instrument aufnehmen

1. Öffne `Optionen › Audioeinstellungen...` und wähle ein Eingabegerät.
2. Öffne den [Mixer](/wiki/mixer/) (`F9`) und wähle ein leeres Insert.
3. Klicke auf das Eingangsfeld über den Effekt-Slots des Inserts und wähle einen Eingang. Stereopaare stehen zuerst, einzelne Kanäle sind mit `(mono)` markiert.
4. Klicke auf den Aufnahme-Scharfschalter neben dem Eingangsfeld.
5. Drücke `R` und wähle `Audio, in die Playlist als Audioclip`.
6. Drücke Wiedergabe, falls sie nicht von selbst gestartet ist, und spiel.
7. Drücke Stopp. Der Take ist ein Audioclip in der Playlist.

`Shift+R` erledigt die Schritte 4 bis 6 mit einer Taste: Es schaltet das gewählte Insert scharf, nimmt nur Audio auf und startet die Wiedergabe. Drücke es erneut, um zu stoppen.

Wählst du für einen Insert einen Eingang, der mehr als diesen Eingang aufnimmt (andere Kanäle, die an ihn senden, werden mit aufgenommen), fragt LYNX SHARE wie FL Studio, ob auf `Nur externer Eingang` umgeschaltet werden soll. Antworte mit `Ja`, um auf diesem Insert nur den Eingang aufzunehmen.

## Audioaufnahme mit einem Klick

`Tools › Audioaufnahme mit einem Klick` startet einen Take in einem Schritt, wie in FL Studio. Es wirkt auf den gewählten Mixer-Insert. Ist der Master oder nichts gewählt, nimmt es den ersten Insert mit einem Eingang oder sonst einen freien Insert.

| Befehl | Was er tut |
|---|---|
| `In den Edison-Audioeditor/-Recorder` | Legt Edison auf den Insert, eingestellt auf Aufnahme beim Start der Wiedergabe, und startet die Wiedergabe. Siehe [Mixer](/wiki/mixer/) |
| `In die Playlist als Audio-Clip` | Wechselt in den Song-Modus, schaltet den Insert scharf, fragt nach seinem Eingang, falls er keinen hat, und startet Aufnahme und Wiedergabe. Bereits scharfgeschaltete Inserts nehmen so auf, wie sie sind |

## Was das Aufnahmefenster anbietet

| Auswahl | Was sie tut |
|---|---|
| `Audio, in die Playlist als Audioclip` | Schaltet das gewählte Mixer-Insert scharf und fragt nach seinem Eingang, falls es keinen hat |
| `Noten und Automation` | Nimmt Noten von einem MIDI-Keyboard oder der Computertastatur auf, außerdem die Regler, die du bewegst |
| `Alles` | Noten, Automation und Audio von den scharfgeschalteten Inserts |
| `Audio, in den Audio-Editor` | Nimmt den Eingang in einen neuen Audio-Editor auf, während der Song läuft |

Hake `Nicht mehr fragen` an, um deine Wahl zu behalten. Danach klickst du mit rechts auf den Aufnahme-Button für den Aufnahmefilter: `Automation`, `Noten`, `Audio`, `Clips` und `Beim Aufnahmestart fragen, was aufgenommen wird`, um das Fenster zurückzuholen.

## Das Eingangsfeld eines Inserts

![Der Mixer mit scharfgeschaltetem Insert 3 und dem Eingangsfeld auf den Eingängen 1 und 2](/assets/img/wiki/mixer-input.webp)

| Einstellung | Was sie tut |
|---|---|
| `Audio-Eingangsquelle` | Der Geräteeingang, den dieses Insert abspielt und aufnimmt, oder `(keine)` |
| `Aufnahme` | Wo der Take abgegriffen wird: `Nur externer Eingang`, `Externer und Mixer-Eingang`, `Nach den Effekten`, `Nach dem EQ`, `Nach Pegel und Panorama` oder `Nach der Spur` |
| `Zusätzlich den trockenen Eingang als zweiten Take aufnehmen (LYNX)` | Behält den unbearbeiteten Eingang neben einem bearbeiteten Take |
| `Externen Eingang abhören` | `Aus`, `Wenn aufnahmebereit`, `Auto (wenn aufnahmebereit, nicht während der Wiedergabe)` oder `An` |
| `Aufnahmeschutz (nicht aufnahmebereit)` | Schützt das Insert davor, versehentlich scharfgeschaltet zu werden |

## Noten aufnehmen

Noten kommen von einem MIDI-Keyboard, der Computertastatur (`Ctrl+T`) oder der Touch-Tastatur und gehen an die gewählten Kanäle im aktuellen Pattern. Siehe [MIDI-Controller](/wiki/midi-controllers/).

- `Aufgenommene Noten mischen` (`Ctrl+B`) entscheidet zwischen Overdub und Ersetzen. An: Neue Noten kommen zu den Noten des Patterns dazu. Aus: Die alten Noten eines aufgenommenen Kanals werden gelöscht.
- `Aufgenommene Noten am globalen Raster quantisieren` steht in den Aufnahmeoptionen.
- `Schritteingabe` (`Ctrl+E`) funktioniert im Stillstand: Jede Note, die du spielst, geht ein Raster-Step nach dem anderen ins Pattern.
- `MIDI einfangen` (`Ctrl+Shift+Alt+R`) legt die Noten, die du gerade gespielt hast, ins Pattern, auch wenn die Aufnahme aus war.
- Das Sustain-Pedal (CC 64) und das Sostenuto-Pedal (CC 66) halten die Noten, die du spielst, und eine aufgenommene Note dauert, bis das Pedal losgelassen wird. Ein Layer-Kanal gibt das Pedal an seine Kanäle weiter. Ist `Fußpedal steuert Note-Off` unter `Optionen › MIDI-Einstellungen...` aus, geht das Pedal stattdessen als Controller an die Plugins.
- Im Song-Modus bleiben Noten, die in einen Pattern-Clip aufgenommen werden, der länger ist als sein Pattern, dort, wo du sie gespielt hast, und das Pattern wächst. Die Aufnahme wird nicht über die ersten Takte des Patterns zurückgefaltet. Ist `Gedehnte Pattern-Clips wiederholen ihr Pattern (LYNX)` an, gehen nur Noten in den Loop, die über Takten gespielt wurden, die schon Noten hatten.

Regler, die du während der Aufnahme bewegst, werden im Song-Modus als Automationsclips geschrieben und im Pattern-Modus als Pattern-Events. Siehe [Automation](/wiki/automation/).

## Aufnahmeoptionen

Die Optionen stehen unter `Optionen › Aufnahme` und im Rechtsklick-Menü des Aufnahme-Buttons.

| Befehl | Taste | Was er tut |
|---|---|---|
| `Einzähler` | `Ctrl+P` | Aus, 1 Takt, 2 Takte oder 4 Takte Metronom, bevor die Aufnahme startet. `Ctrl+P` wechselt zwischen aus und der zuletzt gewählten Länge. Die Wahl bleibt beim Schließen des Programms erhalten; eine neue Installation startet mit 1 Takt, wie FL Studio |
| `Metronom` | `Ctrl+M` | Klick bei der Wiedergabe |
| `Mit dem Start auf die erste Note warten` | `Ctrl+I` | Die Aufnahme wartet, bis du spielst |
| `Loop-Aufnahme` | keine | Die Aufnahme läuft im Loop, statt weiterzulaufen |
| `Audio: Punch-in/out im Loop-Bereich` | keine | Spielt durch die Loop-Region und behält nur das Audio darin |
| `Audio: Pre-Roll` und `post-roll` | keine | Aus, 1, 2 oder 4 Takte, die vor dem Punch-in und nach dem Punch-out gespielt werden |
| `Aufnahme startet die Wiedergabe` | keine | Das Einschalten der Aufnahme drückt auch Wiedergabe |
| `Aufnahmebereitschaft der Inserts beim Aufnahmestopp aufheben` | keine | Scharfgeschaltete Inserts werden nach jedem Take freigegeben |

`Niedrige Latenz bei der Aufnahme` steht im Rechtsklick-Menü des Aufnahme-Buttons und im Insert-Menü des Mixers unter `Aufnahme auf Festplatte`. Standardmäßig ist es aus. Solange ein Insert mit einem Eingang scharf geschaltet ist oder sein Monitoring auf `An` steht, werden die Effekte umgangen, die auf seinem Weg zum Master Latenz hinzufügen. Das ändert nur, was du hörst: Ein Insert behält alle seine Effekte, wenn die Aufnahme hinter ihnen abgegriffen wird, zum Beispiel mit `Nach den Effekten`. Die Effekte kommen zurück, wenn das Insert nicht mehr scharf ist.

`Latenzausgleich` steht im selben Menü `Aufnahme auf Festplatte` und ist standardmäßig an, wie in FL Studio. Es verschiebt jeden Take um die Rundlaufzeit des Audiogeräts zurück, um den Wert `Feinabstimmung (Samples)` der Audio-Einstellungen und um den Latenzausgleich der Plugins. Schalte es aus, damit die Takes dort bleiben, wo sie aufgenommen wurden.

`Tools › Aufnahme abbrechen` verwirft, was der aktuelle Aufnahmedurchgang geschrieben hat (seine Noten, Automation und Audio-Takes), und schaltet die Aufnahme aus. Die Wiedergabe läuft weiter, und die scharf geschalteten Inserts bleiben scharf.

## Aufnahme in Clip-Slots

Im [Clip-Start-Arbeitsablauf](/wiki/layouts/) können Takes wie in Ableton Live ins Clip-Raster statt in die Playlist gehen.

- Schalte eine Spur mit ihrer Aufnahme-Taste im Session-Mixer scharf. Ihre leeren Slots zeigen dann statt des Stopp-Quadrats eine runde Aufnahme-Taste. Klicke auf eine, um in diesen Slot aufzunehmen.
- `Session-Aufnahme` unter dem Raster nimmt die scharfgeschalteten Spuren in die gewählte Szene auf. Während ihre Clips spielen, werden ihnen neue Noten hinzugefügt (Overdub).
- `Feste Aufnahmelänge` im Menü eines leeren Slots beendet eine Aufnahme in einen Slot nach 1 oder mehr Takten, und der Clip spielt weiter. `Aus` nimmt auf, bis du stoppst.

## Loop-Aufnahme und Takes

Bei eingeschaltetem `Loop-Aufnahme` läuft die Aufnahme im Song-Modus über die Loop-Region im Loop, im Pattern-Modus über das Pattern. Ist es aus, läuft die Aufnahme einfach weiter.

Jeder Durchgang einer Audio-Loop-Aufnahme landet auf der nächsten freien Playlist-Spur, und die Spuren bilden eine Gruppe. `Aufgenommene Noten mischen` entscheidet, was nach dem Stopp spielt: aus schaltet jeden Durchgang stumm, an spielt alle. Klicke mit rechts auf eine Spur der Gruppe und wähle `Comp aus diesen Takes erstellen`, um aus den Durchgängen einen Take zu bauen. `Nach der Loop-Aufnahme nur den letzten Durchgang abspielen` in den Aufnahmeoptionen geht zurück auf eine einzelne Spur.

## Wo Aufnahmen gespeichert werden

- Ein gespeichertes Projekt behält seine Takes in `Audio/Recorded`, neben der Projektdatei.
- Ein ungespeichertes Projekt nimmt in `LYNX/Recordings` in deinem Musikordner auf.
- Takes sind WAV-Dateien mit der Samplerate des Audiogeräts, 16-Bit, 24-Bit oder 32-Bit Float. Das Format (`Dateiformat`) und die Namen der Takes legst du unter `Aufnahme` in `Optionen › Audioeinstellungen...` fest.
- `Aufnahmeordner öffnen` steht im Rechtsklick-Menü des Aufnahme-Buttons. `Datei › Unbenutzte Aufnahmen löschen und speichern...` entfernt Takes, die das Projekt nicht mehr nutzt.

## Gut zu wissen

- Ein scharfgeschaltetes Insert ohne Eingang nimmt nichts auf. LYNX SHARE sagt es dir und zeigt auf das Eingangsfeld.
- Die Aufnahme startet nicht, wenn auf dem Laufwerk weniger als 30 Sekunden Platz sind, und warnt unter 10 Minuten.
- `Optionen › Eingangsaudio aufnehmen` nimmt die ersten beiden Eingangskanäle in einem neuen Insert auf, wenn nichts scharfgeschaltet ist.
- Unter macOS fragt das System beim ersten Aufnehmen nach dem Zugriff auf das Mikrofon.
- Wenn LYNX SHARE während der Aufnahme geschlossen wird, bietet der nächste Start an, die geretteten Takes in die Playlist zu legen.

## Siehe auch

- [Audio einrichten](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automation](/wiki/automation/)
- [MIDI-Controller](/wiki/midi-controllers/)
- [Fehlerbehebung](/wiki/troubleshooting/)
