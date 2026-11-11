---
source: b2de12e24d
---
![Der Mixer](/assets/img/mixer.webp)

Jeder Kanal spielt über ein Mixer-Insert. Jedes Insert hat einen Fader, Pan, Stumm und Solo, eine Spitzenpegelanzeige, zehn Effekt-Slots mit je einem Dry/Wet-Anteil und einen eingebauten EQ. Alle Inserts enden im Master, solange du sie nicht anders routest. Mit `F9` zeigst du den Mixer an oder blendest ihn aus.

Ein Projekt kann bis zu 127 Inserts haben. Der Mixer hat eine Leiste zum Hinzufügen eines neuen. Das Feld in der Titelleiste des Mixers wählt eines von acht Streifenlayouts, von `Kompakt` bis `Extra groß`.

## Einen Effekt auf einen Kanal legen

1. Wähle im [Channel Rack](/wiki/channel-rack/) den Kanal aus und drücke `Ctrl+L`. Der Kanal geht an ein freies Insert, das seinen Namen und seine Farbe übernimmt.
2. Drücke `F9`, um den Mixer anzuzeigen, und klicke auf den Streifen dieses Inserts.
3. Klicke im Effektbereich auf einen leeren Slot und wähle einen Effekt.
4. Klicke auf den Namen des Slots, um das Fenster des Effekts zu zeigen oder auszublenden.
5. Klicke auf das Lämpchen des Slots, um den Effekt zu umgehen und zu vergleichen.

## Einen Kanal an ein Insert senden

Ziehe im Channel Rack das Routing-Feld des Kanals (oder nutze das Mausrad darauf), um die Insert-Nummer zu wählen. Ein Doppelklick auf das Feld zeigt dieses Insert im Mixer.

`Ctrl+L` im Channel Rack wirkt auf mehrere gewählte Kanäle gleichzeitig. Jeder geht an sein eigenes freies Insert.

Hat der Mixer den Fokus, verknüpft `Ctrl+L` die gewählten Kanäle mit dem gewählten Insert, und `Ctrl+Shift+L` verknüpft sie mit aufeinanderfolgenden Inserts ab diesem.

## Inserts auswählen und verschieben

- Klicke auf einen Streifen, um ihn zu wählen.
- `Ctrl`+Klick oder Ziehen über mehrere Streifen wählt einen Bereich. `Ctrl+Shift`+Klick fügt einen hinzu oder nimmt ihn heraus.
- Ziehe einen Streifen zur Seite oder drücke `Alt+Left` und `Alt+Right`, um die gewählten Inserts zu verschieben.
- Klicke mit rechts auf einen Streifen für das Insert-Menü. `F2` benennt das gewählte Insert um und färbt es.

Beim Verschieben, Einfügen oder Löschen von Inserts bleiben Sends, Sidechains und Automation bei ihrem Insert.

## Stumm und Solo

Klicke auf das Stumm-Lämpchen, um ein Insert stummzuschalten. Mit Rechtsklick oder `Ctrl`+Klick darauf schaltest du es solo. `S` schaltet die gewählten Inserts solo, und `Alt+S` schaltet sie zusammen mit den Inserts solo, die sie speisen.

## Effekte

Der Effektbereich zeigt die zehn Slots des gewählten Inserts.

- Ziehe einen Slot oder nutze das Mausrad darüber, um ihn nach oben oder unten zu verschieben.
- Klicke mit rechts auf einen Slot für seine Optionen (ersetzen, verschieben, löschen).
- `Del` löscht das Plugin im gewählten Slot.
- Der Schalter in der Kopfzeile des Bereichs schaltet alle Effekte des Inserts ein oder aus.
- Du kannst einen Effekt aus der Plugin-Datenbank des [Browsers](/wiki/browser/) auf ein Insert oder einen Effekt-Slot ziehen.

## Insert-EQ

