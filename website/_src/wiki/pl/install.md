---
source: 8a120ab7de
---
Każde wydanie ma wersje dla Windows, macOS i Linuksa na [stronie wydań](https://github.com/Ka1-S/LYNX-SHARE/releases). Pliki są budowane z otagowanego kodu źródłowego przez GitHub Actions i wymienione z sumami SHA-256 w `SHA256SUMS.txt`. Strona [Pobieranie](/download/) odsyła do bieżących.

## Instalacja w Windows

Windows 10 lub 11, 64-bitowy.

1. Pobierz `LYNX-<version>-setup-x64.exe`.
2. Uruchom go. Instaluje się tylko dla Twojego użytkownika i nie wymaga praw administratora.
3. SmartScreen może ostrzec, bo plik wykonywalny nie jest jeszcze podpisany. Wybierz `More info`, a potem `Run anyway`.
4. Uruchom LYNX SHARE z menu Start albo ze skrótu na pulpicie.

Instalator czyni LYNX SHARE programem otwierającym projekty `.lynx`. Dla plików `.flp` oferuje LYNX SHARE w *Otwórz za pomocą*, nie zmieniając domyślnego programu FL Studio.

Są dwie inne drogi:

| Droga | Plik lub miejsce | Co robi |
|---|---|---|
| Przenośny zip | `LYNX-<version>-windows-x64.zip` | Rozpakuj go gdziekolwiek i uruchom `LYNX.exe`. Nic się nie instaluje. |
| Microsoft Store | [LYNX SHARE w Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Store instaluje program i dba o aktualizacje. |

## Instalacja w macOS

macOS 11 Big Sur lub nowszy. Jeden uniwersalny plik `LYNX-<version>-macos-universal.dmg` obejmuje Intela i Apple silicon.

1. Otwórz `.dmg` i przeciągnij LYNX SHARE do *Programy*.
2. Aplikacja nie jest notaryzowana przez Apple i nie ma podpisu Developer ID, więc pierwsze uruchomienie wymaga dodatkowego kroku. Kliknij LYNX SHARE w *Programy* prawym przyciskiem (lub z Control), wybierz *Otwórz*, a potem jeszcze raz *Otwórz*.
3. W macOS 15 Sequoia lub nowszym spróbuj raz ją otworzyć, potem przejdź do *Ustawienia systemowe › Prywatność i ochrona* i kliknij *Otwórz mimo to*.

Wariant z Terminalem:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS prosi o dostęp do mikrofonu przy pierwszym nagrywaniu.

## Instalacja w Linuksie

x86_64 z glibc 2.35 lub nowszym (Ubuntu 22.04+, Debian 12+, Fedora 36+, aktualny Arch lub Manjaro). LYNX SHARE wymaga X11 lub XWayland. Dźwięk idzie przez PipeWire, JACK lub ALSA i nie trzeba w tym celu instalować niczego więcej.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Niektóre dystrybucje potrzebują libfuse2 do uruchamiania AppImage (`libfuse2` w Ubuntu 22.04, `libfuse2t64` w 24.04, `fuse2` w Archu, `fuse-libs` w Fedorze). Przy pierwszym starcie kreator konfiguracji proponuje dodanie LYNX SHARE do menu aplikacji i otwieranie nim plików `.lynx`. Możesz to zmienić później w `Opcje › Ustawienia ogólne...` albo z terminala przez `./LYNX-*.AppImage --menu-entry on` (lub `wył.`).

Jeśli wolisz nie instalować libfuse2, użyj `LYNX-<version>-linux-x86_64.tar.gz`: rozpakuj go i uruchom `./install.sh`. To instaluje LYNX SHARE dla Twojego użytkownika w `~/.local` z wpisem w menu, ikoną i typem pliku `.lynx`.

## Gdzie leżą ustawienia

| System | Folder ustawień |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (lub w `$XDG_CONFIG_HOME`) |

Folder zawiera Twoje opcje, wybrany język (`language.txt`), skróty (`keys.json`), foldery pluginów (`plugin-folders.txt`) i listę przeskanowanych pluginów (`plugins.xml`).

## Aktualizacja

Przy starcie LYNX SHARE pyta GitHub o numer najnowszej wersji. Nic o Tobie nie jest wysyłane. Gdy wyjdzie nowa wersja, pasek pokazuje `LYNX SHARE <version> is available` z przyciskiem `Co nowego` i przyciskiem aktualizacji. `Pomoc › Sprawdź aktualizacje...` pyta o to w dowolnej chwili.

| Wersja | Jak się aktualizuje |
|---|---|
| Instalator Windows | `Aktualizuj` pobiera nowy instalator, pyta o niezapisane zmiany, instaluje i uruchamia ponownie |
| Przenośny zip Windows, macOS | `Aktualizuj` pobiera nowy plik i pokazuje go w jego folderze |
| AppImage na Linuksie | `Aktualizuj` aktualizuje plik AppImage na miejscu. Gear Lever i AppImageUpdate potrafią to samo z zewnątrz |
| Linux `.tar.gz` | `Pobierz...` otwiera stronę pobierania |
| Microsoft Store | Aktualizuje Store. Ta wersja nie ma własnego sprawdzania aktualizacji |

Aby wyłączyć sprawdzanie, otwórz `Opcje › Ustawienia ogólne...` i odznacz `Sprawdzaj aktualizacje automatycznie`.

## Usuwanie

- Instalator Windows: odinstaluj LYNX SHARE z listy *Aplikacje i funkcje* w Windows.
- Przenośny zip: usuń rozpakowany folder.
- Linux `.tar.gz`: uruchom `./install.sh --uninstall`.

## Warto wiedzieć

- Wersje dla Windows są niepodpisane, dopóki wniosek projektu o podpis kodu nie zostanie przyjęty. Aplikacja macOS ma tylko podpis ad hoc. Pakiety linuksowe nie są podpisane.
- Aby sprawdzić pobrany plik, porównaj jego SHA-256 z wierszem w `SHA256SUMS.txt` na stronie wydania.
- Instalator, zip, `.dmg` i `.tar.gz` zawierają projekt demo `Demo.lynx`.
- Zamknięcie paska aktualizacji jego przyciskiem zamknięcia pomija tę wersję.

## Powiązane

- [Zacznij tutaj](/wiki/getting-started/)
- [Ustawienia audio](/wiki/audio-setup/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)
- [Pobieranie](/download/)
- [Aktualizacje](/updates/)
