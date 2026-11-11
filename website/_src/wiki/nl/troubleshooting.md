---
source: a3748d4a2a
---
Elke kop hieronder is één probleem. Staat het jouwe er niet bij, meld het dan zoals aan het einde beschreven.

## Het programma starten

### Windows: SmartScreen waarschuwt bij de eerste start

Het uitvoerbare bestand is nog niet ondertekend, dus SmartScreen kan waarschuwen de eerste keer dat je het uitvoert.

1. Klik op `More info`.
2. Klik op `Run anyway`.

Het installatieprogramma vraagt geen beheerdersrechten.

### macOS: "kan niet worden geopend"

De app is niet genotariseerd door Apple en heeft geen Developer ID-handtekening, dus de eerste start vraagt één extra stap.

1. Sleep LYNX SHARE naar `Applications`.
2. Klik met rechts (Control-klik) erop en kies `Openen`.
3. Klik nog een keer op `Openen`.

Probeer op macOS 15 Sequoia of nieuwer het één keer te openen, ga dan naar `System Settings › Privacy & Security` en klik op `Open Anyway`. Of in Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS vraagt om toegang tot de microfoon de eerste keer dat je opneemt.

### Linux: de AppImage start niet

LYNX SHARE heeft een x86_64-systeem nodig met glibc 2.35 of nieuwer (Ubuntu 22.04, Debian 12, Fedora 36, actuele Arch of Manjaro) en X11 of XWayland.

1. Maak het bestand uitvoerbaar en start het, met de twee opdrachten hieronder.
2. Start het nog steeds niet, installeer dan libfuse2. Het pakket heet `libfuse2` op Ubuntu 22.04, `libfuse2t64` op Ubuntu 24.04, `fuse2` op Arch en `fuse-libs` op Fedora.
3. Wil je het liever niet installeren, download dan de `.tar.gz`, pak hem uit en voer `./install.sh` uit.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` installeert LYNX SHARE voor jouw gebruiker onder `~/.local`, met een menu-item. `./install.sh --uninstall` verwijdert het.

## Geluid

### Geen geluid, of het verkeerde audioapparaat

1. Open `Opties › Audio-instellingen...`.
2. Kies een driver.
3. Kies een uitvoerapparaat.

| Systeem | Drivers |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO en de ingebouwde LYNX ASIO-driver |
| macOS | Core Audio |
| Linux | PipeWire (native), JACK en ALSA. Bij een eerste start opent LYNX SHARE de eerste hiervan die apparaten heeft |

ASIO is een Windows-drivermodel en bestaat alleen op Windows. `Opties › Installatiewizard (taal, audio, pluginmappen)...` test ook het audioapparaat. Zie [Audio instellen](/wiki/audio-setup/).

### Linux: het geluid valt weg

De audiothread hoort met realtimeprioriteit te draaien.

1. Open `Opties › Audio-instellingen...` en lees `Status`. Dat toont of de thread realtimeprioriteit heeft gekregen.
2. Zo niet, installeer dan rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` of `sudo pacman -S rtkit`), of voeg jezelf toe aan de groep `audio` met een realtimelimiet, zoals bij JACK-opstellingen.
3. Log uit en weer in.

De details staan in [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

## Plugins

### Een plugin crasht

Draai hem in een eigen proces, zodat alleen de plugin stopt.

1. Open `Opties › Algemene instellingen...`.
2. Zet `Plugins in een apart proces draaien (beschermt LYNX SHARE tegen crashes)` aan.
3. Open het project opnieuw.

Een plugin die in een eigen proces is gecrasht of niet meer reageerde, wordt gemute. Sla het project op en open het opnieuw om de plugin weer te laden. Zie [Plugins van derden](/wiki/plugins/).

### Een plugin staat niet in de lijst

1. Kies `Opties › Plugins scannen`.
2. Ontbreekt hij nog steeds, open dan `Opties › Pluginbeheer...`, klik op `Pluginmappen...` en voeg de map van de plugin toe.
3. Kijk op de tab `Mislukt`. Een pluginbestand dat de scan niet doorstaat, wordt overgeslagen. Op Windows staat de reden in `%APPDATA%\LYNX\scan-failures.log`.
4. Klik op `Herscannen...` om de mislukte bestanden opnieuw te proberen.

### Een project gebruikt een plugin die niet is geïnstalleerd

Een effect wordt vervangen door een LYNX SHARE-effect met dezelfde rol, en de instellingen blijven in het project bewaard. `Help › Ontbrekende bestanden...` toont wat ontbreekt. Zie [Ingebouwde instrumenten en effecten](/wiki/built-in/).

### Video werkt niet op Linux

Videotracks en `Export with video` hebben FFmpeg nodig. LYNX SHARE zegt hoe je het installeert.

## Crashrapporten

![De vraag over het crashrapport bij de volgende start, met Send, Show what will be sent en Don't send](/assets/img/wiki/crash-report.webp)

Crashrapporten zijn opt-in. Na een crash of een vastloper vraagt de volgende start of er een rapport moet worden verstuurd.

| Commando | Wat het doet |
|---|---|
| `Tonen wat er verstuurd wordt` | Toont de exacte tekst |
| `In plaats daarvan melden op GitHub` | Opent het issueformulier van GitHub, vooraf ingevuld. De bestanden voeg je zelf bij |
| `De map met het rapport openen` | Toont de rapportbestanden op je computer |
| `Include file names (projects, samples)` | Uit: alleen de extensie van elk bestand wordt verstuurd |
| `Altijd versturen zonder te vragen` | Verstuurt toekomstige rapporten zonder de vraag |

`Crashrapporten` in `Opties › Algemene instellingen...` stelt `Ask` (de standaard), `Altijd versturen` of `Nooit versturen` in.

Een rapport bevat de LYNX SHARE-versie, het besturingssysteem, de cpu, de videokaarten en hun drivers, de schermgrootte, de audiodriver en het apparaat, het crash- of freezebestand, de laatste 60 stappen van de sessie en de namen en versies van de geladen plugins van derden.

Het bevat geen audio, projecten, samples, de naam van je computer of je gebruikersnaam. De server slaat geen IP-adres op. Verstuurde id's staan in `crash-reports\sent.log` in de map met instellingen. Om een rapport te laten verwijderen, open je een issue met de id ervan. De volledige tekst staat in het [privacybeleid](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Een probleem melden

![Het venster Report a problem: een beschrijvingsvak, een optioneel e-mailadres en de knop Send](/assets/img/wiki/report-problem.webp)

1. Kies `Help › Een probleem melden...`.
2. Beschrijf wat er gebeurde. Het e-mailadres is optioneel, voor als je antwoord wilt.
3. Laat `De nieuwste crash- / freeze-bestanden en de stappen van deze sessie bijvoegen` aangevinkt als het probleem een crash was.
4. Klik op `Tonen wat er verstuurd wordt` om de tekst te controleren.
5. Klik op `Sturen`. Daarvoor wordt niets verstuurd.

Je kunt ook een issue openen op [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Goed om te weten

- Na een crash staan back-ups van je project in de map van LYNX SHARE voor automatisch opslaan.
- Als een plugin bij de crash betrokken was, biedt de vraag aan om die plugin voortaan in een apart proces te draaien.
- De map met instellingen is `%APPDATA%\LYNX` op Windows, `~/Library/Application Support/LYNX` op macOS en `~/.config/LYNX` op Linux.

## Gerelateerd

- [Audio instellen](/wiki/audio-setup/)
- [Plugins van derden](/wiki/plugins/)
- [Installeren](/wiki/install/)
- [FAQ](/faq/)
- [Crashbeveiliging](/why/#crash)
