---
source: 3b54a39c97
---
![Die MIDI-Seite der Einstellungen: Eingabegeräte, Ausgabegeräte mit Master-Sync und externer Sync](/assets/img/wiki/settings-midi.webp)

LYNX SHARE nimmt Noten und Controller-Bewegungen von jedem MIDI-Eingabegerät entgegen. Die Geräte richtest du auf der MIDI-Seite der Einstellungen ein: `Optionen › MIDI-Einstellungen...` oder `F10`. Ohne Hardware spielen auch die Computertastatur und die Touch-Tastatur Noten.

## Ein Keyboard anschließen

1. Stecke das Keyboard an und öffne `Optionen › MIDI-Einstellungen...` (`F10`).
2. Such es unter `Eingabegeräte` und hake es an. Wird es nicht aufgelistet, klicke auf `Geräteliste aktualisieren`.
3. Lass die beiden Felder auf `Jeder MIDI-Kanal` und `Spielt die ausgewählten Kanäle`.
4. Schließe die Einstellungen und wähle im [Channel Rack](/wiki/channel-rack/) einen Kanal aus.
5. Spiel. Der gewählte Kanal klingt.

## Eingabegeräte

| Einstellung | Was sie tut |
|---|---|
| Kontrollkästchen | Schaltet das Gerät ein oder aus |
| `Jeder MIDI-Kanal` | Hört auf jeden MIDI-Kanal oder nur auf einen |
| `Spielt die ausgewählten Kanäle` | Noten gehen an die im Channel Rack gewählten Kanäle oder immer an einen festen Kanal |
| `Velocity` | `Wie gespielt`, `Weich (leichtes Spiel klingt lauter)`, `Hart (erfordert kräftigeres Spiel)` oder `Fest (jede Note mit voller Velocity)` |
| `Kanal des Performance-Modus` | Noten auf diesem MIDI-Kanal starten Playlist-Clips, solange der Performance-Modus an ist |
| `Song-Marker-Sprung` | Noten auf diesem MIDI-Kanal springen im Song zu einem Marker, statt zu spielen |

## Einen Regler mit einem Parameter verknüpfen

1. Klicke in LYNX SHARE mit rechts auf den Regler oder Fader.
2. Wähle `Mit MIDI-Controller verknüpfen (einen Regler am Controller bewegen)`.
3. Bewege den Regler oder Fader an deiner Hardware. Die Verknüpfung steht.

Die Verknüpfung wird mit dem Projekt gespeichert. Um sie zu entfernen, klicke erneut mit rechts auf den Regler und wähle `Controller-Verknüpfung lösen`. Der Menüeintrag zeigt die verwendete CC-Nummer.

| Befehl | Taste | Was er tut |
|---|---|---|
| `Override global link...` | keine | Erzeugt eine Verknüpfung, die in jedem Projekt gilt. Sie wird in den Einstellungen gespeichert, nicht im Projekt |
| `Globale Verknüpfung entfernen` | keine | Entfernt sie wieder |
| `Multilink zu Controllern` | `Ctrl+J` | Solange es an ist, werden die Regler, die du bewegst, der Reihe nach mit den Hardware-Reglern verknüpft, die du als Nächstes bewegst. Es steht im Menü `Optionen` |
| `Pickup (Übernahmemodus)` | keine | Auf der MIDI-Seite. Ein Controller bewegt einen Parameter erst, wenn er dessen aktuellen Wert erreicht hat, damit nichts springt |

Die MIDI-Seite listet die Verknüpfungen unter `Verknüpfungen (Projekt, global)` auf. `Controller-Typ` auf derselben Seite steht auf `Generischer Controller`.

Die Parameter eines Plugins von Drittanbietern erreichst du über `Plugin-Parameter automatisieren` im Menü des Kanals oder des Effekt-Slots. Jeder Parameter dort hat dieselben Verknüpfungseinträge. Siehe [Automation](/wiki/automation/).

## Die Computertastatur

`Ctrl+T` oder `Optionen › Tastatur als Klavier` macht aus der Computertastatur ein Klavier. Die Reihen von `Z` bis `M` und von `Q` bis `P` spielen die gewählten Kanäle.

- Solange sie an ist, sind Buchstaben- und Zifferntasten Noten. Die Einzelbuchstaben-Werkzeugtasten von Piano Roll und Playlist funktionieren nur, solange sie aus ist.
- Sie spielt immer mit der Velocity des Piano Roll. Die Einstellung `Velocity` auf der MIDI-Seite gilt für MIDI-Geräte.
- Derselbe Schalter steht in `Optionen › Allgemeine Einstellungen...`.

## Die Touch-Tastatur

![Die Touch-Tastatur: Oktavbuttons, Tastengröße, Velocity-Modus und eine Reihe Klaviertasten ab C4](/assets/img/wiki/touch-keyboard.webp)

`Ansicht › Bildschirmtastatur` (`Alt+F7`) öffnet ein Klavier, das du mit der Maus oder einem Touchscreen spielst. Es spielt den im Channel Rack gewählten Kanal und zeigt dessen Namen rechts an.

| Einstellung | Was sie tut |
|---|---|
| `-` und `+` | Eine Oktave tiefer oder höher |
| Tastengröße | `Kleine Tasten`, `Mittlere Tasten` oder `Große Tasten` |
| `Velocity: nach Berührungspunkt` | Nahe der Tastenoberkante spielt leise, nahe der Unterkante laut |
| `Velocity: fest` | Jede Note mit der Velocity, die der Schieberegler vorgibt |
| `Notennamen` | Zeigt die Namen auf den Tasten |

## MIDI-Sync

`Ausgangsgeräte` auf der MIDI-Seite sendet den Transport an Hardware und andere Software. Hake für ein Gerät `Master-Sync senden` an und wähle `MIDI Clock` (mit Start, Continue, Stop und Song Position Pointer) oder MIDI Time Code. `Versatz (ms)` sendet den Sync bei positivem Wert später und bei negativem früher. `All Notes Off senden` sendet diese Meldung beim Initialisieren und nach einem Export an das Gerät.

`Sync zu externer Clock` funktioniert andersherum: LYNX SHARE folgt der MIDI-Clock des gewählten Eingangs, mit ihrem Tempo, Start, Continue, Stop und Song Position Pointer. `Versatz der externen Synchronisation (Millisekunden)` spielt LYNX SHARE bei positivem Wert später und bei negativem früher. `Status` zeigt, ob eine Clock läuft.

## Gut zu wissen

- Wenn du für `Sync zu externer Clock` einen Eingang wählst, wird dieses Gerät unter `Eingabegeräte` eingeschaltet.
- Ein Tempo, das ein verknüpfter MIDI-Controller verstellt, ändert die Größe von Audioclips nicht. Tippe das endgültige Tempo ein oder setze es.
- Wie du aufnimmst, was du spielst, steht unter [Aufnahme](/wiki/recording/). Eingehende Noten können im [Piano Roll](/wiki/piano-roll/) auf eine Skala gerastet werden.
- MIDI-CC, Channel-Aftertouch und Pitch Bend, die an ein Plugin gehen, lassen sich mit `MIDI-Controller automatisieren (CC, Aftertouch, Pitchbend)` automatisieren.

## Siehe auch

- [Aufnahme](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Einstellungen](/wiki/settings/)
- [Tastenkürzel](/wiki/shortcuts/)
