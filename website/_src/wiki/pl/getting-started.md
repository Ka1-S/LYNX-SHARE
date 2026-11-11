---
source: a6d9063ec3
---
LYNX SHARE to darmowy DAW o otwartym kodzie dla Windows, macOS i Linuksa. Pracuje na patternach: programujesz je w Channel Rack i Piano Roll, aranżujesz w Playliście i miksujesz w Mixerze. Otwiera projekty FL Studio (`.flp`) i sety Ableton Live (`.als`), a zapisuje we własnym formacie `.lynx`.

To wersja alfa. Spodziewaj się błędów i zapisuj często.

## Uruchomienie

1. Sprawdź w tabeli poniżej, czy Twój system jest obsługiwany.
2. Pobierz i zainstaluj program. [Instalacja](/wiki/install/) podaje kroki dla każdego systemu.
3. Uruchom go. Krótki kreator konfiguracji wybiera język, testuje wyjście audio i wymienia foldery pluginów, które potem są skanowane w tle. Możesz go pominąć i otworzyć później przez `Opcje › Kreator konfiguracji (język, audio, foldery pluginów)...`.
4. Jeśli nic nie słychać, przejdź do [Ustawień audio](/wiki/audio-setup/).
5. Przeczytaj [Przegląd okna](/wiki/tour/), a potem zrób [Twój pierwszy utwór](/wiki/first-song/).

## Czego potrzebujesz

| System | Wymagania |
|---|---|
| Windows | Windows 10 lub 11, 64-bitowy |
| macOS | macOS 11 Big Sur lub nowszy, Intel lub Apple silicon |
| Linux | x86_64 z glibc 2.35 lub nowszym (Ubuntu 22.04+, Debian 12+, Fedora 36+, aktualny Arch lub Manjaro), X11 lub XWayland |

Nie potrzebujesz interfejsu audio. LYNX SHARE gra przez urządzenie dźwiękowe, którego Twój system już używa. FL Studio też nie jest potrzebne. Jeśli jest zainstalowane na tym samym komputerze, LYNX SHARE znajduje jego sample i używa tych samych plików pluginów dla projektów `.flp`.

Pluginy innych firm są opcjonalne. LYNX SHARE ma własne instrumenty i efekty, wymienione w [katalogu](/instruments-and-effects/).

## Zapisywanie pracy

`Ctrl+S` zapisuje, `Ctrl+Shift+S` zapisuje pod nową nazwą, a `Ctrl+Alt+S` zapisuje nową wersję (Song_2, Song_3 i tak dalej). Jest autozapis, dostępny przez `Plik › Otwórz folder autozapisu` i `Plik › Przywróć ostatnią kopię zapasową...`.

`Plik › Nowy z szablonu` ma cztery punkty startowe: Pusty, Basic beat, Trap with 808 i House.

## Projekt demo

Instalator i zip dla Windows, `.dmg` dla macOS i `.tar.gz` dla Linuksa zawierają projekt demo, `Demo.lynx`. Otwórz go przez `Plik › Otwórz...` (`Ctrl+O`). W Windows leży w folderze instalacji, obok `LYNX.exe`.

Możesz go też wygenerować z terminala. Zobacz [Wiersz poleceń](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Co dalej

| Chcesz | Przeczytaj |
|---|---|
| Poznać każde okno | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Otworzyć projekt z innego programu | [Przejście z FL Studio](/wiki/coming-from-fl-studio/), [Przejście z Ableton Live](/wiki/coming-from-ableton/) |
| Używać swoich pluginów VST, CLAP, LV2 lub AU | [Pluginy](/wiki/plugins/) |
| Sprawdzić klawisz | [Skróty](/wiki/shortcuts/) |
| Naprawić problem | [Rozwiązywanie problemów](/wiki/troubleshooting/) i [FAQ](/faq/) |

## Warto wiedzieć

- Plik programu nazywa się `LYNX.exe` w Windows i `LYNX` w macOS i Linuksie. Folder ustawień też nazywa się `LYNX`.
- W Windows i macOS pierwsze uruchomienie może pokazać ostrzeżenie systemowe, bo wersja dla Windows nie jest jeszcze podpisana, a wersja dla macOS nie jest notaryzowana. [Instalacja](/wiki/install/) wyjaśnia, w co kliknąć.
- Interfejs jest domyślnie po angielsku w systemie, którego języka LYNX SHARE nie ma. Możesz go zmienić w `Opcje › Ustawienia ogólne...`. Nowy język obowiązuje po restarcie.
- Paczki FLEX w projektach FL Studio są zaszyfrowane i nigdy nie są otwierane. Te kanały dostają patch syntezatora dobrany według nazwy presetu.

## Powiązane

- [Instalacja](/wiki/install/)
- [Ustawienia audio](/wiki/audio-setup/)
- [Przegląd okna](/wiki/tour/)
- [Twój pierwszy utwór](/wiki/first-song/)
- [Pobieranie](/download/)
