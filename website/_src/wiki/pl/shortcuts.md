---
source: 5efc5c8254
---
![Okno Zarządzaj skrótami: przeszukiwalna lista akcji z klawiszami każdej z nich](/assets/img/wiki/shortcuts.webp)

To domyślne klawisze w wersji 0.1.25 alfa. `Pomoc › Skróty` (`F1`) pokazuje ściągawkę skrótów w programie. `Opcje › Zarządzaj skrótami...` otwiera edytor widoczny na obrazku, w którym je zmieniasz.

> W macOS program pokazuje klawisze pod nazwami z Maca: Ctrl jest pokazywany jako ⌘, a Alt jako ⌥.

## Zmiana skrótu

1. Wybierz `Opcje › Zarządzaj skrótami...`.
2. Wpisz w polu wyszukiwania akcję albo klawisz, na przykład `Ctrl+L`. Lista obok ogranicza wyszukiwanie do jednego miejsca, a `Tylko zmienione` pokazuje te, które zmieniłeś.
3. Kliknij akcję.
4. Kliknij `Zmień`, aby zastąpić jej klawisze, albo `Dodaj skrót`, aby dać jej drugi skrót, a potem naciśnij nowe klawisze. `Esc` anuluje.
5. Jeśli klawisze są już użyte w tym samym miejscu, komunikat wskazuje drugą akcję i pozwala przenieść skrót.

`Usuń` czyści klawisze akcji. `Przywróć domyślne` przywraca jedną akcję, a `Resetuj wszystko...` wszystkie. Twoje zmiany są zapisywane w `keys.json` w folderze ustawień (`%APPDATA%\LYNX` w Windows, `~/Library/Application Support/LYNX` w macOS, `~/.config/LYNX` w Linuksie). `Otwórz keys.json` otwiera ten plik.

## Transport i nagrywanie

| Klawisz | Co robi |
|---|---|
| `Space` | Odtwarza albo zatrzymuje i wraca tam, gdzie odtwarzanie się zaczęło |
| `Ctrl+Space` | Odtwarza lub pauzuje |
| `Shift+Space` | Odtwarza albo zatrzymuje na początku utworu lub patternu |
| `L` | Przełącza między trybem Pattern a trybem Song |
| `R` | Włącza lub wyłącza nagrywanie |
| `Shift+R` | Nagrywa audio od razu: uzbraja zaznaczony insert i nagrywa |
| `Ctrl+M` | Włącza lub wyłącza metronom |
| `Ctrl+P` | Włącza lub wyłącza odliczanie przed nagrywaniem |
| `Ctrl+I` | Włącza lub wyłącza czekanie nagrywania na pierwszą nutę |
| `Ctrl+B` | Włącza lub wyłącza łączenie nagranych nut |
| `Ctrl+E` | Włącza lub wyłącza wprowadzanie krokowe |
| `Ctrl+H` | Zatrzymuje dźwięk (all notes off) |
| `Ctrl+T` | Włącza lub wyłącza klawiaturę jako pianino |
| `Alt+Numpad *` / `Alt+Numpad /` | Skok do następnego lub poprzedniego znacznika utworu |

Niektóre z tych klawiszy robią coś innego w edytorze, który ma fokus. W Piano Roll `Ctrl+M` importuje plik MIDI, a `Ctrl+B` duplikuje. W Playliście `Ctrl+P` to tryb Performance.

## Pliki i edycja

| Klawisz | Co robi |
|---|---|
| `Ctrl+N` | Nowy projekt |
| `Ctrl+O` | Otwórz projekt |
| `Ctrl+S` | Zapisz |
| `Ctrl+Shift+S` | Zapisz jako |
| `Ctrl+Alt+S` | Zapisz nową wersję |
| `Ctrl+R` | Eksport audio |
| `Ctrl+Shift+R` | Eksportuj plik MP3 |
| `Ctrl+Shift+M` | Eksportuj plik MIDI |
| `Ctrl+Z` | Cofnij. Naciśnięte ponownie, ponawia ten krok |
| `Ctrl+Alt+Z` | Cofnij wstecz, krok po kroku |
| `Ctrl+Y` lub `Ctrl+Shift+Z` | Ponów |
| `Ctrl+Alt+H` | Historia cofania |
| `Ctrl+F` | Szukaj w Browserze |

## Okna

| Klawisz | Co robi |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Wybór pluginu |
| `Shift+F8` | Baza pluginów (w Browserze) |
| `Enter` | Maksymalizuje Playlist lub go przywraca |
| `F10` | Ustawienia MIDI |
| `F11` | Info o projekcie |
| `F12` | Zamyka wszystkie okna (pluginy, ustawienia kanałów, narzędzia) |
| `Alt+F12` | Zamyka wszystkie okna pluginów |
| `Ctrl+F12` | Zamyka wszystkie okna oprócz tego, które ma fokus |
| `Alt+F7` | Klawiatura dotykowa |
| `Ctrl+F5` | Tap tempo |
| `F1` | Ściągawka skrótów |

