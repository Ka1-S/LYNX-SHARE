---
source: a06504f3a0
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
| `Jeder MIDI-Kanal` | Hört auf jeden MIDI-Kanal oder nur auf einen. `MPE-Controller` steht am Ende dieser Liste |
| `Spielt die ausgewählten Kanäle` | Noten gehen an die im Channel Rack gewählten Kanäle oder immer an einen festen Kanal |
| `Velocity` | `Wie gespielt`, `Weich (leichtes Spiel klingt lauter)`, `Hart (erfordert kräftigeres Spiel)` oder `Fest (jede Note mit voller Velocity)` |
| `Kanal des Performance-Modus` | Noten auf diesem MIDI-Kanal starten Playlist-Clips, solange der Performance-Modus an ist |
| `Song-Marker-Sprung` | Noten auf diesem MIDI-Kanal springen im Song zu einem Marker, statt zu spielen |
| `Controller-Typ` | `Automatisch erkennen`, `Generischer Controller` oder ein Controller-Profil |
| `Fußpedal steuert Note-Off` | Standardmäßig an. Ein Sustain-Pedal (CC 64) oder ein Sostenuto-Pedal (CC 66) hält die Noten. Aus, geht das Pedal als Controller an die Plugins |

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

Die MIDI-Seite listet die Verknüpfungen unter `Verknüpfungen (Projekt, global)` auf. Ein Gerät, dessen `Controller-Typ` auf `Generischer Controller` steht, macht nur das, was du von Hand verknüpfst.

Die Parameter eines Plugins von Drittanbietern erreichst du über `Plugin-Parameter automatisieren` im Menü des Kanals oder des Effekt-Slots. Jeder Parameter dort hat dieselben Verknüpfungseinträge. Siehe [Automation](/wiki/automation/).

## Controller-Profile

Ein Controller-Profil richtet Transport-Buttons, Fader, Regler, Pads, Lichter und Display eines Geräts ein, ohne dass du etwas verknüpfen musst.

1. Öffne `Optionen › MIDI-Einstellungen...` (`F10`) und hake das Gerät unter `Eingabegeräte` an.
2. Lass seinen `Controller-Typ` auf `Automatisch erkennen`. Der Name in Klammern ist das Profil, das anhand des Port-Namens des Geräts gefunden wurde.
3. Steht dort `Generischer Controller`, wähle das Profil selbst aus der Liste.
4. Lies die Zeile neben dem Feld. `Feedback to` nennt den Ausgangsport, der die Lichter, Motorfader und den Display-Text bekommt.

| Profil | Was es belegt |
|---|---|
| `Mackie Control Universal` | Fader auf die Lautstärken der Inserts und den Master. V-Pots auf Pan, auf die Parameter des Plugins des gewählten Kanals oder auf die Lautstärken im Channel Rack. Arm, Solo, Stumm und Auswahl pro Strip, Fader-Bänke, Wiedergabe, Stopp, Aufnahme, Zurückspulen, Vorspulen, Pattern- oder Song-Modus, Metronom, Rückgängig und das Jogwheel. Das Display zeigt Namen und Werte |
| `Mackie Control Extender (XT)` | Dasselbe für 8 weitere Strips. Die Geräte teilen sich eine Bank, in der Reihenfolge der Liste `Eingabegeräte` |
| `Novation Launchkey MK3 (DAW mode)` | Regler auf Pan, Lautstärke oder Plugin-Parameter, Fader auf Lautstärke, Fader-Buttons wählen ein Insert. Buttons für Bank, Wiedergabe, Stopp, Aufnahme, Loop, Metronom und Rückgängig. Die Session-Pads sind die 16 Steps des gewählten Kanals. Nutze es am zweiten Port des Keyboards |
| `Akai APC Key 25 mk2` | Regler auf Lautstärke, Pan oder Plugin-Parameter. Die zwei oberen Pad-Reihen sind die 16 Steps des gewählten Kanals, und die drei darunter spielen Noten. Track-Buttons wählen ein Insert. Stopp, Wiedergabe und Aufnahme. Nutze es am zweiten Port |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | Die Mackie-Control-Belegung, am MCU/HUI-Port des Keyboards |
| `Korg nanoKONTROL2 (Mackie Control mode)` | Die Mackie-Control-Belegung. Es wird nicht erkannt: Starte das Gerät in einem Mackie-Control-Modus und wähle diesen Typ von Hand |

Regler und Fader, die eine Position senden, nutzen Soft Takeover: Sie bewegen einen Regler erst, wenn sie seinen Wert erreicht haben. Regler, die du von Hand verknüpft hast, behalten ihre Verknüpfungen. `Ordner für Controller-Profile` öffnet den Ordner für deine eigenen Profile (`*.lynxctl`), und `Geräteliste aktualisieren` liest sie neu ein.

Die eingebauten Profile wurden nach der Dokumentation des jeweiligen Herstellers geschrieben. Sie sind noch nicht an der Hardware ausprobiert worden.

## MPE-Controller

MPE ist in einer ersten Stufe. Ein MPE-Controller spielt jede Note mit eigenem Pitch Bend, eigenem Timbre (CC 74) und eigenem Druck.

1. Öffne auf der MIDI-Seite das Feld des Geräts, in dem `Jeder MIDI-Kanal` steht, und wähle `MPE-Controller`.
2. Wähle in dem Feld, das daneben erscheint, die Member-Kanäle, von `Kanal 2` bis `Channels 2-16`.
3. Spiele. Zum Aufnehmen siehe [Aufnahme](/wiki/recording/).

- Eine aufgenommene Note behält von jedem einen Wert: ihr Bend als Fine pitch, ihr Timbre als Mod X und ihren stärksten Druck als Mod Y. Es ist keine Kurve.
- Von den eingebauten Instrumenten folgen der Sampler und der eingebaute Synth diesen Werten.
- Ein Fremd-Plugin erhält MPE, wenn `MPE` unter `MIDI` in den `Wrapper-Einstellungen` des Plugin-Fensters an ist. Jede Note geht dann auf einem eigenen MIDI-Kanal hinaus, 2 bis 16.
- Es wurde noch nicht mit einem echten MPE-Controller ausprobiert.

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
- Eine von einem Pedal gehaltene Note wird bis zu dem Moment aufgenommen, in dem das Pedal losgelassen wird.
- Ein Automationsclip nimmt einen Regler nicht zurück, solange du ihn von einem Controller aus bewegst. Der Regler folgt seiner Automation wieder, wenn die Wiedergabe das nächste Mal startet.

## Siehe auch

- [Aufnahme](/wiki/recording/)
- [Automation](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Einstellungen](/wiki/settings/)
- [Tastenkürzel](/wiki/shortcuts/)
