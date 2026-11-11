---
source: b96330a448
---
![Edytor automatyzacji z krzywą rosnącą przez sześć taktów](/assets/img/wiki/automation.webp)

Klip automatyki to krzywa na [Playliście](/wiki/playlist/), która porusza jednym elementem sterującym podczas odtwarzania utworu: głośnością kanału, suwakiem miksera, parametrem pluginu, tempem. Klip ma własne okno edycji, edytor automatyzacji.

Są trzy inne sposoby poruszania elementem sterującym. Zdarzenia to wartości zapisane wewnątrz patternu. Powiązanie sprawia, że gałka MIDI albo kontroler wewnętrzny steruje elementem. Modulator kołysze gałką wokół jej własnej wartości.

## Automatyzuj gałkę

1. Przesuń kursor odtwarzania tam, gdzie automatyka ma się zacząć, albo zaznacz zakres czasu w Playliście.
2. Kliknij gałkę lub suwak prawym przyciskiem i wybierz `Utwórz klip automatyki`.
3. Klip ląduje na pierwszej wolnej ścieżce Playlisty i otwiera się edytor automatyzacji. Krzywa zaczyna się jako płaska linia na bieżącej wartości elementu.
4. Kliknij w edytorze, żeby dodać punkt, i przeciągnij go na miejsce.
5. Przytrzymaj `Alt` i przeciągnij odcinek w górę lub w dół, żeby go wygiąć.
6. Odtwórz utwór w trybie Song. Element podąża za krzywą.

Inne sposoby na ten sam klip:

- `Dodaj › Automatyka ostatnio zmienionego parametru` tworzy klip dla elementu, którym ruszałeś ostatnio.
- Wskaźnik ostatnio zmienianego elementu oraz `Narzędzia › Ostatnio zmieniany` otwierają to samo menu co prawy klik na elemencie.
- Dla pluginu zewnętrznego otwórz menu kanału w Channel Racku albo menu slotu efektu w Mixerze i użyj `Automatyzuj parametr pluginu`. Obok jest `Automatyzuj kontroler MIDI (CC, aftertouch, pitch bend)`.

Żeby znów otworzyć edytor, kliknij klip dwukrotnie albo kliknij go prawym przyciskiem i wybierz `Edytuj punkty...`. To samo robi `Edytuj klip automatyki...` w menu samego elementu.

## Edytor automatyzacji

| Polecenie | Klawisz | Co robi |
|---|---|---|
| Nowy punkt | Klik lub prawy klik, potem przeciągnięcie | Dodaje punkt i przesuwa go, dopóki trzymasz przycisk |
| Przesunięcie punktu | Przeciągnięcie | `Shift` zachowuje jego czas, `Ctrl` zachowuje jego wartość, `Alt` pomija przyciąganie |
| Wygięcie odcinka | `Alt`+przeciągnięcie w górę lub w dół | Zmienia napięcie odcinka pod myszą |
| Uchwyt napięcia | Przeciągnięcie w górę lub w dół | Wygina odcinek. `Ctrl` działa precyzyjniej, prawy klik prostuje |
| Punkt na krzywej | `Shift`+prawy klik | Dodaje punkt na linii |
| Menu punktu | Prawy klik na punkcie | `Wpisz wartość...`, `Kopiuj wartość`, `Wklej wartość`, `Usuń punkt` oraz `Cała krzywa` do skalowania, odwracania lub prostowania całości |
| Zoom | `Ctrl`+kółko | Zmienia powiększenie widoku |
| Przewijanie | Przeciągnięcie środkowym przyciskiem | Przesuwa widok |

## Tryb LFO

![Edytor automatyzacji w trybie LFO, z falą trójkątną i pokrętłami SPD, TNS, SKEW, PW i AMNT](/assets/img/wiki/automation-lfo.webp)

Zaznacz `LFO` na górze edytora. Wartość klipu kształtuje teraz przebieg fali, a punkty, które rysujesz, ustalają amplitudę LFO zamiast samej wartości.

| Ustawienie | Co robi |
|---|---|
| `SPD` | Długość jednego cyklu, w miarach |
| `TNS` | Napięcie fali |
| `SKEW` | Skos fali |
| `PW` | Szerokość impulsu |
| `AMNT` | Jak daleko LFO porusza wartość |
| `MUL` | Włączone: LFO jest mnożone przez poziom obwiedni. Wyłączone: jest dodawane |

