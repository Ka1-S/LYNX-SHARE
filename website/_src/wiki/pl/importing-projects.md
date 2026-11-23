---
source: 532b2eee04
---
`Plik › Otwórz...` (`Ctrl+O`) otwiera własne projekty `.lynx` LYNX SHARE, a także pliki `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx` i `.mid`. Projekt z innego programu jest importem: staje się nowym, niezapisanym projektem LYNX SHARE, a oryginalny plik nigdy nie jest zapisywany.

## Otwarcie projektu z innego programu

1. Wybierz `Plik › Otwórz...` i wskaż plik `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip` albo moduł trackerowy (`.mod`, `.xm`, `.s3m`, `.it`).
2. Poczekaj, aż LYNX SHARE poszuka pluginów i sampli projektu.
3. Jeśli czegoś nie znaleziono, otwiera się okno `Brakujące pliki`. Wskaż w nim swoje sample albo je zamknij.
4. Pasek tytułu pokazuje teraz, skąd pochodzi projekt, na przykład „(imported from FL Studio)".
5. Wybierz `Plik › Zapisz`. LYNX SHARE zapyta, gdzie zapisać plik `.lynx`.

## Co jest przenoszone

| Format | Konwertowane | Niekonwertowane |
|---|---|---|
| `.flp` (FL Studio) | Kanały, sample, patterny, nuty slide i porta, wszystkie aranżacje, znaczniki, zmiany metrum, automatyka, powiązania z kontrolerami, Mixer z routingiem, sidechainy, EQ ścieżek i efekty natywne | Paczki FLEX są zaszyfrowane i nigdy nie są otwierane. Kanał FLEX dostaje patch syntezatora dobrany według nazwy presetu |
| `.als` (Live 11 i 12) | Ścieżki MIDI, audio, grupowe i return, tempo, metrum, lokatory, pętla, głośność, panorama, wyciszenie, solo i wysyłki wraz z automatyką, klipy sesji i sceny, znaczniki warp, racki i Racki perkusyjne oraz wiele własnych urządzeń Live (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable i inne) jako własne urządzenia LYNX SHARE | Urządzenia Live bez odpowiednika w LYNX SHARE (Amp, Cabinet, Beat Repeat, Vocoder i inne) są pomijane i wymienione na liście |
| `.flm` (FL Studio Mobile) | Tempo, metrum, jeden insert miksera na kanał, jedna ścieżka Playlisty na tor, patterny, klipy audio z przesunięciem i wysokością, zestawy perkusyjne, DW Sampler i programy `.dwp`, Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth i SuperSaw, efekty, automatyka i zestawy akordów | Pozostałe generatory FLM grają wbudowanym syntezatorem |
| `.dawproject` (Bitwig Studio, Studio One, Cubase i inne programy) | Ścieżki, foldery, klipy, nuty, audio ze znacznikami warp, automatyka, tempo, znaczniki, mikser z wysyłkami, sceny w siatce klipów oraz pluginy VST3, VST2 i CLAP z ich stanem. EQ, kompresor, bramka i limiter z tego formatu stają się własnymi urządzeniami LYNX SHARE | Efekty nut, ekspresja pojedynczych nut i własne urządzenia innego programu są wymienione w raporcie importu |
| `.band` (GarageBand 10, GarageBand na iOS) | Ścieżki, regiony MIDI i audio, tempo, metrum, tonacja, sekcje jako znaczniki, poziom, panorama i wyciszenie, jako ścieżki Prostego układu. Instrumenty Sampler z GarageBand na iOS | Ścieżki instrumentów grają wbudowanym syntezatorem i perkusją. Regiony Drummer, foldery z podejściami i zmiany tempa są wymienione. Projekty z GarageBand 11 i starszych nie są czytelne |
| `.blx` (BandLab) | Ścieżki, regiony MIDI i audio z ich pętlami, tempo, metrum, tonacja, głośność, panorama i wyciszenie, automatyka głośności i panoramy oraz typowe efekty, jako ścieżki Prostego układu | Ścieżki instrumentów grają wbudowanym syntezatorem i perkusją. Inne efekty i preset Mastering są pomijane i wymienione. Testowane tylko na zmyślonych projektach |
| `.mid` | Nuty, metrum i każda zmiana tempa | Plik MIDI nie niesie dźwięków: kanały dostają syntezator LYNX SHARE albo kanał MIDI Out |
| `.mod`, `.xm`, `.s3m`, `.it` (moduły trackerowe) | Instrumenty jako kanały Samplera z ich samplami, patterny, lista kolejności jako Playlist, szybkość i tempo, głośność nut, panorama, opóźnienie i cięcie; zmiany głośności i panoramy jako zdarzenia kanału (MOD i XM) oraz slajdy, vibrato, arpeggia i obwiednie wysokości jako krzywe wysokości nut | Inne efekty i obwiednie instrumentów są liczone w raporcie importu. `Widok › Tracker` edytuje nuty na sposób trackera |
| `.zip` | Spakowany pakiet loopów FL Studio albo projekt wyeksportowany z samplami: rozpakowany do własnego folderu, po czym otwiera się projekt w środku wraz z samplami | |

