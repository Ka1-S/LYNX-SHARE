---
source: ab2c2f14eb
---
![Die Seite Allgemein des Einstellungsfensters, mit den Seitenreitern oben](/assets/img/wiki/settings-general.webp)

Das Einstellungsfenster hat acht Seiten: Audio, MIDI, Allgemein, Theme, Dateien, LYNX extras, Projekt und Info. Die ersten sechs gehören zum Programm. Projekt und Info gehören zum geöffneten Projekt und werden mit ihm gespeichert. Jede Seite hat oben im Menü `Optionen` einen eigenen Eintrag.

## Eine Einstellung ändern

1. Öffne das Menü `Optionen` und wähle eine Seite, zum Beispiel `Allgemeine Einstellungen...`.
2. Klicke oben auf einen anderen Reiter, um zu einer anderen Seite zu wechseln.
3. Ändere die Einstellung. Sie gilt sofort, außer Sprache und Oberflächenschrift, die erst nach einem Neustart von LYNX SHARE gelten.

`F10` öffnet die MIDI-Seite und `F11` die Info-Seite.

## Audio

`Optionen › Audioeinstellungen...`. Treiber, Geräte, Samplerate, Puffergröße und Aufnahme. [Audio einrichten](/wiki/audio-setup/) beschreibt diese Seite. Unter `Aufnahme` bietet `Dateiformat` jetzt auch `WAV 16 Bit (die Abtastrate des Geräts)` an.

## MIDI

![Die MIDI-Seite: Eingabegeräte, Ausgabegeräte mit Master-Sync und externer Sync](/assets/img/wiki/settings-midi.webp)

`Optionen › MIDI-Einstellungen...`

| Einstellung | Was sie tut |
|---|---|
| Eingabegeräte | Jedes Gerät lässt sich ausschalten, kann nur auf einen MIDI-Kanal hören und statt der gewählten einen festen Kanal des Channel Rack spielen |
| `Controller-Typ` | Für jedes Eingabegerät: `Automatisch erkennen`, `Generischer Controller` oder ein Controller-Profil, etwa Mackie Control. Die Zeile daneben nennt den Port, der die Lichter und das Display bekommt |
| `MPE-Controller` | Am Ende der MIDI-Kanalliste eines Geräts. Jede Note kommt mit eigenem Bend, Timbre und Druck. Ein zweites Feld legt die Member-Kanäle fest |
| `Geräteliste aktualisieren` | Sucht erneut nach Geräten und liest deine Controller-Profile neu ein |
| `Ordner für Controller-Profile` | Öffnet den Ordner für deine eigenen Controller-Profile (`*.lynxctl`) |
| `Master-Sync senden` | Sendet MIDI-Clock mit Song Position Pointer oder MIDI Time Code an ein Ausgabegerät. `Versatz (ms)` sendet später oder früher |
| `All Notes Off senden` | Sendet beim Initialisieren und nach einem Export All Notes Off an das Gerät |
| `Sync zu externer Clock` | LYNX SHARE folgt der MIDI-Clock des gewählten Eingangs: Tempo, Start, Continue, Stopp und Position |
| `Velocity` | Für Noten von MIDI-Geräten: wie gespielt, weich, hart oder fest |
| `Kanal des Performance-Modus` | Noten auf diesem Kanal starten im Performance-Modus Playlist-Clips |
| `Song-Marker-Sprung` | Noten auf diesem Kanal springen im Song zu einem Marker, statt zu spielen |
| `Pickup (Übernahmemodus)` | Ein Controller bewegt einen Parameter erst, wenn er dessen aktuellen Wert erreicht hat |
| `Fußpedal steuert Note-Off` | Standardmäßig an: Das Sustain- und das Sostenuto-Pedal halten Noten. Aus, geht das Pedal an die Plugins |
| `Gamecontroller verwenden` | Ein Gamecontroller steuert den Transport und lässt sich wie ein MIDI-Controller verknüpfen. Standardmäßig aus |
| Verknüpfungen | Die Controller-Verknüpfungen dieses Projekts, dann die globalen, mit `Verknüpfung entfernen` und `Alle entfernen` |

## Allgemein

`Optionen › Allgemeine Einstellungen...`

