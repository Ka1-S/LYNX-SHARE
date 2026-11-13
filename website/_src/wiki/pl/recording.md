---
source: 094cbefd18
---
![Okno Nagrywanie pyta, co nagrywać: audio, nuty i automatykę, wszystko albo audio do edytora audio](/assets/img/wiki/record-dialog.webp)

Przycisk nagrywania w transporcie albo klawisz `R` włącza nagrywanie. Za pierwszym razem okno pyta, co chcesz nagrywać. Audio trafia do [Playlisty](/wiki/playlist/) jako klipy audio. Nuty trafiają do bieżącego patternu. Elementy, którymi poruszasz, stają się automatyką.

## Nagraj mikrofon lub instrument

1. Otwórz `Opcje › Ustawienia audio...` i wybierz urządzenie wejściowe.
2. Otwórz [Mixer](/wiki/mixer/) (`F9`) i zaznacz pusty insert.
3. Kliknij pole wejścia nad slotami efektów insertu i wybierz wejście. Pary stereo są na początku, pojedyncze kanały są oznaczone `(mono)`.
4. Kliknij przycisk uzbrojenia nagrywania obok pola wejścia.
5. Naciśnij `R` i wybierz `Audio do Playlisty jako klip audio`.
6. Naciśnij odtwarzanie, jeśli samo się nie zaczęło, i graj.
7. Naciśnij stop. Ujęcie to klip audio na Playliście.

`Shift+R` robi kroki od 4 do 6 jednym klawiszem: uzbraja zaznaczony insert, nagrywa tylko audio i uruchamia odtwarzanie. Naciśnij go ponownie, żeby zatrzymać.

Gdy wybierasz wejście dla insertu, który nagrywa więcej niż to wejście (nagrywane są też inne kanały wysyłające do niego), LYNX SHARE pyta, tak jak FL Studio, czy przełączyć na `Tylko wejście zewnętrzne`. Odpowiedz `Tak`, aby nagrać samo wejście na tym insercie.

## Nagrywanie audio jednym kliknięciem

`Narzędzia › Nagrywanie audio jednym kliknięciem` rozpoczyna ujęcie w jednym kroku, jak w FL Studio. Działa na wybranym insercie miksera. Gdy wybrany jest Master albo nic, bierze pierwszy insert z wejściem, a w razie braku wolny insert.

| Polecenie | Co robi |
|---|---|
| `Do edytora/rejestratora audio Edison` | Umieszcza Edisona na insercie, ustawionego na nagrywanie od startu odtwarzania, i uruchamia odtwarzanie. Zobacz [Mixer](/wiki/mixer/) |
| `Do playlisty jako klip audio` | Przełącza na tryb Song, uzbraja insert, pyta o jego wejście, jeśli go nie ma, i uruchamia nagrywanie oraz odtwarzanie. Inserty już uzbrojone nagrywają tak, jak są |

## Co oferuje okno Nagrywanie

| Wybór | Co robi |
|---|---|
| `Audio do Playlisty jako klip audio` | Uzbraja zaznaczony insert miksera i pyta o jego wejście, jeśli go nie ma |
| `Nuty i automatyka` | Nagrywa nuty z klawiatury MIDI lub klawiatury komputera oraz elementy, którymi poruszasz |
| `Wszystko` | Nuty, automatykę i audio z uzbrojonych insertów |
| `Audio do edytora audio` | Nagrywa wejście do nowego edytora audio, gdy utwór gra |

Zaznacz `Nie pytaj więcej`, żeby zachować wybór. Potem kliknij przycisk nagrywania prawym przyciskiem, żeby zobaczyć filtr nagrywania: `Automatyka`, `Nuty`, `Audio`, `Klipy` oraz `Pytaj, co nagrywać, gdy zaczyna się nagrywanie`, który przywraca okno.

## Pole wejścia insertu

![Mixer z uzbrojonym insertem 3 i polem wejścia ustawionym na wejścia 1 i 2](/assets/img/wiki/mixer-input.webp)