Pluginy innych firm VST2, VST3 i CLAP w pliku `.flp` lub `.dawproject` oraz VST2 i VST3 w pliku `.als` wczytują się z zapisanym stanem, jeśli ten sam plugin jest zainstalowany na Twoim komputerze.

## Własne pluginy FL Studio

Generatory i efekty FL Studio nie są ładowane. LYNX SHARE czyta ich zapisane ustawienia i odtwarza je na silnikach, które odtworzył od nowa. Każdy silnik ma poziom jakości, od `Exact` (bit w bit jak oryginał w każdym przetestowanym stanie) po `Placeholder` (ustawienia są zachowane, ale plugin milczy albo przepuszcza dźwięk bez zmian). Poziomy opisuje strona [Przejście z FL Studio](/wiki/coming-from-fl-studio/).

Otwarcie pliku `.flp` działa bez zainstalowanego FL Studio. Niektóre silniki potrzebują fal, sampli lub impulsów, które są czytane z Twojej własnej instalacji FL Studio w czasie pracy.

Plik `.flp`, który używa „FL Studio Mobile Rack” z FL Studio (własne „Save as FLP” w FL Studio Mobile zapisuje jeden na ścieżkę instrumentu), gra swój instrument i efekty na tych samych silnikach co `.flm`, podobnie efekt „FL Studio Mobile Rack FX”. Rack z zestawem perkusyjnym lub kilkoma instrumentami nadal gra wbudowanym syntezatorem.

## Fabryczne brzmienia FL Studio Mobile

Fabryczne brzmienia używane przez `.flm` (zestawy perkusyjne, programy DirectWave, SoundFonty, fale GMSynth) należą do FL Studio Mobile i nigdy nie są dołączane do LYNX SHARE. LYNX SHARE szuka ich w twojej własnej kopii folderu `Resources` aplikacji: dodaj go w `Ustawienia › Pliki › Zasoby FL Studio Mobile` (wskazanie folderu nad nim albo folderu instalacji FL Studio też działa). Instalacja FL Studio zawiera tylko część z nich i jest przeszukiwana po twoich folderach. Pliki, których nadal brakuje, są wymienione w oknie Brakujące pliki.

## Brakujące sample

![Okno Brakujące pliki na karcie Sample: znalezione pliki na zielono, brakujące na bursztynowo, z przyciskami Przeszukaj folder, Znajdź i Pomiń](/assets/img/wiki/missing-files.webp)

Karta `Próbki` wymienia każdy sample, którego nie znaleziono, wraz z zapisaną ścieżką i tym, co go używa.

| Polecenie | Co robi |
|---|---|
| `Przeszukaj folder...` | Przeszukuje folder i jego podfoldery w poszukiwaniu wszystkich brakujących sampli |
| `Znajdź...` | Pyta, gdzie leży jeden sample. Pozostałe znalezione w tym samym folderze też są podchwytywane |
| `Pomiń` | Pomija sample. Jego kanał milczy |

`Zastosuj` używa znalezionych plików.

Karta `Znalezione automatycznie` wymienia pliki, które LYNX SHARE sam znalazł w innym folderze. Zapisz projekt, aby zachować nowe miejsca.

## Brakujące pluginy

LYNX SHARE szuka każdego pluginu innej firmy najpierw po identyfikatorze, potem po pliku, a na końcu po nazwie. Projekt zapisany z VST2 otwiera się z VST3 tego samego produktu i odwrotnie, ale wtedy z ustawieniami domyślnymi.