## Patterny

| Klawisz | Co robi |
|---|---|
| `Numpad +` / `Numpad -` | Następny, poprzedni pattern |
| `Numpad 1` do `Numpad 9` | Wybiera pattern od 1 do 9 |
| `F4` | Następny pusty pattern i nadanie mu nazwy |
| `Ctrl+F4` | Następny pusty pattern, bez nadawania nazwy |
| `Shift+F4` | Pierwszy pusty pattern i nadanie mu nazwy |
| `F2` | Zmienia nazwę i kolor patternu |
| `Ctrl+Shift+C` | Klonuje pattern |
| `Ctrl+Shift+Ins` | Wstawia pusty pattern przed bieżącym |
| `Ctrl+Shift+Del` | Usuwa pattern |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Przesuwa pattern w górę, w dół |

## Channel Rack

| Klawisz | Co robi |
|---|---|
| `Up` / `Down` | Zaznacza kanał powyżej, poniżej |
| `Shift+Up` / `Shift+Down` | Rozszerza zaznaczenie |
| `Alt+Up` / `Alt+Down` | Przesuwa zaznaczone kanały |
| `Ctrl+A` | Zaznacz wszystkie kanały |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Wycina, kopiuje, wkleja kroki zaznaczonych kanałów |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Obraca kroki w lewo, w prawo |
| `Alt+C` | Klonuje zaznaczone kanały |
| `Alt+Del` | Usuwa zaznaczone kanały |
| `Alt+G` | Grupuje zaznaczone kanały |
| `Ctrl+L` | Kieruje zaznaczone kanały na wolne inserty Mixera |
| `Ctrl+K` | Włącza lub wyłącza pasy wykresu kroków |
| `1` do `0` | Wycisza lub przywraca kanały od 1 do 10 |
| `Ctrl+1` do `Ctrl+0` | Włącza solo dla kanałów od 1 do 10 |

## Piano Roll

| Klawisz | Co robi |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Ołówek, Pędzel, Pędzel perkusyjny, Gumka, Wycisz, Nóż, Lasso, Zoom, Odsłuch |
| `S` | Włącza lub wyłącza nuty slide (nowe nuty) |
| `G` / `K` | Poprzedni, następny kanał |
| `H` / `J` | Poprzedni, następny kanał z nutami |
| `Ctrl+A` | Zaznacz wszystko |
| `Ctrl+D` lub `Esc` | Odznacz |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiuj, wytnij, wklej |
| `Ctrl+B` | Duplikuj |
| `Del` | Usuwa zaznaczenie |
| `Up` / `Down` | Przesuwa zaznaczenie o półton |
| `Ctrl+Up` / `Ctrl+Down` | Przesuwa zaznaczenie o oktawę |
| `Left` / `Right` | Przesuwa zaznaczenie o jeden krok siatki |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Obraca nuty w obrębie taktu |
| `Backspace` | Wyłącza lub włącza przyciąganie |
| `Ctrl+Q` | Szybka kwantyzacja (początki i długości) |
| `Shift+Q` | Kwantyzuje początki |
| `Ctrl+U` | Tnie według siatki |
| `Ctrl+G` | Łączy stykające się nuty |
| `Ctrl+L` | Legato |
| `Shift+D` | Skraca nuty do siatki |
| `Ctrl+P` | Włącza lub wyłącza porta dla zaznaczenia |
| `Alt+M` / `Shift+Alt+M` | Wycisza, przywraca zaznaczenie |
| `Alt+V` | Włącza lub wyłącza nuty-duchy |
| `Shift+I` | Odwraca zaznaczenie |
| `Shift+G` / `Alt+G` | Grupuje, rozgrupowuje zaznaczone nuty |
| `Alt+C` | Koloruje zaznaczone nuty kolorem nuty |
| `Shift+C` | Zaznacza nuty o tym samym kolorze |
| `Shift+N` | Stempel akordu: brak (pojedyncze nuty) |
| `Ctrl+Ins` / `Ctrl+Del` | Wstawia, usuwa przestrzeń (zaznaczenie czasu) |
| `Ctrl+M` | Importuje plik MIDI |
| `Ctrl+Shift+M` | Eksportuje jako plik MIDI |
| `PageUp` / `PageDown` | Przybliża, oddala |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom do 1, 4, 16 taktów |
| `Shift+4` | Zoom, aby pokazać wszystko |
| `Shift+5` | Zoom do zaznaczenia |
| `Shift+0` | Centruje widok na kursorze odtwarzania |

### Narzędzia nut

Każdy klawisz otwiera okno jednego narzędzia nut w Piano Roll.

| Klawisz | Co robi |
|---|---|
| `Alt+A` | Arpeggiuj |
| `Alt+S` | Strum |
| `Alt+Y` | Odwróć |
| `Alt+R` | Losuj |
| `Alt+X` | Skaluj poziomy |
| `Alt+K` | Ogranicz do zakresu |
| `Alt+Q` | Kwantyzuj |
| `Alt+U` | Potnij |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Articulate |
| `Alt+E` | Riff machine |
| `Alt+P` | Generuj progresję akordów |

