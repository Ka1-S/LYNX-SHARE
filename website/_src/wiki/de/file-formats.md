---
source: 98a5eb9945
---
LYNX SHARE speichert Projekte als `.lynx`-Dateien. Es öffnet auch Projekte anderer Programme, lädt die gängigen Audiodateien und exportiert Audio, MIDI und Projekte für andere Programme. Die Tabelle listet jedes Format von 0.1.30 alpha.

Ein Projekt, das aus dem Format eines anderen Programms geöffnet wurde, ist ein Import. `Datei › Speichern` fragt dann, wohin die `.lynx`-Datei geschrieben werden soll, und die Originaldatei wird nie verändert.

## Ein Projekt aus einem anderen Programm öffnen

1. Wähle `Datei › Öffnen...` (`Ctrl+O`).
2. Such eine `.flp`-, `.als`-, `.flm`-, `.dawproject`-, `.band`-, `.blx`-, `.mid`- oder `.zip`-Datei aus.
3. Wähle `Hilfe › Letzter Projekt-Importbericht`, um zu lesen, was angenähert oder weggelassen wurde.
4. Wähle `Datei › Speichern` und such aus, wohin die `.lynx`-Datei geschrieben wird.

## Formate

| Format | Öffnen | Speichern / Exportieren | Hinweise |
|---|---|---|---|
| `.lynx` | `Datei › Öffnen...` | `Datei › Speichern`, mit Autosave | Das eigene Projektformat von LYNX SHARE (JSON) |
| `.zip` | `Datei › Öffnen...` entpackt es in einen Ordner und öffnet das Projekt darin | `Datei › Exportieren › Projekt mit Samples (.zip)...` | Öffnet auch das gezippte Loop-Paket von FL Studio |
| `.lynxpack` | Öffnet sich nach einer Zusammenfassung. Ein Part wird in das Projekt des Hosts eingefügt | `Datei › Online-Session › Projekt senden...` | Ein Projekt oder ein Part, der in einer [Online-Session](/wiki/online-sessions/) gesendet wurde. Parts mit Plugins von Drittanbietern reisen als Audio-Renders plus Plugin-Einstellungen |
| `.flp` | `Datei › Öffnen...` | `Datei › Exportieren › FL Studio-Projekt...`, ein FL-Studio-2026-Projekt oder ein gezipptes Loop-Paket | Native Plugins laufen auf nachgebauten Engines unterschiedlicher Genauigkeit. FLEX-Packs werden nie geöffnet. Beim Export werden Instrumente und Effekte nur für LYNX in Audio gerendert oder durch das nächstliegende FL-Plugin ersetzt, und Modulatoren werden weggelassen. Siehe [Umstieg von FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Datei › Öffnen...` | Nein | FL-Studio-Mobile-Projekt. Unvollständig: Andere Instrumente als MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, der Slicer und DirectWave spielen den eingebauten Synth |
| `.als` | `Datei › Öffnen...`, Sets von Live 11 und 12 | `Datei › Exportieren › Ableton-Live-Set (.als)...`, für Live 12 oder Live 11 | Beim Import entfallen Devices von Live ohne Entsprechung in LYNX SHARE. Beim Export werden die eigenen Instrumente und Effekte von LYNX SHARE nicht umgewandelt, und Automation von Effektparametern sowie Routen von Insert zu Insert entfallen. Siehe [Umstieg von Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `Datei › Öffnen...` | `Datei › Exportieren › DAWproject (.dawproject)...` | Das offene Austauschformat von Bitwig, Studio One, Cubase und anderen Programmen: Spuren, Ordner, Clips, Noten, Audio mit seinen Warp-Markern, Automation, Tempo, Marker, der Mixer mit Sends und VST3-, VST2- und CLAP-Plugins mit ihrem Zustand. Notenmodulationen und MIDI-Expressions entfallen beim Import; das Clip-Raster, andere Arrangements und einige Details pro Note beim Export. In beiden Richtungen wird aufgelistet, was entfallen ist |
| `.band` | `Datei › Öffnen...` (das Paket, seine `projectData`-Datei oder ein Zip davon) | Nein | GarageBand-10- oder GarageBand-für-iOS-Projekt, als Spuren im Einfachen Arbeitsablauf. Unvollständig: Instrumentenspuren spielen den eingebauten Synth und die Drums, außer Sampler-Instrumenten, die in GarageBand für iOS gemacht wurden. Projekte aus GarageBand 11 und älter sind nicht lesbar |
| `.blx` | `Datei › Öffnen...`, oder sein Ordner mit dem Import `Ordner...` des Einfachen Arbeitsablaufs | Nein | BandLab-Projekt, das von Cakewalk oder Sonar heruntergeladen wurde, als Spuren im Einfachen Arbeitsablauf mit Regionen, Tempo, Tonart, dem Mix, Lautstärke- und Pan-Automation und den gängigen Effekten. Unvollständig: Die Instrumentenklänge und andere Pedale von BandLab stehen im Importbericht |
| `.mid`, `.midi` | `Datei › Öffnen...` erzeugt ein neues Projekt. `Datei › Importieren › MIDI-Datei...` und das Piano Roll importieren ins aktuelle | `Datei › Exportieren › MIDI-Datei...` (`Ctrl+Shift+M`) | |
| `.wav` | Als Sample oder Audioclip | `Datei › Exportieren › Wave-Datei...`, 16-Bit, 24-Bit oder 32-Bit Float | Optional Dither, Tempoinformation, Loop-Marker und Playlist-Marker |
| `.mp3` | Als Sample oder Audioclip | `Datei › Exportieren › MP3-Datei...` | Konstante Bitrate |
| `.ogg` | Als Sample oder Audioclip | `Datei › Exportieren › OGG-Datei...` | |
| `.flac` | Als Sample oder Audioclip | `Datei › Exportieren › FLAC-Datei...`, Kompression 0 bis 8 | Auf jeder Stufe verlustfrei |
| `.aif`, `.aiff` | Als Sample oder Audioclip | `Datei › Exportieren › AIFF-Datei...`, 16-Bit oder 24-Bit | |
| `.m4a` | – | `Datei › Exportieren › M4A-Datei...` | Nur Export. AAC mit dem Encoder des Systems unter Windows und macOS; unter Linux nicht angeboten |
| `.wv` | Als Sample | Nein | WavPack-Samples, wie sie in den Packs von FL Studio mitkommen |
| `.sf2`, `.sf3` | Wird von LYNX Soundfont gespielt | Nein | SoundFonts |
| `.dwp`, `.dwb` | Doppelklick im Browser, ein Ablegen auf dem Channel Rack oder `Programm / Bank öffnen...` von DirectWave | Speichern von Programm und Bank in DirectWave | DirectWave-Programme und -Bänke. Die Samples werden dort gesucht, wo DirectWave sie sucht |
| `.gig` | Als DirectWave-Kanal | Nein | GigaStudio-Instrumente: Velocity-Layer, Round Robin, Loops, Stimmung und die Amp-Hüllkurve. Release-Trigger-Samples, Keyswitches und komprimierte Samples werden noch nicht gelesen |
| `.ncw` | Als Sample oder Audioclip | Nein | Die komprimierten Samples von Kontakt. Kontakt-`.nki`-Instrumente brauchen Kontakt selbst als Plugin; geschützte Bibliotheken werden nie entschlüsselt |
| `.fst` | Aus dem Browser | Nein | Preset eines nativen FL-Studio-Plugins. Nur für die Plugins, für die LYNX SHARE eine Engine hat |
| `.fsc` | Piano Roll und Event-Editor | Piano Roll und Event-Editor | Score- und Automationsdatei von FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Preset-Datei laden...` im Preset-Menü eines gehosteten Plugins | `Preset speichern...` und `Preset-Datei exportieren...` | Presets von Plugins von Drittanbietern. `.fxp` und `.fxb` gelten für VST2-Plugins und `.vstpreset` für VST3-Plugins, und nur solange das Plugin in LYNX SHARE läuft |
| Videodatei | `Datei › Importieren › Videodatei...` legt ein Video in die Playlist | `Auch ein Video mit diesem Mix schreiben` im Export-Fenster | Der Import nutzt unter Windows und macOS die Decoder des Systems und braucht unter Linux FFmpeg. Der Export braucht FFmpeg |

## Weitere Exporte

| Befehl | Was er tut |
|---|---|
| Option `Dateien` des Export-Fensters | Stems: eine Datei pro Mixer-Insert, pro Kanal oder pro Kanal nur mit Instrumenten |
| `Datei › Exportieren › Alle Playlist-Spuren` | Eine Datei pro Playlist-Spur |
| `Datei › Exportieren › Alle Playlist-Spurgruppen` | Eine Datei pro Spurgruppe |
| `Datei › Exportieren › Mastern...` | Rendert das Projekt und mastert es auf einen Lautheitswert, auf deinem Computer |
| `Datei › Exportieren › Projektknochen...` | Schreibt Presets und Scores von Kanälen und Effekten in einen Ordner |

Das Export-Fenster rendert den Song, das aktuelle Pattern oder die Auswahl in der Playlist.

## Von der Kommandozeile

Unter Windows heißt das Programm `LYNX.exe`. Unter macOS und Linux heißt es `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` und `--export-flp` nehmen als Eingabe auch eine `.als`.

## Gut zu wissen

- Audiodateien über 30 Sekunden werden aus einem Festplatten-Cache gestreamt.
- Eine `.flp`, `.als`, `.flm`, `.dawproject`, `.band` oder `.blx` wird beim Speichern nie geschrieben. Die Exportbefehle schreiben neue Dateien.
- `.wv`-Dateien werden nur gelesen.
- Eine `.zip` wird zuerst in einen Ordner entpackt, dann wird das Projekt darin geöffnet.

## Siehe auch

- [Projekte importieren](/wiki/importing-projects/)
- [Export](/wiki/exporting/)
- [Kommandozeile](/wiki/command-line/)
- [Umstieg von FL Studio](/wiki/coming-from-fl-studio/)
- [Umstieg von Ableton Live](/wiki/coming-from-ableton/)
