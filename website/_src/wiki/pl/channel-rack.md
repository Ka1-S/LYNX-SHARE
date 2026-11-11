---
source: f08a3da66d
---
![Channel Rack z wierszami kroków](/assets/img/rack.webp)

Channel Rack wymienia wszystkie instrumenty (kanały) projektu, po jednym w wierszu, i pokazuje kroki bieżącego patternu. `F6` pokazuje go lub ukrywa. Prawy klik na przycisku Pattern w transporcie również go pokazuje.

## Zrób pattern perkusyjny

1. Naciśnij `F6`, aby pokazać Channel Rack.
2. Kliknij pole `+ dodaj kanał` pod ostatnim wierszem i wybierz instrument z menu. Możesz też kliknąć dwukrotnie sample w [Browserze](/wiki/browser/), aby dostać nowy kanał samplera.
3. Klikaj kroki w wierszu kanału, aby je włączać. Przeciągnij, aby narysować ciąg kroków.
4. Kliknij krok prawym przyciskiem, aby go skasować.
5. Naciśnij `Space`, aby odtworzyć. W trybie Pattern pattern gra w pętli.
6. Przeciągnij gałkę głośności wiersza, aby ustawić poziom.

## Programowanie kroków

- Kliknij krok, aby go włączyć. Przeciąganie rysuje ciąg kroków. Jeśli przeciąganie zaczyna się na zapalonym kroku, kasuje zamiast rysować.
- Prawy klik lub prawe przeciąganie kasuje.
- Kliknięcie za końcem patternu dodaje krok i wydłuża pattern.
- Wiersz, którego nuty nie są zwykłymi krokami (inne klawisze, dłuższe nuty, slide), pokazuje miniaturę nut. Kliknięcie otwiera je w [Piano Roll](/wiki/piano-roll/).
- Pliki audio możesz upuszczać na rack, aby tworzyć kanały.

Menu kanału (prawy klik na nazwie kanału) zawiera `Wypełnij co 2 kroki`, `Wypełnij co 4 kroki`, `Wypełnij co 8 kroków`, `Zaawansowane wypełnianie` oraz `Obróć w lewo` i `Obróć w prawo`.

## Pas wykresu

![Pas wykresu otwarty pod kanałem Kick, z jednym słupkiem velocity na krok](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` pokazuje pas wykresu pod zaznaczonym kanałem. Przyciski po jego lewej stronie wybierają, co edytuje: Velocity, Panoramę, Release, Dostrojenie, Mod X lub Mod Y. Przeciąganie ustawia wartość dla każdego kroku, prawe przeciąganie rysuje rampę, a `Alt`+klik resetuje krok.

## Elementy wiersza

| Element | Co robi |
|---|---|
| Lampka wyciszenia | Klik wycisza. `Ctrl`+klik lub prawy klik włącza solo. |
| Gałki panoramy i głośności | Przeciągaj lub używaj kółka. |
| Pole routingu | Przeciąganie lub kółko wybiera insert Mixera. Dwuklik pokazuje ten insert w Mixerze. Prawy klik bierze następny wolny insert. |
| Przycisk nazwy | Klik otwiera lub zamyka edytor kanału. `Alt`+klik zostawia otwarte pozostałe edytory. `Ctrl`+klik daje odsłuch. Przeciągnięcie w górę lub w dół zmienia kolejność. Prawy klik otwiera menu kanału. |
| Selektor | Klik zaznacza jeden kanał. `Ctrl`+klik lub prawy klik dodaje albo usuwa. `Shift`+klik zaznacza zakres. Dwuklik zaznacza wszystkie. |

## Patterny

Rack zawsze edytuje bieżący pattern. Selektor patternu w racku pokazuje jego nazwę: kliknij nazwę, aby zobaczyć listę, albo użyj strzałek. `Shift`+kółko nad tytułem racka również zmienia pattern.

Pole długości patternu w racku ustawia długość w taktach (przeciągnij je lub użyj kółka). Kliknij je dwukrotnie, aby długość została ustalona automatycznie na podstawie nut.

Gałka swingu na głównym pasku narzędzi działa na wszystkie kanały. `Miks swingu` w menu kanału ustawia, w jakim stopniu podąża za nią jeden kanał.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Następny pattern | `Numpad +` | Czyni następny pattern bieżącym |
| Poprzedni pattern | `Numpad -` | Czyni poprzedni pattern bieżącym |
| Następny pusty pattern | `F4` | Przechodzi do następnego pustego patternu i pyta o nazwę |
| Zmień nazwę i kolor | `F2` | Zmienia nazwę i kolor bieżącego patternu |

## Granie nut z klawiatury komputera

`Ctrl+T` zamienia klawiaturę do pisania w pianino: `Z` do `M` oraz `Q` do `P` grają zaznaczonymi kanałami. Gdy jest włączone, jednoliterowe skróty edytorów są wstrzymane.

## Klawisze

Działają, gdy Channel Rack ma fokus.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Zaznacz kanał | `Up` / `Down` | Zaznacza kanał powyżej lub poniżej |
| Rozszerz zaznaczenie | `Shift+Up` / `Shift+Down` | Dodaje do zaznaczenia kanał powyżej lub poniżej |
| Przesuń kanały | `Alt+Up` / `Alt+Down` | Przesuwa zaznaczone kanały w górę lub w dół |
| Zaznacz wszystko | `Ctrl+A` | Zaznacza wszystkie kanały |
| Wytnij, kopiuj, wklej | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Wycina, kopiuje lub wkleja kroki zaznaczonych kanałów |
| Obróć kroki | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Obraca kroki zaznaczonych kanałów |
| Klonuj | `Alt+C` | Klonuje zaznaczone kanały |
| Usuń | `Alt+Del` | Usuwa zaznaczone kanały |
| Grupuj | `Alt+G` | Umieszcza zaznaczone kanały w nowej grupie filtra |
| Kieruj do Mixera | `Ctrl+L` | Kieruje zaznaczone kanały na wolne inserty Mixera |
| Wycisz | `1` do `0` | Wycisza lub przywraca kanały od 1 do 10 |
| Solo | `Ctrl+1` do `Ctrl+0` | Włącza solo dla kanałów od 1 do 10 |
| Pas wykresu | `Ctrl+K` | Pokazuje lub ukrywa pas wykresu |

## Inne pozycje w menu kanału

`Piano Roll`, `Zmień nazwę i kolor`, `Zastąp` (wymiana instrumentu), `Klonuj`, `Delete`, `Arpeggiator`, `Punkt pętli`, `Zapisz MIDI do`, `Sortuj kanały`, `Zaznacz nieużywane kanały` i `Ustawienia kanału`.

## Warto wiedzieć

- Menu dodawania otwiera tylko pole `+ dodaj kanał`.
- Rack nie ma własnego patternu. Zmiana bieżącego patternu gdziekolwiek zmienia to, co pokazuje rack.
- Wiersza, który pokazuje miniaturę nut, nie da się edytować jako kroków. Użyj do tego Piano Roll.
- Klawisze numeryczne sięgają tylko pierwszych dziesięciu kanałów.
- Klawisze z tabeli wymagają, aby rack miał fokus. Najpierw kliknij rack.

## Powiązane

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Skróty](/wiki/shortcuts/)
