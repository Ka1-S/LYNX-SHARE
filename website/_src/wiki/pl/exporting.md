---
source: da5e41c322
---
![Okno eksportu z wybranym WAV: zakres, ogon i pliki po lewej, format po prawej, opcje miksu poniżej](/assets/img/wiki/export.webp)

Wszystko, co opuszcza LYNX SHARE, przechodzi przez `Plik › Eksportuj`. Formaty audio (WAV, MP3, OGG i FLAC) mają wspólne okno eksportu. MIDI, projekty FL Studio, sety Ableton Live i spakowane projekty mają własne pozycje w tym samym menu.

## Eksportuj utwór jako WAV

1. Wybierz `Plik › Eksportuj › Plik WAV...`.
2. Zostaw `Zakres` na `Utwór (Playlist)`, a `Pliki` na `Jeden plik (miks)`.
3. Wybierz `Głębia bitowa`: 16 bits, 24 bits albo 32 bits (float).
4. Ustaw `Ogon` na `Zostaw resztę`, żeby pogłosy i release wybrzmiały po ostatnim takcie.
5. Kliknij `Eksportuj...`, wybierz nazwę i folder, po czym poczekaj na render.
6. Gdy skończy, LYNX SHARE poda, gdzie trafił plik, i jego poziom szczytowy.

`Ctrl+R` otwiera okno z formatem, który eksportowałeś ostatnio. `Ctrl+Shift+R` otwiera je na MP3.

## Co renderować

| Ustawienie | Co robi |
|---|---|
| `Zakres` | `Utwór (Playlist)`, `Bieżący pattern` albo `Zaznaczenie w Playliście` |
| `Odtwórz` | Ile razy zakres gra w pliku, od `Raz` do 16 razy |
| `Ogon` | `Utnij resztę` kończy dokładnie na końcu. `Zostaw resztę` pozwala dźwiękowi wybrzmieć. `Zawiń resztę` miksuje ogon z początkiem, dla pętli |
| `Maks. ogon` | 3, 6, 12 lub 30 sekund. Ogon kończy się wcześniej, gdy dźwięk zanika |
| `Pliki` | Jeden plik albo stemy (zobacz niżej) |

## Format wyjściowy

Cztery przyciski wybierają format. `Też zapisz` zapisuje inne formaty z tego samego renderu, pod tą samą nazwą.

| Format | Opcje |
|---|---|
| WAV | `Głębia bitowa`: 16 bits, 24 bits albo 32 bits (float). `Dithering`: `Wył.`, `TPDF` lub `TPDF + noise shaping` |
| MP3 | `Bitrate`, stały |
| OGG | Bitrate |
| FLAC | `Głębia bitowa`, `Kompresja` od `0 (najszybsza)` do `8 (najmniejszy plik)`, `Dithering`. Bezstratny na każdym poziomie |

`Próbkowanie` i `Kanały` dotyczą każdego formatu. `Kanały` to `Stereo`, `Mono (obie strony zmiksowane)`, `Mono (tylko lewy)` albo `Mono (tylko prawy)`.

## Opcje miksu

| Ustawienie | Co robi |
|---|---|
| `Normalizuj (szczyt do 0 dB)` | Podnosi lub obniża cały plik tak, żeby jego najgłośniejszy szczyt sięgnął pełnej skali |
| `Efekty Master` | Wyłączone: efekty insertu Master są omijane |
| `Efekty ścieżek` | Wyłączone: efekty pozostałych insertów są omijane |
| `HQ dla wszystkich pluginów` | Pluginy używają jakości renderu. Wyłączone: eksport używa jakości odtwarzania |
| `Rozgrzewka pluginów` | Pluginy zewnętrzne przetwarzają chwilę ciszy, zanim eksport się zacznie |
| `Wyłącz maksymalną polifonię` | Znosi limit głosów kanałów, więc żadna nuta nie jest ucinana |
| `Przytnij ciszę z kompensacji opóźnień` | Usuwa ciszę, którą kompensacja latencji pluginów dodaje na początku |
| `Informacje o tempie`, `Znaczniki pętli`, `Znaczniki playlisty` | Tylko WAV: zapisują w pliku tempo, pętlę i znaczniki Playlisty |
| `Pokaż plik(i) po zakończeniu` | Otwiera menedżer plików na wyniku |

## Stemy i ścieżki

