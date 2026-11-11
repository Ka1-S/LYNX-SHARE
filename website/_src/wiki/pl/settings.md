---
source: ab2c2f14eb
---
![Strona Ogólne okna Ustawienia, z kartami stron na górze](/assets/img/wiki/settings-general.webp)

Okno Ustawienia ma osiem stron: Audio, MIDI, Ogólne, Motyw, Pliki, Dodatki LYNX, Projekt i Info. Pierwszych sześć należy do programu. Projekt i Info należą do otwartego projektu i są z nim zapisywane. Każda strona ma własną pozycję na górze menu `Opcje`.

## Zmień ustawienie

1. Otwórz menu `Opcje` i wybierz stronę, na przykład `Ustawienia ogólne...`.
2. Kliknij inną kartę na górze, żeby przejść na inną stronę.
3. Zmień ustawienie. Działa od razu, z wyjątkiem języka i czcionki interfejsu, które działają po ponownym uruchomieniu LYNX SHARE.

`F10` otwiera stronę MIDI, a `F11` stronę Info.

## Audio

`Opcje › Ustawienia audio...`. Sterownik, urządzenia, częstotliwość próbkowania, rozmiar bufora i nagrywanie. Tę stronę opisuje [Ustawienia audio](/wiki/audio-setup/). W `Nagrywanie` `Format pliku` oferuje teraz także `WAV 16 bitów (częstotliwość próbkowania urządzenia)`.

## MIDI

![Strona MIDI: urządzenia wejściowe, urządzenia wyjściowe z synchronizacją master i synchronizacja zewnętrzna](/assets/img/wiki/settings-midi.webp)

`Opcje › Ustawienia MIDI...`

| Ustawienie | Co robi |
|---|---|
| Urządzenia wejściowe | Każde urządzenie można wyłączyć, ustawić na nasłuch jednego kanału MIDI i na granie stałym kanałem Channel Racku zamiast zaznaczonych |
| `Typ kontrolera` | Dla każdego urządzenia wejściowego: `Wykryj automatycznie`, `Kontroler ogólny` albo profil kontrolera, na przykład Mackie Control. Wiersz obok podaje port, który dostaje lampki i wyświetlacz |
| `Kontroler MPE` | Na końcu listy kanałów MIDI urządzenia. Każda nuta przychodzi z własnym bendem, barwą i naciskiem. Drugie pole ustawia kanały członkowskie |
| `Odśwież listę urządzeń` | Szuka urządzeń jeszcze raz i wczytuje ponownie twoje profile kontrolerów |
| `Folder profili kontrolerów` | Otwiera folder na własne profile kontrolerów (`*.lynxctl`) |
| `Wysyłaj synchronizację master` | Wysyła MIDI clock z Song Position Pointer albo MIDI Time Code do urządzenia wyjściowego. `Przesunięcie (ms)` wysyła go później lub wcześniej |
| `Wysyłaj all notes off` | Wysyła all notes off do urządzenia przy inicjacji i po eksporcie |
| `Synchronizacja z zewnętrznym clockiem` | LYNX SHARE podąża za zegarem MIDI wybranego wejścia: tempo, start, continue, stop i pozycja |
| `Velocity` | Dla nut z urządzeń MIDI: jak zagrane, miękka, twarda lub stała |
| `Kanał trybu performance` | Nuty na tym kanale uruchamiają klipy Playlisty w trybie performance |
| `Skok do znacznika utworu` | Nuty na tym kanale przenoszą utwór do znacznika zamiast grać |
| `Pickup (tryb przejęcia)` | Kontroler porusza parametr dopiero po dojściu do jego bieżącej wartości |
| `Pedał steruje wyłączaniem nut` | Domyślnie włączone: pedały sustain i sostenuto przytrzymują nuty. Po wyłączeniu pedał trafia do pluginów |
| `Używaj kontrolera do gier` | Kontroler do gier steruje transportem i można go powiązać jak kontroler MIDI. Domyślnie wyłączone |
| Powiązania | Powiązania kontrolerów tego projektu, potem globalne, z `Usuń powiązanie` i `Usuń wszystkie` |

## Ogólne

`Opcje › Ustawienia ogólne...`

