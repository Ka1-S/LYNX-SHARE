---
source: f08a3da66d
---
![Das Channel Rack mit Step-Zeilen](/assets/img/rack.webp)

Das Channel Rack listet jedes Instrument (jeden Kanal) des Projekts auf, eines pro Zeile, und zeigt die Steps des aktuellen Patterns. Mit `F6` blendest du es ein oder aus. Auch ein Rechtsklick auf die Pattern-Taste im Transportbereich zeigt es an.

## Ein Drum-Pattern bauen

1. Drücke `F6`, um das Channel Rack anzuzeigen.
2. Klicke auf das Feld `+ Kanal hinzufügen` unter der letzten Zeile und wähle im Menü ein Instrument. Du kannst auch im [Browser](/wiki/browser/) ein Sample doppelklicken, um einen neuen Sampler-Kanal zu erhalten.
3. Klicke in der Zeile des Kanals auf Steps, um sie einzuschalten. Ziehe, um eine Reihe von Steps zu zeichnen.
4. Klicke mit rechts auf einen Step, um ihn zu löschen.
5. Drücke `Space`, um abzuspielen. Im Pattern-Modus läuft das Pattern in einer Schleife.
6. Ziehe am Lautstärkeregler der Zeile, um den Pegel einzustellen.

## Steps programmieren

- Klicke auf einen Step, um ihn einzuschalten. Beim Ziehen zeichnest du eine Reihe von Steps. Beginnt das Ziehen auf einem leuchtenden Step, wird stattdessen gelöscht.
- Rechtsklick oder Ziehen mit der rechten Maustaste löscht.
- Ein Klick hinter dem Ende des Patterns fügt einen Step hinzu und verlängert das Pattern.
- Eine Zeile, deren Noten keine einfachen Steps sind (andere Tonhöhen, längere Noten, Slides), zeigt eine Miniatur der Noten. Ein Klick darauf öffnet sie im [Piano Roll](/wiki/piano-roll/).
- Du kannst Audiodateien auf das Rack ziehen, um Kanäle anzulegen.

Das Kanalmenü (Rechtsklick auf den Kanalnamen) enthält `Jeden 2. Step füllen`, `Jeden 4. Step füllen`, `Jeden 8. Step füllen`, `Erweitertes Füllen` sowie `Nach links rotieren` und `Nach rechts rotieren`.

## Die Graph-Spur