| Ustawienie | Co robi |
|---|---|
| `Źródło wejścia audio` | Wejście urządzenia, które ten insert gra i nagrywa, albo `(brak)` |
| `Nagrywaj` | Skąd pobierane jest ujęcie: `Tylko wejście zewnętrzne`, `Wejście zewnętrzne i miksera`, `Po efektach`, `Po EQ`, `Po poziomie i panoramie` lub `Po ścieżce` |
| `Nagraj też suche wejście jako drugi take (LYNX)` | Zachowuje nieprzetworzone wejście obok przetworzonego ujęcia |
| `Odsłuch wejścia zewnętrznego` | `Wył.`, `Gdy uzbrojony`, `Auto (gdy uzbrojony, nie podczas odtwarzania)` albo `Wł.` |
| `Ochrona nagrywania (nie można uzbroić)` | Chroni insert przed przypadkowym uzbrojeniem |

## Nagrywanie nut

Nuty pochodzą z klawiatury MIDI, klawiatury komputera (`Ctrl+T`) lub klawiatury dotykowej i trafiają do zaznaczonych kanałów w bieżącym patternie. Zobacz [Kontrolery MIDI](/wiki/midi-controllers/).

- `Łącz nagrane nuty` (`Ctrl+B`) rozstrzyga między nakładaniem a zastępowaniem. Włączone: nowe nuty dołączają do nut patternu. Wyłączone: stare nuty nagrywanego kanału są czyszczone.
- `Kwantyzuj nagrane nuty do globalnego przyciągania` jest w opcjach nagrywania.
- `Wprowadzanie krokowe` (`Ctrl+E`) działa przy zatrzymaniu: każda zagrana nuta trafia do patternu o jeden krok przyciągania dalej.
- `Przechwyć MIDI` (`Ctrl+Shift+Alt+R`) wstawia do patternu nuty, które właśnie zagrałeś, choć nagrywanie było wyłączone.
- Pedał sustain (CC 64) i pedał sostenuto (CC 66) przytrzymują grane nuty, a nagrana nuta trwa do chwili, gdy pedał się podnosi. Kanał Layer przekazuje pedał swoim kanałom. Gdy `Pedał steruje wyłączaniem nut` w `Opcje › Ustawienia MIDI...` jest wyłączone, pedał trafia do pluginów jako kontroler.
- W trybie Song nuty nagrane do klipu patternu dłuższego niż jego pattern zostają tam, gdzie je zagrałeś, a pattern rośnie. Ujęcie nie jest zawijane na pierwsze takty patternu. Przy włączonym `Rozciągnięte klipy patternów zapętlają swój pattern (LYNX)` do pętli trafiają tylko nuty zagrane w taktach, które już miały nuty.

Elementy poruszane podczas nagrywania są zapisywane jako klipy automatyki w trybie Song i jako zdarzenia patternu w trybie Pattern. Zobacz [Automatyka](/wiki/automation/).

## Opcje nagrywania

Opcje są w `Opcje › Nagrywanie` i w menu prawego przycisku przycisku nagrywania.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| `Odliczanie` | `Ctrl+P` | Wyłączone, 1 takt, 2 takty lub 4 takty metronomu przed startem nagrywania. `Ctrl+P` przełącza między wyłączeniem a ostatnio wybraną długością. Wybór jest zachowywany po zamknięciu programu; nowa instalacja startuje z 1 taktem, jak FL Studio |
| `Metronom` | `Ctrl+M` | Klik podczas odtwarzania |
| `Czekaj na pierwszą nutę` | `Ctrl+I` | Nagrywanie czeka, aż zagrasz |
| `Nagrywanie w pętli` | brak | Nagrywanie zapętla się zamiast iść dalej |
| `Audio: punch in/out na regionie pętli` | brak | Gra przez region pętli i zachowuje tylko audio w jego wnętrzu |
| `Audio: pre-roll` i `post-roll` | brak | Wyłączone, 1, 2 lub 4 takty odtwarzane przed punch-in i po punch-out |
| `Nagrywanie uruchamia odtwarzanie` | brak | Włączenie nagrywania naciska też odtwarzanie |
| `Rozbrój inserty po zatrzymaniu nagrywania` | brak | Uzbrojone inserty są zwalniane po każdym ujęciu |

`Niska latencja podczas nagrywania` jest w menu prawego przycisku myszy przy przycisku nagrywania i w menu inserta Mixera w sekcji `Nagrywanie na dysk`. Domyślnie jest wyłączone. Gdy insert z wejściem jest uzbrojony albo jego monitoring ma wartość `Wł.`, efekty dodające latencję na drodze do Master są omijane. Zmienia to tylko to, co słyszysz: insert zachowuje wszystkie swoje efekty, gdy ujęcie jest pobierane po nich, na przykład przez `Po efektach`. Efekty wracają, gdy insert przestaje być uzbrojony.