## Zdarzenia w patternie

![Edytor zdarzeń z zdarzeniami głośności narysowanymi jako słupki](/assets/img/wiki/eventeditor.webp)

Zdarzenia należą do patternu i grają wszędzie tam, gdzie gra ten pattern. Kliknij element prawym przyciskiem i wybierz `Edytuj zdarzenia w patternie`, żeby otworzyć dla niego edytor zdarzeń.

- `Rysuj` ustawia wartości podczas przeciągania, a przeciągnięcie prawym przyciskiem kasuje. `Linia` rysuje prostą rampę. `Zaznacz` zaznacza zakres czasu dla narzędzi na dole.
- `Przyciąganie` sięga od 1/2 kroku do 1 taktu.
- `Skala`, `Przesunięcie` i `Losuj` zmieniają zdarzenia zaznaczenia albo całego patternu.
- `LFO` zapisuje sinus, trójkąt lub prostokąt na długość od 1 miary do 4 taktów.
- `Wyczyść` usuwa zdarzenia.
- `Na automatykę...` zamienia zdarzenia w klip automatyki i usuwa je z patternu.
- `Plik` otwiera i zapisuje zdarzenia jako plik `.fsc` oraz importuje plik MIDI.

`Inicjuj utwór tą pozycją` w tym samym menu prawego przycisku zapisuje wartość, do której element wraca przy każdym starcie utworu.

## Nagrywanie twoich ruchów

Włącz nagrywanie z zaznaczonym `Automatyka` w filtrze nagrywania, naciśnij odtwarzanie i poruszaj elementem. W trybie Song ruchy stają się klipem automatyki. W trybie Pattern stają się zdarzeniami w bieżącym patternie. Zobacz [Nagrywanie](/wiki/recording/).

## Powiązania i modulatory

- `Powiąż z kontrolerem MIDI (porusz gałką na kontrolerze)` opisujemy w [Kontrolerach MIDI](/wiki/midi-controllers/).
- `Powiąż z kontrolerem wewnętrznym` pozwala, żeby elementem sterowała Obwiednia, Formula albo Peak Controller z projektu. Najpierw dodaj jeden z nich.
- `Moduluj` podpina modulator, czyli LFO albo losowe źródło, które porusza gałką LYNX SHARE wokół jej własnej wartości. `Narzędzia › Modulatory...` je wylicza.

## Ścieżki automatyzacji

Ścieżki są domyślnie wyłączone. Włącz `Ścieżki automatyzacji` w menu Playlisty. Nagłówek każdej ścieżki dostaje wtedy przycisk, który dodaje ścieżkę automatyzacji pod nią, a menu elementu zyskuje `Automatyzuj na ścieżce`. Na ścieżce kliknij, żeby dodać punkt, lub przeciągnij go, kliknij prawym przyciskiem, żeby go usunąć, a `Alt`+przeciągnięcie wygina linię. Menu ścieżki ma `Na klip automatyzacji`.

## Warto wiedzieć

- Nowy klip zaczyna się na zaznaczeniu czasu, jeśli jest, a w przeciwnym razie na kursorze odtwarzania w trybie Song. Wyłącz `Twórz klipy automatyzacji w pozycji odtwarzania` w `Opcje › Ustawienia ogólne...`, żeby zaczynać od początku utworu.
- Kilka klipów na jednym elemencie gra każdy w swoim miejscu. Przed pierwszym klipem element zachowuje własną wartość.
- Na początku klipu element osiąga nową wartość w około 8 ms. FL Studio dojeżdża tam płynnie, więc zaimportowany projekt może w tych punktach nieco się różnić.
- Tam, gdzie klipy na tym samym elemencie się nakładają, klip wybrany przez LYNX SHARE może być inny niż wybrany przez FL Studio.
- Modulatory działają tylko na własnych gałkach LYNX SHARE i są pomijane przy eksporcie do `.flp`.

## Powiązane

- [Playlist](/wiki/playlist/)
- [Nagrywanie](/wiki/recording/)
- [Kontrolery MIDI](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Skróty](/wiki/shortcuts/)