| Ustawienie | Co robi |
|---|---|
| `Styl układu` | Praca na wzorcach (domyślnie), praca z uruchamianiem klipów albo praca uproszczona |
| `Domyślny szablon`, `Projekt przy starcie` | Szablon, który otwiera `Plik › Nowy`, i to, co otwiera się przy starcie: `Pusty projekt`, `Domyślny szablon` albo `Ostatnio używany projekt`. `Plik › Zapisz jako szablon...` dodaje twoje własne |
| `Klipy sesji z zestawów Live` | Dokąd trafiają klipy sesji z zaimportowanego setu Live: do siatki klipów albo na Playlistę po aranżacji |
| `Język`, `Czcionka interfejsu` | Zastosowane przy następnym uruchomieniu |
| `Rozmiar interfejsu`, `Gęstość menu` | Skala całego interfejsu oraz kompaktowe lub przestronne menu |
| `Przebiegi` | Clean (kolor klipu) albo kolory widmowe, opcjonalnie z osobnymi kanałami stereo |
| `Nazwy nut`, `Czarne klawisze` | Nazwy angielskie, germańskie lub solfeżowe, z krzyżykami albo bemolami |
| `Styl pluginów` | Wygląd okien wbudowanych pluginów: automatyczny albo jeden styl dla każdego pluginu |
| `Animacje`, `Częstotliwość animacji` | Animacje interfejsu i to, jak często poruszają się kursor odtwarzania i mierniki |
| `Rysuj z użyciem GPU (Direct2D)` | Wyłącz, jeśli sterownik grafiki się wiesza albo ekran migocze |
| Uruchamianie | Ekran powitalny, dźwięk startowy i szukanie nowych pluginów w tle przy każdym starcie |
| Edycja | Klawiatura jako pianino, zostawianie otwartych edytorów kanałów, zapamiętywanie czasu przeszukiwania, zapętlanie rozciągniętych klipów patternów |
| `Odłączaj wszystkie nowe wtyczki` | Włączone: okno dodanego pluginu otwiera się odłączone, nad panelami. Wyłączone (domyślnie): otwiera się zadokowane w głównym oknie |
| `Malowanie monofoniczne w piano rollu` | Włączone: narzędzie Paint zastępuje nuty pod każdą nową nutą, a Shift maluje nad nimi. Wyłączone: odwrotnie |
| `Automatycznie zaznaczaj powiązane moduły` | Fokus na oknie pluginu zaznacza jego ścieżkę Mixera. Domyślnie włączone |
| `Domyślnie wyłącz priorytet klawiatury do grania` | Dla pluginów dodawanych od tej chwili. Domyślnie wyłączone |
| `Maksymalna liczba kroków cofania` | Od 25 do 1000 |
| `Zarządzaj komunikatami` | W `Wskazówki i ostrzeżenia`. Zaznaczone komunikaty są pokazywane. Odznacz jeden, żeby pominąć jego pytanie |
| Kopia zapasowa | Jak często zapisywana jest kopia, ile ich jest trzymanych na projekt, oraz `Otwórz folder kopii zapasowych` |
| `Uruchamiaj pluginy w osobnym procesie` | Plugin, który się wiesza, nie zabiera LYNX SHARE ze sobą. Domyślnie wyłączone |
| `Zastępuj brakujące pluginy odpowiednikami LYNX SHARE` | Domyślnie włączone. Gdy projekt używa pluginu innego producenta, który nie jest zainstalowany, wczytuje się zastępnik LYNX SHARE z pasującymi ustawieniami, jeśli taki ma. Projekt zachowuje oryginalny plugin i używa go ponownie, gdy zostanie zainstalowany |
| `Ogranicz liczbę otwartych edytorów wtyczek` | Otwartych zostaje najwyżej 25 okien edytorów pluginów. Domyślnie włączone |
| `Raporty o awariach` | Pytaj po awarii lub zawieszeniu (domyślnie), zawsze wysyłaj albo nigdy nie wysyłaj |
| `Sprawdzaj aktualizacje automatycznie` | Sprawdzanie aktualizacji przy starcie, z `Sprawdź teraz` |

## Motyw

`Opcje › Motyw (kolory, tło)...`

| Ustawienie | Co robi |
|---|---|
| Pole motywu | Wybiera preset. `Importuj...` i `Eksportuj...` czytają i zapisują pliki `.lynxtheme`, `Zapisz preset...` zachowuje bieżący motyw, `Domyślny` go resetuje |
| `Kolor akcentu`, `Tło`, `Obszar roboczy` | Trzy kolory motywu. Kolor obszaru roboczego może automatycznie podążać za tłem |
| Obraz obszaru roboczego | PNG, JPEG lub GIF za oknami, z `Dopasowanie obrazu`, `Krycie obrazu` i `Przyciemnienie` |
| Dźwięk startowy | Twój własny dźwięk do 30 sekund albo własny dźwięk LYNX SHARE |

## Pliki

`Opcje › Ustawienia plików (foldery wyszukiwania)...`

| Ustawienie | Co robi |
|---|---|
| Foldery wyszukiwania brakujących sampli | Okno Brakujące pliki samo przeszukuje te foldery i ich podfoldery |
| Foldery Browsera | Pokazywane na liście folderów Browsera i też przeszukiwane pod kątem brakujących sampli |
| Foldery pluginów | Skanowane w poszukiwaniu pluginów przez `Opcje › Skanuj pluginy` |

## Dodatki LYNX

