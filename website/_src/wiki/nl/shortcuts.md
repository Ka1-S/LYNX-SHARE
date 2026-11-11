---
source: 5efc5c8254
---
![Het venster Sneltoetsen beheren: een doorzoekbare lijst van acties met de toetsen van elke actie](/assets/img/wiki/shortcuts.webp)

Dit zijn de standaardtoetsen in 0.1.25 alpha. `Help › Sneltoetsen` (`F1`) toont het sneltoetsenoverzicht in de app. `Opties › Sneltoetsen beheren...` opent de editor op de afbeelding, waar je ze wijzigt.

> Op macOS toont de app de toetsen met Mac-namen: Ctrl wordt getoond als ⌘ en Alt als ⌥.

## Een sneltoets wijzigen

1. Kies `Opties › Sneltoetsen beheren...`.
2. Typ een actie of een toets, zoals `Ctrl+L`, in het zoekvak. De lijst ernaast beperkt de zoekopdracht tot één plek, en `Alleen gewijzigd` toont de toetsen die je hebt gewijzigd.
3. Klik op de actie.
4. Klik op `Wijzigen` om de toetsen te vervangen, of op `Sneltoets toevoegen` om een tweede toe te voegen, en druk dan op de nieuwe toetsen. `Esc` annuleert.
5. Worden de toetsen op dezelfde plek al gebruikt, dan noemt een melding de andere actie en kun je ze verplaatsen.

`Verwijderen` wist de toetsen van de actie. `Standaard herstellen` herstelt één actie en `Alles resetten...` herstelt ze allemaal. Je wijzigingen worden opgeslagen in `keys.json` in de map met instellingen (`%APPDATA%\LYNX` op Windows, `~/Library/Application Support/LYNX` op macOS, `~/.config/LYNX` op Linux). `keys.json openen` opent dat bestand.

## Transport en opname

| Toets | Wat het doet |
|---|---|
| `Space` | Afspelen, of stoppen en teruggaan naar waar het afspelen begon |
| `Ctrl+Space` | Afspelen of pauzeren |
| `Shift+Space` | Afspelen, of stoppen aan het begin van de song of het pattern |
| `L` | Wisselen tussen de Pattern-modus en de Song-modus |
| `R` | Opname aan of uit |
| `Shift+R` | Nu audio opnemen: armt de geselecteerde insert en neemt op |
| `Ctrl+M` | Metronoom aan of uit |
| `Ctrl+P` | Aftellen vóór de opname aan of uit |
| `Ctrl+I` | Opname wacht op de eerste noot, aan of uit |
| `Ctrl+B` | Opgenomen noten mengen aan of uit |
| `Ctrl+E` | Stapinvoer aan of uit |
| `Ctrl+H` | Geluid stoppen (alle noten uit) |
| `Ctrl+T` | Typtoetsenbord als piano aan of uit |
| `Alt+Numpad *` / `Alt+Numpad /` | Naar de volgende of vorige songmarker springen |

Een aantal van deze toetsen doet iets anders binnen een editor die de focus heeft. In de Piano Roll importeert `Ctrl+M` een MIDI-bestand en dupliceert `Ctrl+B`. In de Playlist is `Ctrl+P` de Performance-modus.

## Bestanden en bewerken

| Toets | Wat het doet |
|---|---|
| `Ctrl+N` | Nieuw project |
| `Ctrl+O` | Project openen |
| `Ctrl+S` | Opslaan |
| `Ctrl+Shift+S` | Opslaan als |
| `Ctrl+Alt+S` | Nieuwe versie opslaan |
| `Ctrl+R` | Audio exporteren |
| `Ctrl+Shift+R` | Een MP3-bestand exporteren |
| `Ctrl+Shift+M` | Een MIDI-bestand exporteren |
| `Ctrl+Z` | Ongedaan maken. Nog een keer ingedrukt maakt die stap opnieuw |
| `Ctrl+Alt+Z` | Achterwaarts ongedaan maken, stap na stap |
| `Ctrl+Y` of `Ctrl+Shift+Z` | Opnieuw |
| `Ctrl+Alt+H` | Bewerkgeschiedenis |
| `Ctrl+F` | Zoeken in de Browser |

## Vensters

| Toets | Wat het doet |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Plugin-kiezer |
| `Shift+F8` | Plugindatabase (in de Browser) |
| `Enter` | De Playlist maximaliseren of herstellen |
| `F10` | MIDI-instellingen |
| `F11` | Projectinfo |
| `F12` | Alle vensters sluiten (plugins, kanaalinstellingen, tools) |
| `Alt+F12` | Alle pluginvensters sluiten |
| `Ctrl+F12` | Alle vensters sluiten behalve dat met de focus |
| `Alt+F7` | Aanraaktoetsenbord |
| `Ctrl+F5` | Tempo tapper |
| `F1` | Overzicht sneltoetsen |

