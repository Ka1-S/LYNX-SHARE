---
source: b6e9ef0dd0
---
![Mixer](/assets/img/mixer.webp)

Każdy kanał gra przez insert Mixera. Każdy insert ma suwak, panoramę, wyciszenie i solo, miernik szczytów, dziesięć slotów efektów z własną ilością dry/wet oraz wbudowany korektor. Wszystkie inserty kończą w Masterze, chyba że poprowadzisz je gdzie indziej. Naciśnij `F9`, żeby pokazać lub ukryć Mixer.

Projekt może mieć do 127 insertów. Mixer ma pasek dodawania nowego. Pole w pasku tytułu Mixera wybiera jeden z ośmiu układów pasków, od `Kompaktowy` do `Bardzo duży`.

## Dodaj efekt do kanału

1. W [Channel Racku](/wiki/channel-rack/) zaznacz kanał i naciśnij `Ctrl+L`. Kanał trafia do wolnego insertu, który przejmuje jego nazwę i kolor.
2. Naciśnij `F9`, żeby pokazać Mixer, i kliknij pasek tego insertu.
3. W panelu efektów kliknij pusty slot i wybierz efekt.
4. Kliknij nazwę slotu, żeby pokazać lub ukryć okno efektu.
5. Kliknij kontrolkę slotu, żeby ominąć efekt i porównać.

## Wyślij kanał do insertu

W Channel Racku przeciągnij pole trasy kanału (albo użyj na nim kółka), żeby wybrać numer insertu. Dwuklik na polu pokazuje ten insert w Mixerze.

`Ctrl+L` w Channel Racku działa na kilku zaznaczonych kanałach naraz. Każdy trafia do własnego wolnego insertu.

Przy aktywnym Mixerze `Ctrl+L` łączy zaznaczone kanały z zaznaczonym insertem, a `Ctrl+Shift+L` łączy je z kolejnymi insertami, zaczynając od niego.

## Zaznaczaj i przesuwaj inserty

- Kliknij pasek, żeby go zaznaczyć.
- `Ctrl`+klik albo przeciągnięcie przez paski zaznacza zakres. `Ctrl+Shift`+klik dodaje lub usuwa jeden.
- Przeciągnij pasek na bok albo naciśnij `Alt+Left` i `Alt+Right`, żeby przesunąć zaznaczone inserty.
- Kliknij pasek prawym przyciskiem, żeby zobaczyć menu insertu. `F2` zmienia nazwę i kolor zaznaczonego insertu.
- `Del` lub `Delete` w menu inserta usuwa zaznaczone inserty. LYNX SHARE najpierw pyta. Zaznacz `Zapamiętaj wybór`, żeby następnym razem pominąć pytanie.

Przesuwanie, wstawianie i usuwanie insertów zachowuje sendy, sidechainy i automatykę przy ich insertach.

Sekcja `Mixer` w menu inserta i `Opcje › Mixer` mają jeszcze dwa polecenia. `Ułóż inserty jak w Playliście` przenosi inserty powiązane ze ścieżkami Playlisty na początek, w kolejności Playlisty. Gdy włączone jest `Zachowaj połączenia z usuniętymi insertami, wyszarzone (LYNX)`, wysyłki, które szły do usuniętego inserta, zostają wyszarzone, więc możesz je połączyć ponownie.

## Wyciszenie i solo

Kliknij kontrolkę wyciszenia, żeby wyciszyć insert. Kliknij ją prawym przyciskiem albo z `Ctrl`, żeby włączyć solo. `S` włącza solo na zaznaczonych insertach, a `Alt+S` włącza je razem z insertami, które je zasilają.

Wyciszony insert nadal porusza miernikiem. W nowych projektach i w zaimportowanych projektach FL Studio nadal też uruchamia swoje efekty, a odcięte jest tylko jego wyjście do Master i jego wysyłki. Aby wyciszony insert przestał przetwarzać dźwięk, włącz `Wyciszone inserty nie przetwarzają (LYNX, oszczędza CPU; ten projekt)` w sekcji `Mixer` menu inserta lub w `Opcje › Mixer`. Jego miernik się wtedy zatrzymuje, a efekty startują od zera po wyłączeniu wyciszenia.