## Playlist

| Klawisz | Co robi |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Ołówek, Pędzel, Gumka, Wycisz, Nóż, Lasso, Zoom, Odsłuch, Slip |
| `Ctrl+A` | Zaznacz wszystko |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopiuj, wytnij, wklej |
| `Ctrl+B` | Duplikuj |
| `Del` | Usuwa zaznaczone klipy |
| `Shift+Del` | Usuwa zaznaczone klipy i zamyka luki |
| `Shift+S` | Włącza lub wyłącza edycję ripple |
| `Backspace` | Wyłącza lub włącza przyciąganie |
| `Alt+M` / `Shift+Alt+M` | Wycisza, przywraca zaznaczone klipy |
| `Shift+M` | Włącza lub wyłącza rozciąganie audio przy zmianie rozmiaru klipów audio |
| `Ctrl+Shift+V` | Czyni zaznaczone klipy unikalnymi |
| `Ctrl+G` | Scala klipy patternów |
| `Ctrl+Shift+G` | Scala podobne klipy patternów |
| `Ctrl+Alt+G` | Scala klipy automatyki |
| `Ctrl+Shift+B` | Klonuje zaznaczone ścieżki Playlisty |
| `Ctrl+Q` | Kwantyzuje początki klipów |
| `7` / `8` | Obniża, podwyższa zaznaczone klipy audio o półton |
| `9` | Odwraca zaznaczone klipy audio |
| `Ctrl+Ins` / `Ctrl+Del` | Wstawia, usuwa długość zaznaczenia czasu na wszystkich ścieżkach |
| `Ctrl+Left` / `Ctrl+Right` | Zaznacza poprzedni, następny czas |
| `Ctrl+Enter` | Zaznacza czas wokół zaznaczenia |
| `Alt+T` | Dodaje znacznik czasu |
| `Ctrl+T` | Dodaje znacznik automatycznie |
| `Shift+Alt+T` | Dodaje zmianę metrum |
| `Shift+T` | Umieszcza znacznik pętli |
| `Ctrl+Alt+C` | Konsoliduje zaznaczenie w Playliście |
| `Ctrl+P` | Włącza lub wyłącza tryb Performance |
| `PageUp` / `PageDown` | Przybliża, oddala |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom do 1, 4, 16 taktów |
| `Shift+4` | Zoom, aby pokazać wszystko |
| `Shift+5` | Zoom do zaznaczenia |
| `Shift+0` | Centruje widok na kursorze odtwarzania |

## Mixer

| Klawisz | Co robi |
|---|---|
| `Alt+Left` / `Alt+Right` | Przesuwa zaznaczone inserty w lewo, w prawo |
| `F2` | Zmienia nazwę i kolor zaznaczonego inserta |
| `S` | Solo dla zaznaczonych insertów |
| `Alt+S` | Solo dla zaznaczonych insertów razem z insertami, które je zasilają |
| `Ctrl+L` | Łączy zaznaczone kanały z zaznaczonym insertem |
| `Ctrl+Shift+L` | Łączy zaznaczone kanały z insertami, zaczynając od zaznaczonego |
| `Alt+L` | Zaznacza kanały połączone z zaznaczonym insertem |
| `Del` | Usuwa plugin w zaznaczonym slocie efektu |
| `Ctrl+E` | Otwiera edytor audio |
| `Shift+E` | Otwiera edytor audio i nagrywa |
| `Alt+R` | Renderuje uzbrojone inserty do plików WAV |
| `Shift+Alt+R` | Renderuje zaznaczone inserty do plików WAV |

## Browser

| Klawisz | Co robi |
|---|---|
| `Up` / `Down` | Poprzedni, następny plik (z odsłuchem) |
| `Shift+Up` / `Shift+Down` | Poprzedni, następny plik do zaznaczonego kanału |
| `Enter` | Wczytuje plik do zaznaczonego kanału |
| `Left` | Zamyka folder albo przechodzi do folderu nadrzędnego |
| `Right` | Otwiera folder |
| `F2` | Zmienia nazwę zaznaczonego pluginu (baza pluginów) |

## Warto wiedzieć

- Gdy pianino z klawiatury komputera jest włączone, jego klawisze grają nuty, a większość jednoliterowych skrótów jest wstrzymana (`L` nadal działa). Wyłączasz je przez `Ctrl+T`.
- Skrót działa albo wszędzie, albo w jednym oknie. Skrót przypisany do okna wygrywa z tymi samymi klawiszami działającymi wszędzie, dopóki to okno ma fokus.
- Niektóre akcje nie mają domyślnego klawisza, na przykład Paleta szybkiego dodawania. Nadaj im go w edytorze.

## Powiązane

- [Przegląd okna](/wiki/tour/)
- [Ustawienia](/wiki/settings/)
- [Kontrolery MIDI](/wiki/midi-controllers/)
- [Przejście z FL Studio](/wiki/coming-from-fl-studio/)
