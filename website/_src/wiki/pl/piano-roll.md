---
source: a73763934f
---
![Piano Roll](/assets/img/pianoroll.webp)

Piano Roll pokazuje nuty jednego kanału w bieżącym patternie, z klawiaturą po lewej i ścieżką wartości poszczególnych nut na dole. Naciśnij `F7`, żeby go pokazać lub ukryć, albo wybierz `Piano Roll` w menu kanału w [Channel Racku](/wiki/channel-rack/).

Pole kanału na pasku narzędzi wybiera, który kanał edytujesz. Kanały, które mają nuty w tym patternie, są oznaczone. Kółko nad polem przechodzi przez kanały z nutami, `Ctrl`+kółko przez wszystkie.

## Zapisz kilka nut

1. W Channel Racku kliknij nazwę kanału prawym przyciskiem i wybierz `Piano Roll`.
2. Naciśnij `P`, żeby wybrać Ołówek.
3. Kliknij w siatce, żeby dodać nutę. Trzymaj przycisk i przeciągnij, żeby ją ustawić.
4. Przeciągnij koniec nuty, żeby ją wydłużyć lub skrócić.
5. Kliknij nutę prawym przyciskiem, żeby ją usunąć.
6. Naciśnij `Space`, żeby usłyszeć pattern.

## Narzędzia

![Pasek narzędzi Piano Rolla z polem kanału, narzędziami i polem przyciągania](/assets/img/wiki/pianoroll-tools.webp)

Każde narzędzie ma klawisz literowy. Litery działają, gdy pianino z klawiatury komputera (`Ctrl+T`) jest wyłączone.

| Narzędzie | Klawisz | Co robi |
|---|---|---|
| Ołówek | `P` | Klik dodaje nutę, przeciągnięcie ją ustawia |
| Pędzel | `B` | Przeciągnięcie maluje ciąg nut. Z `Shift` po jednej |
| Pędzel perkusyjny | `N` | Jedno uderzenie na komórkę siatki na klawiszu pod myszą |
| Gumka | `D` | Klik lub przeciągnięcie usuwa |
| Wycisz | `T` | Klik lub przeciągnięcie wycisza albo włącza nuty |
| Nóż | `C` | Przeciągnięcie linii tnie nuty w poprzek, klik tnie w tym punkcie |
| Lasso | `E` | Przeciągnięcie rysuje ramkę zaznaczenia. `Shift` dodaje |
| Zoom | `Z` | Klik przybliża, przeciągnięcie zakresu przybliża do niego, prawy klik oddala |
| Odsłuch | `Y` | Klik lub przeciągnięcie gra nuty pod kursorem |

## Praca z nutami

- Przeciągnij nutę, żeby ją przesunąć. `Shift`+przeciągnięcie robi kopię.
- Przeciągnij koniec nuty, żeby zmienić jej długość. Można też przeciągnąć początek, a koniec zostaje na miejscu.
- Prawy klik usuwa nutę, w każdym narzędziu.
- Dwuklik na nucie otwiera jej właściwości.
- `Alt`+kółko nad nutą zmienia jej velocity.
- `Ctrl`+przeciągnięcie zaznacza. `Ctrl`+klik dodaje nutę do zaznaczenia albo ją z niego usuwa.
- Przytrzymaj `Alt` podczas przeciągania, żeby pominąć przyciąganie. `Backspace` wyłącza i włącza przyciąganie.
- Przy kilku zaznaczonych nutach uchwyt rozciągania rozciąga je lub ściska w czasie.

## Klawiatura i linijka

Kliknij klawisz na klawiaturze, żeby go usłyszeć. Im dalej w prawo na klawiszu, tym głośniej. `Ctrl`+klik klawisza zaznacza jego nuty. Przeciągnięcie środkowym przyciskiem na klawiaturze robi klawisze wyższe lub niższe. To samo robi pionowe kółko zoomu, małe kwadratowe pole na końcu paska przewijania czasu: przeciągnij je w górę lub w dół albo użyj kółka myszy nad nim.

`Widok › Styl klawiatury` w menu Piano Roll wybiera między `Płaskie klawisze` a `Klawisze fortepianu (linie między białymi klawiszami)`, który rysuje tradycyjną klawiaturę.

Kliknij linijkę, żeby ustawić pozycję. `Ctrl`+przeciągnięcie, przeciągnięcie prawym przyciskiem albo dwuklik z przeciągnięciem na linijce zaznacza nuty w zakresie czasu. `Ctrl`+kółko zmienia powiększenie, a `Shift`+kółko przewija.

## Pasek narzędzi

| Ustawienie | Co robi |
|---|---|
| Przyciąganie | "Główne" podąża za przyciąganiem na głównym pasku narzędzi albo wybierasz własne dla tego okna. |
| Akord | Kliknięcie tworzy wybrany akord, z klikniętą nutą jako prymą. `Shift+N` wraca do pojedynczych nut. Ta sama lista to `Stempel akordu` w menu Piano Roll. |
| Skala | Podświetla nuty skali. Osobny przycisk przyciąga nowe i przesunięte nuty oraz przychodzące MIDI do wybranej skali. Przesunięte nuty przesuwają się wtedy o stopnie skali, więc akord zostaje w skali, a `Up` i `Down` przesuwają o jeden stopień skali. |
| Slide (`S`) | Nowe nuty są nutami slide. Brzmiąca nuta ślizga się do nich (glide 808). |
| Nuty-duchy (`Alt+V`) | Pokazuje nuty innych kanałów za twoimi. |
| Kolor nut | Nowe nuty dostają ten kolor. Jest ich 16. |
| Podążanie | Widok przewija się z kursorem odtwarzania, gdy pattern gra. |
| Pole ścieżki | Wybiera, co edytuje dolna ścieżka: Velocity, Panoramę, Dostrojenie, Release, Mod X, Mod Y, Szansę lub Zakres velocity. |

