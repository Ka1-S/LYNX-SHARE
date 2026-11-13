---
source: 1b54c6493f
---
![Playlist z klipami na ścieżkach](/assets/img/playlist.webp)

Playlist to miejsce, w którym patterny stają się utworem. Na jego ścieżkach leżą trzy rodzaje klipów: klipy patternów, klipy audio i klipy automatyki. `F5` pokazuje go lub ukrywa, a `Enter` maksymalizuje go lub przywraca.

Transport ma dwa tryby. Tryb Pattern gra bieżący pattern w pętli. Tryb Song gra Playlist. `L` przełącza.

## Umieść pattern w utworze

1. Naciśnij `F5`, aby pokazać Playlist.
2. W panelu wyboru kliknij pattern. Panel wyboru wymienia patterny, audio i automatykę projektu, a ten, który klikniesz, staje się źródłem klipów.
3. Naciśnij `P`, aby wybrać Ołówek.
4. Kliknij na ścieżce, aby umieścić klip. Przytrzymaj przycisk i przeciągnij, aby go ustawić.
5. Naciśnij `B`, aby wybrać Pędzel, i przeciągnij wzdłuż ścieżki, aby malować klipy jeden za drugim.
6. Naciśnij `L`, aby włączyć tryb Song, a potem `Space`, aby odtworzyć.

Pliki audio można przeciągać z [Browsera](/wiki/browser/) na Playlist.

## Narzędzia

![Pasek narzędzi Playlisty z polem aranżacji, narzędziami i polem przyciągania](/assets/img/wiki/playlist-tools.webp)

Litery działają, gdy pianino z klawiatury komputera (`Ctrl+T`) jest wyłączone.

| Narzędzie | Klawisz | Co robi |
|---|---|---|
| Ołówek | `P` | Klik umieszcza klip z panelu wyboru |
| Pędzel | `B` | Przeciąganie maluje klipy jeden za drugim |
| Gumka | `D` | Klik lub przeciąganie usuwa |
| Wycisz | `T` | Klik lub przeciąganie wycisza lub przywraca klipy |
| Nóż | `C` | Przeciągnięcie linii tnie klipy w poprzek, klik tnie w tym punkcie |
| Lasso | `E` | Przeciąganie rysuje ramkę zaznaczenia. `Shift` dodaje |
| Zoom | `Z` | Klik przybliża, przeciągnięcie zakresu przybliża do niego, prawy klik oddala |
| Odsłuch | `Y` | Klik lub przeciąganie odtwarza od tego punktu |
| Slip | `S` | Przeciąganie przesuwa zawartość klipu wewnątrz klipu |

## Praca z klipami

- Przeciągnij klip, aby go przesunąć. `Shift`+przeciąganie robi kopię.
- Przeciągnij koniec klipu, aby zmienić jego rozmiar. Przeciągnięcie początku przycina go, a zawartość zostaje na miejscu.
- Prawy klik usuwa klip. `Ctrl`+przeciąganie zaznacza.
- Kliknij klip dwukrotnie, aby go otworzyć.
- Przycisk w lewym górnym rogu klipu otwiera menu klipu (uczyń unikalnym, zmień nazwę i kolor, potnij, a dla audio: wysokość, dopasuj do tempa, odwróć).
- Menu klipu audio podąża za układem FL Studio w dwóch kolumnach. Lewa kolumna ma sekcję `Klip audio` (`Podgląd`, `Wyciszony`, `Zmień nazwę i kolor...`, `Zmień kolor...`, `Losowy kolor`, `Zmień ikonę...`, `Zaznacz kanał źródłowy`, `Ustawienia kanału`, `Uczyń unikalnym`, `Zaznacz wszystkie podobne klipy`, `Delete`, `Właściwości...`) i sekcję `Region` (`Wybierz region`, `Tnij`). Prawa kolumna ma `Sample` (`Uczyń unikalnym jako sampel`, `Edytuj sampel`, `Wyodrębnij stemy z sampla`, `Wykryj tempo`, `Dopasuj do tempa`), `Automatyka` (`Automatyzuj › Panorama` lub `Głośność`, `Przenikanie z`) i `Wejścia i wyjścia` (`Resetuj wejścia i wyjścia`). Własne pozycje LYNX SHARE, takie jak `Podziel tutaj`, `Odwróć`, `Normalizuj` i `Konwertuj na MIDI`, są na końcu, pod linią.
- `Automatyzuj › Panorama` lub `Głośność` tworzy klip automatyzacji tak długi jak klip audio, na jego ścieżce, dla panoramy lub głośności z Levels adjustment kanału.
- Przytrzymaj `Alt` podczas przeciągania, aby zignorować przyciąganie.
- Klipy audio mają uchwyty fade i wzmocnienie klipu, które przeciągasz w górę lub w dół. Kliknij prawym przyciskiem uchwyt fade, żeby otworzyć jego menu z `Utwórz przenikanie`. Kliknij prawym przyciskiem uchwyt wzmocnienia, żeby dostać `Resetuj gain` i `Normalizuj`.
- `Shift+M` przełącza Stretch: zmiana rozmiaru klipu audio rozciąga wtedy dźwięk zamiast go przycinać.
- `Shift+Left` i `Shift+Right` przesuwają zaznaczone klipy o jeden krok przyciągania, `Shift+Up` i `Shift+Down` o jedną ścieżkę. `Alt+Left` i `Alt+Right` przesuwają je o jeden piksel ekranu przy bieżącym zoomie, bez przyciągania. Gdy nie zaznaczono żadnego klipu, przesuwają się wszystkie klipy.
- `Uczyń unikalnym jako sampel` w menu klipu audio daje klipowi nowy plik z tym, co klip grał, z jego wysokością, rozciągnięciem i odwróceniem już zapisanymi w audio.
- Klip audio z markerami warp pokazuje je jako małe flagi na górnej krawędzi. Przeciągnij flagę w bok, żeby przesunąć marker.