| Einstellung | Was sie tut |
|---|---|
| `Layout-Stil` | Pattern-Arbeitsablauf (Standard), Clip-Start-Arbeitsablauf oder einfacher Arbeitsablauf |
| `Standardvorlage`, `Projekt beim Start` | Die Vorlage, die mit `Datei › Neu` öffnet, und was beim Start öffnet: `Leeres Projekt`, `Standardvorlage` oder `Zuletzt verwendetes Projekt`. Mit `Datei › Als Vorlage speichern...` fügst du eigene hinzu |
| `Session-Clips aus Live-Sets` | Wohin die Session-Clips eines importierten Live-Sets gehen: ins Clip-Raster oder in die Playlist nach dem Arrangement |
| `Sprache`, `Oberflächen-Schriftart` | Gelten ab dem nächsten Start |
| `Oberflächengröße`, `Menüdichte` | Maßstab der ganzen Oberfläche sowie kompakte oder großzügige Menüs |
| `Wellenformen` | Schlicht (die Clipfarbe) oder Spektralfarben, wahlweise mit getrennten Stereokanälen |
| `Notennamen`, `Schwarze Tasten` | Englische, germanische oder Solmisationsnamen, mit Kreuzen oder Bs |
| `Plugin-Stil` | Das Aussehen der eingebauten Plugin-Fenster: automatisch oder ein Stil für alle Plugins |
| `Animationen`, `Animationsrate` | Animationen der Oberfläche und wie oft sich Abspielcursor und Pegelanzeigen bewegen |
| `Mit der GPU zeichnen (Direct2D)` | Schalte es aus, wenn der Grafiktreiber abstürzt oder der Bildschirm flackert |
| Start | Startbildschirm, Startklang und das Suchen nach neuen Plugins im Hintergrund bei jedem Start |
| Bearbeiten | Tastatur-Klavier, Kanal-Editoren offen halten, die Suchzeit merken, gedehnte Pattern-Clips loopen |
| `Alle neuen Plugins ablösen` | An: Das Fenster eines neu hinzugefügten Plugins öffnet sich abgelöst, über den Panels. Aus (Standard): Es öffnet sich angedockt im Hauptfenster |
| `Monophones Malen in der Piano Roll` | An: Das Werkzeug Zeichnen ersetzt die Noten unter jeder neuen Note, und Shift malt darüber. Aus: umgekehrt |
| `Verknüpfte Module automatisch auswählen` | Wenn ein Plugin-Fenster den Fokus bekommt, wird seine Mixer-Spur gewählt. Standardmäßig an |
| `Vorrang der Schreibmaschinentastatur standardmäßig deaktivieren` | Für Plugins, die du ab dann hinzufügst. Standardmäßig aus |
| `Maximale Rückgängig-Schritte` | 25 bis 1000 |
| `Meldungen verwalten` | Unter `Hinweise und Warnungen`. Angehakte Meldungen werden gezeigt. Nimm bei einer den Haken weg, um ihre Nachfrage zu überspringen |
| Sicherung | Wie oft eine Sicherungskopie gespeichert wird, wie viele pro Projekt behalten werden, und `Sicherungsordner öffnen` |
| `Plugins in einem separaten Prozess ausführen` | Ein abstürzendes Plugin reißt LYNX SHARE nicht mit. Standardmäßig aus |
| `Fehlende Plugins durch LYNX-SHARE-Entsprechungen ersetzen` | Standardmäßig an. Nutzt ein Projekt ein Fremd-Plugin, das nicht installiert ist, lädt der Ersatz von LYNX SHARE mit passenden Einstellungen, wo es einen gibt. Das Projekt behält das Original-Plugin und nutzt es wieder, sobald es installiert ist |
| `Anzahl offener Plugin-Editoren begrenzen` | Höchstens 25 Plugin-Editor-Fenster bleiben offen. Standardmäßig an |
| `Absturzberichte` | Nach einem Absturz oder Hänger fragen (Standard), immer senden oder nie senden |
| `Automatisch nach Updates suchen` | Die Update-Prüfung beim Start, mit `Jetzt suchen` |

## Theme

`Optionen › Theme (Farben, Hintergrund)...`

| Einstellung | Was sie tut |
|---|---|
| Theme-Feld | Wählt ein Preset. `Importieren...` und `Exportieren...` lesen und schreiben `.lynxtheme`-Dateien, `Preset speichern...` behält das aktuelle Theme, `Standard` setzt es zurück |
| `Akzentfarbe`, `Hintergrund`, `Arbeitsbereich` | Die drei Farben eines Themes. Die Arbeitsbereichsfarbe kann automatisch dem Hintergrund folgen |
| Arbeitsbereichsbild | Ein PNG, JPEG oder GIF hinter den Fenstern, mit `Bildanpassung`, `Bilddeckkraft` und `Abdunkeln` |
| Startklang | Dein eigener Klang von 30 Sekunden oder weniger, oder der von LYNX SHARE |

## Dateien

`Optionen › Dateieinstellungen (Suchordner)...`

| Einstellung | Was sie tut |
|---|---|
| Suchordner für fehlende Samples | Das Fenster Fehlende Dateien sucht von selbst in diesen Ordnern und ihren Unterordnern |
| Browser-Ordner | Werden in der Ordnerliste des Browsers gezeigt und ebenfalls nach fehlenden Samples durchsucht |
| Plugin-Ordner | Werden von `Optionen › Nach Plugins suchen` nach Plugins durchsucht |

## LYNX extras

`Optionen › LYNX-Extras...`. Die eigenen Zusätze von LYNX SHARE an einem Ort. Alle sind standardmäßig aus. Jeder Schalter ist dieselbe Einstellung wie im Menü oder auf der Seite, die seine Zeile nennt.

