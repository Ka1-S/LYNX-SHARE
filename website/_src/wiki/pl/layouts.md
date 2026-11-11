---
source: 1c353ba51c
---
![Praca z uruchamianiem klipów: siatka klipów i scen po lewej, wybór pluginu i Playlist po prawej](/assets/img/cliplaunch.webp)

`Widok › Styl układu` ma w 0.1.25 dwie pozycje: `Praca na wzorcach (domyślnie)` i `Praca z uruchamianiem klipów`. Ten sam wybór jest w `Opcje › Ustawienia ogólne`, w sekcji Sposób pracy. Wybór jest zapamiętywany po zamknięciu programu.

Praca na wzorcach to Channel Rack, patterny i Playlist. Praca z uruchamianiem klipów dodaje obok Playlisty siatkę klipów ze scenami. Patterny i Channel Rack pozostają dostępne.

## Uruchamiaj klipy w siatce

1. Wybierz `Widok › Styl układu › Praca z uruchamianiem klipów`. Obok Playlisty pojawia się siatka klipów.
2. Kliknij pusty slot prawym przyciskiem, wybierz `Umieść tu wzorzec` i wskaż pattern. Dwuklik na pustym slocie umieszcza tam bieżący pattern.
3. Kliknij trójkąt po lewej stronie klipu, żeby go uruchomić. Jeśli transport jest zatrzymany, ruszy.
4. Kliknij trójkąt sceny w kolumnie `Sceny`, żeby uruchomić każdy klip w tym wierszu.
5. Kliknij `Zatrzymaj wszystkie klipy`, żeby je zatrzymać.
6. Kliknij `Powrót do Playlisty`, żeby ścieżki znów grały swoje klipy z Playlisty.

## Co zmienia się na ekranie

- Siatka klipów jest pokazana obok Playlisty. Ścieżki Playlisty to kolumny, a sceny to wiersze.
- Pasek tytułu Playlisty dostaje przełącznik `Oba`, `Siatka`, `Song`. `Siatka` pokazuje tylko siatkę, `Song` tylko Playlistę.
- Za pierwszym razem Channel Rack jest ukryty. Pokazuje go `F6` albo przycisk na pasku narzędzi.
- Ścieżki automatyzacji Playlisty są włączone.
- Układ okien jest zapamiętywany osobno dla każdego stylu, więc każdy styl wraca tak, jak go zostawiłeś.

## Siatka

Uruchomiony klip nie startuje od razu. Czeka na następny punkt kwantyzacji uruchamiania, a jego trójkąt miga do tego czasu. Pole na dole siatki ustawia globalną kwantyzację uruchamiania, której używa każdy klip ustawiony na `Globalne`. Zatrzymanie czeka w ten sam sposób.

Kliknij klip prawym przyciskiem, żeby zobaczyć jego ustawienia.

| Ustawienie | Co robi |
|---|---|
| `Kwantyzacja uruchamiania` | Kiedy ten klip startuje: `Globalne`, `Brak`, od `8 taktów` do `1/32` |
| `Tryb uruchamiania` | `Wyzwalanie`, `Gate`, `Przełączanie` albo `Powtórz` |
| `Legato (zachowaj pozycję)` | Klip przejmuje pozycję odtwarzania klipu, który grał wcześniej na tej ścieżce |
| `Pętla` | Włączone: klip się powtarza. Wyłączone: gra raz, po czym ścieżka milknie |
| `Akcja następcza` | Co dzieje się po klipie: dwie akcje, każda z własnym prawdopodobieństwem, i czas, po którym działają |
| `Delete` | Opróżnia slot |

Kliknij pusty slot prawym przyciskiem, żeby zobaczyć `Przycisk stopu (scena zatrzymuje tu tę ścieżkę)`. Kliknięcie kwadratu po lewej stronie pustego slotu zatrzymuje tę ścieżkę. Kliknij scenę prawym przyciskiem, żeby zobaczyć `Wstaw scenę`, `Dodaj scenę poniżej`, `Usuń scenę`, `Zmień nazwę sceny...` i tempo sceny.

Menu slotu ma też dwie opcje dla całej siatki: `Oddawaj ścieżki Playliście po zatrzymaniu transportu` i `Akcje następcze w całej siatce`.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Ruch | Klawisze strzałek | Zaznacza inny slot |
| Uruchom klip | `Enter` | Uruchamia zaznaczony klip |
| Uruchom scenę | `Ctrl+Enter` | Uruchamia zaznaczoną scenę |
| Usuń klip | `Del` | Opróżnia zaznaczony slot |

## Sety Ableton Live

Gdy otwierasz set Live, jego klipy i sceny Session trafiają do siatki klipów razem z ustawieniami uruchamiania. `Plik › Eksportuj › Set Ableton Live (.als)` zapisuje siatkę z powrotem jako klipy Session. `Opcje › Ustawienia ogólne` ma `Klipy sesji z zestawów Live`, gdzie `Playlista, po aranżacji` umieszcza je zamiast tego na Playliście.

## Wróć

Wybierz `Widok › Styl układu › Praca na wzorcach (domyślnie)`. Układ okien, który miałeś w pracy na wzorcach, wraca, a ścieżki automatyzacji wracają do ustawienia sprzed zmiany. Klipy w siatce zostają w projekcie.

## Warto wiedzieć

- Ścieżka, która uruchomiła klip, milczy w Playliście, dopóki nie naciśniesz `Powrót do Playlisty`. Opcja `Oddawaj ścieżki Playliście po zatrzymaniu transportu` robi to za ciebie przy każdym zatrzymaniu.
- W 0.1.25 slot wypełnia się z jego menu albo dwuklikiem. Nie można przeciągnąć klipu ani pliku do slotu.
- Akcje następcze istnieją dla klipów, nie dla scen.
- Ścieżki automatyzacji włączone ręcznie podczas pracy z uruchamianiem klipów nie są zachowywane po powrocie.
- Przy wybranej pracy na wzorcach nic z siatki nie jest wczytywane.

## Powiązane

- [Playlist](/wiki/playlist/)
- [Przejście z Ableton Live](/wiki/coming-from-ableton/)
- [Import projektów](/wiki/importing-projects/)
- [Co robi tylko LYNX: sposoby pracy](/why/#workflows)

Uproszczonego układu dla początkujących nie ma w 0.1.25. Zobacz [Układ prosty](/wiki/simple-layout/).
