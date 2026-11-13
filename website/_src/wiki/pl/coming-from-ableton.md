---
source: d5cc625221
---
![Układ Clip-launch: siatka klipów ze scenami obok Playlisty](/assets/img/cliplaunch.webp)

LYNX SHARE startuje w trybie pracy na wzorcach: Channel Rack, patterny i Playlist. Praca z uruchamianiem klipów jest opcjonalna i dodaje obok Playlisty siatkę klipów ze scenami. LYNX SHARE odczytuje i zapisuje sety `.als`. Nie jest powiązany z firmą Ableton.

## Otwórz set Live

1. Wybierz `Plik › Otwórz...` (`Ctrl+O`) i wskaż plik `.als`. Odczytywane są sety zapisane w Live 11 i Live 12.
2. Wybierz `Widok › Styl układu › Praca z uruchamianiem klipów`, aby pracować z siatką klipów, do której trafiają klipy sesji i sceny z setu.
3. Przełącznik `Grid / Both / Song` na pasku tytułu Playlisty pokazuje siatkę, Playlist albo oba naraz.
4. Wybierz `Plik › Zapisz`. Program zapyta, gdzie zapisać plik `.lynx`. Plik `.als` nie jest zmieniany.

`Widok › Styl układu › Praca na wzorcach (domyślnie)` wraca do poprzedniego trybu. Ten sam wybór jest w `Opcje › Ustawienia ogólne...`. Patterny i Channel Rack są dostępne w obu układach.

## Co odpowiada czemu

| W Live | W LYNX SHARE |
|---|---|
| Widok Session | Siatka klipów w trybie pracy z uruchamianiem klipów |
| Widok Arrangement | Playlist |
| Ścieżki | Ścieżki Playlisty, które są kolumnami siatki klipów |
| Sceny | Sceny, czyli wiersze siatki klipów. Scena może ustawiać tempo przy uruchomieniu |
| Kwantyzacja uruchamiania | `Globalne`, `Brak`, od 8 taktów do 1 taktu albo wartość nuty od 1/2 do 1/32, w razie potrzeby osobno dla każdego klipu |
| Tryby uruchamiania | `Wyzwalanie`, `Gate`, `Przełączanie` i `Powtórz` |
| Akcje następcze | Akcja A i Akcja B, każda z własnym prawdopodobieństwem |
| Przyciski stopu klipów | Pusty slot może mieć przycisk stopu |
| Back to Arrangement | `Powrót do Playlisty` |
| Tryby warp | `Miary`, `Tony`, `Tekstura` i `Złożony` w `Ustawienia kanału › Rozciąganie czasu` |
| Simpler, Sampler i racki perkusyjne | Samplery LYNX SHARE |
| Set `.als` | Plik projektu `.lynx` |

## Siatka klipów

Slot zawiera klip patternu albo klip audio.

- Ścieżka odtwarza naraz jeden klip. Uruchomiony klip zastępuje ten, który gra.
- Uruchomienie czeka na następny krok kwantyzacji uruchamiania.
- `Legato` zaczyna nowy klip od pozycji, do której doszedł zastępowany klip. `Pętla` ustawia się osobno dla klipu.
- Sceny można wstawiać, usuwać i zmieniać im nazwy.
- Ścieżka, która uruchomiła klip, milczy w Playliście, dopóki nie naciśniesz `Powrót do Playlisty`. Opcja oddaje ścieżki, gdy transport się zatrzyma.

Akcje następcze uruchamiają się po określonej liczbie taktów lub przejść klipu. Dostępne akcje to `Brak akcji`, `Stop`, `Odtwórz ponownie`, `Poprzedni`, `Następny`, `Pierwszy`, `Ostatni`, `Dowolny` i `Inne`. Przełącznik włącza lub wyłącza akcje następcze w całej siatce.

## Co jest importowane z pliku .als

- Ścieżki MIDI, audio, grupowe i return, tempo, metrum, lokatory, pętla, ustawienia miksera (głośność, panorama, wyciszenie, solo, wysyłki) i ich automatyka.
- Pluginy VST2 i VST3 wraz ze stanem, jeśli ten sam plugin jest zainstalowany. Zobacz [Pluginy zewnętrzne](/wiki/plugins/).
- Klipy sesji i sceny trafiają do siatki klipów razem z trybem uruchamiania, kwantyzacją, legato, pętlą, akcjami następczymi i przyciskami stopu. Aby umieścić je zamiast tego na Playliście, po jednej sekcji na scenę, zmień `Klipy sesji z zestawów Live` w `Opcje › Ustawienia ogólne...`.
- Znaczniki warp.

## Eksport do pliku .als

1. Wybierz `Plik › Eksportuj › Set Ableton Live (.als)...`.
2. W `Zapisz dla` wybierz `Live 12` lub `Live 11 (otwiera się też w Live 12)`.
3. W `Próbki` skopiuj je do folderu projektu setu albo wskaż je tam, gdzie leżą.
4. Wybierz folder. Set zostanie zapisany jako `<name> Project/<name>.als`.

| Zapisywane | Niezapisywane |
|---|---|
| Ścieżka MIDI na każdy kanał, z nutami jako klipami MIDI | Własne instrumenty i efekty LYNX SHARE. Ich ścieżki i nuty zostają |
| Ścieżki audio z rozciąganiem, transpozycją, wzmocnieniem, fade'ami i trybem warp | Automatyka parametrów efektów |
| Grupy, returny i wysyłki | Trasy między insertami |
| Pluginy VST2 i VST3 wraz ze stanem | |
| Znaczniki oraz automatyka głośności, panoramy i tempa | |
| Siatka klipów jako klipy sesji i sceny, racki i Racki perkusyjne | |

Raport po eksporcie wymienia, co pominięto.

## Warto wiedzieć

- Wiele własnych urządzeń Live wchodzi jako własne urządzenia LYNX SHARE, z ustawieniami i automatyką: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift i Operator (bez FM). Pozostałe są pomijane i wymienione w raporcie importu.
- Racki Instrument i Audio Effect wchodzą ze swoimi łańcuchami, strefami i makrami, a Rack perkusyjny staje się Rackiem perkusyjnym LYNX SHARE. Racki MIDI Effect z Live nie są jeszcze konwertowane i są wymienione w raporcie importu.
- Raport importu wymienia, co pominięto.
- Live przycina klipy audio, które na siebie nachodzą, w wyeksportowanym secie.
- Znaczniki warp z setu Live są zachowywane przy imporcie i zapisywane z powrotem przy eksporcie.

## Powiązane

- [Playlist](/wiki/playlist/)
- [Układy](/wiki/layouts/)
- [Import projektów](/wiki/importing-projects/)
- [Formaty plików](/wiki/file-formats/)
- [Dwa tryby pracy w jednym programie](/why/#workflows)
