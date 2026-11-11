---
source: 8df354b49e
---
![Browser po lewej stronie z trzema kartami, polem wyszukiwania, drzewem folderów i paskiem odsłuchu](/assets/img/wiki/browser-tree.webp)

Browser pokazuje Twoje foldery z samplami, zawartość bieżącego projektu i pluginy, które zna LYNX SHARE. `Alt+F8` pokazuje go lub ukrywa. Samo `F8` otwiera wybór pluginu, osobny pełnoekranowy widok wszystkich instrumentów i efektów.

## Wczytanie sampla do projektu

1. Naciśnij `Alt+F8`, aby pokazać Browser.
2. Na karcie `WSZYSTKO` kliknij folder, aby go otworzyć.
3. Kliknij plik audio, aby go posłuchać. `Up` i `Down` przechodzą po plikach i odtwarzają każdy z nich.
4. Kliknij plik dwukrotnie, aby dostać nowy kanał z samplem w [Channel Rack](/wiki/channel-rack/).
5. Albo przeciągnij plik na [Playlist](/wiki/playlist/), aby dostać klip audio.

## Karty

| Karta | Pokazuje |
|---|---|
| WSZYSTKO | Bieżący projekt, bazę pluginów i każdy folder z samplami |
| PROJEKT | Tylko to, czego używa bieżący projekt: patterny, efekty, generatory, automatykę, pliki audio |
| Ulubione | Pliki i foldery oznaczone gwiazdką (prawy klik, potem `Ulubione`) |

## Przeglądanie i odsłuch

- Kliknij folder, aby go otworzyć lub zamknąć. Prawy klik otwiera menu (zwiń wszystko, pokaż w menedżerze plików).
- Kliknij plik audio, aby go posłuchać. Pasek odsłuchu pod listą pokazuje jego przebieg. Kliknij pasek, aby usłyszeć plik jeszcze raz, albo przeciągnij z niego tak, jakby to był sam plik.
- Panel nad paskiem odsłuchu pokazuje datę, rozmiar i ścieżkę zaznaczonego pliku.

## Użycie sampla

| Działanie | Co robi |
|---|---|
| Dwuklik | Nowy kanał z samplem |
| Przeciągnięcie do Channel Rack | Nowy kanał albo upuszczenie na wiersz, aby wczytać sampla właśnie tam |
| Przeciągnięcie do Playlisty | Klip audio |
| `Enter` | Wczytuje plik do zaznaczonego kanału |
| `Shift+Up` / `Shift+Down` | Przechodzi do poprzedniego lub następnego pliku i wczytuje go do zaznaczonego kanału |

Ostatni skrót pozwala przesłuchiwać różne sample na miejscu, gdy pattern gra.

## Inne typy plików

- **Projekt**: dwuklik go otwiera.
- **Plik MIDI**: dwuklik otwiera go jako projekt.
- **SoundFont lub SFZ**: klik daje odsłuch, dwuklik tworzy kanał LYNX Soundfont (albo wczytuje plik do zaznaczonego).
- **Preset miksera**: dwuklik wczytuje go na zaznaczone inserty Mixera albo przeciągnij go na insert.
- **Preset efektu**: przeciągnij go na insert Mixera.

## Baza pluginów

Pozycja Baza pluginów zawiera wbudowane instrumenty i efekty LYNX SHARE oraz zainstalowane pluginy. `Shift+F8` przeskakuje do niej.

- Kliknij instrument dwukrotnie, aby dodać go jako nowy kanał, albo przeciągnij go na Channel Rack.
- Kliknij efekt dwukrotnie, aby dodać go do zaznaczonego inserta [Mixera](/wiki/mixer/), albo przeciągnij go na insert lub na slot efektu.
- `F2` zmienia nazwę zaznaczonego pluginu.
- Kliknij plugin prawym przyciskiem i wybierz `Przenieś do folderu`, aby posortować pluginy w folderach. Menu slotu w Mixerze i menu dodawania w Channel Rack wymieniają wtedy każdy folder najwyższego poziomu.

## Karta PROJEKT

Patterny wymienione tutaj możesz kliknąć, aby uczynić je bieżącym patternem, kliknąć dwukrotnie, aby otworzyć je w [Piano Roll](/wiki/piano-roll/), albo przeciągnąć na Playlist. Sekcja bieżącego projektu wymienia też historię cofania: kliknij krok, aby do niego wrócić.

## Wyszukiwanie

`Ctrl+F` ustawia kursor w polu wyszukiwania. Pole przeszukuje zaznaczony folder (lub wszystkie foldery) i podfoldery. Na karcie PROJEKT przeszukuje elementy projektu.

Wszystkie słowa muszą pasować. Pole przyjmuje też `OR`, minus do wykluczenia słowa i symbole wieloznaczne `*`:

```
kick -loop
snare OR clap
hat*.wav
```

Foldery są indeksowane w tle. `Odśwież indeks wyszukiwania` w menu Browsera buduje indeks od nowa.

## Własne foldery

Użyj `Dodaj folder` na liście folderów Browsera, aby dodać folder z samplami, projektami lub SoundFontami. Foldery wyszukiwania znajdziesz w `Opcje › Ustawienia plików (foldery wyszukiwania)`.

## Klawisze

Klawisze strzałek, `Enter` i `F2` działają, gdy Browser ma fokus.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Pokaż Browser | `Alt+F8` | Pokazuje lub ukrywa Browser |
| Wybór pluginu | `F8` | Otwiera pełnoekranowy wybór pluginu |
| Baza pluginów | `Shift+F8` | Przeskakuje do pozycji Baza pluginów |
| Wyszukiwanie | `Ctrl+F` | Ustawia kursor w polu wyszukiwania |
| Krok i odsłuch | `Up` / `Down` | Zaznacza poprzedni lub następny plik i go odtwarza |
| Zamknij folder | `Left` | Zamyka folder albo przechodzi do folderu nadrzędnego |
| Otwórz folder | `Right` | Otwiera folder |
| Wczytaj | `Enter` | Wczytuje plik do zaznaczonego kanału |
| Krok i wczytanie | `Shift+Up` / `Shift+Down` | Przechodzi do poprzedniego lub następnego pliku i wczytuje go do zaznaczonego kanału |
| Zmień nazwę pluginu | `F2` | Zmienia nazwę zaznaczonego pluginu w bazie pluginów |

## Warto wiedzieć

- Usunięcie folderu z listy nie kasuje go z dysku.
- `F8` i `Alt+F8` to dwie różne rzeczy. `F8` to wybór pluginu, `Alt+F8` to Browser.
- Wyniki wyszukiwania pochodzą z indeksu budowanego w tle. `Odśwież indeks wyszukiwania` w menu Browsera buduje go od nowa.
- `Enter` wczytuje plik do zaznaczonego kanału. Dwuklik tworzy nowy kanał.

## Powiązane

- [Channel Rack](/wiki/channel-rack/)
- [Pluginy zewnętrzne](/wiki/plugins/)
- [Formaty plików](/wiki/file-formats/)
- [Cofanie i historia](/wiki/undo-history/)
- [Instrumenty i efekty](/instruments-and-effects/)
