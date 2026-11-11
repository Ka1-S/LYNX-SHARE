---
source: edc71061e6
---
LYNX SHARE está escrito en C++20 con JUCE 8 y se compila con CMake. El código fuente está en [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) bajo la GNU Affero General Public License v3.0. Los archivos de las versiones los compila GitHub Actions a partir del código etiquetado, así que solo necesitas esta página para cambiar el código o para compilar por tu cuenta.

## Compilar en Linux

1. Instala las dependencias que se listan abajo.
2. Obtén el código con `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` y entra en la carpeta `lynx`.
3. Configura con `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Compila con `cmake --build build`.

Las dependencias en Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Compilar en Windows con Visual Studio

Requiere Visual Studio 2022 Build Tools con "Desarrollo para el escritorio con C++", CMake 3.22 o posterior y Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Compilar para Windows desde WSL o Linux

Así se desarrolla el propio proyecto: el programa de Windows se compila de forma cruzada con Zig, sin Visual Studio.

```
./scripts/build-windows-zig.sh
```

El resultado es `build-win/LYNX.exe`.

| Requisito | Versión | Ubicación por defecto |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE se puede descargar con:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Define las variables de entorno `ZIG` y `JUCE_DIR` si tus rutas son distintas.

## Compilar en macOS

Necesita las herramientas de línea de comandos de Xcode (`xcode-select --install`), CMake y Ninja, por ejemplo con `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` produce una aplicación universal. Quítalo para compilar solo para el Mac actual.

## Ejecutar las pruebas del núcleo

El núcleo (modelo, secuenciador, instrumentos, efectos e importación de `.flp`) es C++ puro y se prueba sin JUCE, con cualquier compilador C++20.

```
./scripts/build-local.sh ["folder with .flp files"]
```

La ejecución debe terminar con `N checks, 0 failures`. Si le das la carpeta `Data` de una instalación de FL Studio 2026, también analiza los proyectos `.flp` que vienen con FL Studio. El script compila además dos herramientas pequeñas: `build-local/flpdump`, que vuelca el contenido de un `.flp`, y `build-local/renderwav`.

Después de compilar, el programa puede comprobarse a sí mismo sin dispositivo de audio:

```
LYNX.exe --selftest report.txt
```

El informe termina con `result=OK`. Hay más opciones en la página [Línea de comandos](/wiki/command-line/).

## El árbol del código fuente

| Carpeta | Qué contiene |
|---|---|
| `src/core/` | Modelo, secuenciador, instrumentos, efectos e importación de `.flp`. C++ puro, se prueba sin JUCE |
| `src/flp/` | El lector del formato `.flp` |
| `src/engine/` | El motor de audio: dispositivo, grafo, samples, plugins, archivos de proyecto |
| `src/ui/` | La interfaz: Channel Rack, Piano Roll, Playlist, Mixer, ventanas |
| `third_party/` | CLAP y el decodificador WavPack |
| `tests/` | Las pruebas del núcleo |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Arquitectura, estado, hoja de ruta y el análisis del comportamiento de FL Studio |

## Conviene saber

- JUCE 8 no admite MinGW de forma oficial. El script de compilación con Zig sortea las diferencias. Los arreglos están en `cmake/mingw/` y `src/platform/MingwFixes.cpp`.
- Ejecuta un solo `build-windows-zig.sh` a la vez. Dos compilaciones de Zig simultáneas pueden colgarse. Para compilar en paralelo en worktrees separados, da a cada uno su propia caché con `ZIG_GLOBAL_CACHE_DIR` y `ZIG_LOCAL_CACHE_DIR`.
- Algunos motores leen sonidos y tablas integrados de tu propia instalación de FL Studio. En el repositorio no hay nada de FL Studio.
- Parte de la documentación de `docs/` sigue en portugués. Se está traduciendo.
- Si distribuyes una versión modificada, también como servicio en red, la licencia te obliga a publicar su código fuente con la misma licencia.

## Relacionado

- [El entorno de desarrollo completo (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE en Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [El repositorio y su README](https://github.com/Ka1-S/LYNX-SHARE)
- [Línea de comandos](/wiki/command-line/)
- [Formatos de archivo](/wiki/file-formats/)
