---
source: 7db9a73959
---
Z innymi osobami można pracować nad projektem na dwa sposoby, oba w `Plik › Sesja online`. Paczka to plik, który wysyłasz znajomemu, a on odsyła partię z powrotem. Nie potrzebuje sieci, konta ani serwera. Sesja na żywo łączy wszystkich przez internet przez przekaźnik LYNX SHARE, a każda zmiana trafia do wszystkich. W jednym projekcie bierze udział do 4 osób, razem z tobą.

## Wyślij projekt znajomemu

![Wyślij projekt](/assets/img/wiki/session-send.webp)

1. Otwórz projekt i wybierz `Plik › Sesja online › Wyślij projekt...`.
2. Wpisz swoje imię i imię znajomego.
3. Wybierz, co znajomy zachowuje po zakończeniu sesji: `Tylko własną partię (zalecane)` albo `Pełną kopię projektu`.
4. Zapisz plik `.lynxpack`.
5. Wyślij plik, jak chcesz: e-mailem, we wspólnym folderze, na pendrive.

Paczka zawiera projekt, każdy sampel i nagranie, których używa, oraz rendery audio ("zastępstwa") partii, które używają pluginu zewnętrznego. Znajomy słyszy te partie, nie mając twoich pluginów.

## Nagraj partię i odeślij ją

Znajomy robi to tak:

1. Otwiera `.lynxpack` przez `Plik › Otwórz...`. Podsumowanie pokazuje, kto go wysłał i które inserty miksera oraz ścieżki Playlisty są twoje. Klika `Otwórz`.
2. Dodaje własne kanały, patterny i audio na swoich insertach i ścieżkach Playlisty. Materiał gospodarza zostaje bez zmian.
3. Wybiera `Plik › Sesja online › Odeślij moją partię...` i wysyła nową paczkę gospodarzowi.

Każdy gość dostaje kolor, 20 insertów miksera i 16 ścieżek Playlisty.

## Scal partię

Gospodarz robi to tak:

1. Otwiera projekt, który został wysłany.
2. Wybiera `Plik › Sesja online › Scal partię...` i wskazuje paczkę odesłaną przez znajomego.
3. Kanały, patterny, nuty, klipy i inserty znajomego są dodawane w jego kolorze. Nic twojego nie jest zmieniane.

Scalenie to jeden krok cofania. Nowsza partia od tego samego znajomego zastępuje wcześniejszą.

## Rozpocznij sesję na żywo lub dołącz do niej

![Rozpocznij sesję na żywo](/assets/img/wiki/session-start.webp)

Gospodarz rozpoczyna sesję:

1. Otwiera projekt i wybiera `Plik › Sesja online › Rozpocznij sesję na żywo...`.
2. Wpisuje swoje imię.
3. W `Goście mogą edytować` wybiera `Tylko własne kanały, patterny, inserty i wiersze (zalecane)` albo `Wszystko`. W `Po zakończeniu sesji goście zachowują` wybiera ich własną partię albo pełną kopię.
4. Klika `Początek`, po czym kopiuje kod zaproszenia i wysyła go maksymalnie 3 osobom.
5. Gdy ktoś prosi o dołączenie, klika `Wpuść`.

Gość dołącza:

1. Wybiera `Plik › Sesja online › Dołącz do sesji na żywo...`.
2. Wpisuje swoje imię, wkleja kod zaproszenia i klika `Dołącz`.
3. Czeka, aż gospodarz go wpuści. Otwiera się projekt gospodarza.

## Podczas sesji na żywo

![Czat sesji](/assets/img/wiki/session-chat.webp)

Menu `Plik › Sesja online` ma te polecenia, gdy sesja trwa:

| Polecenie | Co robi |
|---|---|
| `Czat sesji...` | Otwiera czat. Pozycja menu pokazuje liczbę nieprzeczytanych wiadomości |
| `Podążaj za transportem osoby prowadzącej` | Dla gości: odtwarzanie, stop i pozycja podążają za gospodarzem |
| `Osoby i uprawnienia...` | Dla gospodarza: co każda osoba może zmieniać i co każda zachowuje na końcu |
| `Udostępnij pluginy gościom...` | Dla gospodarza: których z twoich pluginów mogą używać goście. Pozycja menu pokazuje, ile jest udostępnionych |
| `Pokaż kod zaproszenia...` | Dla gospodarza: pokazuje kod jeszcze raz, z przyciskiem `Kopiuj` |
| `Zakończ sesję na żywo` | Dla gospodarza. Wszyscy wychodzą, a kod przestaje działać |
| `Opuść sesję na żywo` | Dla gościa. Twoja partia projektu zostaje u ciebie |

