---
source: 8a120ab7de
---
Elke release heeft builds voor Windows, macOS en Linux op de [releasepagina](https://github.com/Ka1-S/LYNX-SHARE/releases). De bestanden worden door GitHub Actions uit de getagde broncode gebouwd en staan met hun SHA-256 in `SHA256SUMS.txt`. De pagina [Download](/download/) verwijst naar de actuele.

## Installeren op Windows

Windows 10 of 11, 64-bit.

1. Download `LYNX-<version>-setup-x64.exe`.
2. Voer het uit. Het installeert alleen voor jouw gebruiker en vraagt geen beheerdersrechten.
3. SmartScreen kan waarschuwen, omdat het uitvoerbare bestand nog niet is ondertekend. Kies `More info`, dan `Run anyway`.
4. Start LYNX SHARE vanuit het Startmenu of met de snelkoppeling op het bureaublad.

Het installatieprogramma maakt van LYNX SHARE het programma dat `.lynx`-projecten opent. Het biedt LYNX SHARE aan onder *Openen met* voor `.flp`-bestanden, zonder de standaard van FL Studio te wijzigen.

Er zijn twee andere manieren:

| Manier | Bestand of plek | Wat het doet |
|---|---|---|
| Draagbare zip | `LYNX-<version>-windows-x64.zip` | Pak hem ergens uit en start `LYNX.exe`. Er wordt niets geïnstalleerd. |
| Microsoft Store | [LYNX SHARE in de Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | De Store installeert het en houdt het bijgewerkt. |

## Installeren op macOS

macOS 11 Big Sur of nieuwer. Eén universele `LYNX-<version>-macos-universal.dmg` dekt Intel en Apple silicon.

1. Open de `.dmg` en sleep LYNX SHARE naar *Programma's*.
2. De app is niet genotariseerd door Apple en heeft geen Developer ID-handtekening, dus de eerste start vraagt een extra stap. Klik met rechts (of Control-klik) op LYNX SHARE in *Programma's*, kies *Open* en dan nog een keer *Open*.
3. Probeer op macOS 15 Sequoia of nieuwer het één keer te openen, ga dan naar *Systeeminstellingen › Privacy en beveiliging* en klik op *Open toch*.

Het alternatief in Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS vraagt om toegang tot de microfoon de eerste keer dat je opneemt.

## Installeren op Linux

x86_64 met glibc 2.35 of nieuwer (Ubuntu 22.04+, Debian 12+, Fedora 36+, actuele Arch of Manjaro). LYNX SHARE heeft X11 of XWayland nodig. Audio loopt via PipeWire, JACK of ALSA, en daarvoor hoeft niets extra te worden geïnstalleerd.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Sommige distributies hebben libfuse2 nodig om AppImages te draaien (`libfuse2` op Ubuntu 22.04, `libfuse2t64` op 24.04, `fuse2` op Arch, `fuse-libs` op Fedora). Bij de eerste start biedt de installatiewizard aan om LYNX SHARE aan het toepassingenmenu toe te voegen en `.lynx`-bestanden ermee te openen. Je kunt dat later wijzigen in `Opties › Algemene instellingen...`, of vanuit een terminal met `./LYNX-*.AppImage --menu-entry on` (of `uit`).

Wil je libfuse2 liever niet installeren, gebruik dan `LYNX-<version>-linux-x86_64.tar.gz`: pak hem uit en voer `./install.sh` uit. Dat installeert LYNX SHARE voor jouw gebruiker onder `~/.local`, met een menu-item, een pictogram en het bestandstype `.lynx`.

## Waar de instellingen staan

| Systeem | Map met instellingen |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (of onder `$XDG_CONFIG_HOME`) |

De map bevat je opties, de gekozen taal (`language.txt`), je sneltoetsen (`keys.json`), je pluginmappen (`plugin-folders.txt`) en de lijst met gescande plugins (`plugins.xml`).

## Bijwerken

Bij het opstarten vraagt LYNX SHARE GitHub om het nieuwste versienummer. Er wordt niets over jou verstuurd. Als er een nieuwe versie is, toont een balk `LYNX SHARE <version> is available` met een knop `Wat is er nieuw` en een updateknop. `Help › Controleren op updates...` vraagt het op elk moment.

| Build | Hoe het wordt bijgewerkt |
|---|---|
| Windows-installatieprogramma | `Bijwerken` downloadt het nieuwe installatieprogramma, vraagt naar niet-opgeslagen wijzigingen, installeert en herstart |
| Draagbare Windows-zip, macOS | `Bijwerken` downloadt het nieuwe bestand en toont het in zijn map |
| Linux AppImage | `Bijwerken` werkt het AppImage-bestand ter plekke bij. Gear Lever en AppImageUpdate kunnen hetzelfde van buitenaf |
| Linux `.tar.gz` | `Downloaden...` opent de downloadpagina |
| Microsoft Store | De Store werkt het bij. Deze build heeft geen eigen updatecontrole |

Om de controle uit te zetten, open je `Opties › Algemene instellingen...` en haal je het vinkje bij `Automatisch controleren op updates` weg.

## Verwijderen

- Windows-installatieprogramma: verwijder LYNX SHARE via de lijst *Apps en onderdelen* van Windows.
- Draagbare zip: verwijder de map die je hebt uitgepakt.
- Linux `.tar.gz`: voer `./install.sh --uninstall` uit.

## Goed om te weten

- De Windows-builds zijn niet ondertekend totdat de aanvraag voor codeondertekening van het project is geaccepteerd. De macOS-app heeft alleen een ad-hocondertekening. Linux-pakketten zijn niet ondertekend.
- Om een download te controleren, vergelijk je de SHA-256 ervan met de regel in `SHA256SUMS.txt` op de releasepagina.
- Het installatieprogramma, de zip, de `.dmg` en de `.tar.gz` bevatten het demoproject `Demo.lynx`.
- De updatebalk sluiten met de sluitknop slaat die versie over.

## Gerelateerd

- [Begin hier](/wiki/getting-started/)
- [Audio instellen](/wiki/audio-setup/)
- [Probleemoplossing](/wiki/troubleshooting/)
- [Download](/download/)
- [Updates](/updates/)
