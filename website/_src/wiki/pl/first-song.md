---
source: de4ead4a7a
---
Ta strona przechodzi raz przez całą drogę: pattern perkusyjny, melodia, aranżacja, miks i wyeksportowany plik. Każda część odsyła do strony, która opisuje dane okno w całości. Jeśli w którymkolwiek momencie nic nie słychać, zajrzyj do [Ustawień audio](/wiki/audio-setup/).

## Zrób pattern perkusyjny

1. Wybierz `Plik › Nowy z szablonu › Pusty`. Projekt startuje z pięcioma kanałami: Kick, Clap, Hat, Snare i Synth, i bez żadnych kroków.
2. Naciśnij `F6`, jeśli [Channel Rack](/wiki/channel-rack/) nie jest widoczny.
3. Kliknij kilka kroków w wierszu Kick. Przeciąganie rysuje kilka naraz, a prawy klik kasuje.
4. Naciśnij `Space`. Transport jest w trybie Pattern, więc pattern gra w pętli. Naciśnij `Space` ponownie, aby zatrzymać.
5. Wprowadź własne kroki w wierszach Clap, Hat i Snare.
6. Naciśnij `F2`, aby nadać patternowi nazwę i kolor.

Aby dodać brakujący dźwięk, kliknij pole `+ dodaj kanał` pod ostatnim wierszem i wybierz instrument z menu. Aby użyć własnego sampla, otwórz [Browser](/wiki/browser/) skrótem `Alt+F8`, kliknij plik audio, aby go posłuchać, i kliknij go dwukrotnie, aby dodać go jako nowy kanał.

## Napisz melodię

1. Naciśnij `F4`. LYNX SHARE przechodzi do następnego pustego patternu i pyta o jego nazwę.
2. Kliknij prawym przyciskiem nazwę kanału Synth i wybierz `Piano Roll`.
3. Ołówkiem (`P`) kliknij w siatce, aby dodać nutę.
4. Przeciągnij nutę, aby ją przesunąć, i przeciągnij jej koniec, aby zmienić długość. Prawy klik usuwa nutę.
5. Naciśnij `Space`, aby słyszeć pattern podczas edycji.

`Ctrl+T` zamienia klawiaturę do pisania w pianino, więc możesz najpierw wypróbować nuty na `Z` do `M` i `Q` do `P`. Wyłącz to znowu, zanim użyjesz skrótów literowych takich jak `P`.

## Zaaranżuj patterny

1. Naciśnij `F5`, aby pokazać [Playlist](/wiki/playlist/).
2. W panelu wyboru po lewej stronie Playlisty kliknij pattern perkusyjny. Jest teraz źródłem klipów.
3. Ołówkiem (`P`) kliknij na ścieżce, aby umieścić pattern. Kliknij dalej w prawo, aby umieścić go jeszcze raz, albo weź Pędzel (`B`) i przeciągnij, aby malować klipy jeden za drugim.
4. Kliknij pattern z melodią w panelu wyboru i umieść go na innej ścieżce.
5. Naciśnij `L`, aby przełączyć na tryb Song, a potem `Space`. Playlist gra od pozycji utworu. Kliknij linijkę, aby ją ustawić.

## Zmiksuj i dodaj efekt

1. Naciśnij `F9`, aby pokazać [Mixer](/wiki/mixer/). Pięć początkowych kanałów już gra przez inserty od 1 do 5, a kanał dodany z menu `+ dodaj kanał` zajmuje wolny insert. Numer w polu routingu kanału w Channel Rack mówi, który.
2. Aby przenieść kanały na własne wolne inserty, zaznacz je w Channel Rack i naciśnij `Ctrl+L`. Każdy insert przejmuje nazwę i kolor kanału.
3. Przeciągnij suwak inserta, aby ustawić poziom.
4. Kliknij insert, aby go zaznaczyć. Panel efektów po prawej pokazuje jego dziesięć slotów.
5. Kliknij pusty slot i wybierz efekt. Kliknij nazwę slotu, aby pokazać lub ukryć okno efektu. Lampka slotu omija efekt.

## Wyeksportuj utwór

![Okno eksportu: zakres i ogon po lewej, format, głębia bitowa i próbkowanie po prawej, opcje miksu poniżej](/assets/img/wiki/export.webp)

1. Naciśnij `Ctrl+S` i najpierw zapisz projekt.
2. Wybierz `Plik › Eksportuj › Plik WAV...`. Dla innych formatów to samo menu ma `Plik MP3...`, `Plik OGG...` i `Plik FLAC...`.
3. Ustaw `Zakres` na `Utwór (Playlist)`. Pozostałe możliwości to `Bieżący pattern` i `Zaznaczenie w Playliście`.
4. Pozostałe opcje zostaw bez zmian.
5. Kliknij `Eksportuj...`, wybierz nazwę pliku i poczekaj na render.

## Do czego służył każdy klawisz

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Channel Rack | `F6` | Pokazuje lub ukrywa Channel Rack |
| Następny pusty pattern | `F4` | Przechodzi do następnego pustego patternu i nadaje mu nazwę |
| Piano Roll | `F7` | Pokazuje lub ukrywa Piano Roll |
| Playlist | `F5` | Pokazuje lub ukrywa Playlist |
| Tryb Pattern lub Song | `L` | Przełącza to, co gra `Space` |
| Mixer | `F9` | Pokazuje lub ukrywa Mixer |
| Wyślij na wolne inserty | `Ctrl+L` | Daje zaznaczonym kanałom własne inserty Mixera |
| Zapisz | `Ctrl+S` | Zapisuje projekt jako `.lynx` |
| Eksport | `Ctrl+R` | Otwiera okno eksportu z ostatnio użytym formatem |

## Warto wiedzieć

- W trybie Pattern słychać tylko bieżący pattern. Jeśli po aranżacji utwór milczy, sprawdź, czy transport jest w trybie Song.
- Okno eksportu startuje na `Utwór (Playlist)`, gdy transport jest w trybie Song, a Playlist ma klipy. W przeciwnym razie startuje na `Bieżący pattern`.
- Klik metronomu nigdy nie trafia do eksportu.
- Dół okna eksportu pokazuje długość utworu w taktach, jego całkowity czas i miejsce na dysku, którego potrzebuje plik.
- Domyślnie nowy projekt ma Limiter na wstawce Master. To część tego, co słyszysz i co eksportujesz.
- Cofanie to `Ctrl+Z`. Naciśnięte ponownie ponawia ten krok. `Ctrl+Alt+Z` cofa dalej.

## Powiązane

- [Przegląd okna](/wiki/tour/)
- [Eksport](/wiki/exporting/)
- [Automatyka](/wiki/automation/)
- [Nagrywanie](/wiki/recording/)
- [Instrumenty i efekty](/instruments-and-effects/)
