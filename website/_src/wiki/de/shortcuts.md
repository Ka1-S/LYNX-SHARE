---
source: 98cfcfdbfe
---
![Das Fenster Tastenkürzel verwalten: eine durchsuchbare Liste von Aktionen mit ihren Tasten](/assets/img/wiki/shortcuts.webp)

Das sind die Standardtasten in 0.1.26 Alpha. `Hilfe › Tastenkürzel` (`F1`) zeigt die Tastenkürzel-Übersicht in der App. `Optionen › Tastenkürzel verwalten...` öffnet den Editor aus dem Bild, in dem du sie änderst.

> Unter macOS zeigt die App die Tasten mit Mac-Namen: Ctrl erscheint als ⌘ und Alt als ⌥.

## Ein Tastenkürzel ändern

1. Wähle `Optionen › Tastenkürzel verwalten...`.
2. Tippe im Suchfeld eine Aktion oder eine Taste ein, etwa `Ctrl+L`. Die Liste daneben beschränkt die Suche auf einen Ort, und `Nur geänderte` zeigt die, die du geändert hast.
3. Klicke auf die Aktion.
4. Klicke auf `Ändern`, um ihre Tasten zu ersetzen, oder auf `Tastenkürzel hinzufügen`, um ihr eine zweite zu geben, und drücke dann die neuen Tasten. `Esc` bricht ab.
5. Sind die Tasten am selben Ort schon belegt, nennt eine Meldung die andere Aktion und lässt dich die Tasten verschieben.

`Entfernen` löscht die Tasten der Aktion. `Auf Standard zurücksetzen` stellt eine Aktion wieder her und `Alle zurücksetzen...` jede. Deine Änderungen werden in `keys.json` im Einstellungsordner gespeichert (`%APPDATA%\LYNX` unter Windows, `~/Library/Application Support/LYNX` unter macOS, `~/.config/LYNX` unter Linux). `keys.json öffnen` öffnet diese Datei.

## Transport und Aufnahme

| Taste | Was sie bewirkt |
|---|---|
| `Space` | Wiedergabe, oder Stopp und zurück zum Startpunkt der Wiedergabe |
| `Ctrl+Space` | Wiedergabe oder Pause |
| `Shift+Space` | Wiedergabe, oder Stopp am Anfang des Songs oder Patterns |
| `L` | Zwischen Pattern-Modus und Song-Modus wechseln |
| `R` | Aufnahme ein oder aus |
| `Shift+R` | Jetzt Audio aufnehmen: schaltet den gewählten Insert scharf und nimmt auf |
| `Ctrl+M` | Metronom ein oder aus |
| `Ctrl+P` | Einzähler vor der Aufnahme ein oder aus |
| `Ctrl+I` | Aufnahme wartet auf die erste Note, ein oder aus |
| `Ctrl+B` | Aufgenommene Noten überblenden, ein oder aus |
| `Ctrl+E` | Schritteingabe ein oder aus |
| `Ctrl+H` | Ton stoppen (All Notes Off) |
| `Ctrl+T` | Schreibtastatur als Klavier ein oder aus |
| `Alt+Numpad *` / `Alt+Numpad /` | Zum nächsten oder vorherigen Song-Marker springen |

Einige dieser Tasten tun in einem Editor mit Fokus etwas anderes. Im Piano Roll importiert `Ctrl+M` eine MIDI-Datei und `Ctrl+B` dupliziert. In der Playlist ist `Ctrl+P` der Performance-Modus.

## Dateien und Bearbeiten

| Taste | Was sie bewirkt |
|---|---|
| `Ctrl+N` | Neues Projekt |
| `Ctrl+O` | Projekt öffnen |
| `Ctrl+S` | Speichern |
| `Ctrl+Shift+S` | Speichern unter |
| `Ctrl+Alt+S` | Neue Version speichern |
| `Ctrl+R` | Audio exportieren |
| `Ctrl+Shift+R` | Eine MP3-Datei exportieren |
| `Ctrl+Shift+M` | Eine MIDI-Datei exportieren |
| `Ctrl+Z` | Rückgängig. Noch einmal gedrückt, wiederholt es diesen Schritt |
| `Ctrl+Alt+Z` | Rückwärts rückgängig, Schritt für Schritt |
| `Ctrl+Y` oder `Ctrl+Shift+Z` | Wiederholen |
| `Ctrl+Alt+H` | Rückgängig-Verlauf |
| `Ctrl+F` | Den Browser durchsuchen |

