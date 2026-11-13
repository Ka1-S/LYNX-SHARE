---
source: d2bebc77b1
---
![Strona MIDI w ustawieniach: urządzenia wejściowe, urządzenia wyjściowe z synchronizacją master i synchronizacja zewnętrzna](/assets/img/wiki/settings-midi.webp)

LYNX SHARE przyjmuje nuty i ruchy kontrolerów z dowolnego urządzenia wejściowego MIDI. Urządzenia konfiguruje się na stronie MIDI w ustawieniach: `Opcje › Ustawienia MIDI...` albo `F10`. Bez sprzętu nuty grają też klawiatura komputera i klawiatura dotykowa.

## Podłącz klawiaturę

1. Podłącz klawiaturę i otwórz `Opcje › Ustawienia MIDI...` (`F10`).
2. Znajdź ją w `Urządzenia wejściowe` i zaznacz. Jeśli jej nie ma na liście, kliknij `Odśwież listę urządzeń`.
3. Zostaw dwa pola na `Dowolny kanał MIDI` i `Gra zaznaczonymi kanałami`.
4. Zamknij ustawienia i zaznacz kanał w [Channel Racku](/wiki/channel-rack/).
5. Zagraj. Zaznaczony kanał brzmi.

## Urządzenia wejściowe

| Ustawienie | Co robi |
|---|---|
| Pole wyboru | Włącza lub wyłącza urządzenie |
| `Dowolny kanał MIDI` | Nasłuchuje każdego kanału MIDI albo tylko jednego. `Kontroler MPE` jest na końcu tej listy |
| `Gra zaznaczonymi kanałami` | Nuty idą do kanałów zaznaczonych w Channel Racku albo zawsze do jednego stałego kanału |
| `Velocity` | `Jak zagrane`, `Miękka (lekka gra brzmi głośniej)`, `Twarda (wymaga mocniejszej gry)` albo `Stała (każda nuta z pełną velocity)` |
| `Kanał trybu performance` | Nuty na tym kanale MIDI uruchamiają klipy Playlisty, gdy tryb performance jest włączony |
| `Skok do znacznika utworu` | Nuty na tym kanale MIDI przenoszą utwór do znacznika zamiast grać |
| `Typ kontrolera` | `Wykryj automatycznie`, `Kontroler ogólny` albo profil kontrolera |
| `Pedał steruje wyłączaniem nut` | Domyślnie włączone. Pedał sustain (CC 64) lub sostenuto (CC 66) przytrzymuje nuty. Po wyłączeniu pedał trafia do pluginów jako kontroler |

## Powiąż gałkę z elementem

1. Kliknij gałkę lub suwak w LYNX SHARE prawym przyciskiem.
2. Wybierz `Powiąż z kontrolerem MIDI (porusz gałką na kontrolerze)`.
3. Poruszaj gałką lub suwakiem na sprzęcie. Powiązanie jest gotowe.

Powiązanie zapisuje się z projektem. Żeby je usunąć, kliknij element prawym przyciskiem jeszcze raz i wybierz `Odłącz kontroler`. Pozycja menu pokazuje używany numer CC.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| `Powiąż z kontrolerem...` | brak | Otwiera okno `Ustawienia zdalnego sterowania` dla elementu, w którym widzisz i ustawiasz jego powiązanie ręcznie (poniżej) |
| `Zastąp powiązanie globalne...` | brak | Tworzy powiązanie działające w każdym projekcie. Jest trzymane w ustawieniach, nie w projekcie |
| `Nadpisz powiązanie globalne... (dla wszystkich projektów: porusz gałką na kontrolerze)` | brak | Tworzy powiązanie globalne w jednym kroku: następnie poruszasz element sprzętu. Pozycja bez nawiasów otwiera zamiast tego okno `Ustawienia powiązania globalnego` |
| `Usuń powiązanie globalne` | brak | Usuwa je z powrotem |
| `Multilink do kontrolerów` | `Ctrl+J` | Gdy jest włączone, poruszane elementy są powiązywane po kolei z elementami sprzętu, którymi ruszysz w następnej kolejności. Jest w menu `Opcje` |
| `Pickup (tryb przejęcia)` | brak | Na stronie MIDI. Kontroler porusza parametr dopiero po dojściu do jego bieżącej wartości, więc nic nie skacze |

