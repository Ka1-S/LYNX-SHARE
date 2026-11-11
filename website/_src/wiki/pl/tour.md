---
source: edf15b371e
---
![Główne okno z projektem demo: pasek narzędzi na górze, Browser po lewej, Playlist z tyłu i Channel Rack unoszący się nad nim](/assets/img/wiki/desktop.webp)

LYNX SHARE ma jedno główne okno. Wzdłuż góry biegnie pasek narzędzi. Pod nim jest pulpit z pięcioma oknami: Browser, Playlist, Channel Rack, Piano Roll i Mixer. Przy świeżym starcie Browser jest po lewej, Playlist wypełnia resztę, a Channel Rack unosi się nad nim. Piano Roll i Mixer są ukryte, dopóki ich nie wywołasz.

## Jak się rozeznać

1. Wskaż myszą dowolną kontrolkę. Pasek podpowiedzi pod menu mówi, co ona robi i jakich przycisków myszy i klawiszy używa.
2. Naciśnij `F6`, aby ukryć Channel Rack, i `F6` jeszcze raz, aby go pokazać.
3. Naciśnij `F9`, aby pokazać Mixer, a potem `F7` dla Piano Roll.
4. Przeciągnij okno za jego pasek tytułu, aby je przesunąć. Dwuklik na tytule maksymalizuje okno, a kolejny przywraca.
5. Wybierz `Widok › Resetuj układ`, aby wszystko wrócić na miejsce.

## Pasek narzędzi

![Pasek narzędzi: menu i pasek podpowiedzi, transport, gałki master, opcje nagrywania, przyciąganie i pattern, przyciski okien, przyciski plików, wyświetlacz akordów, mierniki i monitor wyjścia](/assets/img/wiki/toolbar-1920.webp)

Od lewej do prawej, w układzie domyślnym:

| Grupa | Co zawiera |
|---|---|
| Menu i pasek podpowiedzi | Menu `Plik`, `Edycja`, `Dodaj`, `Patterny`, `Widok`, `Opcje`, `Narzędzia` i `Pomoc`, a pod nimi pasek podpowiedzi. Zawsze pierwsze |
| Transport | Przyciski trybów `Pattern` i `Song`, Odtwórz, Stop i Nagrywaj, tempo w BPM i pozycja jako takt, miara i krok |
| Głośność master i wysokość | Gałka głośności `MASTER` i gałka `PITCH`, w centach |
| Opcje nagrywania | Metronom, odliczanie, nagrywanie w pętli, wprowadzanie krokowe i pozostałe przełączniki nagrywania |
| Przyciąganie i pattern | Główne przyciąganie (domyślnie `Linia (wg zoomu)`), bieżący pattern, przycisk nowego patternu i menu patternów |
| Skróty do okien | Jeden przycisk na okno oraz wybór pluginu |
| Plik i cofanie | Nowy, Otwórz, Zapisz, Eksportuj, Cofnij i Ponów |
| Wykrywanie akordów | Nazywa akord granego na urządzeniu MIDI lub klawiaturze komputera |
| CPU, pamięć i polifonia | Obciążenie procesora przez audio, zajęta pamięć i liczba głosów |
| Monitor wyjścia | Oscyloskop wyjścia. Klik przełącza jego kolejne tryby |

Istnieje też grupa o nazwie `Multilink do kontrolerów`, domyślnie ukryta.

`Opcje › Pasek narzędzi` zmienia pasek narzędzi: `Edytuj pasek narzędzi (przeciągaj panele, aby je przesuwać, dodawać lub usuwać)`, `Separatory`, `Ukryj panele paska narzędzi`, `Presety`, przełącznik dla każdej grupy i `Rozmiar ikon` (mały, średni lub duży).

## Transport