## Fenster

| Taste | Was sie bewirkt |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Plugin-Auswahl |
| `Shift+F8` | Plugin-Datenbank (im Browser) |
| `Enter` | Die Playlist maximieren oder wiederherstellen |
| `F10` | MIDI-Einstellungen |
| `F11` | Projektinfo |
| `F12` | Alle Fenster schließen (Plugins, Kanaleinstellungen, Werkzeuge) |
| `Alt+F12` | Alle Plugin-Fenster schließen |
| `Ctrl+F12` | Alle Fenster außer dem fokussierten schließen |
| `Alt+F7` | Bildschirmtastatur |
| `Ctrl+F5` | Tempo-Tapper |
| `F1` | Tastenkürzel-Übersicht |

## Clip View und Device View

Diese Tasten funktionieren nur im Clip-Start-Arbeitsablauf.

| Taste | Was sie macht |
|---|---|
| `Ctrl+Alt+L` | Die Detailansicht zeigen oder ausblenden |
| `Ctrl+Alt+3` | Clip View |
| `Ctrl+Alt+4` | Device View |
| `Shift+Tab` | Zwischen Clip View und Device View wechseln |
| `Left` / `Right` | Device View: das vorherige, nächste Gerät wählen |
| `Del` or `Backspace` | Device View: den gewählten Effekt löschen |

## Patterns

| Taste | Was sie bewirkt |
|---|---|
| `Numpad +` / `Numpad -` | Nächstes, vorheriges Pattern |
| `Numpad 1` bis `Numpad 9` | Pattern 1 bis 9 wählen |
| `F4` | Nächstes leeres Pattern, und benennen |
| `Ctrl+F4` | Nächstes leeres Pattern, ohne Benennen |
| `Shift+F4` | Erstes leeres Pattern, und benennen |
| `F2` | Das Pattern umbenennen und färben |
| `Ctrl+Shift+C` | Das Pattern klonen |
| `Ctrl+Shift+Ins` | Ein leeres Pattern vor dem aktuellen einfügen |
| `Ctrl+Shift+Del` | Das Pattern löschen |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Das Pattern nach oben, unten verschieben |

## Channel Rack

| Taste | Was sie bewirkt |
|---|---|
| `Up` / `Down` | Den Kanal darüber, darunter wählen |
| `Shift+Up` / `Shift+Down` | Die Auswahl erweitern |
| `Alt+Up` / `Alt+Down` | Die gewählten Kanäle verschieben |
| `Ctrl+A` | Alle Kanäle wählen |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Die Steps der gewählten Kanäle ausschneiden, kopieren, einfügen |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Die Steps nach links, rechts rotieren |
| `Alt+C` | Die gewählten Kanäle klonen |
| `Alt+Del` | Die gewählten Kanäle löschen |
| `Alt+G` | Die gewählten Kanäle gruppieren |
| `Ctrl+L` | Die gewählten Kanäle auf freie Mixer-Inserts schicken |
| `Ctrl+K` | Step-Graph-Spuren ein oder aus |
| `1` bis `0` | Die Kanäle 1 bis 10 stumm- oder wieder laut schalten |
| `Ctrl+1` bis `Ctrl+0` | Die Kanäle 1 bis 10 solo schalten |

## Piano Roll