Gdy efekt nie jest zainstalowany, jego slot zajmuje efekt LYNX SHARE o tej samej roli: pogłos za pogłos, kompresor za kompresor.

- Slot pokazuje nazwę oryginalnego pluginu z plakietką LYNX SHARE.
- Tam, gdzie da się odczytać zapisany stan, ustawienia są przekładane. W przeciwnym razie zamiennik startuje z domyślnego presetu. Tabela produktów, ich zamienników i tego, co jest przekładane w każdym z nich, jest w [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- Automatyka parametrów oryginału zostaje zachowana, ale nie steruje zamiennikiem.
- Odwołanie do oryginalnego pluginu i jego stan zostają w projekcie. Po zainstalowaniu pluginu projekt znowu otwiera się z nim.
- `Omiń wszystkie zamienniki LYNX SHARE w tym projekcie` w menu prawego kliknięcia slotu zamiennika wyłącza je wszystkie naraz.

Przełącznik to `Zastępuj brakujące pluginy odpowiednikami LYNX SHARE` w `Opcje › Ustawienia ogólne...`, domyślnie włączony. Gdy jest wyłączony, slot brakującego efektu jest pomijany w mikserze. Brakujący instrument nie jest zastępowany. Staje się wbudowanym syntezatorem z presetem dobranym według nazwy kanału.

Aby odzyskać prawdziwy plugin, zainstaluj go, wybierz `Opcje › Skanuj pluginy`, a potem otwórz projekt jeszcze raz.

## Pliki MIDI

![Okno Importuj dane MIDI: ścieżki, kanały, typ kanału i opcje](/assets/img/wiki/midi-import-dialog.webp)

`Plik › Otwórz...` tworzy nowy projekt z pliku MIDI. `Plik › Importuj › Plik MIDI...` najpierw otwiera okno `Importuj dane MIDI`, w którym wybierasz, co wczytać.

| Ustawienie | Co robi |
|---|---|
| `Które ścieżki importować?` | Wszystkie ścieżki albo jedna |
| `Które kanały importować?` | Kliknij kanał MIDI, aby go pominąć. Prawy klik zostawia tylko ten jeden |
| `Typ kanału` | `Syntezator LYNX SHARE` lub `MIDI Out` |
| `Rozpocznij nowy projekt` | Zaczyna pusty projekt. Wyłączone: dodaje do otwartego |
| `Utwórz jeden kanał na ścieżkę` | Wyłączone: jeden kanał na kanał MIDI |
| `Ustaw ścieżki miksera dla nowych kanałów` | Kieruje nowe kanały na kolejne inserty miksera |
| `Importuj metrum` | Metrum pliku staje się metrum projektu |

`Ctrl+M` w [Piano Roll](/wiki/piano-roll/) importuje plik MIDI do bieżącego kanału.

## Import ścieżek z innego projektu

`Plik › Importuj › Ścieżki z innego projektu...` wprowadza ścieżki innego projektu do otwartego, bez jego zamykania. Czyta każdy format, który czyta `Plik › Otwórz...`.

1. Wybierz projekt. Okno wylicza jego ścieżki Playlisty i to, co każda zawiera.
2. Zaznacz ścieżki, które chcesz.
3. Wybierz, czy `Z insertami miksera i efektami` i `Z automatyką` mają przyjść razem, oraz `Umieść na` początek utworu lub początek zaznaczenia czasu.
4. Kliknij, aby zaimportować. Nowe ścieżki, kanały, patterny i inserty przychodzą jako jeden krok, więc jedno cofnięcie zabiera je wszystkie.

## Warto wiedzieć

- `Pomoc › Brakujące pliki...` otwiera to okno ponownie później. `Pomoc › Ostatni raport importu projektu` wymienia, co przybliżono, pominięto lub co nie jest zainstalowane.
- Klipy sesji z setu Live trafiają do siatki klipów w układzie Clip-launch. `Klipy sesji z zestawów Live` w `Opcje › Ustawienia ogólne...` umieszcza je zamiast tego na Playliście.

## Powiązane

- [Formaty plików](/wiki/file-formats/)
- [Przejście z FL Studio](/wiki/coming-from-fl-studio/)
- [Przejście z Ableton Live](/wiki/coming-from-ableton/)
- [Pluginy](/wiki/plugins/)
- [Eksport](/wiki/exporting/)