`Opcje › Dodatki LYNX...`. Własne dodatki LYNX SHARE w jednym miejscu. Wszystkie są domyślnie wyłączone. Każdy przełącznik to to samo ustawienie, co w menu lub na stronie, którą wymienia jego wiersz.

| Ustawienie | Co robi |
|---|---|
| `Miernik głośności obok Mastera (LYNX)` | Głośność krótkoterminowa i zintegrowana (LUFS) oraz true peak Mastera, z przyciskiem Reset |
| `Pokaż tylko używane inserty` | Ukrywa inserty Mixera, których nic nie używa |
| `Zachowaj połączenia z usuniętymi insertami, wyszarzone (LYNX)` | Usunięcie inserta zachowuje połączenia, które do niego szły, więc można je połączyć ponownie |
| `Grupy miksera podążają za Playlistą (LYNX)` | Grupy Mixera odzwierciedlają grupy ścieżek Playlisty |
| `Ścieżki automatyzacji` | Automatyka może leżeć na ścieżkach pod ścieżką Playlisty |
| `Linijka: takty utworu (LYNX)` | Linijka Piano Roll pokazuje numery taktów miejsca w utworze, w którym gra pattern |
| `Pokazuj wartość kontrolki obok myszy po najechaniu (LYNX)` | Wartość pokazuje się obok kursora, nie tylko na pasku podpowiedzi |
| `Po nagrywaniu w pętli gra tylko ostatnie przejście` | Wcześniejsze przebiegi zostają pod nim, wyciszone |
| `W pokazuje lub ukrywa okno zaznaczonego urządzenia` | Klawisz dla widoku Device |
| `Zaznaczenie urządzenia zaznacza jego insert w Mikserze` | Mixer podąża za urządzeniem wybranym w widoku Device |
| `Używaj kontrolera do gier` | Ten sam przełącznik, co na stronie MIDI |

## Projekt

![Strona Projekt: ustawienia czasu, groove i strojenie, podstawa czasu i opcje Playlisty](/assets/img/wiki/settings-project.webp)

`Opcje › Ustawienia projektu (tempo, metrum)...`

| Ustawienie | Co robi |
|---|---|
| `Ustawienia czasu` | Podział czasu (miary na takt, kroki na miarę) albo tradycyjne metrum (licznik i mianownik) |
| `Tempo`, `Miary na takt` | Tempo projektu i liczba miar w takcie |
| `Swing`, `Master pitch` | Globalna ilość swingu i master pitch w centach |
| `Automatyczne przenikanie` | Nakładające się klipy audio na ścieżce przenikają się na długości nakładki |
| `Przejście compu` | Długość przenikania używana przy comp ujęć, w milisekundach |
| `Odtwarzaj ucięte nuty w klipach` | Nuta, która zaczyna się przed klipem patternu, gra od początku klipu |
| `Szybkie usuwanie trzasków dla grup cięcia` | Szybsze wygaszanie, gdy grupy cięcia ucinają nuty |
| `Prawo panoramy` | Kołowe (środek przy 0 dB, +3 dB po stronie, w którą przesunięto panoramę) albo Trójkątne (głośniejsza strona zachowuje poziom). Dla insertów Mixera i kanałów Samplera |

## Info

`Opcje › Informacje o projekcie...`. Tytuł, autor, gatunek, link i komentarze projektu oraz `Pokazuj tę stronę po otwarciu projektu`. Stopka pokazuje, kiedy projekt powstał, i czas pracy.

## Warto wiedzieć

- Podstawa czasu jest stała i wynosi 960 ticków na ćwierćnutę. Projekty z FL Studio są do niej przeliczane.
- Zmian metrum w trakcie utworu nie ma na stronie Projekt. Dodaj znacznik metrum na linijce Playlisty.
- `Uruchamiaj pluginy w osobnym procesie` dotyczy pluginów otwieranych od tej chwili. Otwórz projekt ponownie, żeby przenieść pozostałe.
- Ustawień aktualizacji nie ma w wersji ze Sklepu Microsoft, bo aktualizuje ją Sklep.
- Skróty są w `Opcje › Zarządzaj skrótami...`, nie na stronie Ustawień.
- Niektóre przełączniki są w menu, nie w tym oknie. `Niska latencja podczas nagrywania` jest w menu opcji nagrywania obok przycisku nagrywania i w menu `Nagrywanie na dysk` Mixera. `Styl klawiatury` jest w menu View Piano Roll.
- Baza pluginów to folder, `Baza pluginów` w folderze `LYNX` w twoich dokumentach. Możesz go uporządkować w menedżerze plików.

## Powiązane

- [Ustawienia audio](/wiki/audio-setup/)
- [Kontrolery MIDI](/wiki/midi-controllers/)
- [Skróty](/wiki/shortcuts/)
- [Pluginy](/wiki/plugins/)
- [Układy](/wiki/layouts/)