W `Osoby i uprawnienia...` każda osoba dostaje jedną z czterech ról: `Własne kanały, patterny, inserty i wiersze`, `Własne partie + tempo i ustawienia projektu`, `Wszystko` albo `Tylko słuchanie (bez edycji)`. Wyłączenie uprawnienia nigdy niczego nie usuwa.

## Pluginy, których inni nie mają

Kanał lub insert, którego pluginu nie ma na twoim komputerze, gra zastępstwo i pokazuje bursztynową odznakę `R` w Channel Racku i Mixerze.

- W paczce zastępstwa to rendery audio zrobione przy zapisie paczki. Podążają za klipami, które przesuwasz, ale nie za zmianami nut pluginu, którego nie masz. Poproś jego właściciela o nową paczkę.
- W sesji na żywo partie używające pluginu, którego ktoś nie ma, są renderowane przez osobę, która go ma, i przesyłane pozostałym jako audio.

## Udostępnij swoje pluginy gościom

W sesji na żywo gospodarz może pozwolić gościom używać pluginów, które ma tylko on.

Gospodarz robi tak:

1. Wybierz `Plik › Sesja online › Udostępnij pluginy gościom...`.
2. Zaznacz pluginy do udostępnienia. `Szukaj w swoich pluginach` zawęża listę. Nic nie jest udostępniane, dopóki tego nie zaznaczysz.

Gość robi tak:

1. Otwórz menu pluginów jednego ze swoich kanałów lub slotów efektów. Udostępnione pluginy są na początku, pod `On <host>'s computer`.
2. Wybierz jeden. Jego okno wylicza parametry pluginu jako suwaki, z `Szukaj parametrów`.
3. Dodawaj nuty i poruszaj suwakami. Okno pokazuje `Updating...`, gdy komputer gospodarza renderuje, a potem `Aktualne`.

- Plugin zostaje na komputerze gospodarza i działa na licencji gospodarza. LYNX SHARE nigdy nie wysyła pluginu, jego plików, presetów ani licencji.
- Gość słyszy render, który po chwili podąża za nutami i wartościami. Nie jest odtwarzany na żywo, a gość nie dostaje własnego okna pluginu.
- Gdy gospodarza nie ma, gra ostatni render. Udostępnianie kończy się wraz z sesją, a ostatni render zostaje.
- Gość z rolą `Tylko słuchanie (bez edycji)` nie może używać udostępnionych pluginów.
- Niektóre licencje pluginów nie pozwalają, by obsługiwał je ktoś inny, nawet zdalnie. Sprawdź każdą licencję, zanim udostępnisz plugin.

## Co idzie przez sieć

Paczki to pliki. Nic nie jest nigdzie wysyłane.

Sesja na żywo łączy się tylko wtedy, gdy w niej jesteś. Wszystko idzie przez przekaźnik `relay.lynx-share.com`, szyfrowane od końca do końca kluczem pochodzącym z kodu zaproszenia. Przekaźnik przekazuje dane dalej, nie mogąc ich odczytać. Widzi twój adres IP, kiedy byłeś połączony i ile bajtów przeszło. Pozostałe osoby w sesji widzą twoje imię, twoje zmiany, sample, których używa twoja partia, i to, czy grasz lub nagrywasz. Nie widzą twojego adresu IP. Szczegóły są w [polityce prywatności](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Warto wiedzieć

- `Tylko własna partia` to uprzejmość między muzykami, nie ochrona przed kopiowaniem. Paczka to zwykły zip.
- Obie strony powinny używać tej samej wersji LYNX SHARE. Podsumowanie paczki ostrzega, gdy się różnią.
- Gość, któremu zerwie się połączenie, łączy się sam ponownie i zachowuje swoje zmiany.
- Jeśli LYNX SHARE gospodarza zamknie się w trakcie sesji, przy następnym starcie program proponuje jej wznowienie, a goście łączą się sami ponownie.
- Jeśli LYNX SHARE gościa zamknie się w trakcie sesji, przy następnym starcie program pyta, czy wrócić. `Wróć` przywraca cię jako tę samą osobę, z twoją partią i wszystkim, co zmieniłeś. Działa to przez 12 godzin.
- Wersja na Linuksa może prowadzić sesję na żywo od 0.1.26.
- W sesji na żywo cofanie i ponawianie przywracają tylko twoje własne zmiany.
- Każdy, kto ma kod zaproszenia, może poprosić o dołączenie, ale dopiero gdy gospodarz go wpuści.

## Powiązane

- [Co robi tylko LYNX SHARE: sesje online](/why/#online)
- [Formaty plików](/wiki/file-formats/)
- [Pluginy](/wiki/plugins/)
- [Cofanie i historia](/wiki/undo-history/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)
