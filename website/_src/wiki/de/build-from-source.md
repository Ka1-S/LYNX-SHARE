---
source: edc71061e6
---
LYNX SHARE ist in C++20 mit JUCE 8 geschrieben und wird mit CMake gebaut. Der Quellcode liegt unter [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) und steht unter der GNU Affero General Public License v3.0. Die Release-Dateien baut GitHub Actions aus dem getaggten Quellcode. Diese Seite brauchst du also nur, wenn du den Code ändern oder selbst bauen willst.

## Unter Linux bauen

1. Installiere die unten aufgeführten Abhängigkeiten.
2. Hole den Quellcode mit `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` und wechsle in den Ordner `lynx`.
3. Konfiguriere mit `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Baue mit `cmake --build build`.

Die Abhängigkeiten unter Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Unter Windows mit Visual Studio bauen

Voraussetzung sind die Visual Studio 2022 Build Tools mit „Desktopentwicklung mit C++“, CMake 3.22 oder neuer und Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Für Windows aus WSL oder Linux bauen

So wird das Projekt selbst entwickelt: Das Windows-Programm wird mit Zig cross-kompiliert, ohne Visual Studio.

```
./scripts/build-windows-zig.sh
```

Das Ergebnis ist `build-win/LYNX.exe`.

| Voraussetzung | Version | Standardort |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE holst du so:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Setze die Umgebungsvariablen `ZIG` und `JUCE_DIR`, wenn deine Pfade abweichen.

## Unter macOS bauen

Du brauchst die Xcode Command Line Tools (`xcode-select --install`), CMake und Ninja, zum Beispiel über `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` erzeugt eine Universal-App. Lässt du es weg, wird nur für den aktuellen Mac gebaut.

## Die Core-Tests ausführen

Der Core (Modell, Sequencer, Instrumente, Effekte und `.flp`-Import) ist reines C++ und wird ohne JUCE getestet, mit jedem C++20-Compiler.

```
./scripts/build-local.sh ["folder with .flp files"]
```

Der Lauf muss mit `N checks, 0 failures` enden. Gibst du den Ordner `Data` einer FL-Studio-2026-Installation an, liest er auch die `.flp`-Projekte ein, die mit FL Studio mitkommen. Das Skript baut außerdem zwei kleine Werkzeuge: `build-local/flpdump`, das eine `.flp` ausgibt, und `build-local/renderwav`.

Nach einem Build kann das Programm sich selbst prüfen, ohne Audiogerät:

```
LYNX.exe --selftest report.txt
```

Der Bericht endet mit `result=OK`. Weitere Optionen stehen auf der Seite [Kommandozeile](/wiki/command-line/).

## Der Quellbaum

| Ordner | Was er enthält |
|---|---|
| `src/core/` | Modell, Sequencer, Instrumente, Effekte und `.flp`-Import. Reines C++, ohne JUCE testbar |
| `src/flp/` | Der Leser für das `.flp`-Format |
| `src/engine/` | Die Audio-Engine: Gerät, Graph, Samples, Plugins, Projektdateien |
| `src/ui/` | Die Oberfläche: Channel Rack, Piano Roll, Playlist, Mixer, Fenster |
| `third_party/` | CLAP und der WavPack-Decoder |
| `tests/` | Die Core-Tests |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Architektur, Status, Roadmap und die Verhaltensanalyse von FL Studio |

## Gut zu wissen

- JUCE 8 unterstützt MinGW offiziell nicht. Das Zig-Build-Skript umgeht die Unterschiede. Die Korrekturen liegen in `cmake/mingw/` und `src/platform/MingwFixes.cpp`.
- Führe immer nur ein `build-windows-zig.sh` gleichzeitig aus. Zwei Zig-Builds auf einmal können hängen bleiben. Für parallele Builds in getrennten Worktrees gib jedem mit `ZIG_GLOBAL_CACHE_DIR` und `ZIG_LOCAL_CACHE_DIR` einen eigenen Cache.
- Manche Engines lesen eingebaute Klänge und Tabellen aus deiner eigenen FL-Studio-Installation. Im Repository steckt nichts von FL Studio.
- Ein Teil der Dokumentation in `docs/` ist noch auf Portugiesisch. Sie wird gerade übersetzt.
- Wenn du eine veränderte Version weitergibst, auch als Netzwerkdienst, verlangt die Lizenz, dass du ihren Quellcode unter derselben Lizenz veröffentlichst.

## Siehe auch

- [Die komplette Entwicklungsumgebung (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE unter Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Das Repository und seine README](https://github.com/Ka1-S/LYNX-SHARE)
- [Kommandozeile](/wiki/command-line/)
- [Dateiformate](/wiki/file-formats/)
