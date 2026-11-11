---
source: edc71061e6
---
LYNX SHARE jest napisany w C++20 z JUCE 8 i budowany przez CMake. Źródła są na [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) na licencji GNU Affero General Public License v3.0. Pliki wydań są budowane z oznaczonych źródeł przez GitHub Actions, więc ta strona jest potrzebna tylko wtedy, gdy chcesz zmienić kod albo zbudować program dla siebie.

## Budowanie w Linuksie

1. Zainstaluj zależności wymienione poniżej.
2. Pobierz źródła przez `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` i wejdź do folderu `lynx`.
3. Skonfiguruj przez `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Zbuduj przez `cmake --build build`.

Zależności w Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Budowanie w Windows z Visual Studio

Wymaga Visual Studio 2022 Build Tools z "Desktop development with C++", CMake 3.22 lub nowszego oraz Gita.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Budowanie dla Windows z WSL lub Linuksa

Tak powstaje sam projekt: program dla Windows jest kompilowany krzyżowo przez Zig, bez Visual Studio.

```
./scripts/build-windows-zig.sh
```

Wynik to `build-win/LYNX.exe`.

| Wymaganie | Wersja | Domyślne miejsce |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE można pobrać tak:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Ustaw zmienne środowiskowe `ZIG` i `JUCE_DIR`, jeśli twoje ścieżki są inne.

## Budowanie w macOS

Potrzebne są narzędzia wiersza poleceń Xcode (`xcode-select --install`), CMake i Ninja, na przykład z `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` daje aplikację uniwersalną. Pomiń to, żeby zbudować tylko dla obecnego Maca.

## Uruchom testy rdzenia

Rdzeń (model, sekwencer, instrumenty, efekty i import `.flp`) to zwykłe C++ i jest testowany bez JUCE, dowolnym kompilatorem C++20.

```
./scripts/build-local.sh ["folder with .flp files"]
```

Przebieg musi kończyć się na `N checks, 0 failures`. Jeśli podasz folder `Data` z instalacji FL Studio 2026, parsuje też projekty `.flp` dostarczane z FL Studio. Skrypt buduje także dwa małe narzędzia: `build-local/flpdump`, który zrzuca zawartość `.flp`, oraz `build-local/renderwav`.

Po zbudowaniu program może sprawdzić sam siebie bez urządzenia audio:

```
LYNX.exe --selftest report.txt
```

Raport kończy się na `result=OK`. Więcej opcji jest na stronie [Wiersz poleceń](/wiki/command-line/).

## Drzewo źródeł

| Folder | Co zawiera |
|---|---|
| `src/core/` | Model, sekwencer, instrumenty, efekty i import `.flp`. Zwykłe C++, testowalne bez JUCE |
| `src/flp/` | Czytnik formatu `.flp` |
| `src/engine/` | Silnik audio: urządzenie, graf, sample, pluginy, pliki projektów |
| `src/ui/` | Interfejs: Channel Rack, Piano Roll, Playlist, Mixer, okna |
| `third_party/` | CLAP i dekoder WavPack |
| `tests/` | Testy rdzenia |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Architektura, stan, plan rozwoju i analiza zachowania FL Studio |

## Warto wiedzieć

- JUCE 8 oficjalnie nie wspiera MinGW. Skrypt budowania z Zig obchodzi różnice. Poprawki są w `cmake/mingw/` i `src/platform/MingwFixes.cpp`.
- Uruchamiaj naraz tylko jeden `build-windows-zig.sh`. Dwa budowania Zig jednocześnie mogą się zawiesić. Przy równoległych budowaniach w osobnych worktree daj każdemu własny cache przez `ZIG_GLOBAL_CACHE_DIR` i `ZIG_LOCAL_CACHE_DIR`.
- Niektóre silniki czytają wbudowane dźwięki i tabele z twojej własnej instalacji FL Studio. W repozytorium nie ma niczego z FL Studio.
- Część dokumentacji w `docs/` jest wciąż po portugalsku. Jest tłumaczona.
- Jeśli rozpowszechniasz zmodyfikowaną wersję, także jako usługę sieciową, licencja wymaga opublikowania jej źródeł na tej samej licencji.

## Powiązane

- [Pełne środowisko programistyczne (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE w Linuksie (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Repozytorium i jego README](https://github.com/Ka1-S/LYNX-SHARE)
- [Wiersz poleceń](/wiki/command-line/)
- [Formaty plików](/wiki/file-formats/)
