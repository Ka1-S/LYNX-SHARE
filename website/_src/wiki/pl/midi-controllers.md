---
source: 3b54a39c97
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
| `Dowolny kanał MIDI` | Nasłuchuje każdego kanału MIDI albo tylko jednego |
| `Gra zaznaczonymi kanałami` | Nuty idą do kanałów zaznaczonych w Channel Racku albo zawsze do jednego stałego kanału |
| `Velocity` | `Jak zagrane`, `Miękka (lekka gra brzmi głośniej)`, `Twarda (wymaga mocniejszej gry)` albo `Stała (każda nuta z pełną velocity)` |
| `Kanał trybu performance` | Nuty na tym kanale MIDI uruchamiają klipy Playlisty, gdy tryb performance jest włączony |
| `Skok do znacznika utworu` | Nuty na tym kanale MIDI przenoszą utwór do znacznika zamiast grać |

## Powiąż gałkę z elementem

1. Kliknij gałkę lub suwak w LYNX SHARE prawym przyciskiem.
2. Wybierz `Powiąż z kontrolerem MIDI (porusz gałką na kontrolerze)`.
3. Poruszaj gałką lub suwakiem na sprzęcie. Powiązanie jest gotowe.

Powiązanie zapisuje się z projektem. Żeby je usunąć, kliknij element prawym przyciskiem jeszcze raz i wybierz `Odłącz kontroler`. Pozycja menu pokazuje używany numer CC.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| `Override global link...` | brak | Tworzy powiązanie działające w każdym projekcie. Jest trzymane w ustawieniach, nie w projekcie |
| `Usuń powiązanie globalne` | brak | Usuwa je z powrotem |
| `Multilink do kontrolerów` | `Ctrl+J` | Gdy jest włączone, poruszane elementy są powiązywane po kolei z elementami sprzętu, którymi ruszysz w następnej kolejności. Jest w menu `Opcje` |
| `Pickup (tryb przejęcia)` | brak | Na stronie MIDI. Kontroler porusza parametr dopiero po dojściu do jego bieżącej wartości, więc nic nie skacze |

Strona MIDI wylicza powiązania w `Powiązania (projekt, globalne)`. `Typ kontrolera` na tej samej stronie odczytuje `Kontroler ogólny`.

Do parametrów pluginu zewnętrznego dochodzi się przez `Automatyzuj parametr pluginu` w menu kanału albo menu slotu efektu. Każdy parametr ma tam te same pozycje powiązań. Zobacz [Automatyka](/wiki/automation/).

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

## Powiązane

- [Nagrywanie](/wiki/recording/)
- [Automatyka](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Ustawienia](/wiki/settings/)
- [Skróty](/wiki/shortcuts/)