Strona MIDI wylicza powiązania w `Powiązania (projekt, globalne)`. Urządzenie, którego `Typ kontrolera` to `Kontroler ogólny`, robi tylko to, co powiążesz ręcznie.

Do parametrów pluginu zewnętrznego dochodzi się przez `Automatyzuj parametr pluginu` w menu kanału albo menu slotu efektu. Każdy parametr ma tam te same pozycje powiązań. Zobacz [Automatyka](/wiki/automation/).

## Ustawienia zdalnego sterowania

`Powiąż z kontrolerem...` otwiera to okno, jak w FL Studio. `Ustawienia powiązania globalnego` (z `Zastąp powiązanie globalne...`) ma te same pola, z wyjątkiem listy powiązań, `Omni`, kontrolera wewnętrznego i `Usuń konflikty`. Podwójne kliknięcie powiązania na liście na stronie MIDI też je otwiera.

| Ustawienie | Co robi |
|---|---|
| `Port`, `Kanał`, `Ctrl` | Numer portu wejściowego MIDI, kanał MIDI i numer kontrolera. Kliknij `Ctrl` prawym przyciskiem, aby zobaczyć standardowe kontrolery z nazwami. `Nuta C0` na tej liście tworzy powiązanie z nutą: zwiększ stamtąd numer, aby wybrać inny klawisz |
| `Wykryj automatycznie` | Domyślnie włączone. Poruszasz elementem na urządzeniu, a numery wypełniają się same |
| `Omni` | Powiązanie podąża za wybranym kanałem, a dla efektu w slocie miksera za wybraną ścieżką miksera |
| `Tryb` | Jak działa powiązanie z nutą lub przyciskiem. `Domyślny` i `Hold` podążają za klawiszem: jego velocity, gdy jest trzymany, zero po puszczeniu. `Latch` przełącza między dwoma końcami przy każdym naciśnięciu. `Inc` zwiększa wartość o krok przy każdym naciśnięciu. `Pozostań` ustawia velocity i zachowuje je po puszczeniu |
| Kontroler wewnętrzny | Zamiast tego łączy element z jednym z kontrolerów wewnętrznych projektu. Widoczne tylko, gdy projekt ma taki kontroler. `Nagrywaj automatykę` pozwala nagrywać ruchy tego kontrolera jako automatyzację |
| `Formuła mapowania` | Jak wejście jest mapowane na element, ze zmienną `Wejście`. `Presety` ma listę formuł z FL. Naciśnij `Enter`, aby zastosować wpisaną formułę. Krzywa obok pokazuje wynik |
| `Wygładzanie` | Wygładza ruchy elementu. `Czas` ustawia, jak długo: 1 biegun dla wartości ujemnych, 3 bieguny dla dodatnich |
| `Usuń konflikty` | Domyślnie włączone. Usuwa inne powiązania używające tego samego kontrolera po kliknięciu `Akceptuj`. Robi się czerwone, gdy takie powiązanie istnieje |
| `Resetuj`, `Pomiń`, `Akceptuj` | `Akceptuj` zapisuje powiązanie (jeden krok cofania). Gdy multilink ustawił w kolejce kilka elementów, `Pomiń` przechodzi do następnego |

## Profile kontrolerów

Profil kontrolera ustawia przyciski transportu, suwaki, pokrętła, pady, lampki i wyświetlacz urządzenia, bez niczego do powiązania.

1. Otwórz `Opcje › Ustawienia MIDI...` (`F10`) i zaznacz urządzenie w `Urządzenia wejściowe`.
2. Zostaw jego `Typ kontrolera` na `Wykryj automatycznie`. Nazwa w nawiasie to profil znaleziony po nazwie portu urządzenia.
3. Jeśli pokazuje `Kontroler ogólny`, wybierz profil z listy sam.
4. Przeczytaj wiersz obok pola. `Feedback to` podaje port wyjściowy, który dostaje lampki, silnikowe suwaki i tekst wyświetlacza.