## Efekty

Panel efektów pokazuje dziesięć slotów zaznaczonego insertu.

- Przeciągnij slot albo użyj kółka nad nim, żeby przesunąć go w górę lub w dół.
- Kliknij slot prawym przyciskiem, żeby zobaczyć jego opcje (zastąp, przesuń, usuń).
- Gdy menu slotu jest otwarte, `Del` usuwa plugin w tym slocie.
- `Grupuj w rack efektów audio` w menu slotu wkłada efekt do nowego racka, a `Grupuj wszystko w rack efektów audio` robi to dla całego łańcucha. `Rozgrupuj` wkłada efekty racka z jednym łańcuchem z powrotem do slotów.
- Przełącznik w nagłówku panelu włącza lub wyłącza wszystkie efekty insertu.
- Efekt z bazy pluginów w [Browserze](/wiki/browser/) możesz przeciągnąć na insert albo slot efektu.
- `Czysty wokal` w menu insertu umieszcza na insercie gotowy łańcuch oczyszczania wokalu.

## Edison w slocie

Edison, edytor audio i rejestrator, może siedzieć w dowolnym slocie efektu, po kilka na insert. Jego okno to edytor audio z rzędem elementów sterowania nagrywaniem.

- `Otwórz edytor audio` (`Ctrl+E`) w menu insertu wczytuje Edisona do pierwszego wolnego slotu insertu i otwiera go. `Otwórz edytor audio i nagrywaj` (`Shift+E`) wczytuje go gotowego do nagrywania, gdy utwór gra.
- Tryby nagrywania to `Teraz`, `Na wejściu` (domyślny: czeka, aż wejście przekroczy próg), `Wejście` i `Przy odtwarzaniu`. `Dołącz` umieszcza nowe nagranie za audio, które już tam jest. Maksymalny czas nagrywania to `Bez limitu` lub 1, 3, 5, 10, 30 albo 60 minut: nowy slot startuje na 30, `Otwórz edytor audio i nagrywaj` ustawia 5.
- `Sync` odtwarza audio Edisona w czasie utworu, gdy utwór gra.
- Edison nagrywa dalej po zamknięciu jego okna.
- `Narzędzia › Makra › Usuń wszystkie instancje Edison` opróżnia każdy slot Edison w projekcie, po pytaniu.
- Sloty Edison w projektach FL Studio (.flp) otwierają się z ich audio.

## Korektor insertu

Każdy insert ma własny korektor. Przeciągnij pasmo, żeby ustawić jego częstotliwość i poziom, użyj kółka do szerokości, a dwuklik na paśmie przywraca 0 dB. Kliknij tytuł korektora prawym przyciskiem, żeby go ominąć. Jeśli chcesz Mixera bez wbudowanego korektora, nowe projekty można ustawić tak, by zaczynały z czystym mikserem.

## Routing, sendy i sidechain

![Mixer z zaznaczonym insertem Kick, jego sendami narysowanymi jako kable pod paskami i panelem efektów po prawej](/assets/img/wiki/mixer-sends.webp)

Zaznacz insert, który chcesz poprowadzić. Każdy pozostały pasek ma wtedy przełącznik trasy:

- Kliknij przełącznik na innym pasku, żeby wysłać tam zaznaczony insert. Przeciągnięcie po włączeniu ustawia poziom sendu.
- `Ctrl`+klik prowadzi tylko do tego insertu.
- `Shift`+klik robi send sidechain. `Ctrl+Shift`+klik robi go tylko jako sidechain.
- Prawy klik otwiera te same wybory jako menu.
- Dla wysyłki pre-fader kliknij prawym przyciskiem przełącznik trasy lub pokrętło poziomu wysyłki i zaznacz `Send pre-fader (fader tej ścieżki go nie zmienia)`. Wysyłka jest wtedy oznaczona `PRE`.

