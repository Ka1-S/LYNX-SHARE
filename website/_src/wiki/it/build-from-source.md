---
source: edc71061e6
---
LYNX SHARE è scritto in C++20 con JUCE 8 e compilato con CMake. Il sorgente è su [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) con licenza GNU Affero General Public License v3.0. I file di rilascio sono compilati dal sorgente taggato da GitHub Actions, quindi questa pagina ti serve solo per modificare il codice o per compilare per conto tuo.

## Compilare su Linux

1. Installa le dipendenze elencate qui sotto.
2. Scarica il sorgente con `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` ed entra nella cartella `lynx`.
3. Configura con `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Compila con `cmake --build build`.

Le dipendenze su Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Compilare su Windows con Visual Studio

Servono Visual Studio 2022 Build Tools con "Desktop development with C++", CMake 3.22 o più recente e Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Compilare per Windows da WSL o Linux

Così si sviluppa il progetto stesso: il programma per Windows viene compilato in cross-compilazione con Zig, senza Visual Studio.

```
./scripts/build-windows-zig.sh
```

Il risultato è `build-win/LYNX.exe`.

| Requisito | Versione | Posizione predefinita |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE si può scaricare con:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Imposta le variabili d'ambiente `ZIG` e `JUCE_DIR` se i tuoi percorsi sono diversi.

## Compilare su macOS

Servono gli strumenti da riga di comando di Xcode (`xcode-select --install`), CMake e Ninja, per esempio da `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` produce un'app universale. Omettilo per compilare solo per il Mac che stai usando.

## Eseguire i test del core

Il core (modello, sequencer, strumenti, effetti e importazione `.flp`) è C++ puro e si testa senza JUCE, con qualsiasi compilatore C++20.

```
./scripts/build-local.sh ["folder with .flp files"]
```

L'esecuzione deve finire con `N checks, 0 failures`. Se gli dai la cartella `Data` di un'installazione di FL Studio 2026, analizza anche i progetti `.flp` che arrivano con FL Studio. Lo script compila anche due piccoli strumenti: `build-local/flpdump`, che fa il dump di un `.flp`, e `build-local/renderwav`.

Dopo una compilazione, il programma può controllarsi da solo senza un dispositivo audio:

```
LYNX.exe --selftest report.txt
```

Il report finisce con `result=OK`. Altre opzioni sono nella pagina [Riga di comando](/wiki/command-line/).

## L'albero dei sorgenti

| Cartella | Cosa contiene |
|---|---|
| `src/core/` | Modello, sequencer, strumenti, effetti e importazione `.flp`. C++ puro, testabile senza JUCE |
| `src/flp/` | Il lettore del formato `.flp` |
| `src/engine/` | Il motore audio: dispositivo, grafo, sample, plugin, file di progetto |
| `src/ui/` | L'interfaccia: Channel Rack, Piano Roll, Playlist, Mixer, finestre |
| `third_party/` | CLAP e il decoder WavPack |
| `tests/` | I test del core |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Architettura, stato, roadmap e l'analisi del comportamento di FL Studio |

## Da sapere

- JUCE 8 non supporta ufficialmente MinGW. Lo script di build Zig aggira le differenze. Le correzioni sono in `cmake/mingw/` e `src/platform/MingwFixes.cpp`.
- Esegui un solo `build-windows-zig.sh` alla volta. Due build Zig insieme possono bloccarsi. Per build parallele in worktree separati, dai a ciascuna la propria cache con `ZIG_GLOBAL_CACHE_DIR` e `ZIG_LOCAL_CACHE_DIR`.
- Alcuni motori leggono suoni e tabelle integrati dalla tua installazione di FL Studio. Nel repository non c'è niente di FL Studio.
- Parte della documentazione in `docs/` è ancora in portoghese. Si sta traducendo.
- Se distribuisci una versione modificata, anche come servizio di rete, la licenza ti obbliga a pubblicarne il sorgente con la stessa licenza.

## Correlati

- [L'ambiente di sviluppo completo (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE su Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Il repository e il suo README](https://github.com/Ka1-S/LYNX-SHARE)
- [Riga di comando](/wiki/command-line/)
- [Formati di file](/wiki/file-formats/)