`Kompensacja opóźnienia` jest w tym samym menu `Nagrywanie na dysk` i jest domyślnie włączone, jak w FL Studio. Przesuwa każde ujęcie wstecz o czas podróży w obie strony urządzenia audio, wartość `Precyzyjna korekta (próbki)` z ustawień audio i kompensację opóźnienia pluginów. Wyłącz je, aby zachować ujęcia tam, gdzie zostały nagrane.

`Narzędzia › Anuluj nagrywanie` wyrzuca to, co zapisał bieżący przebieg nagrywania (jego nuty, automatykę i ujęcia audio) i wyłącza nagrywanie. Odtwarzanie trwa dalej, a uzbrojone inserty zostają uzbrojone.

## Nagrywanie do slotów klipów

W [sposobie pracy Clip-launch](/wiki/layouts/) ujęcia mogą trafiać do siatki klipów zamiast na Playlistę, jak w Ableton Live.

- Uzbrój ścieżkę jej przyciskiem uzbrojenia w Mikser sesji. Jej puste sloty pokazują wtedy okrągły przycisk nagrywania w miejscu kwadratu stop. Kliknij jeden, aby nagrywać do tego slotu.
- `Nagrywanie sesji` pod siatką nagrywa uzbrojone ścieżki do wybranej sceny. Gdy ich klipy grają, nowe nuty są do nich dodawane (overdub).
- `Stała długość nagrania` w menu pustego slotu kończy nagrywanie do slotu po 1 lub więcej taktach, a klip gra dalej. `Wył.` nagrywa, dopóki nie zatrzymasz.

## Nagrywanie w pętli i ujęcia

Przy włączonym `Nagrywanie w pętli` nagrywanie zapętla się nad regionem pętli w trybie Song albo nad patternem w trybie Pattern. Przy wyłączonym nagrywanie biegnie prosto dalej.

Każde przejście nagrywania audio w pętli ląduje na następnej wolnej ścieżce Playlisty, a ścieżki tworzą grupę. `Łącz nagrane nuty` rozstrzyga, co gra po zatrzymaniu: wyłączone wycisza każde przejście, włączone gra wszystkie. Kliknij ścieżkę grupy prawym przyciskiem i wybierz `Utwórz comp z tych ujęć`, żeby złożyć jedno ujęcie z przejść. `Po nagrywaniu w pętli gra tylko ostatnie przejście` w opcjach nagrywania wraca do jednej ścieżki.

## Gdzie zapisywane są nagrania

- Zapisany projekt trzyma swoje ujęcia w `Audio/Recorded`, obok pliku projektu.
- Niezapisany projekt nagrywa do `LYNX/Recordings` w twoim folderze Muzyka.
- Ujęcia to pliki WAV z częstotliwością próbkowania urządzenia audio, 16-bit, 24-bit lub 32-bit float. Format (`Format pliku`) i nazwy ujęć ustawia się w `Nagrywanie` w `Opcje › Ustawienia audio...`.
- `Otwórz folder nagrań` jest w menu prawego przycisku przycisku nagrywania. `Plik › Usuń nieużywane nagrania i zapisz...` usuwa ujęcia, których projekt już nie używa.

## Warto wiedzieć

- Uzbrojony insert bez wejścia niczego nie nagrywa. LYNX SHARE ci o tym mówi i wskazuje pole wejścia.
- Nagrywanie odmawia startu, gdy na dysku jest mniej niż 30 sekund miejsca, i ostrzega poniżej 10 minut.
- `Opcje › Nagrywaj audio z wejścia` nagrywa pierwsze dwa kanały wejścia do nowego insertu, gdy nic nie jest uzbrojone.
- W macOS system prosi o dostęp do mikrofonu przy pierwszym nagrywaniu.
- Jeśli LYNX SHARE zamknie się w trakcie nagrywania, przy następnym starcie program proponuje umieszczenie odzyskanych ujęć na Playliście.

## Powiązane

- [Ustawienia audio](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automatyka](/wiki/automation/)
- [Kontrolery MIDI](/wiki/midi-controllers/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)
