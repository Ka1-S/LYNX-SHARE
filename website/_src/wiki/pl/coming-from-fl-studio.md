---
source: 3bf7e0c0ab
---
LYNX SHARE zachowuje workflow, który znasz: Channel Rack, patterny, Playlist i Mixer, na tych samych klawiszach. Otwiera projekty `.flp` bezpośrednio i potrafi je zapisać z powrotem.

LYNX SHARE to niezależny program. Nie jest powiązany z Image-Line, przez nią popierany ani sponsorowany i nie zawiera kodu Image-Line. FL Studio jest zastrzeżonym znakiem towarowym Image-Line Software.

## Otwórz projekt .flp

1. Wybierz `Plik › Otwórz...` (`Ctrl+O`) i wskaż plik `.flp`.
2. LYNX SHARE znajduje pluginy i sample projektu, po czym go otwiera. Tytuł pokazuje "(zaimportowano z FL Studio)".
3. Wybierz `Pomoc › Last FL Studio import report`, żeby przeczytać, co zostało przybliżone, pominięte albo nie znalezione.
4. Wybierz `Pomoc › Brakujące pliki...`, żeby zobaczyć brakujące sample i pluginy.
5. Wybierz `Plik › Zapisz`. Program zapyta, gdzie zapisać plik `.lynx`. Sam `.flp` nigdy nie jest zapisywany.

## Co odpowiada czemu

| W FL Studio | W LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Te same nazwy, pod `F5`, `F6`, `F7` i `F9` |
| Wybór pluginu | `F8` |
| Browser | `Alt+F8` |
| Tryb Pattern / Song | `L` |
| Cofnij | `Ctrl+Z` cofa albo, po cofnięciu, ponawia. `Ctrl+Alt+Z` cofa wstecz |
| Zapisz nową wersję | `Ctrl+Alt+S` |
| Eksportuj | `Ctrl+R`, MP3 pod `Ctrl+Shift+R`, MIDI pod `Ctrl+Shift+M` |
| Inserty, sendy i sidechain w Mixerze | Tak samo, z 10 slotami efektów na insert |
| Fruity Wrapper | Pluginy zewnętrzne są hostowane bezpośrednio, zobacz [Pluginy zewnętrzne](/wiki/plugins/) |
| Natywne generatory i efekty | Grają na silnikach odtworzonych przez LYNX SHARE, zobacz niżej |
| Plik projektu `.flp` | Plik projektu `.lynx` |

`Pomoc › Skróty` wylicza każdy klawisz.

## Co jest importowane

- Kanały, sample, patterny, nuty slide i porta, każda aranżacja, znaczniki, zmiany metrum, automatyka i powiązania z kontrolerami.
- Mixer: routing, poziomy, sidechainy, EQ ścieżek i natywne efekty z zapisanymi ustawieniami.
- Pluginy zewnętrzne VST3, VST2 i CLAP wewnątrz Fruity Wrapper: zastępowane tym samym pluginem zainstalowanym na twoim komputerze, z jego zapisanym stanem.

## Natywne pluginy FL Studio

Własne generatory i efekty FL Studio nie są wczytywane. LYNX SHARE odtwarza je na silnikach, które sam odtworzył, a każdy ma poziom jakości:

| Poziom | Znaczenie |
|---|---|
| Exact | Identyczny co do bitu z oryginałem w każdym testowanym stanie |
| Near-exact | Identyczny co do bitu w większości stanów, reszta różni się poniżej około -100 dB |
| Close | Ten sam algorytm, w granicach -60 dB od oryginału w prawie wszystkich testowanych stanach |
| Approximate | Zapisane ustawienia są odwzorowane na wbudowany syntezator lub efekt LYNX SHARE |
| Placeholder | Stan jest zachowany, a plugin milczy albo przepuszcza audio bez zmian |

Poziom każdego silnika i to, czego jeszcze brakuje, jest w [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Zobacz też [Wbudowane instrumenty i efekty](/wiki/built-in/).

## Co wymaga twojej własnej instalacji FL Studio

Otwieranie `.flp` działa bez zainstalowanego FL Studio. Instalacja na tym samym komputerze dodaje dwie rzeczy:

- Fabryczne sample FL Studio i jego bazę pluginów, więc sample i pluginy są znajdowane bez skanowania.
- Wbudowane fale, sample, impulsy i tabele, których potrzebują niektóre silniki (FL Keys, Soundgoodizer, Maximus i pozostałe oznaczone krzyżykiem w PLUGINS.md). Są czytane z twojej instalacji w czasie działania i nie są dołączane do LYNX SHARE.

W Linuksie LYNX SHARE szuka FL Studio dla Windows pod Wine. W macOS szuka pakietu aplikacji, ale nie zostało to przetestowane.

## Eksport z powrotem do .flp

1. Wybierz `Plik › Eksportuj › Projekt FL Studio...`.
2. Wybierz `Projekt FL Studio (.flp)` albo `Spakowany pakiet (.zip, ze wszystkimi samplami)`.
3. Zdecyduj, co ma się stać z rzeczami, których FL Studio nie ma: wyrenderować je do audio (brzmi tak samo) albo użyć najbliższego pluginu FL (pozostaje edytowalny, może brzmieć inaczej, a instrumenty tylko z LYNX są pomijane).

Plik to projekt FL Studio 2026. Samplery, klipy audio, własne generatory i efekty FL, pluginy zewnętrzne (wewnątrz Fruity Wrapper), patterny, Playlist, Mixer i automatyka przechodzą z ustawieniami. Raport wylicza, co przeszło dokładnie, a co zostało wyrenderowane, zastąpione albo pominięte.

## FL Studio Mobile (.flm)

`Plik › Otwórz...` przyjmuje też projekty `.flm`. Import jest częściowy.

- Importowane: tempo, metrum, jeden insert miksera na kanał FLM, jedna ścieżka Playlisty na lane, patterny (każde przejście pętli jako klip), klipy audio z przesunięciem i wysokością, zestawy perkusyjne, DW Sampler i programy `.dwp` oraz Slicer (na własnych silnikach FLM), instrumenty MiniSynth, Transistor Bass, 3x Osc, GMSynth i SuperSaw, efekty FLM (jako efekty Pocket), automatyka poziomu, panoramy, mute, solo, pitch bendu, sendów i parametrów instrumentów oraz zestawy akordów i key locker.
- Nieimportowane: pozostałe generatory FLM, które grają wbudowanym syntezatorem.
- Sample są szukane obok pliku `.flm` i w kopii zasobów FLM w FL Studio. Nieznaleziony `.dwp` gra zastępczym dźwiękiem.

## Warto wiedzieć

- Paczki FLEX są zaszyfrowane i nigdy nie są otwierane. Kanał FLEX dostaje patch syntezatora dobrany po nazwie presetu.
- Modulatory są pomijane w eksporcie `.flp`, a raport eksportu o tym informuje.
- Raport importu wylicza, co przybliżył wbudowany syntezator, co nie miało odpowiednika, które pluginy znaleziono lub nie są zainstalowane, które efekty pominięto lub odtworzono częściowo i ile sampli brakuje.
- Niezainstalowany efekt zewnętrzny jest zastępowany efektem LYNX SHARE o tej samej roli. Oryginał zostaje w projekcie.

## Powiązane

- [Wbudowane instrumenty i efekty](/wiki/built-in/)
- [Pluginy zewnętrzne](/wiki/plugins/)
- [Formaty plików](/wiki/file-formats/)
- [Skróty](/wiki/shortcuts/)
- [Co robi tylko LYNX SHARE z ustawieniami FL](/why/#fl-settings)