| Profil | Co mapuje |
|---|---|
| `Mackie Control Universal` | Suwaki na głośności insertów i Master. V-Pots na panoramę, parametry pluginu zaznaczonego kanału lub głośności w Channel Rack. Arm, solo, mute i select na pasek, banki suwaków, play, stop, record, przewijanie wstecz i w przód, tryb Pattern lub Song, metronom, cofnij i jog wheel. Wyświetlacz pokazuje nazwy i wartości |
| `Mackie Control Extender (XT)` | To samo dla 8 kolejnych pasków. Urządzenia dzielą jeden bank, w kolejności listy `Urządzenia wejściowe` |
| `Novation Launchkey MK3 (DAW mode)` | Pokrętła na panoramę, głośność lub parametry pluginu, suwaki na głośność, przyciski suwaków wybierają insert. Przyciski banku, play, stop, record, loop, metronomu i cofnij. Pady Session to 16 kroków zaznaczonego kanału. Użyj go na drugim porcie klawiatury |
| `Akai APC Key 25 mk2` | Gałki na głośność, panoramę lub parametry pluginu. Dwa górne rzędy padów to 16 kroków zaznaczonego kanału, a trzy poniżej grają nuty. Przyciski ścieżek wybierają insert. Stop, play i record. Użyj go na drugim porcie |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | Mapa Mackie Control, na porcie MCU/HUI klawiatury |
| `Korg nanoKONTROL2 (Mackie Control mode)` | Mapa Mackie Control. Nie jest wykrywany: uruchom urządzenie w trybie Mackie Control i wybierz ten typ ręcznie |

Gałki i suwaki, które wysyłają pozycję, używają soft takeover: poruszają kontrolkę dopiero po osiągnięciu jej wartości. Kontrolki powiązane ręcznie zachowują swoje powiązania. `Folder profili kontrolerów` otwiera folder na własne profile (`*.lynxctl`), a `Odśwież listę urządzeń` wczytuje je ponownie.

Wbudowane profile napisano na podstawie dokumentacji każdego producenta. Nie sprawdzono ich jeszcze na sprzęcie.

## Kontrolery MPE

MPE jest na pierwszym etapie. Kontroler MPE gra każdą nutę z własnym pitch bendem, barwą (CC 74) i naciskiem.

1. Na stronie MIDI otwórz pole urządzenia z napisem `Dowolny kanał MIDI` i wybierz `Kontroler MPE`.
2. W polu, które pojawi się obok, wybierz kanały członkowskie, od `Kanał 2` do `Channels 2-16`.
3. Graj. Nagrywanie opisuje strona [Nagrywanie](/wiki/recording/).

- Nagrana nuta zachowuje po jednej wartości z każdego: bend jako fine pitch, barwę jako Mod X, a najsilniejszy nacisk jako Mod Y. To nie jest krzywa.
- Spośród wbudowanych instrumentów te wartości śledzą Sampler i wbudowany syntezator.
- Plugin innego producenta dostaje MPE, gdy włączone jest `MPE` w sekcji `MIDI` w `Ustawienia wrappera` okna pluginu. Każda nuta wychodzi wtedy na własnym kanale MIDI, od 2 do 16.
- Nie sprawdzono tego jeszcze z prawdziwym kontrolerem MPE.

## MIDI Map Mode i Key Map Mode

W [sposobie pracy Clip-launch](/wiki/layouts/) klipy, sceny i przyciski można mapować jak w Ableton Live. Przyciski `MIDI` i `Tonacja` obok przełącznika `Oba`, `Siatka`, `Song` włączają te tryby, tak samo jak `Ctrl+M` i `Ctrl+K`.

1. Naciśnij `Ctrl+M` dla MIDI Map Mode lub `Ctrl+K` dla Key Map Mode. Każdy element, który można zmapować, jest podświetlony, na niebiesko dla MIDI i na pomarańczowo dla klawiszy.
2. Kliknij element: slot klipu, scenę, przycisk stop ścieżki, `Zatrzymaj wszystkie klipy`, play, stop, record, `Nagrywanie sesji`, metronom albo pokrętło Mikser sesji lub Device View.
3. Poruszaj elementem lub naciśnij klawisz na swoim urządzeniu MIDI, albo naciśnij klawisz komputera w Key Map Mode. Element pokaże swoje przypisanie, na przykład `1/19` (kanał i kontroler).
4. Mapowania są wyświetlane w miejscu Browsera: Channel, Note/Control lub Key, Path, Name, Min i Max. Przeciągnij Min lub Max albo kliknij dwukrotnie, aby wpisać wartość. Kliknij wiersz prawym przyciskiem, aby zobaczyć `Odwróć zakres` i `Delete`.
5. Naciśnij ten sam klawisz ponownie, aby zakończyć tryb.