| Einstellung | Was sie macht |
|---|---|
| `Lautheitsanzeige neben dem Master (LYNX)` | Short-Term- und Integrated-Lautheit (LUFS) und True Peak des Masters, mit einem Reset-Button |
| `Nur benutzte Inserts anzeigen` | Blendet die Mixer-Inserts aus, die nichts nutzt |
| `Verbindungen zu gelöschten Inserts grau behalten (LYNX)` | Beim Löschen eines Inserts bleiben die Verbindungen, die in es gingen, erhalten, sodass sie wieder verbunden werden können |
| `Mixer-Gruppen folgen der Playlist (LYNX)` | Die Gruppen des Mixers spiegeln die Spurgruppen der Playlist |
| `Automationsspuren` | Automation kann in Spuren unter einer Playlist-Spur liegen |
| `Lineal: Song-Takte (LYNX)` | Das Lineal des Piano Roll zeigt die Taktnummern der Stelle im Song, an der das Pattern spielt |
| `Den Wert eines Reglers beim Überfahren neben der Maus anzeigen (LYNX)` | Der Wert erscheint neben dem Mauszeiger, nicht nur in der Hinweisleiste |
| `Nach der Loop-Aufnahme nur den letzten Durchgang abspielen` | Die früheren Durchgänge bleiben darunter, stummgeschaltet |
| `W blendet das Fenster des ausgewählten Geräts ein oder aus` | Eine Taste für die Device View |
| `Ein ausgewähltes Gerät wählt seinen Insert im Mixer aus` | Der Mixer folgt dem Gerät, das in der Device View gewählt ist |
| `Gamecontroller verwenden` | Derselbe Schalter wie auf der MIDI-Seite |

## Projekt

![Die Projekt-Seite: Zeiteinstellungen, Groove und Stimmung, Zeitbasis und Playlist-Optionen](/assets/img/wiki/settings-project.webp)

`Optionen › Projekteinstellungen (Tempo, Taktart)...`

| Einstellung | Was sie tut |
|---|---|
| `Zeiteinstellungen` | Zeitteilung (Schläge pro Takt, Steps pro Schlag) oder eine klassische Taktart (Zähler und Nenner) |
| `Tempo`, `Schläge pro Takt` | Das Tempo des Projekts und die Schläge in einem Takt |
| `Swing`, `Master-Tonhöhe` | Der globale Swing-Anteil und die Master-Tonhöhe in Cent |
| `Automatische Crossfades` | Sich überlappende Audioclips auf einer Spur blenden über die Überlappung über |
| `Comp-Überblendung` | Die Länge der Überblendung beim Comping von Takes, in Millisekunden |
| `Abgeschnittene Noten in Clips spielen` | Eine Note, die vor einem Pattern-Clip beginnt, spielt ab dem Clipanfang |
| `Schnelles Declick für Cut-Gruppen` | Schnelleres Ausblenden, wenn Cut-Gruppen Noten abschneiden |
| `Panorama-Gesetz` | Kreisförmig (Mitte bei 0 dB, +3 dB auf der Seite, zu der gepant wird) oder Dreieckig (die lautere Seite behält ihren Pegel). Für Mixer-Inserts und Sampler-Kanäle |

## Info

`Optionen › Projektinfo...`. Titel, Autor, Genre, Weblink und Kommentare des Projekts sowie `Diese Seite beim Öffnen des Projekts anzeigen`. Unten stehen der Zeitpunkt der Erstellung des Projekts und die Arbeitszeit.

## Gut zu wissen

- Die Zeitbasis ist fest auf 960 Ticks pro Viertelnote. Projekte aus FL Studio werden darauf umgerechnet.
- Taktartwechsel im Verlauf des Songs stehen nicht auf der Projekt-Seite. Füge einen Taktart-Marker auf dem Lineal der Playlist hinzu.
- `Plugins in einem separaten Prozess ausführen` gilt für Plugins, die ab dann geöffnet werden. Öffne das Projekt neu, um die anderen umzuziehen.
- Die Update-Einstellungen fehlen in der Version aus dem Microsoft Store, weil der Store sie aktualisiert.
- Tastenkürzel stehen unter `Optionen › Tastenkürzel verwalten...`, nicht auf einer Einstellungsseite.
- Manche Schalter sind in Menüs, nicht in diesem Fenster. `Niedrige Latenz bei der Aufnahme` steht im Aufnahme-Optionsmenü neben dem Aufnahme-Button und im Menü `Aufnahme auf Festplatte` des Mixers. `Klaviatur-Stil` steht im Menü View des Piano Roll.
- Die Plugin database ist ein Ordner, `Plugin-Datenbank` im Ordner `LYNX` deiner Dokumente. Du kannst sie im Dateimanager ordnen.

## Siehe auch

- [Audio einrichten](/wiki/audio-setup/)
- [MIDI-Controller](/wiki/midi-controllers/)
- [Tastenkürzel](/wiki/shortcuts/)
- [Plugins](/wiki/plugins/)
- [Layouts](/wiki/layouts/)
