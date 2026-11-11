---
source: 27354bbca9
---
Es gibt zwei Wege, mit anderen an einem Projekt zu arbeiten, und beide stehen unter `Datei › Online-Session`. Ein Paket ist eine Datei, die du an einen Freund schickst, der einen Part zurückschickt. Es braucht kein Netzwerk, kein Konto und keinen Server. Eine Live-Session verbindet alle über das Internet mit dem LYNX-SHARE-Relay, und jede Änderung erreicht alle. Bis zu 4 Personen arbeiten an einem Projekt, du eingeschlossen.

## Ein Projekt an einen Freund schicken

![Projekt senden](/assets/img/wiki/session-send.webp)

1. Öffne das Projekt und wähle `Datei › Online-Session › Projekt senden...`.
2. Gib deinen Namen und den Namen deines Freundes ein.
3. Wähle, was dein Freund behält, wenn die Session endet: `Nur den eigenen Part (empfohlen)` oder `Eine vollständige Kopie des Projekts`.
4. Speichere die `.lynxpack`-Datei.
5. Schicke die Datei, wie du willst: per E-Mail, über einen gemeinsamen Ordner, per USB-Stick.

Das Paket enthält das Projekt, jedes Sample und jede Aufnahme, die es nutzt, sowie Audio-Renders („Platzhalter“) der Parts, die ein Plugin von Drittanbietern benutzen. Dein Freund hört diese Parts, ohne deine Plugins zu besitzen.

## Einen Part aufnehmen und zurückschicken

Der Freund macht Folgendes:

1. Öffne die `.lynxpack` mit `Datei › Öffnen...`. Eine Zusammenfassung zeigt, wer sie geschickt hat und welche Mixer-Inserts und Playlist-Spuren dir gehören. Klicke auf `Öffnen`.
2. Füge auf dein Inserts und Playlist-Spuren eigene Kanäle, Patterns und Audio hinzu. Das Material des Hosts bleibt unverändert.
3. Wähle `Datei › Online-Session › Meinen Part zurücksenden...` und schicke das neue Paket an den Host.

Jeder Gast bekommt eine Farbe, 20 Mixer-Inserts und 16 Playlist-Spuren.

## Einen Part zusammenführen

Der Host macht Folgendes:

1. Öffne das Projekt, das verschickt wurde.
2. Wähle `Datei › Online-Session › Part zusammenführen...` und such das Paket aus, das dein Freund zurückgeschickt hat.
3. Die Kanäle, Patterns, Noten, Clips und Inserts des Freundes werden in seiner Farbe hinzugefügt. An deinen Sachen ändert sich nichts.

Das Zusammenführen ist ein einziger Rückgängig-Schritt. Ein neuerer Part vom selben Freund ersetzt den früheren.

## Eine Live-Session starten oder ihr beitreten

![Live-Session starten](/assets/img/wiki/session-start.webp)

Der Host startet die Session:

1. Öffne das Projekt und wähle `Datei › Online-Session › Live-Session starten...`.
2. Gib deinen Namen ein.
3. Wähle unter `Gäste dürfen bearbeiten` entweder `Nur die eigenen Kanäle, Patterns, Inserts und Zeilen (empfohlen)` oder `Alles`. Wähle unter `Wenn die Session endet, behalten Gäste` ihren eigenen Part oder eine vollständige Kopie.
4. Klicke auf `Start`, kopiere dann den Einladungscode und schicke ihn an bis zu 3 Personen.
5. Wenn jemand beitreten möchte, klicke auf `Hereinlassen`.

Ein Gast tritt bei:

1. Wähle `Datei › Online-Session › Live-Session beitreten...`.
2. Gib deinen Namen ein, füge den Einladungscode ein und klicke auf `Beitreten`.
3. Warte, bis der Host dich hereinlässt. Das Projekt des Hosts öffnet sich.

## Während einer Live-Session

![Session-Chat](/assets/img/wiki/session-chat.webp)

