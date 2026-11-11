---
source: 7ca5e689db
---
![Paleta szybkiego dodawania: instrumenty po lewej, efekty po prawej, każdy z oznaczeniem wbudowany lub wbudowany silnik FL](/assets/img/wiki/quickadd.webp)

LYNX SHARE ma własne instrumenty i efekty, więc projekt gra bez żadnych pluginów innych firm. Są trzy rodzaje: oryginały LYNX SHARE (syntezator, sampler, odtwarzacz SoundFontów, perkusje, korektory, kompresory, limitery, pogłosy, delaye), silniki napisane tak, by odtwarzały ustawienia natywnych pluginów FL Studio, oraz efekty zastępujące znane pluginy innych firm.

Każdy z nich ma własną stronę w [katalogu](/instruments-and-effects/). `Pomoc › Wbudowane pluginy (lista online)...` otwiera listę ze stanem każdego silnika.

## Dodanie instrumentu i efektu

1. Naciśnij `F6`, aby pokazać Channel Rack.
2. Kliknij pole `+ dodaj kanał` pod ostatnim wierszem i wybierz instrument. Menu grupuje je według kategorii.
3. Naciśnij `F9`, aby pokazać Mixer, i zaznacz insert, przez który gra kanał.
4. Kliknij pusty slot w panelu efektów i wybierz efekt. Otworzy się jego okno.
5. Kliknij nazwę slotu, aby ukryć to okno lub pokazać je ponownie.

## Wybór pluginu

`F8` otwiera wybór pluginu. Zajmuje cały ekran i pokazuje każdy generator i efekt jako obrazek jego własnego edytora, a kategorie jako karty na dole.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Wyszukiwanie | Pisz | Przyciemnia pluginy, które nie pasują |
| Przechodzenie po pluginach | `Up` / `Down` | Przesuwa podświetlenie |
| Dodaj | `Enter`, dwuklik lub prawy klik | Generator staje się nowym kanałem. Efekt trafia do zaznaczonego inserta Mixera |
| Przeciągnij obrazek | | Na Channel Rack, aby dostać nowy kanał, na kanał, aby zastąpić jego instrument, albo na insert lub slot Mixera |
| Zamknij | `Esc` lub `F8` | Zamyka wybór bez dodawania czegokolwiek |

Obrazki własnych pluginów LYNX SHARE są rysowane przez LYNX SHARE w tle. Plugin innej firmy pokazuje zwykły kafelek z nazwą producenta, dopóki jego edytor nie zostanie otwarty choć raz.

`Edycja › Paleta szybkiego dodawania...` otwiera zamiast tego zwykłą listę: instrumenty po lewej, efekty po prawej, filtrowane w trakcie pisania. `Tab` przełącza listy, a `Enter` dodaje podświetloną pozycję. Baza pluginów w [Browserze](/wiki/browser/) (`Shift+F8`) wymienia te same pluginy w postaci drzewa.

![Baza pluginów w Browserze z otwartym folderem silników FL](/assets/img/wiki/browser-plugins.webp)

## Presety

Okno wbudowanego efektu ma w nagłówku pasek presetów.

| Kontrolka | Co robi |
|---|---|
| Strzałki | Poprzedni i następny preset |
| Nazwa presetu | Otwiera menu: `Presety LYNX SHARE`, `Twoje presety`, `Presety FL Studio` tam, gdzie istnieją, `Zapisz preset...` i `Pokaż folder presetów` |
| `A` / `B` | Dwa ustawienia do porównania. Przycisk obok kopiuje to, które słyszysz, do drugiego |

Nazwa presetu oraz ustawienia A i B są zapisywane razem z projektem.

## Co znaczy „otwiera ustawienia FL"

Natywne pluginy FL Studio nigdy nie są ładowane. LYNX SHARE ma własny silnik dla każdego z nich, a ten silnik czyta ustawienia zapisane przez plugin FL. Działa to w dwóch miejscach:

- Projekt `.flp`. Każdy generator i efekt FL otwiera się na swoim silniku LYNX SHARE z zapisanymi ustawieniami, a jego kontrolki można dalej edytować i automatyzować.
- Preset `.fst`. Kliknij dwukrotnie preset syntezatora w Browserze, aby dostać nowy kanał, albo przeciągnij preset efektu na insert Mixera. Kliknij prawym przyciskiem zapełniony slot efektu, aby zobaczyć `Presety FL`, czyli presety znalezione w Twojej instalacji FL Studio.

Jak blisko oryginału jest każdy silnik, podajemy osobno dla każdego, od Exact do Placeholder. Poziomy opisuje strona [Przejście z FL Studio](/wiki/coming-from-fl-studio/).

## Silniki, które potrzebują Twojej instalacji FL Studio

LYNX SHARE nie zawiera kodu, audio ani danych fal Image-Line. Niektóre silniki potrzebują fal, sampli, impulsów lub tablic należących do oryginalnego pluginu i czytają je z Twojej własnej instalacji FL Studio w czasie pracy.

| Typ | Silniki |
|---|---|
| Instrumenty | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Efekty | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Jeśli plików nie uda się znaleźć, dodanie takiego silnika może się zakończyć komunikatem, że nie znaleziono plików danych instalacji FL Studio albo nie dało się ich odczytać.

## Zamienniki pluginów innych firm

Gdy projekt używa efektu innej firmy, który nie jest zainstalowany, jego slot zajmuje efekt LYNX SHARE o tej samej roli: pogłos za pogłos, delay za delay.

- Slot pokazuje nazwę oryginalnego pluginu z plakietką LYNX SHARE.
- Tam, gdzie da się odczytać zapisany stan, jego ustawienia są przekładane. W przeciwnym razie zamiennik startuje z domyślnego presetu.
- Oryginalny plugin i jego ustawienia zostają w projekcie. Po zainstalowaniu pluginu projekt znowu otwiera się z nim.
- Kliknij prawym przyciskiem slot zamiennika i wybierz `Omiń wszystkie zamienniki LYNX SHARE w tym projekcie`, aby usłyszeć projekt bez nich.

Przełącznik to `Zastępuj brakujące pluginy odpowiednikami LYNX SHARE` w `Opcje › Ustawienia ogólne...`, domyślnie włączony. W nowym projekcie te same efekty są w menu pustego slotu pod `Odpowiedniki LYNX SHARE`.

## Warto wiedzieć

- Brakujące instrumenty nie są jeszcze zastępowane. Brakujący plugin instrumentu staje się wbudowanym syntezatorem dobranym według nazwy kanału.
- Automatyka parametrów oryginalnego pluginu zostaje zachowana, ale nie steruje zamiennikiem.
- Paczki FLEX są zaszyfrowane i nigdy nie są otwierane. Kanał FLEX dostaje patch syntezatora dobrany według nazwy presetu.
- Insert ma 10 slotów efektów.

## Powiązane

- [Katalog instrumentów i efektów](/instruments-and-effects/)
- [Pluginy zewnętrzne](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Przejście z FL Studio](/wiki/coming-from-fl-studio/)