## Klawiatura komputera

`Ctrl+T` albo `Opcje › Klawiatura jako pianino` zamienia klawiaturę komputera w pianino. Rzędy od `Z` do `M` i od `Q` do `P` grają zaznaczonymi kanałami.

- Gdy jest włączona, klawisze liter i cyfr są nutami. Jednoliterowe skróty narzędzi Piano Rolla i Playlisty działają tylko wtedy, gdy jest wyłączona.
- Zawsze gra z velocity Piano Rolla. Ustawienie `Velocity` na stronie MIDI dotyczy urządzeń MIDI.
- Ten sam przełącznik jest w `Opcje › Ustawienia ogólne...`.

## Klawiatura dotykowa

![Klawiatura dotykowa: przyciski oktaw, rozmiar klawiszy, tryb velocity i rząd klawiszy pianina od C4](/assets/img/wiki/touch-keyboard.webp)

`Widok › Klawiatura dotykowa` (`Alt+F7`) otwiera pianino, na którym grasz myszą albo ekranem dotykowym. Gra kanałem zaznaczonym w Channel Racku i pokazuje jego nazwę po prawej.

| Ustawienie | Co robi |
|---|---|
| `-` i `+` | Oktawa w dół lub w górę |
| Rozmiar klawiszy | `Małe klawisze`, `Średnie klawisze` albo `Duże klawisze` |
| `Velocity: miejsce dotyku` | Blisko góry klawisza gra cicho, blisko dołu głośno |
| `Velocity: stała` | Każda nuta z velocity ustawioną suwakiem |
| `Nazwy nut` | Pokazuje nazwy na klawiszach |

## Synchronizacja MIDI

`Urządzenia wyjściowe` na stronie MIDI wysyła transport do sprzętu i innego oprogramowania. Zaznacz `Wysyłaj synchronizację master` dla urządzenia i wybierz `MIDI clock` (ze Start, Continue, Stop i Song Position Pointer) albo MIDI Time Code. `Przesunięcie (ms)` wysyła synchronizację później przy wartości dodatniej i wcześniej przy ujemnej. `Wysyłaj all notes off` wysyła ten komunikat do urządzenia przy inicjacji i po eksporcie.

`Synchronizacja z zewnętrznym clockiem` działa w drugą stronę: LYNX SHARE podąża za zegarem MIDI wybranego wejścia, z jego tempem, Start, Continue, Stop i Song Position Pointer. `Przesunięcie synchronizacji zewnętrznej (milisekundy)` odtwarza LYNX SHARE później przy wartości dodatniej i wcześniej przy ujemnej. `Stan` pokazuje, czy zegar działa.

## Warto wiedzieć

- Wybranie wejścia dla `Synchronizacja z zewnętrznym clockiem` włącza to urządzenie w `Urządzenia wejściowe`.
- Tempo przesunięte przez powiązany kontroler MIDI nie zmienia rozmiaru klipów audio. Wpisz lub ustaw docelowe tempo.
- Jak nagrywać to, co grasz, opisuje [Nagrywanie](/wiki/recording/). Nadchodzące nuty można przyciągać do skali w [Piano Rollu](/wiki/piano-roll/).
- MIDI CC, aftertouch kanału i pitch bend wysyłane do pluginu można automatyzować przez `Automatyzuj kontroler MIDI (CC, aftertouch, pitch bend)`.
- Nuta przytrzymana pedałem jest nagrywana do chwili, gdy pedał się podnosi.
- Klip automatyki nie odbiera kontrolki, gdy poruszasz nią z kontrolera. Kontrolka znów podąża za swoją automatyką, gdy odtwarzanie następnym razem się zaczyna.
- Powiązania z nutami utworzone w `Ustawienia zdalnego sterowania` nie są zapisywane do eksportu .flp.
- `Ctrl+M` i `Ctrl+K` uruchamiają tryby mapowania tylko w sposobie pracy Clip-launch. Map Mode nie ma jeszcze mapowania makr ani zakresów nut.

## Powiązane

- [Nagrywanie](/wiki/recording/)
- [Automatyka](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Ustawienia](/wiki/settings/)
- [Skróty](/wiki/shortcuts/)