## Patterns

| Toets | Wat het doet |
|---|---|
| `Numpad +` / `Numpad -` | Volgend, vorig pattern |
| `Numpad 1` tot `Numpad 9` | Pattern 1 tot 9 selecteren |
| `F4` | Volgend leeg pattern, en een naam geven |
| `Ctrl+F4` | Volgend leeg pattern, zonder naam |
| `Shift+F4` | Eerste lege pattern, en een naam geven |
| `F2` | Het pattern hernoemen en kleuren |
| `Ctrl+Shift+C` | Het pattern klonen |
| `Ctrl+Shift+Ins` | Een leeg pattern invoegen vóór het huidige |
| `Ctrl+Shift+Del` | Het pattern verwijderen |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Het pattern omhoog, omlaag verplaatsen |

## Channel Rack

| Toets | Wat het doet |
|---|---|
| `Up` / `Down` | Het kanaal erboven, eronder selecteren |
| `Shift+Up` / `Shift+Down` | De selectie uitbreiden |
| `Alt+Up` / `Alt+Down` | De geselecteerde kanalen verplaatsen |
| `Ctrl+A` | Alle kanalen selecteren |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | De steps van de geselecteerde kanalen knippen, kopiëren, plakken |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | De steps naar links, naar rechts roteren |
| `Alt+C` | De geselecteerde kanalen klonen |
| `Alt+Del` | De geselecteerde kanalen verwijderen |
| `Alt+G` | De geselecteerde kanalen groeperen |
| `Ctrl+L` | De geselecteerde kanalen naar vrije Mixer-inserts sturen |
| `Ctrl+K` | Step-grafiekbanen aan of uit |
| `1` tot `0` | Kanaal 1 tot 10 muten of unmuten |
| `Ctrl+1` tot `Ctrl+0` | Kanaal 1 tot 10 solo zetten |

## Piano Roll

| Toets | Wat het doet |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Potlood, Kwast, Drumkwast, Gum, Mute, Mes, Lasso, Zoom, Luisteren |
| `S` | Slide-noten aan of uit (nieuwe noten) |
| `G` / `K` | Vorig, volgend kanaal |
| `H` / `J` | Vorig, volgend kanaal met noten |
| `Ctrl+A` | Alles selecteren |
| `Ctrl+D` of `Esc` | Deselecteren |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiëren, knippen, plakken |
| `Ctrl+B` | Dupliceren |
| `Del` | De selectie verwijderen |
| `Up` / `Down` | De selectie een halve toon verplaatsen |
| `Ctrl+Up` / `Ctrl+Down` | De selectie een octaaf verplaatsen |
| `Left` / `Right` | De selectie één rasterstap verplaatsen |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | De noten binnen de maat roteren |
| `Backspace` | Snap uit of aan |
| `Ctrl+Q` | Snel kwantiseren (begin en lengte) |
| `Shift+Q` | Begin kwantiseren |
| `Ctrl+U` | Chop tot het raster |
| `Ctrl+G` | Aansluitende noten samenvoegen |
| `Ctrl+L` | Legato |
| `Shift+D` | De noten inkorten tot het raster |
| `Ctrl+P` | Porta aan of uit voor de selectie |
| `Alt+M` / `Shift+Alt+M` | De selectie muten, unmuten |
| `Alt+V` | Ghost notes aan of uit |
| `Shift+I` | De selectie omkeren |
| `Shift+G` / `Alt+G` | De geselecteerde noten groeperen, de groep opheffen |
| `Alt+C` | De geselecteerde noten kleuren met de nootkleur |
| `Shift+C` | De noten met dezelfde kleur selecteren |
| `Shift+N` | Akkoordstempel: geen (losse noten) |
| `Ctrl+Ins` / `Ctrl+Del` | Ruimte (de tijdselectie) invoegen, verwijderen |
| `Ctrl+M` | Een MIDI-bestand importeren |
| `Ctrl+Shift+M` | Exporteren als MIDI-bestand |
| `PageUp` / `PageDown` | In, uit zoomen |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoomen naar 1, 4, 16 maten |
| `Shift+4` | Zoomen zodat alles zichtbaar is |
| `Shift+5` | Zoomen naar de selectie |
| `Shift+0` | Het beeld op de afspeelkop centreren |

### Nootgereedschappen

Elke toets opent het dialoogvenster van één nootgereedschap in de Piano Roll.

| Toets | Wat het doet |
|---|---|
| `Alt+A` | Arpeggiëren |
| `Alt+S` | Strum |
| `Alt+Y` | Spiegelen |
| `Alt+R` | Willekeurig maken |
| `Alt+X` | Niveaus schalen |
| `Alt+K` | Beperken tot een bereik |
| `Alt+Q` | Quantize |
| `Alt+U` | Chop |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Articuleren |
| `Alt+E` | Riff machine |
| `Alt+P` | Akkoordprogressie genereren |