Efekty z wejściem bocznym, jak kompresor, mają wybór `Sidechain z`, który wylicza inserty do nasłuchu. Wybranie jednego tworzy trasę za ciebie.

## Grupy i suwaki VCA

Grupy Mixera zwija się strzałką (albo `Alt`+klik dla wszystkich grup). Suwak VCA porusza poziomem swoich insertów, które zachowują własne suwaki. Utwórz go z menu insertu: w sekcji Grupa wybierz `VCA › Nowy VCA z zaznaczonych`.

## Nagrywanie

Każdy insert ma przycisk uzbrojenia nagrywania. Gdy insert jest uzbrojony, Nagrywaj i Odtwórz nagrywają jego wejście audio do klipu Playlisty. `Shift+R` uzbraja zaznaczony insert i od razu zaczyna nagrywanie. Zobacz [Nagrywanie](/wiki/recording/).

Gdy wybierzesz wejście zewnętrzne dla insertu, który nagrywa też inne audio, LYNX SHARE pyta, czy przełączyć go na `Tylko wejście zewnętrzne`, aby nagranie zawierało samo wejście. `Mixer › Nagrywanie na dysk › Kompensacja opóźnienia` w menu insertu jest domyślnie włączone. Po wyłączeniu nagrane ujęcia są umieszczane bez żadnej kompensacji opóźnienia.

## Klawisze

`Ctrl+L` i `Ctrl+Shift+L` mają to znaczenie tylko wtedy, gdy aktywny jest Mixer.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Połącz kanały | `Ctrl+L` | Łączy zaznaczone kanały z zaznaczonym insertem |
| Połącz z kolejnymi insertami | `Ctrl+Shift+L` | Łączy zaznaczone kanały z insertami, zaczynając od zaznaczonego |
| Przesuń inserty | `Alt+Left` / `Alt+Right` | Przesuwa zaznaczone inserty |
| Zmień nazwę i kolor | `F2` | Zmienia nazwę i kolor zaznaczonego insertu |
| Solo | `S` | Włącza solo na zaznaczonych insertach |
| Solo ze źródłami | `Alt+S` | Włącza solo na zaznaczonych insertach i insertach, które je zasilają |
| Usuń inserty | `Del` | Usuwa zaznaczone inserty, po pytaniu |
| Uzbrój i nagrywaj | `Shift+R` | Uzbraja zaznaczony insert i zaczyna nagrywanie |
| Open audio editor | `Ctrl+E` | Wczytuje Edisona do pierwszego wolnego slotu insertu i otwiera go |
| Open audio editor and record | `Shift+E` | To samo, gotowy do nagrywania, gdy utwór gra |

## Warto wiedzieć

- `Miernik głośności obok Mastera (LYNX)` w `Opcje › Mixer` pokazuje głośność krótkoterminową i zintegrowaną (LUFS) oraz true peak wyjścia Mastera, z przyciskiem `Resetuj`.
- Plugin instrumentu z kilkoma wyjściami może grać każde wyjście na własnym insercie. Zobacz [Pluginy innych producentów](/wiki/plugins/).
- `Ctrl+L` robi co innego w Channel Racku i w Mixerze. W racku wybiera wolne inserty. W Mixerze używa zaznaczonego insertu.
- Przełączniki tras pojawiają się na innych paskach, nie na zaznaczonym.
- Limit to 127 insertów na projekt i dziesięć slotów efektów na insert.
- Nagranie Edisona jest przechowywane jako plik WAV w folderze projektu, a nie w pliku projektu.
- Mikser sesji pod siatką klipów należy tylko do sposobu pracy Clip-launch. Pokazuje te same inserty co ten Mixer. Zobacz [Układy](/wiki/layouts/).

## Powiązane

- [Channel Rack](/wiki/channel-rack/)
- [Nagrywanie](/wiki/recording/)
- [Automatyka](/wiki/automation/)
- [Pluginy zewnętrzne](/wiki/plugins/)
- [Instrumenty i efekty](/instruments-and-effects/)