![Die Graph-Spur unter dem Kanal Kick, mit einem Velocity-Balken pro Step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` zeigt die Graph-Spur unter dem gewählten Kanal. Die Tasten links davon wählen, was sie bearbeitet: Velocity, Pan, Release, Feinstimmung, Mod X oder Mod Y. Beim Ziehen legst du einen Wert pro Step fest, mit Ziehen per rechter Maustaste zeichnest du eine Rampe, und `Alt`+Klick setzt einen Step zurück.

## Die Teile einer Zeile

| Teil | Was er bewirkt |
|---|---|
| Stumm-Lämpchen | Klick schaltet stumm. `Ctrl`+Klick oder Rechtsklick schaltet auf Solo. |
| Regler für Pan und Lautstärke | Ziehen oder das Mausrad benutzen. |
| Routing-Feld | Ziehen oder Mausrad wählt das Mixer-Insert. Doppelklick zeigt dieses Insert im Mixer. Rechtsklick nimmt den nächsten freien Insert. |
| Namenstaste | Klick öffnet oder schließt den Editor des Kanals. `Alt`+Klick lässt die anderen Editoren offen. `Ctrl`+Klick hört vor. Ziehen nach oben oder unten ordnet um. Rechtsklick öffnet das Kanalmenü. |
| Auswahlfeld | Klick wählt einen Kanal. `Ctrl`+Klick oder Rechtsklick fügt hinzu oder entfernt. `Shift`+Klick wählt einen Bereich. Doppelklick wählt alle. |

## Patterns

Das Rack bearbeitet immer das aktuelle Pattern. Die Pattern-Auswahl des Racks zeigt dessen Namen: Klicke auf den Namen für die Liste oder benutze die Pfeile. Auch `Shift`+Mausrad über dem Titel des Racks wechselt das Pattern.

Das Längenfeld des Racks legt die Länge in Takten fest (ziehen oder Mausrad benutzen). Doppelklicke darauf für eine automatische Länge, die aus den Noten bestimmt wird.

Der Swing-Regler in der Haupt-Werkzeugleiste gilt für alle Kanäle. `Swing-Mix` im Kanalmenü legt fest, wie stark ein Kanal ihm folgt.

| Befehl | Taste | Was sie bewirkt |
|---|---|---|
| Nächstes Pattern | `Numpad +` | Macht das nächste Pattern zum aktuellen |
| Vorheriges Pattern | `Numpad -` | Macht das vorherige Pattern zum aktuellen |
| Nächstes leeres Pattern | `F4` | Springt zum nächsten leeren Pattern und fragt nach einem Namen |
| Umbenennen und färben | `F2` | Benennt das aktuelle Pattern um und färbt es |

## Noten über die PC-Tastatur spielen

`Ctrl+T` macht aus der Schreibtastatur ein Klavier: `Z` bis `M` und `Q` bis `P` spielen die gewählten Kanäle. Solange es an ist, ruhen die Ein-Buchstaben-Kürzel der Editoren.

## Tasten

Diese gelten, solange das Channel Rack den Fokus hat.

| Befehl | Taste | Was sie bewirkt |
|---|---|---|
| Kanal wählen | `Up` / `Down` | Wählt den Kanal darüber oder darunter |
| Auswahl erweitern | `Shift+Up` / `Shift+Down` | Fügt den Kanal darüber oder darunter zur Auswahl hinzu |
| Kanäle verschieben | `Alt+Up` / `Alt+Down` | Verschiebt die gewählten Kanäle nach oben oder unten |
| Alles auswählen | `Ctrl+A` | Wählt alle Kanäle |
| Ausschneiden, Kopieren, Einfügen | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Schneidet die Steps der gewählten Kanäle aus, kopiert oder fügt sie ein |
| Steps rotieren | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotiert die Steps der gewählten Kanäle |
| Klonen | `Alt+C` | Klont die gewählten Kanäle |
| Löschen | `Alt+Del` | Löscht die gewählten Kanäle |
| Gruppieren | `Alt+G` | Legt die gewählten Kanäle in eine neue Filtergruppe |
| Zum Mixer routen | `Ctrl+L` | Schickt die gewählten Kanäle auf freie Mixer-Inserts |
| Stummschalten | `1` bis `0` | Schaltet die Kanäle 1 bis 10 stumm oder wieder laut |
| Solo | `Ctrl+1` bis `Ctrl+0` | Schaltet die Kanäle 1 bis 10 solo |
| Graph-Spur | `Ctrl+K` | Blendet die Graph-Spur ein oder aus |

## Weitere Einträge im Kanalmenü

`Piano Roll`, `Umbenennen und färben`, `Ersetzen` (das Instrument austauschen), `Klonen`, `Delete`, `Arpeggiator`, `Loop-Punkt`, `MIDI schreiben in`, `Kanäle sortieren`, `Unbenutzte Kanäle auswählen` und `Kanaleinstellungen`.

## Gut zu wissen

- Nur das Feld `+ Kanal hinzufügen` öffnet das Hinzufügen-Menü.
- Das Rack hat kein eigenes Pattern. Wer irgendwo das aktuelle Pattern wechselt, ändert, was das Rack zeigt.
- Eine Zeile mit einer Miniatur von Noten lässt sich nicht als Steps bearbeiten. Nimm dafür das Piano Roll.
- Die Zifferntasten erreichen nur die ersten zehn Kanäle.
- Die Tasten in der Tabelle brauchen den Fokus auf dem Rack. Klicke also zuerst auf das Rack.

## Verwandt

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Tastenkürzel](/wiki/shortcuts/)
