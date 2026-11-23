---
source: b05790879f
---
![Okno eksportu z wybranym WAV: zakres, ogon i pliki po lewej, format po prawej, opcje miksu poniżej](/assets/img/wiki/export.webp)

Wszystko, co opuszcza LYNX SHARE, przechodzi przez `Plik › Eksportuj`. Formaty audio (WAV, MP3, OGG, FLAC, AIFF i M4A) mają wspólne okno eksportu. MIDI, projekty FL Studio, sety Ableton Live, pliki DAWproject i spakowane projekty mają własne pozycje w tym samym menu.

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

Przyciski wybierają format. M4A jest dostępny tylko tam, gdzie system ma enkoder AAC: w Windows i macOS, nie w Linuksie. `Też zapisz` zapisuje inne formaty z tego samego renderu, pod tą samą nazwą.

| Format | Opcje |
|---|---|
| WAV | `Głębia bitowa`: 16 bits, 24 bits albo 32 bits (float). `Dithering`: `Wył.`, `TPDF` lub `TPDF + noise shaping` |
| MP3 | `Bitrate`, stały |
| OGG | Bitrate |
| FLAC | `Głębia bitowa`, `Kompresja` od `0 (najszybsza)` do `8 (najmniejszy plik)`, `Dithering`. Bezstratny na każdym poziomie |
| AIFF | `Głębia bitowa`: 16 bits albo 24 bits. `Dithering` |
| M4A | `Bitrate`: 96, 128, 160 albo 192 kbps. AAC, zapisywany przez systemowy enkoder |

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

Gdy utwór ma sekcje na ścieżce sekcji, `Jeden plik na sekcję` tnie go na sekcjach i nazywa każdy plik od jego sekcji. Przy stemach każda sekcja dostaje własny zestaw.

`Plik › Eksportuj › Wszystkie ścieżki Playlisty` i `Wszystkie grupy ścieżek Playlisty` zapisują po jednym pliku na ścieżkę Playlisty albo na grupę. W menu Mixera `Nagrywanie na dysk › Renderuj uzbrojone ścieżki do pliku(-ów) WAV...` (`Alt+R`) otwiera okno eksportu tylko dla uzbrojonych insertów.

## Pozostałe pozycje menu Eksportuj

| Polecenie | Klawisz | Co robi |
|---|---|---|
| `Plik MIDI...` | `Ctrl+Shift+M` | Zapisuje nuty jako standardowy plik MIDI (`.mid`) |
| `Mastering...` | `Ctrl+Alt+M` | Renderuje projekt i masteruje go do docelowej głośności w LUFS, na twoim komputerze |
| `Spakowany pakiet loopów...` | brak | Projekt i każdy używany przez niego sampel w jednym pliku `.zip`, jako spakowany pakiet loopów FL Studio |
| `Pliki danych projektu...` | brak | Kopiuje każdy plik używany przez projekt do wybranego folderu; projekt nadal używa własnych plików |
| `Projekt FL Studio...` | brak | Plik `.flp` albo spakowana paczka pętli ze wszystkimi samplami |
| `Set Ableton Live (.als)...` | brak | Set dla Live 12 lub Live 11 |
| `DAWproject (.dawproject)...` | brak | Projekt dla Bitwig Studio, Studio One, Cubase i innych programów, które czytają DAWproject |
| `Dla innych aplikacji...` | brak | Jeden folder ze stemami, plikami MIDI, setem Live, projektem FL Studio, zgranym miksem i plikiem README, spakowany, jeśli chcesz |
| `Kości projektu...` | brak | Presety i wyniki kanałów oraz efektów, w folderze |

## Eksport do FL Studio, Ableton Live i DAWproject

`.flp` to projekt FL Studio 2026. Samplery, klipy audio, własne generatory i efekty FL Studio, pluginy zewnętrzne, patterny, Playlist, Mixer i automatyka przechodzą z ustawieniami. Dla rzeczy, których FL Studio nie ma, wybierasz między `Wyrenderuj do audio (brzmi tak samo)` a `Użyj najbliższej wtyczki FL (pozostaje edytowalna, może brzmieć inaczej)`. Przy drugim wyborze instrumenty tylko z LYNX są pomijane. Modulatory, które podążają za pozycją w utworze, stają się klipami automatyki: zsynchronizowane LFO staje się klipowym LFO FL Studio, a kroki, losowe źródła i makra stają się punktami. Followery, obwiednie, keytrack i velocity są pomijane. Raport wylicza, co wyrenderowano, zastąpiono lub pominięto. Szczegóły są w [Przejściu z FL Studio](/wiki/coming-from-fl-studio/).

`.als` zawiera ścieżkę MIDI na kanał, ścieżki audio, grupy, zwroty, sendy, racki i Racki perkusyjne, pluginy VST2 i VST3 z ich stanem, znaczniki oraz automatykę głośności, panoramy i tempa. Własne instrumenty i efekty LYNX SHARE nie są konwertowane, choć ich ścieżki i nuty zostają. Wyjątkiem jest Helix: staje się Wavetable z Live. Automatyka parametrów efektów i trasy insert do insertu są pomijane. Szczegóły są w [Przejściu z Ableton Live](/wiki/coming-from-ableton/).

`.dawproject` zawiera ścieżki, foldery, klipy, nuty, audio ze znacznikami warp, automatykę, tempo, znaczniki, mikser z wysyłkami oraz pluginy VST3, VST2 i CLAP z ich stanem. Własne instrumenty i efekty LYNX SHARE są zapisywane tak, żeby LYNX SHARE mógł je znowu otworzyć. Siatka klipów, inne aranżacje i niektóre szczegóły pojedynczych nut są pomijane, a raport je wymienia.

`Plik › Zapisz kopię jako projekt FL Studio Mobile (.flm)...` zapisuje plik `.flm` dla FL Studio Mobile i kopiuje sample do folderu `<name> Samples` obok. Przed zapisem okno wypisuje, co kopia traci, tak jak przy `Plik › Zapisz kopię dla starszego LYNX...`. Plik `.flm` zachowuje nuty i patterny (FL Studio Mobile ma jeden pattern na ścieżkę), tempo, liczbę uderzeń na takt, tonację i zestaw akordów, poziomy, panoramę, wyciszenie i solo miksera oraz wysyłki do busów, własne instrumenty FL Studio Mobile (MiniSynth, SuperSaw, GMSynth i Transistor Bass oraz MiniSynth i Transistor Bass z FL Studio), samplery perkusyjne jako zestaw perkusyjny, Layery jako rack z kilkoma instrumentami, każdy efekt Pocket (FLM) z jego miksem oraz klipy audio z poziomem, panoramą, wysokością dźwięku i odwróceniem. Plik zawiera też DW Sampler (z jego strefami) i 3x Osc oraz klipy automatyki poziomu i panoramy miksera, efektów Pocket (FLM) i własnych wartości DW Samplera i GMSynth. Pozostałe instrumenty (na razie także Slicer i SoundFont Player z FL Studio Mobile) zachowują nuty na MiniSynth z FL Studio Mobile, a efekty bez modułu FLM i automatyka innych celów, takich jak własne pokrętła syntezatorów i wtyczki innych firm, są pomijane; wszystko trafia na listę.

## Warto wiedzieć

- `Dithering` jest wyszarzony dla MP3, OGG, M4A i 32-bitowego WAV, a opcje tempa i znaczników są wyszarzone dla każdego formatu poza WAV.
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