Jedes Insert hat einen eigenen EQ. Ziehe ein Band, um Frequenz und Pegel einzustellen, nutze das Mausrad für die Breite und klicke doppelt auf ein Band, um es auf 0 dB zurückzusetzen. Klicke mit rechts auf den Titel des EQ, um ihn zu umgehen. Willst du einen Mixer ohne eingebauten EQ, kannst du neue Projekte so einstellen, dass sie mit einem sauberen Mixer starten.

## Routing, Sends und Sidechain

![Der Mixer mit gewähltem Insert Kick, seinen Sends als Kabel unter den Streifen und dem Effektbereich rechts](/assets/img/wiki/mixer-sends.webp)

Wähle das Insert, das du routen willst. Jeder andere Streifen hat dann einen Routing-Schalter:

- Klicke auf den Schalter eines anderen Streifens, um das gewählte Insert dorthin zu senden. Ziehen nach dem Einschalten stellt den Send-Pegel ein.
- `Ctrl`+Klick routet nur an dieses Insert.
- `Shift`+Klick erzeugt einen Sidechain-Send. `Ctrl+Shift`+Klick macht ihn zu einem reinen Sidechain.
- Ein Rechtsklick öffnet dieselben Möglichkeiten als Menü.

Effekte mit Side-Eingang, etwa ein Kompressor, haben eine Auswahl `Sidechain von`, die die Inserts zum Abhören auflistet. Wählst du eines, wird die Route für dich angelegt.

## Gruppen und VCA-Fader

Mixer-Gruppen klappst du mit ihrem Pfeil ein (oder mit `Alt`+Klick alle Gruppen). Ein VCA-Fader bewegt den Pegel seiner Mitglieds-Inserts, die ihre eigenen Fader behalten. Lege einen im Insert-Menü an: Wähle unter Gruppe `VCA › Neuer VCA aus Auswahl`.

## Aufnahme

Jedes Insert hat einen Button zum Scharfschalten der Aufnahme. Ist ein Insert scharf, nehmen Rec und Wiedergabe seinen Audioeingang in einen Playlist-Clip auf. `Shift+R` schaltet das gewählte Insert scharf und startet die Aufnahme in einem Schritt. Siehe [Aufnahme](/wiki/recording/).

## Tasten

`Ctrl+L` und `Ctrl+Shift+L` haben diese Bedeutung nur, solange der Mixer den Fokus hat.

| Befehl | Taste | Was er tut |
|---|---|---|
| Kanäle verknüpfen | `Ctrl+L` | Verknüpft die gewählten Kanäle mit dem gewählten Insert |
| Mit aufeinanderfolgenden Inserts verknüpfen | `Ctrl+Shift+L` | Verknüpft die gewählten Kanäle mit Inserts ab dem gewählten |
| Inserts verschieben | `Alt+Left` / `Alt+Right` | Verschiebt die gewählten Inserts |
| Umbenennen und färben | `F2` | Benennt das gewählte Insert um und färbt es |
| Solo | `S` | Schaltet die gewählten Inserts solo |
| Solo mit Quellen | `Alt+S` | Schaltet die gewählten Inserts und die Inserts solo, die sie speisen |
| Effekt löschen | `Del` | Löscht das Plugin im gewählten Slot |
| Scharfschalten und aufnehmen | `Shift+R` | Schaltet das gewählte Insert scharf und startet die Aufnahme |

## Gut zu wissen

- `Zu Audio rendern` (ein Insert, das in einen Audioclip gebounct wird) spielt etwa 5 dB lauter als das Insert, das es ersetzt. Das ist ein bekanntes Problem in den Release Notes von 0.1.25.
- `Ctrl+L` macht im Channel Rack und im Mixer Unterschiedliches. Im Rack wählt es freie Inserts. Im Mixer nutzt es das gewählte Insert.
- Die Routing-Schalter erscheinen auf den anderen Streifen, nicht auf dem gewählten.
- Das Limit sind 127 Inserts pro Projekt und zehn Effekt-Slots pro Insert.

## Siehe auch

- [Channel Rack](/wiki/channel-rack/)
- [Aufnahme](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Plugins von Drittanbietern](/wiki/plugins/)
- [Instrumente und Effekte](/instruments-and-effects/)