| Taste | Was sie bewirkt |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Stift, Pinsel, Drum-Pinsel, Radierer, Stumm, Messer, Lasso, Zoom, Abhören |
| `S` | Slide-Noten ein oder aus (für neue Noten) |
| `G` / `K` | Vorheriger, nächster Kanal |
| `H` / `J` | Vorheriger, nächster Kanal mit Noten |
| `Ctrl+A` | Alles auswählen |
| `Ctrl+D` oder `Esc` | Auswahl aufheben |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopieren, Ausschneiden, Einfügen |
| `Ctrl+B` | Duplizieren |
| `Del` | Die Auswahl löschen |
| `Up` / `Down` | Die Auswahl um einen Halbton verschieben |
| `Ctrl+Up` / `Ctrl+Down` | Die Auswahl um eine Oktave verschieben |
| `Left` / `Right` | Die Auswahl um einen Rasterschritt verschieben |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Die Noten innerhalb des Takts rotieren |
| `Backspace` | Raster aus oder ein |
| `Ctrl+Q` | Schnell quantisieren (Anfänge und Längen) |
| `Shift+Q` | Starts quantisieren |
| `Ctrl+U` | Ans Raster zerteilen |
| `Ctrl+G` | Aneinanderstoßende Noten verbinden |
| `Ctrl+L` | Legato |
| `Shift+D` | Die Noten aufs Raster verkürzen |
| `Ctrl+P` | Porta für die Auswahl ein oder aus |
| `Alt+M` / `Shift+Alt+M` | Die Auswahl stumm-, wieder laut schalten |
| `Alt+V` | Ghost-Notes ein oder aus |
| `Shift+I` | Die Auswahl umkehren |
| `Shift+G` / `Alt+G` | Die gewählten Noten gruppieren, Gruppierung aufheben |
| `Alt+C` | Die gewählten Noten mit der Notenfarbe färben |
| `Shift+C` | Die Noten derselben Farbe wählen |
| `Shift+N` | Akkord-Stempel: keiner (Einzelnoten) |
| `Ctrl+Ins` / `Ctrl+Del` | Platz (die Zeitauswahl) einfügen, löschen |
| `Ctrl+M` | Eine MIDI-Datei importieren |
| `Ctrl+Shift+M` | Als MIDI-Datei exportieren |
| `PageUp` / `PageDown` | Hinein-, herauszoomen |
| `Shift+1` / `Shift+2` / `Shift+3` | Auf 1, 4, 16 Takte zoomen |
| `Shift+4` | Zoomen, bis alles zu sehen ist |
| `Shift+5` | Auf die Auswahl zoomen |
| `Shift+0` | Die Ansicht auf den Abspielkopf zentrieren |

### Notenwerkzeuge

Jede Taste öffnet im Piano Roll den Dialog eines Notenwerkzeugs.

| Taste | Was sie bewirkt |
|---|---|
| `Alt+A` | Arpeggieren |
| `Alt+S` | Strum |
| `Alt+Y` | Spiegeln |
| `Alt+R` | Zufällig |
| `Alt+X` | Werte skalieren |
| `Alt+K` | Limit to a range |
| `Alt+Q` | Quantisieren |
| `Alt+U` | Zerhacken |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Artikulieren |
| `Alt+E` | Riff machine |
| `Alt+P` | Akkordfolge erzeugen |
| `Ctrl+Alt+Y` | Das letzte Skript erneut ausführen |

## Playlist

| Taste | Was sie bewirkt |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Stift, Pinsel, Radierer, Stumm, Messer, Lasso, Zoom, Abhören, Slip |
| `Ctrl+A` | Alles auswählen |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopieren, Ausschneiden, Einfügen |
| `Ctrl+B` | Duplizieren |
| `Del` | Die gewählten Clips löschen |
| `Shift+Del` | Die gewählten Clips löschen und die Lücken schließen |
| `Shift+Left` / `Shift+Right` | Die gewählten Clips um einen Raster-Schritt nach links, rechts verschieben |
| `Shift+Up` / `Shift+Down` | Die gewählten Clips um eine Spur nach oben, unten verschieben |
| `Alt+Left` / `Alt+Right` | Die gewählten Clips in einem feinen Schritt nach links, rechts nudgen, ohne Raster. `Shift+Alt` tut dasselbe |
| `Ctrl+D` | Clips und Zeit abwählen |
| `Shift+I` | Die Auswahl umkehren |
| `Shift+C` | Die Clips der gewählten Quelle auswählen |
| `Shift+S` | Ripple-Bearbeitung ein oder aus |
| `Backspace` | Raster aus oder ein |
| `Alt+M` / `Shift+Alt+M` | Die gewählten Clips stumm-, wieder laut schalten |
| `Shift+M` | Audio beim Ändern der Größe von Audioclips dehnen, ein oder aus |
| `Ctrl+Shift+V` | Die gewählten Clips einzigartig machen |
| `Ctrl+G` | Pattern-Clips zusammenführen |
| `Ctrl+Shift+G` | Ähnliche Pattern-Clips zusammenführen |
| `Ctrl+Alt+G` | Automationsclips zusammenführen |
| `Ctrl+Shift+B` | Die gewählten Playlist-Spuren klonen |
| `Ctrl+Q` | Die Anfänge der Clips quantisieren |
| `7` / `8` | Die gewählten Audioclips um einen Halbton nach unten, oben transponieren |
| `9` | Die gewählten Audioclips umkehren |
| `Ctrl+Ins` / `Ctrl+Del` | Die Länge der Zeitauswahl auf allen Spuren einfügen, entfernen |
| `Ctrl+Alt+Ins` | Die Clips teilen und Platz einfügen |
| `Ctrl+Alt+Home` | Clips von links in der Größe ändern, an oder aus |
| `Ctrl+Left` / `Ctrl+Right` | Die vorherige, nächste Zeit wählen |
| `Ctrl+Enter` | Zeit um die Auswahl herum wählen |
| `Alt+T` | Einen Zeitmarker hinzufügen |
| `Ctrl+T` | Automatisch einen Marker hinzufügen |
| `Shift+Alt+T` | Einen Taktartwechsel hinzufügen |
| `Shift+T` | Loop-Marker setzen |
| `Ctrl+Alt+C` | Die Playlist-Auswahl konsolidieren |
| `Ctrl+P` | Performance-Modus ein oder aus |
| `PageUp` / `PageDown` | Hinein-, herauszoomen |
| `Shift+1` / `Shift+2` / `Shift+3` | Auf 1, 4, 16 Takte zoomen |
| `Shift+4` | Zoomen, bis alles zu sehen ist |
| `Shift+5` | Auf die Auswahl zoomen |
| `Shift+0` | Die Ansicht auf den Abspielkopf zentrieren |