## Szansa i zakres velocity

Każda nuta ma Szansę i Zakres velocity. Szansa to prawdopodobieństwo, że nuta zagra za każdym razem, gdy przychodzi jej kolej. Zakres velocity pozwala jej velocity zmieniać się między velocity nuty a tym velocity plus zakres. Wybierz `Szansa` lub `Zakres velocity (LYNX)` w polu ścieżki, żeby je edytować. `Narzędzia › Szansa (LYNX)` w menu Piano Roll grupuje zaznaczone nuty tak, że gra albo każda z nich, albo tylko jedna, i ma `Resetuj szansę i zakres velocity`.

## Narzędzia nut

Menu `Narzędzia` w Piano roll otwiera narzędzia nut z FL Studio jako okna dialogowe. Działają na zaznaczonych nutach albo na wszystkich nutach, gdy nic nie jest zaznaczone. Niektóre z nich:

| Narzędzie | Klawisz | Co robi |
|---|---|---|
| Limit | `Alt+K` | Przesuwa nuty o oktawy do zakresu klawiszy, a potem je przycina. W `Ograniczenie tonacji / skali` utrzymuje nuty na klawiszu bazowym i jednym z akordów lub skal FL Studio, z `Przyciąganie` ustawionym na `Powyżej`, `Poniżej` lub `Naprzemiennie`. `Zawijaj do góry / dołu` i `Transpozycja` ustawiają, dokąd trafiają nuty spoza zakresu |
| Flip | `Alt+Y` | `Odwróć w poziomie` odbija nuty w czasie. Z `Zachowaj czasy początku`, na początku włączonym, czasy startu są odbijane, a nuty zachowują swoje długości. `Odwróć w pionie` odbija klawisze względem środka ich zakresu |
| Randomise | `Alt+R` | Losowe zmiany velocity, panoramy, drobnej wysokości, czasu i długości oraz Mod X, Mod Y i release |
| Scale levels | `Alt+X` | Skaluje poziom nuty od początku do końca zaznaczenia, z `Przesunięcie`, `Środek` (wartość, wokół której skaluje) i `Napięcie` |
| LFO | `Alt+O` | Zapisuje kształt LFO w poziomie nuty. Kliknij suwak `Okres` prawym przyciskiem, aby zobaczyć listę od 1 kroku do 512 kroków |

## Narzędzia MIDI i skrypty

`Narzędzia › Narzędzia MIDI...` w menu Piano Roll otwiera panel z Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp i Velocity Shaper.

Kolumna `Skrypty` menu `Narzędzia` wymienia skrypty Piano Roll: własne skrypty LYNX SHARE i pliki `.pyscript`, także te z folderów skryptów FL Studio, jeśli FL Studio jest zainstalowane. `Otwórz folder skryptów...` otwiera folder na własne skrypty, a `Uruchom ponownie ostatni skrypt` powtarza ostatni.

## Klawisze

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Zaznacz wszystko | `Ctrl+A` | Zaznacza każdą nutę |
| Odznacz | `Ctrl+D` lub `Esc` | Czyści zaznaczenie |
| Kopiuj, wytnij, wklej | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiuje, wycina lub wkleja nuty |
| Duplikuj | `Ctrl+B` | Duplikuje zaznaczenie |
| Usuń | `Del` | Usuwa zaznaczenie |
| Transpozycja | `Up` / `Down` | Przesuwa zaznaczenie o półton |
| Transpozycja o oktawę | `Ctrl+Up` / `Ctrl+Down` | Przesuwa zaznaczenie o oktawę |
| Przesunięcie | `Left` / `Right` | Przesuwa zaznaczenie o jeden krok siatki |
| Szybka kwantyzacja | `Ctrl+Q` | Kwantyzuje początki i długości |
| Kwantyzacja początków | `Shift+Q` | Kwantyzuje tylko początki |
| Cięcie | `Ctrl+U` | Tnie nuty do siatki |
| Sklejanie | `Ctrl+G` | Łączy stykające się nuty |
| Legato | `Ctrl+L` | Sprawia, że każda nuta trwa do następnej |
| Wycisz, włącz | `Alt+M` / `Shift+Alt+M` | Wycisza lub włącza zaznaczenie |
| Import MIDI | `Ctrl+M` | Importuje plik MIDI do tego kanału |
| Eksport MIDI | `Ctrl+Shift+M` | Eksportuje pattern jako plik MIDI |
| Zoom | `PageUp` / `PageDown` | Przybliża lub oddala |

Narzędzia nut (Arpeggiuj, Strum, Odwróć, Losuj, Kwantyzuj, Tnij i inne) otwierają okna dialogowe. Ich klawisze są na stronie [Skróty](/wiki/shortcuts/).

## Warto wiedzieć

- Piano Roll pokazuje jeden kanał w jednym patternie. Zmień bieżący pattern, a pokaże nuty tamtego patternu.
- Litery narzędzi nic nie robią, gdy pianino z klawiatury komputera (`Ctrl+T`) jest włączone.
- Prawy klik usuwa każdym narzędziem, więc nie trzeba przełączać się na Gumkę dla jednej nuty.
- `S` jest tu przełącznikiem Slide. W Playliście ta sama litera to narzędzie Slip.
- Nuty, które nie są zwykłymi krokami (inne klawisze, dłuższe nuty, slide'y), sprawiają, że wiersz kanału w Channel Racku pokazuje miniaturę zamiast przycisków kroków.

## Powiązane

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Skróty](/wiki/shortcuts/)
- [Formaty plików](/wiki/file-formats/)
