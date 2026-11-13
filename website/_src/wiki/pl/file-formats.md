---
source: a79e43f031
---
LYNX SHARE zapisuje projekty jako pliki `.lynx`. Otwiera też projekty innych programów, wczytuje typowe pliki audio oraz eksportuje audio, MIDI i projekty dla innych programów. Tabela wylicza każdy format wersji 0.1.26 alpha.

Projekt otwarty z formatu innego programu to import. `Plik › Zapisz` pyta wtedy, gdzie zapisać plik `.lynx`, a oryginalny plik nigdy nie jest zmieniany.

## Otwórz projekt z innego programu

1. Wybierz `Plik › Otwórz...` (`Ctrl+O`).
2. Wskaż plik `.flp`, `.als`, `.flm`, `.mid` lub `.zip`.
3. Dla `.flp` wybierz `Pomoc › Last FL Studio import report`, żeby przeczytać, co zostało przybliżone lub pominięte.
4. Wybierz `Plik › Zapisz` i wskaż, gdzie zapisać plik `.lynx`.

## Formaty

| Format | Otwieranie | Zapis / eksport | Uwagi |
|---|---|---|---|
| `.lynx` | `Plik › Otwórz...` | `Plik › Zapisz`, z autozapisem | Własny format projektu LYNX SHARE (JSON) |
| `.zip` | `Plik › Otwórz...` rozpakowuje go do folderu i otwiera projekt w środku | `Plik › Eksportuj › Projekt z samplami (.zip)...` | Otwiera też spakowaną paczkę pętli FL Studio |
| `.lynxpack` | Otwiera się po podsumowaniu. Partia jest scalana z projektem gospodarza | `Plik › Sesja online › Wyślij projekt...` | Projekt lub partia wysłana w [sesji online](/wiki/online-sessions/). Partie pluginów zewnętrznych podróżują jako rendery audio plus ustawienia pluginu |
| `.flp` | `Plik › Otwórz...` | `Plik › Eksportuj › Projekt FL Studio...`, projekt FL Studio 2026 albo spakowana paczka pętli | Natywne pluginy grają na odtworzonych silnikach o różnej dokładności. Paczki FLEX nigdy nie są otwierane. Przy eksporcie instrumenty i efekty tylko z LYNX są renderowane do audio albo zamieniane na najbliższy plugin FL, a modulatory są pomijane. Zobacz [Przejście z FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Plik › Otwórz...` | Nie | Projekt FL Studio Mobile. Częściowo: instrumenty inne niż MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, Slicer i DirectWave grają wbudowanym syntezatorem |
| `.als` | `Plik › Otwórz...`, sety Live 11 i 12 | `Plik › Eksportuj › Set Ableton Live (.als)...`, dla Live 12 lub Live 11 | Przy imporcie urządzenia Live bez odpowiednika w LYNX SHARE są pomijane. Przy eksporcie własne instrumenty i efekty LYNX SHARE nie są konwertowane, a automatyka parametrów efektów i trasy insert do insertu są pomijane. Zobacz [Przejście z Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `Plik › Otwórz...` tworzy nowy projekt. `Plik › Importuj › Plik MIDI...` i Piano Roll importują do bieżącego | `Plik › Eksportuj › Plik MIDI...` (`Ctrl+Shift+M`) | |
| `.wav` | Jako sampel albo klip audio | `Plik › Eksportuj › Plik WAV...`, 16-bit, 24-bit lub 32-bit float | Opcjonalnie dither, informacje o tempie, znaczniki pętli i znaczniki Playlisty |
| `.mp3` | Jako sampel albo klip audio | `Plik › Eksportuj › Plik MP3...` | Stały bitrate |
| `.ogg` | Jako sampel albo klip audio | `Plik › Eksportuj › Plik OGG...` | |
| `.flac` | Jako sampel albo klip audio | `Plik › Eksportuj › Plik FLAC...`, kompresja od 0 do 8 | Bezstratny na każdym poziomie |
| `.aif`, `.aiff` | Jako sampel albo klip audio | Nie | |
| `.wv` | Jako sampel | Nie | Sample WavPack, takie jak w paczkach FL Studio |
| `.sf2`, `.sf3` | Gra je LYNX Soundfont | Nie | SoundFonty |
| `.fst` | Z Browsera | Nie | Preset natywnego pluginu FL Studio. Tylko dla pluginów, do których LYNX SHARE ma silnik |
| `.fsc` | Piano Roll i edytor zdarzeń | Piano Roll i edytor zdarzeń | Plik partii i automatyki FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Wczytaj plik presetu...` w menu presetów hostowanego pluginu | `Zapisz preset...` i `Eksportuj plik presetu...` | Presety pluginów zewnętrznych. `.fxp` i `.fxb` dotyczą pluginów VST2, a `.vstpreset` pluginów VST3, i tylko dopóki plugin działa wewnątrz LYNX SHARE |
| Plik wideo | `Plik › Importuj › Plik wideo...` umieszcza wideo na Playliście | `Zapisz też wideo z tym miksem` w oknie eksportu | Import używa dekoderów systemowych w Windows i macOS, a w Linuksie wymaga FFmpeg. Eksport wymaga FFmpeg |

## Inne eksporty

| Polecenie | Co robi |
|---|---|
| Opcja `Pliki` okna eksportu | Stemy: jeden plik na insert miksera, na kanał albo na kanał tylko z instrumentami |
| `Plik › Eksportuj › Wszystkie ścieżki Playlisty` | Jeden plik na ścieżkę Playlisty |
| `Plik › Eksportuj › Wszystkie grupy ścieżek Playlisty` | Jeden plik na grupę ścieżek |
| `Plik › Eksportuj › Mastering...` | Renderuje projekt i masteruje go do docelowej głośności, na twoim komputerze |
| `Plik › Eksportuj › Kości projektu...` | Zapisuje presety i wyniki kanałów oraz efektów do folderu |

Okno eksportu renderuje utwór, bieżący pattern albo zaznaczenie w Playliście.

## Z wiersza poleceń

W Windows program to `LYNX.exe`. W macOS i Linuksie to `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` i `--export-flp` przyjmują jako wejście także plik `.als`.

## Warto wiedzieć

- Pliki audio dłuższe niż 30 sekund są strumieniowane z cache na dysku.
- Pliki `.flp`, `.als` i `.flm` nigdy nie są nadpisywane przy zapisie. Polecenia eksportu zapisują nowe pliki.
- Pliki `.wv` są tylko do odczytu.
- Plik `.zip` jest najpierw rozpakowywany do folderu, a potem otwierany jest projekt w środku.

## Powiązane

- [Import projektów](/wiki/importing-projects/)
- [Eksport](/wiki/exporting/)
- [Wiersz poleceń](/wiki/command-line/)
- [Przejście z FL Studio](/wiki/coming-from-fl-studio/)
- [Przejście z Ableton Live](/wiki/coming-from-ableton/)