Das Menü `Datei › Online-Session` hat diese Befehle, solange eine Session läuft:

| Befehl | Was er tut |
|---|---|
| `Session-Chat...` | Öffnet den Chat. Der Menüeintrag zeigt die Zahl ungelesener Nachrichten |
| `Dem Transport der Session-Leitung folgen` | Für Gäste: Wiedergabe, Stopp und Position folgen dem Host |
| `Personen und Rechte...` | Für den Host: was jede Person ändern darf und was jede am Ende behält |
| `Einladungscode anzeigen...` | Für den Host: zeigt den Code erneut, mit einem Button `Kopieren` |
| `Live-Session beenden` | Für den Host. Alle verlassen die Session, und der Code funktioniert nicht mehr |
| `Live-Session verlassen` | Für einen Gast. Dein Part des Projekts bleibt bei dir |

Unter `Personen und Rechte...` bekommt jede Person eine von vier Rollen: `Die eigenen Kanäle, Patterns, Inserts und Zeilen`, `Die eigenen Parts + Tempo und Projekteinstellungen`, `Alles` oder `Nur zuhören (keine Änderungen)`. Eine Berechtigung auszuschalten löscht nie etwas.

## Plugins, die die anderen nicht haben

Ein Kanal oder Insert, dessen Plugin auf deinem Computer nicht installiert ist, spielt einen Platzhalter und zeigt im Channel Rack und im Mixer ein bernsteinfarbenes Abzeichen `R`.

- In einem Paket sind die Platzhalter Audio-Renders, die beim Speichern des Pakets erstellt werden. Sie folgen Clips, die du verschiebst, aber nicht Änderungen an den Noten eines Plugins, das du nicht hast. Bitte dessen Besitzer um ein neues Paket.
- In einer Live-Session werden Parts, die ein Plugin nutzen, das jemand nicht hat, von der Person gerendert, die es hat, und als Audio an die anderen gestreamt.

## Was über das Netzwerk geht

Pakete sind Dateien. Nichts wird irgendwohin hochgeladen.

Eine Live-Session verbindet sich nur, solange du in einer bist. Alles läuft über das Relay `relay.lynx-share.com`, Ende-zu-Ende-verschlüsselt mit einem Schlüssel, der aus dem Einladungscode stammt. Das Relay gibt die Daten weiter, ohne sie lesen zu können. Es sieht deine IP-Adresse, wann du verbunden warst und wie viele Bytes durchgingen. Die anderen Personen in der Session sehen deinen Namen, deine Änderungen, die Samples, die dein Part nutzt, und ob du gerade abspielst oder aufnimmst. Deine IP-Adresse sehen sie nicht. Details stehen in der [Datenschutzerklärung](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Gut zu wissen

- `Nur den eigenen Part` ist eine Höflichkeit unter Musikern, kein Kopierschutz. Das Paket ist ein normales Zip.
- Beide Seiten sollten dieselbe Version von LYNX SHARE benutzen. Die Zusammenfassung des Pakets warnt, wenn sie abweichen.
- Ein Gast, dessen Verbindung abbricht, verbindet sich von selbst neu und behält seine Änderungen.
- Wenn LYNX SHARE beim Host während einer Session geschlossen wird, bietet der nächste Start an, sie fortzusetzen, und die Gäste verbinden sich von selbst neu.
- In einer Live-Session holen Rückgängig und Wiederholen nur deine eigenen Änderungen zurück.
- Jeder mit dem Einladungscode kann um Beitritt bitten, aber erst nachdem der Host ihn hereingelassen hat.

## Siehe auch

- [Was nur LYNX SHARE kann: Online-Sessions](/why/#online)
- [Dateiformate](/wiki/file-formats/)
- [Plugins](/wiki/plugins/)
- [Rückgängig und Verlauf](/wiki/undo-history/)
- [Fehlerbehebung](/wiki/troubleshooting/)