## Menu Edit i Select

Menu Playlisty ma podmenu `Edycja` i `Zaznacz`. `Edycja` zawiera polecenia klipów: wytnij, kopiuj i wklej, `Przesuń w lewo`, `Przesuń w prawo`, `Przesuń w górę`, `Przesuń w dół`, `Uczyń unikalnym`, `Uczyń unikalnym jako sampel`, `Wstaw odstęp`, `Usuń odstęp`, polecenia scalania i opcje fade. `Zaznacz` ma `Zaznacz według wybranego źródła` (`Shift+C`), `Odwróć zaznaczenie` (`Shift+I`), `Odznacz` (`Ctrl+D`), klipy wyciszone i nakładające się oraz `Magiczne lasso`. Przy włączonym `Magiczne lasso` poruszaj myszą w pętli wokół klipów, bez wciskania przycisku, żeby je zaznaczyć.

## Ścieżki

W nagłówku ścieżki klik zaznacza, `Ctrl`+klik dodaje lub usuwa, `Shift`+klik zaznacza zakres, a prawy klik otwiera menu ścieżki. `Shift`+kółko przesuwa ścieżkę w górę lub w dół. Lampka ścieżki wycisza ją (klik) lub włącza solo (`Ctrl`+klik lub prawy klik). Przeciągnij linię między dwiema ścieżkami, aby zmienić rozmiar jednej, albo `Shift`+przeciąganie, aby zmienić rozmiar wszystkich.

`Widok` w menu Playlisty wybiera, co pokazują nagłówki ścieżek: `Pokazuj kontrolki na ścieżkach audio`, `Pokazuj poziomy na ścieżkach audio` i `Pokazuj poziomy na ścieżkach instrumentów`.

`Tryb ścieżki` w menu ścieżki ustawia ścieżkę jako `Nieprzypisana`, `Ścieżka audio` lub `Ścieżka instrumentu`, jak w FL Studio. `Ścieżka audio` wymienia najpierw `Nowe ścieżki miksera`, a potem każdy insert miksera według numeru i nazwy. Wybór dotyczy wszystkich zaznaczonych ścieżek. `Ścieżka instrumentu` ma `Użyj istniejącego kanału`, `Więcej wtyczek...` i listę instrumentów z Channel rack.

Znak `+` w lewym górnym rogu nagłówków ścieżek otwiera te same wybory dla nowej ścieżki: `Dodaj ścieżkę audio`, `Więcej wtyczek...` i instrumenty. Kliknięcie `+` prawym przyciskiem od razu dodaje ścieżkę audio.

## Bloki patternów

Gdy w menu Playlisty włączone jest `Bloki patternów (bieżący projekt)`, sekcja pod ścieżkami ma jeden wiersz na pattern. Tam Ołówek stawia jeden blok i przeciąga go wzdłuż wiersza, Pędzel maluje bloki, a Gumka je usuwa. Prawy przycisk myszy kasuje dowolnym narzędziem.

## Linijka, pętla i znaczniki

- Kliknij linijkę, aby ustawić pozycję utworu.
- Prawe przeciąganie, `Ctrl`+przeciąganie lub dwuklik z przeciąganiem na linijce tworzy zaznaczenie czasu, które jest też pętlą. `Shift`+przeciąganie je przesuwa.
- `Ctrl+Ins` wstawia długość zaznaczenia czasu na wszystkich ścieżkach, `Ctrl+Del` ją usuwa.
- `Alt+T` dodaje znacznik. Kliknij znacznik, aby do niego skoczyć.
- `Ctrl`+kółko zmienia zoom, `Alt`+kółko zmienia wysokość ścieżek.