| Kontrolka | Klawisz | Co robi |
|---|---|---|
| Tryb Pattern | `L` | Gra bieżący pattern w pętli. Prawy klik na przycisku pokazuje Channel Rack |
| Tryb Song | `L` | Gra aranżację z Playlisty. Prawy klik na przycisku pokazuje Playlist |
| Odtwórz | `Space` | Odtwarza. Naciśnięte ponownie, zatrzymuje i wraca tam, gdzie się zaczęło. `Ctrl+Space` pauzuje |
| Stop | | Zatrzymuje i wraca na początek. Dwuklik wysyła też all notes off |
| Nagrywaj | `R` | Włącza lub wyłącza nagrywanie. Zobacz [Nagrywanie](/wiki/recording/) |

## Pasek podpowiedzi

W spoczynku pasek podpowiedzi pokazuje `Point at anything for help`, z `F1` dla ściągawki skrótów, `F5` do `F9` dla okien i `Space` dla odtwarzania i zatrzymania. Nad kontrolką podaje jej nazwę i wymienia, co robią tam przeciąganie, `Ctrl`, `Alt` i prawy klik. Długie zadania, takie jak skanowanie pluginów w tle, raportują w nim postęp.

## Pięć okien

| Okno | Klawisz | Do czego służy |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Układanie klipów patternów, audio i automatyki w utwór |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Jeden wiersz na instrument, z krokami bieżącego patternu |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Nuty jednego kanału w bieżącym patternie |
| [Mixer](/wiki/mixer/) | `F9` | Inserty z suwakami, efekty i routing |
| [Browser](/wiki/browser/) | `Alt+F8` | Sample, presety, zawartość projektu i baza pluginów |

Każdy klawisz pokazuje okno i daje mu fokus albo ukrywa je, gdy już ma fokus. Samo `F8` otwiera wybór pluginu, który nie jest jednym z pięciu. Przyciski okien na pasku narzędzi robią to samo co klawisze. Prawy klik na jednym z nich pokazuje to okno zmaksymalizowane. `Enter` maksymalizuje Playlist lub go przywraca.

## Jak okna się przesuwają i dokują

Każde okno ma pasek tytułu. Po jego lewej stronie jest przycisk menu z opcjami samego okna, a w nim `Odłączony (własne okno)`, `Zmaksymalizowany` i `Zamknij`. Po prawej są przyciski maksymalizacji i zamknięcia.

- Przeciągnij pasek tytułu, aby przesunąć okno. Przyciąga się do innych okien i do krawędzi. Przytrzymaj `Alt`, aby przesuwać bez przyciągania.
- Przeciągnij krawędź, aby zmienić rozmiar.
- `Odłączony (własne okno)` zamienia okno w osobne okno systemowe, przydatne na drugim monitorze. Odłączone okno ma przycisk `Zadokuj`, który je przywraca. `Widok › Odłączone panele (własne okno)` wymienia wszystkie pięć.
- `Widok › Przywróć panele spoza ekranu` odzyskuje okno, które wylądowało poza pulpitem.
- `Widok › Zapisz układ jako...` zapisuje układ pod nazwą, a `Widok › Wczytaj układ` go przywraca.

Browser jest tłem pulpitu: zostaje zadokowany z boku, a pozostałe okna zawsze leżą nad nim.

## Warto wiedzieć

- Dopóki niczego nie przesuniesz, nie zmienisz rozmiaru ani nie odłączysz, okna wracają na domyślne miejsca za każdym razem, gdy główne okno zmienia rozmiar. Potem Twój układ jest zapamiętywany i skaluje się razem z oknem.
- `F12` zamyka okna pluginów, ustawień kanałów i narzędzi. Nie zamyka pięciu głównych okien.
- Gdy pianino z klawiatury komputera jest włączone (`Ctrl+T`), klawisze liter grają nuty, a większość jednoliterowych skrótów jest wstrzymana.
- `Widok › Styl układu` przełącza między pracą na wzorcach (domyślną) a pracą z uruchamianiem klipów, która dodaje obok Playlisty siatkę klipów.

## Powiązane

- [Twój pierwszy utwór](/wiki/first-song/)
- [Układy](/wiki/layouts/)
- [Skróty](/wiki/shortcuts/)
- [Ustawienia](/wiki/settings/)
