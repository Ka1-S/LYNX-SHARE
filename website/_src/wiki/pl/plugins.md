---
source: be0fc22373
---
![Dolna część Menedżera pluginów z uwagą, że każdy plik pluginu jest badany w osobnym procesie](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE hostuje pluginy instrumentów i efektów innych producentów. Znajduje je, skanując foldery pluginów, a `Opcje › Menedżer pluginów...` pokazuje, co znalazł. Własne instrumenty i efekty LYNX SHARE opisuje strona [Wbudowane instrumenty i efekty](/wiki/built-in/).

## Skanowanie i dodanie pluginu

1. Wybierz `Opcje › Skanuj pluginy`. Postęp widać na pasku podpowiedzi i możesz dalej pracować.
2. Jeśli plugin leży we własnym folderze, otwórz `Opcje › Menedżer pluginów...`, kliknij `Foldery pluginów...`, dodaj folder, a potem kliknij `Znajdź nowe pluginy`.
3. Naciśnij `F8`, aby otworzyć wybór pluginu, i wpisz fragment nazwy pluginu.
4. Kliknij go dwukrotnie albo naciśnij `Enter`. Instrument staje się nowym kanałem. Efekt trafia do zaznaczonego inserta Mixera.

Przy pierwszym starcie kreator konfiguracji wymienia foldery pluginów i skanuje je w tle. `Opcje › Kreator konfiguracji (język, audio, foldery pluginów)...` otwiera go ponownie. LYNX SHARE szuka też nowych pluginów kilka sekund po każdym uruchomieniu.

## Formaty w poszczególnych systemach

| System | Hostowane formaty |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2 nie jest hostowany w macOS. W Linuksie pluginy z Windows działają przez [yabridge](https://github.com/robbert-vdh/yabridge) i Wine, które instalujesz sam: dodaj folder, do którego synchronizuje yabridge, jako folder pluginów.

## Menedżer pluginów

| Element | Co robi |
|---|---|
| Pole wyszukiwania | Filtruje według nazwy, producenta, formatu lub pliku |
| `Wszystkie`, `Instrumenty`, `Efekty`, `Ulubione`, `Nieudane` | Wybierają, które pluginy są wymienione |
| Kolumna z gwiazdką | Oznacza ulubiony |
| Kolumna `Bity` | 32, 64 lub 32+64 (Windows) |
| Kolumna `Stan` | `OK`, `Brak pliku` lub `Błąd wczytania` |
| `Znajdź nowe pluginy` | Skanuje w poszukiwaniu plików, których nie ma jeszcze na liście |
| `Skanuj od nowa...` | Zapomina pliki z błędami i bada każdy plik od nowa |
| `Foldery pluginów...` | Edytuje skanowane foldery |
| `Dodaj plik pluginu...` | Dodaje jeden plik pluginu |

Każdy plik pluginu jest badany w osobnym procesie. Plik, który się wywala albo zawiesza na 30 sekund, jest oznaczany jako błędny i później pomijany. W Windows pliki z błędami są wypisane z przyczyną w `%APPDATA%\LYNX\scan-failures.log`.

Skanowane są foldery standardowe oraz te, które dodasz:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` i `Steinberg\VSTPlugins` w Program Files oraz foldery, których używa FL Studio, jeśli jest zainstalowane.
- macOS: `VST3`, `Components` i `CLAP` w `~/Library/Audio/Plug-Ins` i `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` oraz te same foldery w `/usr/lib` i `/usr/local/lib`.

## Pluginy zadokowane i odłączone

Edytor pluginu otwiera się w głównym oknie, tak jak inne panele. `Odłączony` w menu okna pluginu daje temu pluginowi własne okno, a `Przypnij na wierzchu` w tym samym menu trzyma odłączone okno nad innymi oknami LYNX SHARE.

`Odłączaj wszystkie nowe wtyczki` w `Opcje › Ustawienia ogólne...` otwiera każdy dodany plugin jako odłączony. `Narzędzia › Przełącz wszystkie edytory pluginów na` ustawia `Zadokowany` lub `Odłączony` dla wszystkich pluginów projektu.

## Uruchamianie pluginów w osobnym procesie

Domyślnie plugin działa wewnątrz LYNX SHARE, więc plugin, który się wywali, może zabrać ze sobą program. Opcjonalnie każdy plugin działa we własnym procesie.

1. Otwórz `Opcje › Ustawienia ogólne...`.
2. Włącz `Uruchamiaj pluginy w osobnym procesie (chroni LYNX SHARE przed awariami)`.
3. Otwórz projekt ponownie, aby zastosować to do pluginów, które są już otwarte.

Jeśli taki plugin się wywali albo przestanie odpowiadać, zostaje wyciszony, pojawia się komunikat, a LYNX SHARE działa dalej. `Odśwież` w tym komunikacie uruchamia plugin ponownie z ustawieniami z ostatniego zapisu lub kopii zapasowej, bez ponownego otwierania projektu. Edytor pluginu otwiera się w osobnym oknie i nie można go zadokować ani przypiąć.

Aby zrobić to tylko dla jednego pluginu, użyj `Uruchamiaj w osobnym procesie` albo `Always run <plugin> in a separate process` w menu wrappera w oknie pluginu. Gdy edytor pluginu się wywali, komunikat, który się pojawi, oferuje także to drugie.

Działa to w Windows i Linuksie, a od wersji 0.1.25 także w macOS.

Plugin, który się wywali wewnątrz LYNX SHARE, zostaje wyłączony, a jego komunikat też ma `Odśwież`. Później użyj `Odśwież plugin po awarii` w menu kanału lub w menu slotu efektu.

## Pluginy 32-bitowe (tylko Windows)

W Windows 32-bitowe pluginy VST2 i VST3 działają przez most (`LYNX-bridge32.exe`). Most nie dodaje latencji. Plugin, który się w nim wywali, zatrzymuje się sam, a LYNX SHARE gra dalej. Edytor pluginu przez most otwiera się w osobnym oknie i nie można go zadokować ani przypiąć. Gdy zainstalowane są i 32-bitowa, i 64-bitowa wersja pluginu, `Rozmiar bitowy` w `Ustawienia wrappera` w oknie pluginu wybiera, która się wczyta.

## Monitor wydajności pluginów

![Monitor wydajności pluginów: jeden wiersz na instrument i efekt z jego udziałem w czasie przetwarzania](/assets/img/wiki/plugin-monitor.webp)

`Narzędzia › Monitor wydajności pluginów...` wymienia każdy instrument i efekt z czasem przetwarzania, który zajmuje: teraz, średnio i w szczycie. 100% oznacza jeden rdzeń procesora zajęty przez cały czas trwania audio.

| Ustawienie | Co robi |
|---|---|
| `Znajdź` | Filtruje według pluginu, kanału lub inserta |
| `Tylko pluginy` | Ukrywa własne instrumenty i efekty LYNX SHARE |
| `Zamroź` | Przestaje odświeżać liczby |
| `Reset przy odtwarzaniu` | Zaczyna średnie i szczyty od nowa przy każdym starcie odtwarzania |
| `Resetuj` | Czyści średnie i szczyty od razu |

## Wyślij wyjścia instrumentu na osobne inserty

Plugin instrumentu z kilkoma wyjściami może grać każde z nich na własnym insercie Mixera. W oknie pluginu kliknij `Ustawienia wrappera`, a potem kartę `Przetwarzanie`. W `Połączenia` każde wyjście ma lampkę, która je włącza lub wyłącza, oraz numer: na którym insercie po własnym insercie kanału gra. `Automatycznie przypisz wyjścia` daje każdemu wyjściu następny insert, a `Resetuj` wraca do ustawień domyślnych.

## Folder Plugin database

Foldery Plugin database w [Browserze](/wiki/browser/), wybór pluginu i menu pluginów to folder na dysku: `LYNX/Plugin database` w folderze Dokumenty. Aby go otworzyć, kliknij prawym przyciskiem `Baza pluginów` w Browserze i wybierz `Otwórz`.

- `Efekty` i `Generatory` zawierają po jednym pliku `.lynxplugin` na plugin. Twórz foldery i podfoldery (do 4 poziomów) i przenoś do nich pliki.
- Zmień nazwę pliku, żeby zmienić nazwę pluginu w menu. Usuń plik, żeby wyjąć plugin z menu. Plugin nie zostaje odinstalowany.
- `Zainstalowane` ma wpis dla każdego pluginu. Skopiuj wpis do `Efekty` lub `Generatory`, żeby przywrócić plugin.

## Otwórz projekt bez jego pluginów

`Plik › Otwórz bez pluginów innych firm...` otwiera projekt LYNX SHARE z wyłączonymi wszystkimi pluginami innych producentów, dla projektu, który się nie otwiera albo wywala podczas wczytywania. Zapisane ustawienia pluginów zostają. `Plik › Włącz ponownie pluginy...` włącza je ponownie.

## Gdy plugin nie jest zainstalowany

LYNX SHARE najpierw szuka tego samego pluginu po identyfikatorze, potem po pliku, a na końcu po nazwie. Projekt zapisany z VST2 otwiera się z VST3 tego samego produktu i odwrotnie, ale wtedy plugin gra z ustawieniami domyślnymi.

Jeśli efekt w ogóle nie jest zainstalowany, jego slot zajmuje efekt LYNX SHARE o tej samej roli, a oryginał zostaje w projekcie. Tam, gdzie da się odczytać zapisane ustawienia pluginu, zastępnik startuje z pasującymi ustawieniami. Przełącznik to `Zastępuj brakujące pluginy odpowiednikami LYNX SHARE` w `Opcje › Ustawienia ogólne...`. Zobacz [Wbudowane instrumenty i efekty](/wiki/built-in/). `Pomoc › Brakujące pliki...` wymienia pluginy, które zastąpiono.

## Warto wiedzieć

- Okno hostowanego pluginu ma pasek presetów: własne programy pluginu, Twoje presety, `Zapisz preset...`, `Wczytaj plik presetu...` i `Eksportuj plik presetu...`. Pliki `.fxp`, `.fxb` i `.vstpreset` wczytują się tylko do pluginu, który działa wewnątrz LYNX SHARE, a nie w osobnym procesie.
- Monitor mierzy tylko wtedy, gdy jego okno jest otwarte.
- Gdy okno pluginu ma fokus, klawisz, którego plugin nie używa, gra na klawiaturze pianina. W Windows, przy wyłączonym przycisku klawiatury w nagłówku okna, LYNX SHARE obsługuje klawisze przed pluginem, razem ze skrótami.
- Własne menu parametru pluginu VST3 zaczyna się od pozycji LYNX SHARE, takich jak `Utwórz klip automatyki`. Zobacz [Automatyka](/wiki/automation/).
- `MPE` w sekcji `MIDI` w `Ustawienia wrappera` pluginu wysyła każdą nutę na jej własnym kanale MIDI. Zobacz [Kontrolery MIDI](/wiki/midi-controllers/).
- Brakujące instrumenty nie są jeszcze zastępowane. Brakujący plugin instrumentu staje się wbudowanym syntezatorem dobranym według nazwy kanału.

## Powiązane

- [Wbudowane instrumenty i efekty](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)
