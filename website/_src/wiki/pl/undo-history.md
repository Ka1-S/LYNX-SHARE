---
source: 9f36c60cc7
---
![Okno Historia cofania: sześć kroków od Start do Add notes, z Mute: Kick jako bieżącym](/assets/img/wiki/history.webp)

Każda zmiana w projekcie to krok w historii cofania: dodana nuta, przesunięty klip, suwak, zmiana routingu, tempo. `Edycja › Historia cofania...` (`Ctrl+Alt+H`) pokazuje kroki jako listę. Osobno LYNX SHARE zapisuje kopie zapasowe projektu podczas pracy.

## Wróć do wcześniejszego kroku

1. Wybierz `Edycja › Historia cofania...` albo naciśnij `Ctrl+Alt+H`.
2. Lista pokazuje kroki z najnowszym na dole. Pierwszy wiersz, `Początek`, to projekt w stanie, w jakim został otwarty.
3. Kliknij krok. Projekt wraca do stanu tuż po nim, a późniejsze kroki szarzeją.
4. Kliknij szary krok, żeby znów iść do przodu.
5. Wprowadź nową zmianę, żeby kontynuować od wybranego kroku. Szare kroki znikają.

## Klawisze cofania

`Ctrl+Z` nie działa tak jak w większości programów. Cofa ostatni krok, a naciśnięty ponownie go ponawia, więc przełącza między dwoma ostatnimi stanami. Żeby cofać dalej, użyj `Ctrl+Alt+Z`.

| Polecenie | Klawisz | Co robi |
|---|---|---|
| `Cofnij` | `Ctrl+Z` | Cofa ostatni krok. Po cofnięciu ponawia następny krok, a pozycja menu brzmi `Cofnij (ponów)` |
| `Cofnij wstecz` | `Ctrl+Alt+Z` | Zawsze cofa, o jeden krok na naciśnięcie |
| `Ponów` | `Ctrl+Y` lub `Ctrl+Shift+Z` | Idzie do przodu |
| `Historia cofania...` | `Ctrl+Alt+H` | Otwiera listę |

Po każdym cofnięciu lub ponowieniu pasek podpowiedzi nazywa krok i pokazuje poziom, na przykład `Level 2/3`.

Jeśli wolisz zwykłe zachowanie, włącz `Alternatywny tryb cofania (klasyczne cofanie zamiast cofnij/ponów pod tym samym skrótem)` w `Opcje › Ustawienia ogólne...`. `Ctrl+Z` zawsze cofa wtedy o krok.

## Co jest krokiem, a co nie

- Zmiany w projekcie są krokami: nuty, patterny, klipy, kanały, poziomy i routing miksera, efekty, automatyka, tempo, wyciszenia.
- Nagranie jest krokiem. Tak samo scalenie partii w [sesji online](/wiki/online-sessions/) i `Utwórz comp z tych ujęć`.
- Rzeczy, które nie zmieniają projektu, nie są krokami: zoom, przewijanie, przesuwanie okien i własne ustawienia programu.
- Zapisu i eksportu nie da się cofnąć. Zapisane pliki zostają.
- Własne okna pluginów LYNX SHARE mają osobne cofanie i ponawianie zmian tego pluginu, obok ustawień A i B.

## Ustawienia

Oba są w `Opcje › Ustawienia ogólne...`.

| Ustawienie | Co robi |
|---|---|
| `Maksymalna liczba kroków cofania` | Ile kroków jest trzymanych: 25, 50, 100, 200, 500 lub 1000. Domyślnie 100 |
| `Alternatywny tryb cofania` | Robi z `Ctrl+Z` klasyczne cofanie |

## Kopie zapasowe

LYNX SHARE domyślnie co 2 minuty zapisuje kopię zapasową otwartego projektu i trzyma 10 najnowszych na projekt. Kopia to zwykły plik `.lynx` w folderze `autosave` wewnątrz folderu ustawień, nazwany od projektu, z `(autosaved on ...)` oraz datą i godziną.

Za każdym razem, gdy zapisujesz nad istniejącym plikiem projektu, zastępowana wersja jest kopiowana do tego samego folderu z `(overwritten on ...)` w nazwie.

| Polecenie | Co robi |
|---|---|
| `Plik › Przywróć ostatnią kopię zapasową...` | Zastępuje otwarty projekt jego najnowszą kopią, po zapytaniu. Sam plik projektu zmienia się dopiero, gdy zapiszesz |
| `Plik › Otwórz folder autozapisu` | Pokazuje kopie w menedżerze plików |
| `Plik › Zapisz nową wersję` (`Ctrl+Alt+S`) | Zapisuje jako `Song_2`, `Song_3` i tak dalej, obok bieżącego pliku |

Sekcja `Kopia zapasowa` w `Opcje › Ustawienia ogólne...` ma te ustawienia:

| Ustawienie | Co robi |
|---|---|
| `Zapisuj kopię zapasową` | `Nigdy` albo odstęp w minutach |
| `Kopie zapasowe na projekt` | 1, 3, 5, 10, 20 lub 50 |
| `Wstrzymaj kopie zapasowe podczas odtwarzania utworu lub gry na klawiaturze MIDI` | Wstrzymuje kopię, żeby ci nie przeszkadzała |
| `Najdłuższa przerwa między kopiami` | Od 5 do 60 minut. Kopia powstaje po tym czasie nawet podczas gry |

Jeśli ostatnia sesja skończyła się nieoczekiwanie, przy następnym starcie program proponuje otwarcie najnowszej kopii zapisanej w tej sesji.

## Warto wiedzieć

- Historia nie jest zapisywana z projektem. Otwarcie projektu albo rozpoczęcie nowego ją opróżnia.
- Gdy historia jest pełna, najstarsze kroki są odrzucane.
- W sesji online na żywo cofanie i ponawianie przywracają tylko twoje własne zmiany, nigdy cudze.
- Przy `Zapisuj kopię zapasową` ustawionym na `Nigdy` kopie robione przy zapisie nad plikiem są nadal zachowywane.
- Folder ustawień to `%APPDATA%\LYNX` w Windows, `~/Library/Application Support/LYNX` w macOS i `~/.config/LYNX` w Linuksie.

## Powiązane

- [Skróty](/wiki/shortcuts/)
- [Ustawienia](/wiki/settings/)
- [Formaty plików](/wiki/file-formats/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)
- [Co robi tylko LYNX SHARE: bezpieczeństwo przed awarią](/why/#crash)