## Mixer

| Taste | Was sie bewirkt |
|---|---|
| `Alt+Left` / `Alt+Right` | Die gewählten Inserts nach links, rechts verschieben |
| `F2` | Den gewählten Insert umbenennen und färben |
| `S` | Die gewählten Inserts solo schalten |
| `Alt+S` | Die gewählten Inserts samt der Inserts solo schalten, die sie speisen |
| `Ctrl+L` | Die gewählten Kanäle mit dem gewählten Insert verknüpfen |
| `Ctrl+Shift+L` | Die gewählten Kanäle mit Inserts verknüpfen, beginnend beim gewählten |
| `Alt+L` | Die mit dem gewählten Insert verknüpften Kanäle wählen |
| `Del` | Die gewählten Inserts löschen. LYNX SHARE fragt vorher nach |
| `Ctrl+E` | Den Audio-Editor öffnen |
| `Shift+E` | Den Audio-Editor öffnen und aufnehmen |
| `Alt+R` | Die scharfgeschalteten Inserts in Wave-Dateien rendern |
| `Shift+Alt+R` | Die gewählten Inserts in Wave-Dateien rendern |

## Browser

| Taste | Was sie bewirkt |
|---|---|
| `Up` / `Down` | Vorherige, nächste Datei (mit Vorschau) |
| `Shift+Up` / `Shift+Down` | Vorherige, nächste Datei in den gewählten Kanal |
| `Enter` | Die Datei in den gewählten Kanal laden |
| `Left` | Den Ordner schließen oder zum übergeordneten Ordner gehen |
| `Right` | Den Ordner öffnen |
| `F2` | Das gewählte Plugin umbenennen (Plugin-Datenbank) |

## Gut zu wissen

- Solange das Tastaturklavier der Schreibtastatur an ist, spielen seine Tasten Noten, und die meisten Ein-Buchstaben-Kürzel ruhen (`L` funktioniert weiterhin). Schalte es mit `Ctrl+T` aus.
- Ein Tastenkürzel gilt entweder überall oder in einem Fenster. Eines, das zu einem Fenster gehört, hat Vorrang vor denselben Tasten, die überall gelten, solange dieses Fenster den Fokus hat.
- Einige Aktionen haben keine Standardtaste, zum Beispiel die Schnellauswahl. Gib ihnen im Editor eine.
- Wenn in der Playlist kein Clip gewählt ist, bewegen die Tasten, die Clips verschieben, jeden Clip. Der feine Schritt von `Alt+Left` und `Alt+Right` ist ein Pixel bei der aktuellen Zoomstufe.
- Die Tastenkürzel von LYNX SHARE funktionieren auch, während ein Plugin-Fenster den Fokus hat, zum Beispiel `Space`. Einfache Buchstaben und Ziffern gehen zuerst an das Plugin, und `Esc` schließt sein Fenster.

## Verwandt

- [Rundgang durch das Fenster](/wiki/tour/)
- [Einstellungen](/wiki/settings/)
- [MIDI-Controller](/wiki/midi-controllers/)
- [Umstieg von FL Studio](/wiki/coming-from-fl-studio/)
