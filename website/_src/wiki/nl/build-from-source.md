---
source: edc71061e6
---
LYNX SHARE is geschreven in C++20 met JUCE 8 en wordt gebouwd met CMake. De broncode staat op [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) onder de GNU Affero General Public License v3.0. De releasebestanden worden door GitHub Actions gebouwd uit de getagde broncode, dus je hebt deze pagina alleen nodig om de code te wijzigen of voor jezelf te bouwen.

## Bouwen op Linux

1. Installeer de afhankelijkheden die hieronder staan.
2. Haal de broncode op met `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` en ga naar de map `lynx`.
3. Configureer met `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Bouw met `cmake --build build`.

De afhankelijkheden op Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Bouwen op Windows met Visual Studio

Vereist Visual Studio 2022 Build Tools met "Desktop development with C++", CMake 3.22 of nieuwer en Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Voor Windows bouwen vanuit WSL of Linux

Zo wordt het project zelf ontwikkeld: het Windows-programma wordt met Zig gecrosscompileerd, zonder Visual Studio.

```
./scripts/build-windows-zig.sh
```

Het resultaat is `build-win/LYNX.exe`.

| Vereiste | Versie | Standaardlocatie |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE haal je op met:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Stel de omgevingsvariabelen `ZIG` en `JUCE_DIR` in als jouw paden anders zijn.

## Bouwen op macOS

Je hebt de Xcode-opdrachtregeltools nodig (`xcode-select --install`), CMake en Ninja, bijvoorbeeld met `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` levert een universele app op. Laat het weg om alleen voor de huidige Mac te bouwen.

## De kerntests draaien

De kern (model, sequencer, instrumenten, effecten en `.flp`-import) is gewone C++ en wordt zonder JUCE getest, met elke C++20-compiler.

```
./scripts/build-local.sh ["folder with .flp files"]
```

De run moet eindigen met `N checks, 0 failures`. Krijgt het script de map `Data` van een FL Studio 2026-installatie, dan leest het ook de `.flp`-projecten die met FL Studio meekomen. Het script bouwt ook twee kleine tools: `build-local/flpdump`, dat een `.flp` dumpt, en `build-local/renderwav`.

Na een build kan het programma zichzelf controleren zonder audioapparaat:

```
LYNX.exe --selftest report.txt
```

Het rapport eindigt met `result=OK`. Meer opties staan op de pagina [Opdrachtregel](/wiki/command-line/).

## De broncodeboom

| Map | Wat erin zit |
|---|---|
| `src/core/` | Model, sequencer, instrumenten, effecten en `.flp`-import. Gewone C++, testbaar zonder JUCE |
| `src/flp/` | De lezer van het `.flp`-formaat |
| `src/engine/` | De audio-engine: apparaat, graph, samples, plugins, projectbestanden |
| `src/ui/` | De interface: Channel Rack, Piano Roll, Playlist, Mixer, vensters |
| `third_party/` | CLAP en de WavPack-decoder |
| `tests/` | De kerntests |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Architectuur, status, roadmap en de gedragsanalyse van FL Studio |

## Goed om te weten

- JUCE 8 ondersteunt MinGW niet officieel. Het Zig-buildscript omzeilt de verschillen. De fixes staan in `cmake/mingw/` en `src/platform/MingwFixes.cpp`.
- Draai maar één `build-windows-zig.sh` tegelijk. Twee Zig-builds tegelijk kunnen vastlopen. Geef bij parallelle builds in aparte worktrees elk een eigen cache met `ZIG_GLOBAL_CACHE_DIR` en `ZIG_LOCAL_CACHE_DIR`.
- Sommige engines lezen ingebouwde geluiden en tabellen uit jouw eigen FL Studio-installatie. Er zit niets van FL Studio in de repository.
- Een deel van de documentatie in `docs/` is nog in het Portugees. Het wordt vertaald.
- Verspreid je een aangepaste versie, ook als netwerkdienst, dan verplicht de licentie je de broncode ervan onder dezelfde licentie te publiceren.

## Gerelateerd

- [De volledige ontwikkelomgeving (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE op Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [De repository en de README](https://github.com/Ka1-S/LYNX-SHARE)
- [Opdrachtregel](/wiki/command-line/)
- [Bestandsformaten](/wiki/file-formats/)
