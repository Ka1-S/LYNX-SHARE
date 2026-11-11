---
source: de4ead4a7a
---
Diese Seite geht einmal den ganzen Weg durch: ein Drum-Pattern, eine Melodie, ein Arrangement, ein Mix und eine exportierte Datei. Jeder Abschnitt verweist auf die Seite, die sein Fenster vollständig behandelt. Wenn du an irgendeiner Stelle nichts hörst, lies [Audio einrichten](/wiki/audio-setup/).

## Ein Drum-Pattern bauen

1. Wähle `Datei › Neu aus Vorlage › Leer`. Das Projekt startet mit fünf Kanälen, Kick, Clap, Hat, Snare und Synth, und ohne Steps.
2. Drücke `F6`, falls das [Channel Rack](/wiki/channel-rack/) nicht zu sehen ist.
3. Klicke ein paar Steps in der Zeile Kick an. Ziehen zeichnet mehrere, Rechtsklick löscht.
4. Drücke `Space`. Der Transport steht im Pattern-Modus, das Pattern läuft also in einer Schleife. Drücke `Space` noch einmal, um zu stoppen.
5. Gib den Zeilen Clap, Hat und Snare ihre eigenen Steps.
6. Drücke `F2`, um das Pattern zu benennen und zu färben.

Um einen Sound hinzuzufügen, der noch fehlt, klicke unter der letzten Zeile auf das Feld `+ Kanal hinzufügen` und wähle im Menü ein Instrument. Um ein eigenes Sample zu verwenden, öffne den [Browser](/wiki/browser/) mit `Alt+F8`, klicke eine Audiodatei an, um sie zu hören, und doppelklicke sie, um sie als neuen Kanal hinzuzufügen.

## Eine Melodie schreiben

1. Drücke `F4`. LYNX SHARE springt zum nächsten leeren Pattern und fragt nach dessen Namen.
2. Klicke mit rechts auf den Namen des Kanals Synth und wähle `Piano Roll`.
3. Klicke mit dem Stift (`P`) ins Raster, um eine Note hinzuzufügen.
4. Ziehe eine Note, um sie zu verschieben, und ziehe an ihrem Ende, um ihre Länge zu ändern. Rechtsklick löscht eine Note.
5. Drücke `Space`, um das Pattern beim Bearbeiten zu hören.

`Ctrl+T` macht aus der Schreibtastatur ein Klavier, sodass du Noten zuerst auf `Z` bis `M` und `Q` bis `P` ausprobieren kannst. Schalte es wieder aus, bevor du Buchstabenkürzel wie `P` benutzt.

## Die Patterns arrangieren

1. Drücke `F5`, um die [Playlist](/wiki/playlist/) anzuzeigen.
2. Klicke im Bereich Auswahl links in der Playlist auf das Drum-Pattern. Es ist nun die Clip-Quelle.
3. Klicke mit dem Stift (`P`) auf eine Spur, um das Pattern zu platzieren. Klicke weiter rechts noch einmal, um es erneut zu platzieren, oder nimm den Pinsel (`B`) und ziehe, um Clips lückenlos aneinanderzureihen.
4. Klicke in der Auswahl auf das Melodie-Pattern und platziere es auf einer anderen Spur.
5. Drücke `L`, um in den Song-Modus zu wechseln, dann `Space`. Die Playlist spielt ab der Song-Position. Klicke auf das Lineal, um sie zu setzen.

## Mischen und einen Effekt hinzufügen

1. Drücke `F9`, um den [Mixer](/wiki/mixer/) anzuzeigen. Die fünf Start-Kanäle spielen bereits über die Inserts 1 bis 5, und ein Kanal, der über das Menü `+ Kanal hinzufügen` hinzukommt, nimmt einen freien Insert. Die Zahl im Routing-Feld des Kanals im Channel Rack sagt, welchen.
2. Um Kanäle auf eigene freie Inserts zu legen, wähle sie im Channel Rack aus und drücke `Ctrl+L`. Jeder Insert übernimmt Namen und Farbe des Kanals.
3. Ziehe am Fader eines Inserts, um den Pegel einzustellen.
4. Klicke auf ein Insert, um ihn zu wählen. Der Effektbereich rechts zeigt seine zehn Slots.
5. Klicke auf einen leeren Slot und wähle einen Effekt. Klicke auf den Namen des Slots, um das Fenster des Effekts ein- oder auszublenden. Das Lämpchen des Slots schaltet den Effekt auf Bypass.

## Den Song exportieren

![Das Export-Fenster: Bereich und Ausklang links, Format, Bittiefe und Samplerate rechts, Mix-Optionen darunter](/assets/img/wiki/export.webp)

1. Drücke `Ctrl+S` und speichere zuerst das Projekt.
2. Wähle `Datei › Exportieren › Wave-Datei...`. Für andere Formate bietet dasselbe Menü `MP3-Datei...`, `OGG-Datei...` und `FLAC-Datei...`.
3. Stelle `Bereich` auf `Song (Playlist)`. Die anderen Möglichkeiten sind `Aktuelles Pattern` und `Playlist-Auswahl`.
4. Lass die übrigen Optionen, wie sie sind.
5. Klicke auf `Exportieren...`, wähle den Dateinamen und warte, bis gerendert ist.

## Was jede Taste getan hat

| Befehl | Taste | Was sie bewirkt |
|---|---|---|
| Channel Rack | `F6` | Blendet das Channel Rack ein oder aus |
| Nächstes leeres Pattern | `F4` | Springt zum nächsten leeren Pattern und benennt es |
| Piano Roll | `F7` | Blendet das Piano Roll ein oder aus |
| Playlist | `F5` | Blendet die Playlist ein oder aus |
| Pattern- oder Song-Modus | `L` | Schaltet um, was `Space` abspielt |
| Mixer | `F9` | Blendet den Mixer ein oder aus |
| Auf freie Inserts schicken | `Ctrl+L` | Gibt den gewählten Kanälen eigene Mixer-Inserts |
| Speichern | `Ctrl+S` | Speichert das Projekt als `.lynx` |
| Exportieren | `Ctrl+R` | Öffnet das Export-Fenster mit dem zuletzt benutzten Format |

## Gut zu wissen

- Im Pattern-Modus hörst du nur das aktuelle Pattern. Ist der Song nach dem Arrangieren stumm, prüfe, ob der Transport im Song-Modus steht.
- Das Export-Fenster startet auf `Song (Playlist)`, wenn der Transport im Song-Modus steht und die Playlist Clips enthält. Sonst startet es auf `Aktuelles Pattern`.
- Der Klick des Metronoms ist nie Teil eines Exports.
- Am unteren Rand des Export-Fensters stehen die Länge des Songs in Takten, seine Gesamtzeit und der Speicherplatz, den die Datei braucht.
- Ein neues Projekt hat standardmäßig einen Limiter auf dem Master-Insert. Er gehört zu dem, was du hörst und was du exportierst.
- Rückgängig ist `Ctrl+Z`. Noch einmal gedrückt, wiederholt es diesen Schritt. `Ctrl+Alt+Z` geht weiter zurück.

## Verwandt

- [Rundgang durch das Fenster](/wiki/tour/)
- [Export](/wiki/exporting/)
- [Automation](/wiki/automation/)
- [Aufnahme](/wiki/recording/)
- [Instrumente und Effekte](/instruments-and-effects/)
