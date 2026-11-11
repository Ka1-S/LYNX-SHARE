---
source: 3801d5daff
---
![Die Seite Audio im Einstellungsfenster, noch ohne gewähltes Ausgabegerät](/assets/img/wiki/settings-audio.webp)

Auf der Seite Audio im Einstellungsfenster legst du fest, wohin der Ton geht und woher Aufnahmen kommen. Du öffnest sie mit `Optionen › Audioeinstellungen...`. Der Einrichtungsassistent (`Optionen › Einrichtungsassistent (Sprache, Audio, Plugin-Ordner)...`) testet ebenfalls den Audioausgang.

## Ton ausgeben

1. Öffne `Optionen › Audioeinstellungen...`.
2. Wähle einen `Treiber`. Der Hinweis unter dem Feld sagt, wofür dieser Treiber taugt.
3. Wähle ein `Ausgabegerät`.
4. Klicke auf `Testton abspielen`.
5. Lies den Bereich `Status`. Er zeigt den laufenden Treiber, die Samplerate und die Ausgabelatenz, oder `Kein Audiogerät geöffnet: ein Ausgabegerät wählen.`

## Treiber

Das Feld `Treiber` listet die Treibertypen deines Systems auf.

| System | Treiber | Was er tut |
|---|---|---|
| Windows | Windows Audio (shared) | Andere Programme behalten ihren Ton. Windows mischt alle Programme in festen Schritten, deshalb kann der Puffer nicht unter diesen Schritt fallen |
| Windows | Windows Audio (shared, low-latency period) | Ebenfalls geteilt, fordert von Windows aber eine kürzere Mischperiode an. Nur manche Treiber unterstützen das |
| Windows | Windows Audio (exclusive, lowest latency) | LYNX SHARE nutzt das Gerät allein. Andere Programme sind stumm, solange es spielt |
| Windows | DirectSound (legacy) | Alter Windows-Audiopfad mit hoher Latenz. Für den Fall, dass die anderen versagen |
| Windows | ASIO | Treiber mit niedriger Latenz. Der ASIO-Treiber eines Herstellers funktioniert mit dessen eigenem Gerät |
| Windows | LYNX ASIO (built in) | Der eigene ASIO-Treiber von LYNX SHARE, für jedes Gerät, Wiedergabe und Aufnahme. Mit `Treibereinstellungen...` wählst du den geteilten oder exklusiven Modus und die Geräte |
| macOS | Core Audio | Das systemeigene Audio, mit anderen Programmen geteilt |
| Linux | PipeWire (native) | LYNX SHARE spricht direkt mit PipeWire. `System default` folgt dem Ausgang, den du in den Soundeinstellungen des Systems gewählt hast |
| Linux | JACK / PipeWire | Für einen laufenden JACK-Server oder PipeWire mit `pipewire-jack` |
| Linux | ALSA | Spricht direkt mit der Soundkarte. `default` oder `pipewire` teilen das Gerät, ein `hw:`-Gerät belegt die Karte allein für LYNX SHARE |

Unter Linux öffnet der erste Start das erste von PipeWire, JACK und ALSA, das Geräte hat. Eine gespeicherte Wahl bleibt erhalten.

## Die Einstellungen auf der Seite

| Einstellung | Was sie tut |
|---|---|
| `Treiber` | Der Treibertyp, aus der Tabelle oben |
| `Ausgabegerät` | Wo der Master abspielt |
| `Eingabegerät` | Die Quelle für Audioaufnahmen und für die Eingänge der Mixer-Inserts. Standardmäßig `(keine)` |
| `Samplerate` | Die Raten, die das Gerät anbietet |
| `Puffergröße` | Die Größen, die das Gerät anbietet, jeweils mit ihrer Latenz in Millisekunden. Kleinere Puffer reagieren beim Spielen schneller. Größere geben der CPU mehr Luft, mit weniger Knacksern |
| `Treibereinstellungen...` | Öffnet das eigene Fenster des Treibers (ASIO-Treiber) |
| `Testton abspielen` | Spielt einen Testton über das Ausgabegerät |
| `Eingangsaudio aufnehmen` | Nimmt die ersten beiden Eingangskanäle in einem neuen Insert auf, wenn kein Insert scharfgeschaltet ist |
| `Dateiformat` | Aufnahmen sind WAV 24-Bit oder WAV 32-Bit Float, mit der Samplerate des Geräts |
| `Round Trip messen` | Spielt 8 Klicks vom `Ping-Ausgang` zum `Ping-Eingang` über ein Kabel oder Loopback und misst die Verzögerung, damit Takes dort landen, wo du gespielt hast |
| `Feinabstimmung (Samples)` | Verschiebt jeden neuen Take. Positive Werte liegen in der Playlist früher |
| `Metronom-Spur` | Die Mixer-Spur, über die das Metronom läuft. `---` ist der Master. Der Klick ist nie in einem Export |
| `Abgeschnittene Noten beim Transport spielen` | Spielt Noten, die über die Startposition hinausgehen |
| `Plugins beim Transport zurücksetzen` | Löscht die Ausklänge der Effekte, wenn die Wiedergabe startet oder springt |

## Wenn kein Ton kommt

- Sieh zuerst auf `Status`. Ist kein Gerät geöffnet, wähle ein Ausgabegerät.
- Probiere einen anderen Treiber. Unter Windows lässt `Windows Audio (gemeinsam)` anderen Programmen ihren Ton, und `DirectSound (veraltet)` gibt es für den Fall, dass die anderen versagen.
- Prüfe den Master-Fader im [Mixer](/wiki/mixer/) und den Regler `MASTER` in der Werkzeugleiste.
- Im Pattern-Modus spielt nur das aktuelle Pattern. Ist es leer, hörst du nichts. Drücke `L` für den Song-Modus.
- Wenn der Ton unter Linux aussetzt, zeigt `Status`, ob der Audio-Thread Echtzeitpriorität bekommen hat. Die Lösung steht unter [Fehlerbehebung](/wiki/troubleshooting/).

## Gut zu wissen

- ASIO gibt es nur unter Windows.
- Mit `Windows Audio (gemeinsam)` ist der kleinste Puffer der Mischschritt des Systems. Der Hinweis unter dem Feld `Treiber` nennt den Wert für dein Gerät. Für niedrigere Latenz nimm den exklusiven Modus oder einen ASIO-Treiber.
- Mit PipeWire reicht der Puffer von 32 bis 2048 Samples und die Samplerate von 44,1 bis 192 kHz. Behält PipeWire seine eigene Rate, resampelt es den Stream von LYNX SHARE.
- Wird mit PipeWire ein benanntes Gerät abgesteckt, fällt LYNX SHARE auf `System default` zurück und behält die Puffergröße.
- `Status` zeigt auch die CPU-Last des Audios und zählt Aussetzer (Buffer Underruns).
- Du kannst die Treiber und Geräte auflisten, ohne das Fenster zu öffnen. Siehe `--devices` unter [Kommandozeile](/wiki/command-line/).

## Siehe auch

- [Einstellungen](/wiki/settings/)
- [Aufnahme](/wiki/recording/)
- [MIDI-Controller](/wiki/midi-controllers/)
- [Fehlerbehebung](/wiki/troubleshooting/)