## Playlist

| Toets | Wat het doet |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Potlood, Kwast, Gum, Mute, Mes, Lasso, Zoom, Luisteren, Slip |
| `Ctrl+A` | Alles selecteren |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiëren, knippen, plakken |
| `Ctrl+B` | Dupliceren |
| `Del` | De geselecteerde clips verwijderen |
| `Shift+Del` | De geselecteerde clips verwijderen en de gaten sluiten |
| `Shift+S` | Ripple-bewerking aan of uit |
| `Backspace` | Snap uit of aan |
| `Alt+M` / `Shift+Alt+M` | De geselecteerde clips muten, unmuten |
| `Shift+M` | Audio stretchen bij het aanpassen van audioclips, aan of uit |
| `Ctrl+Shift+V` | De geselecteerde clips uniek maken |
| `Ctrl+G` | Patternclips samenvoegen |
| `Ctrl+Shift+G` | Vergelijkbare patternclips samenvoegen |
| `Ctrl+Alt+G` | Automatiseringsclips samenvoegen |
| `Ctrl+Shift+B` | De geselecteerde Playlist-tracks klonen |
| `Ctrl+Q` | De beginpunten van de clips kwantiseren |
| `7` / `8` | De geselecteerde audioclips een halve toon omlaag, omhoog pitchen |
| `9` | De geselecteerde audioclips reversen |
| `Ctrl+Ins` / `Ctrl+Del` | De lengte van de tijdselectie op alle tracks invoegen, verwijderen |
| `Ctrl+Left` / `Ctrl+Right` | De vorige, volgende tijd selecteren |
| `Ctrl+Enter` | Tijd rond de selectie selecteren |
| `Alt+T` | Een tijdmarker toevoegen |
| `Ctrl+T` | Automatisch een marker toevoegen |
| `Shift+Alt+T` | Een wijziging van de maatsoort toevoegen |
| `Shift+T` | Loop plaatsen |
| `Ctrl+Alt+C` | De Playlist-selectie consolideren |
| `Ctrl+P` | Performance-modus aan of uit |
| `PageUp` / `PageDown` | In, uit zoomen |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoomen naar 1, 4, 16 maten |
| `Shift+4` | Zoomen zodat alles zichtbaar is |
| `Shift+5` | Zoomen naar de selectie |
| `Shift+0` | Het beeld op de afspeelkop centreren |

## Mixer

| Toets | Wat het doet |
|---|---|
| `Alt+Left` / `Alt+Right` | De geselecteerde inserts naar links, rechts verplaatsen |
| `F2` | De geselecteerde insert hernoemen en kleuren |
| `S` | De geselecteerde inserts solo zetten |
| `Alt+S` | De geselecteerde inserts solo zetten samen met de inserts die ze voeden |
| `Ctrl+L` | De geselecteerde kanalen koppelen aan de geselecteerde insert |
| `Ctrl+Shift+L` | De geselecteerde kanalen koppelen aan inserts, te beginnen bij de geselecteerde |
| `Alt+L` | De kanalen selecteren die aan de geselecteerde insert zijn gekoppeld |
| `Del` | De plugin in het geselecteerde effectslot verwijderen |
| `Ctrl+E` | De audio-editor openen |
| `Shift+E` | De audio-editor openen en opnemen |
| `Alt+R` | De gearmde inserts renderen naar wave-bestanden |
| `Shift+Alt+R` | De geselecteerde inserts renderen naar wave-bestanden |

## Browser

| Toets | Wat het doet |
|---|---|
| `Up` / `Down` | Vorig, volgend bestand (met voorbeluisteren) |
| `Shift+Up` / `Shift+Down` | Vorig, volgend bestand in het geselecteerde kanaal |
| `Enter` | Het bestand in het geselecteerde kanaal laden |
| `Left` | De map sluiten, of naar de bovenliggende map gaan |
| `Right` | De map openen |
| `F2` | De geselecteerde plugin hernoemen (Plugindatabase) |

## Goed om te weten

- Zolang het pianotoetsenbord van het typtoetsenbord aan staat, spelen de toetsen noten en liggen de meeste sneltoetsen met één letter stil (`L` werkt nog). Zet het uit met `Ctrl+T`.
- Een sneltoets werkt overal of in één venster. Een sneltoets die bij een venster hoort, wint van dezelfde toetsen die overal werken, zolang dat venster de focus heeft.
- Sommige acties hebben geen standaardtoets, bijvoorbeeld het Snel-toevoegpalet. Geef ze er in de editor een.

## Gerelateerd

- [Rondleiding door het venster](/wiki/tour/)
- [Instellingen](/wiki/settings/)
- [MIDI-controllers](/wiki/midi-controllers/)
- [Overstappen van FL Studio](/wiki/coming-from-fl-studio/)