![Okno eksportu ustawione na FLAC z jednym plikiem na insert miksera, w sumie dziewięć plików](/assets/img/wiki/export-flac-stems.webp)

`Pliki` ma trzy tryby stemów:

- `Na insert miksera (stemy)`: plik dla każdego insertu, który dostaje dźwięk.
- `Na kanał (stemy)`: plik dla każdego kanału, który gra.
- `Na kanał, tylko instrumenty (bez wysyłek i szyn)`.

Zaznacz `Także pełny miks`, żeby dostać miks obok stemów. Linia na dole pokazuje, ile plików zostanie zapisanych.

`Plik › Eksportuj › Wszystkie ścieżki Playlisty` i `Wszystkie grupy ścieżek Playlisty` zapisują po jednym pliku na ścieżkę Playlisty albo na grupę. W menu Mixera `Nagrywanie na dysk › Renderuj uzbrojone ścieżki do pliku(-ów) WAV...` (`Alt+R`) otwiera okno eksportu tylko dla uzbrojonych insertów.

## Pozostałe pozycje menu Eksportuj

| Polecenie | Klawisz | Co robi |
|---|---|---|
| `Plik MIDI...` | `Ctrl+Shift+M` | Zapisuje nuty jako standardowy plik MIDI (`.mid`) |
| `Mastering...` | `Ctrl+Alt+M` | Renderuje projekt i masteruje go do docelowej głośności w LUFS, na twoim komputerze |
| `Projekt z samplami (.zip)...` | brak | Projekt i jego sample w jednym archiwum |
| `Projekt FL Studio...` | brak | Plik `.flp` albo spakowana paczka pętli ze wszystkimi samplami |
| `Set Ableton Live (.als)...` | brak | Set dla Live 12 lub Live 11 |
| `Kości projektu...` | brak | Presety i wyniki kanałów oraz efektów, w folderze |

## Eksport do FL Studio i Ableton Live

`.flp` to projekt FL Studio 2026. Samplery, klipy audio, własne generatory i efekty FL Studio, pluginy zewnętrzne, patterny, Playlist, Mixer i automatyka przechodzą z ustawieniami. Dla rzeczy, których FL Studio nie ma, wybierasz między `Wyrenderuj do audio (brzmi tak samo)` a `Użyj najbliższej wtyczki FL (pozostaje edytowalna, może brzmieć inaczej)`. Przy drugim wyborze instrumenty tylko z LYNX są pomijane. Modulatory są pomijane zawsze. Raport wylicza, co wyrenderowano, zastąpiono lub pominięto. Szczegóły są w [Przejściu z FL Studio](/wiki/coming-from-fl-studio/).

`.als` zawiera ścieżkę MIDI na kanał, ścieżki audio, grupy, zwroty, sendy, racki i Racki perkusyjne, pluginy VST2 i VST3 z ich stanem, znaczniki oraz automatykę głośności, panoramy i tempa. Własne instrumenty i efekty LYNX SHARE nie są konwertowane, choć ich ścieżki i nuty zostają. Automatyka parametrów efektów i trasy insert do insertu są pomijane. Szczegóły są w [Przejściu z Ableton Live](/wiki/coming-from-ableton/).

## Warto wiedzieć

- `Dithering` jest wyszarzony dla MP3, a opcje tempa i znaczników są wyszarzone dla każdego formatu poza WAV.
- Przy `Zaznaczenie w Playliście` i braku zaznaczenia w Playliście renderowany jest cały utwór. Okno ostrzega o tym.
- Jeśli insert Master jest wyciszony, okno ostrzega, że plik będzie cichy.
- `Zapisz też wideo z tym miksem` pojawia się tylko wtedy, gdy projekt ma ścieżkę wideo, i wymaga FFmpeg.
- `Renderuj przez sprzęt w czasie rzeczywistym` pojawia się tylko wtedy, gdy projekt ma aktywny insert sprzętowy. Taki eksport trwa tyle, co utwór.
- Render do audio, czyli zgranie insertu do klipu audio, gra w tej wersji około 5 dB głośniej niż insert, który zastępuje.

## Powiązane

- [Formaty plików](/wiki/file-formats/)
- [Wiersz poleceń](/wiki/command-line/)
- [Przejście z FL Studio](/wiki/coming-from-fl-studio/)
- [Przejście z Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