## Automatyka

Kliknij gałkę lub suwak prawym przyciskiem i wybierz `Utwórz klip automatyki`. Klip pojawia się na Playliście. Ołówkiem przeciągaj jego punkty, a kliknięcie na krzywej dodaje nowy. Tory automatyki pod ścieżkami są opcjonalne i domyślnie wyłączone. Zobacz [Automatyka](/wiki/automation/).

Kliknij punkt prawym przyciskiem, aby zobaczyć jego menu. Jego lista `Tryb` ma 13 trybów krzywej z FL Studio dla odcinka kończącego się w tym punkcie: `Wygładź`, `Hold`, trzy krzywe pojedyncze, trzy krzywe podwójne, `Półsinus`, `Schody`, `Gładkie schody`, `Puls` i `Fala`. Nowy punkt przyjmuje ostatnio używany tryb. Tryby są odczytywane z plików .flp i do nich zapisywane.

## Aranżacje

Pole aranżacji na pasku narzędzi przełącza między aranżacjami. Projekt może mieć ich kilka.

## Klawisze

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Zaznacz wszystko | `Ctrl+A` | Zaznacza każdy klip |
| Kopiuj, wytnij, wklej | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiuje, wycina lub wkleja klipy |
| Duplikuj | `Ctrl+B` | Duplikuje zaznaczenie |
| Usuń | `Del` | Usuwa zaznaczone klipy |
| Usuń z zamknięciem luk | `Shift+Del` | Usuwa zaznaczone klipy i zamyka luki |
| Edycja ripple | `Shift+S` | Włącza lub wyłącza edycję ripple |
| Wycisz, przywróć | `Alt+M` / `Shift+Alt+M` | Wycisza lub przywraca zaznaczone klipy |
| Przesuń klipy | `Shift+Left` / `Shift+Right` | Przesuwa zaznaczone klipy o jeden krok przyciągania |
| Przesuń na inną ścieżkę | `Shift+Up` / `Shift+Down` | Przesuwa zaznaczone klipy o jedną ścieżkę |
| Przesuń klipy delikatnie | `Alt+Left` / `Alt+Right` | Przesuwa zaznaczone klipy o jeden piksel ekranu, bez przyciągania |
| Uczyń unikalnym | `Ctrl+Shift+V` | Czyni zaznaczone klipy unikalnymi |
| Scal | `Ctrl+G` | Scala klipy patternów |
| Stretch | `Shift+M` | Przełącza Stretch przy zmianie rozmiaru klipów audio |
| Wstaw czas | `Ctrl+Ins` | Wstawia długość zaznaczenia czasu na wszystkich ścieżkach |
| Usuń czas | `Ctrl+Del` | Usuwa zaznaczenie czasu na wszystkich ścieżkach |
| Dodaj znacznik | `Alt+T` | Dodaje znacznik |
| Zoom | `PageUp` / `PageDown` | Przybliża lub oddala |
| Zoom do dopasowania | `Shift+4` | Przybliża tak, aby pokazać wszystko |
| Maksymalizuj | `Enter` | Maksymalizuje Playlist lub go przywraca |

## Warto wiedzieć

- Playlist słychać tylko w trybie Song. W trybie Pattern `Space` gra bieżący pattern, niezależnie od tego, co leży na ścieżkach.
- `Widok › Styl układu › Praca z uruchamianiem klipów` dodaje obok Playlisty siatkę klipów i scen. Jest opcjonalny. Zobacz [Układy](/wiki/layouts/).
- Gdy na jednym elemencie sterującym leży kilka nakładających się klipów automatyki, wybór tego, który gra, może się różnić od FL Studio (notatki do wydania 0.1.25).
- Jak w FL Studio, pattern gra z jednego klipu naraz. Gdy zaczyna się późniejszy klip tego samego patternu, na dowolnej ścieżce, wcześniejszy klip od tego miejsca nie startuje już żadnych nut. Klipy różnych patternów grają razem. Dotyczy to nowych projektów i projektów FL Studio. Projekt .lynx zapisany przed 0.1.27 nadal gra każdy klip w całości.
- Tempo przesunięte przez powiązany kontroler MIDI nie zmienia rozmiaru klipów audio.
- W Linuksie wideo wymaga FFmpeg.

## Powiązane

- [Channel Rack](/wiki/channel-rack/)
- [Automatyka](/wiki/automation/)
- [Nagrywanie](/wiki/recording/)
- [Eksport](/wiki/exporting/)
- [Układy](/wiki/layouts/)
