---
source: 98829adcb8
---
LYNX SHARE działa też bez otwierania okna. Jedna opcja renderuje projekt do audio, inne konwertują go do projektu FL Studio albo setu Ableton Live, a kilka służy do testów. Własne testy projektu używają tych samych opcji.

Program nazywa się `LYNX.exe` w Windows i `LYNX` w macOS i Linuksie. W macOS pełna ścieżka to `/Applications/LYNX.app/Contents/MacOS/LYNX`. Opcje są takie same w każdym systemie.

## Renderowanie projektu do pliku

1. Otwórz terminal w folderze, w którym jest program. W Windows to folder instalacji albo folder, do którego rozpakowano zip.
2. Uruchom `LYNX.exe --export project.flp out.flac`, ze swoim projektem i plikiem wyjściowym.
3. Poczekaj, aż program się zakończy. Żadne okno się nie otwiera.
4. Przeczytaj `out.flac.txt`, zapisany obok pliku wyjściowego. Dla zaimportowanego projektu zawiera listę tego, co zostało przekonwertowane, przybliżone i pominięte, oraz które pluginy znaleziono, a których brakuje.

## Otwarcie projektu

```
LYNX.exe project.lynx
```

Uruchamia LYNX SHARE z oknem i otwiera plik. Przyjmuje `.lynx`, `.flp`, `.als`, `.flm` i `.mid`. Ścieżka względna jest liczona od bieżącego folderu.

## Opcje na co dzień

| Opcja | Co robi |
|---|---|
| `--export <project> <out>` | Renderuje cały utwór razem z ogonem, tak jak okno eksportu. Rozszerzenie `<out>` wybiera format: `.wav` (24 bity), `.flac`, `.ogg` lub `.mp3` (320 kb/s) |
| `--export ... --pattern` | Renderuje tylko bieżący pattern |
| `--export ... --insert N` | Renderuje tylko wyjście inserta N Mixera |
| `--export ... --rate N` | Renderuje z inną częstotliwością próbkowania, w Hz |
| `--stems <project> <base.wav>` | Zapisuje jeden plik WAV na każdy insert Mixera, który ma kanały: `base - 1.wav`, `base - 2.wav` i tak dalej |
| `--export-flp <project> <out.flp>` | Zapisuje projekt FL Studio. Z `out.zip` zapisuje spakowaną paczkę pętli |
| `--export-flp ... --substitute` | Zamienia pluginy dostępne tylko w LYNX na najbliższy plugin FL zamiast renderować je do audio |
| `--export-als <project> <folder>` | Zapisuje set Ableton Live w `<folder>/<name> Project/<name>.als`, dla Live 12 |
| `--export-als ... --live11` | Zapisuje set dla Live 11 |
| `--devices <report.txt>` | Wypisuje typy sterowników audio i urządzenia tego komputera |
| `--showcase <out.lynx>` | Zapisuje własny projekt demo LYNX SHARE |
| `--lang <id>` | Używa na tę sesję innego języka interfejsu, na przykład `--lang pt-BR` |
| `--menu-entry on` lub `wył.` | Tylko AppImage na Linuksie: dodaje LYNX SHARE do menu aplikacji albo je stamtąd usuwa |

`--export`, `--stems` i `--export-flp` przyjmują jako projekt plik `.lynx`, `.flp` lub `.als`. `--export-als` przyjmuje także `.flm`.

Przykłady:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Opcje do testowania

Służą do sprawdzania samego LYNX SHARE. Przydają się w zgłoszeniu błędu.

| Opcja | Co robi |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Uruchamia autotest bez urządzenia audio. Raport kończy się wierszem `result=OK`, gdy test przechodzi |
| `--snapshot <prefix> [project]` | Rysuje interfejs i jego okna do plików PNG nazwanych od prefiksu. Obrazki w tej wiki powstają w ten sposób |
| `--render <project> <out.wav> <seconds>` | Renderuje podaną liczbę sekund bieżącego patternu do 24-bitowego WAV |
| `--plugintest <plugin> <report.txt>` | Wczytuje i testuje jeden plugin VST3 lub CLAP |
| `--sandboxtest <plugin> <report.txt>` | Testuje plugin w osobnym procesie, także taki, który się zawiesza |
| `--scantest <folder> <report.txt>` | Skanuje folder w poszukiwaniu pluginów, w osobnych procesach |
| `--fldb <report.txt> [project.flp]` | Podaje, jaką instalację FL Studio znaleziono tutaj, jej bazę pluginów i dopasowania pluginów projektu |
| `--devicetest <type> <device> <report.txt>` | Otwiera sterownik audio na sekundę ciszy i podaje wynik |
| `--uiperf <project> <report.txt>` | Mierzy czas rysowania Mixera, Playlisty i Channel Rack |

Przykłady:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Warto wiedzieć

- Wyniki trafiają do plików, nie do terminala. `--export` zapisuje `<out>.txt`, `--export-flp` zapisuje `<out.flp>.txt`, `--export-als` zapisuje `export-als.txt` w folderze, a `--stems` zapisuje `<base.wav>.txt`.
- Program czyta standardowe wejście. W pętli powłoki dodaj `< /dev/null` do każdego wywołania.
- `--snapshot` rysuje w rozdzielczości 1280x720, chyba że zmienna środowiskowa `LYNX_SNAPSHOT_SIZE` mówi inaczej, na przykład `1920x1080`.
- `--lang` przyjmuje identyfikatory języków z listy w `Opcje › Ustawienia ogólne...`, takie jak `en`, `pt-BR`, `de`, `es`, `fr` lub `ja`. Nieznany identyfikator jest ignorowany.
- `--export` używa tej samej ścieżki renderowania co okno eksportu, ale żadnej z pozostałych opcji tego okna. Dla ditheringu, głębi bitowej lub normalizacji użyj okna.
- Kod źródłowy ma znacznie więcej opcji testowych. Są wewnętrzne i mogą się zmieniać między wersjami.

## Powiązane

- [Eksport](/wiki/exporting/)
- [Formaty plików](/wiki/file-formats/)
- [Budowanie ze źródeł](/wiki/build-from-source/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)
