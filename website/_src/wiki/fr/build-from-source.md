---
source: edc71061e6
---
LYNX SHARE est écrit en C++20 avec JUCE 8 et compilé avec CMake. Le code source est sur [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE), sous licence GNU Affero General Public License v3.0. Les fichiers de version sont compilés depuis le source étiqueté par GitHub Actions : cette page ne vous sert donc que si vous voulez modifier le code ou compiler pour vous-même.

## Compiler sous Linux

1. Installez les dépendances listées ci-dessous.
2. Récupérez le source avec `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` et entrez dans le dossier `lynx`.
3. Configurez avec `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Compilez avec `cmake --build build`.

Les dépendances sous Ubuntu 24.04 :

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Compiler sous Windows avec Visual Studio

Il faut Visual Studio 2022 Build Tools avec « Desktop development with C++ », CMake 3.22 ou plus récent et Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Compiler pour Windows depuis WSL ou Linux

C'est ainsi que le projet lui-même est développé : le programme Windows est compilé en croisé avec Zig, sans Visual Studio.

```
./scripts/build-windows-zig.sh
```

Le résultat est `build-win/LYNX.exe`.

| Prérequis | Version | Emplacement par défaut |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE peut être récupéré avec :

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Définissez les variables d'environnement `ZIG` et `JUCE_DIR` si vos chemins diffèrent.

## Compiler sous macOS

Il faut les outils en ligne de commande de Xcode (`xcode-select --install`), CMake et Ninja, par exemple avec `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` produit une app universelle. Omettez-le pour compiler uniquement pour le Mac actuel.

## Lancer les tests du noyau

Le noyau (modèle, séquenceur, instruments, effets et import `.flp`) est du C++ pur et se teste sans JUCE, avec n'importe quel compilateur C++20.

```
./scripts/build-local.sh ["folder with .flp files"]
```

L'exécution doit se terminer par `N checks, 0 failures`. Si vous lui donnez le dossier `Data` d'une installation de FL Studio 2026, il analyse aussi les projets `.flp` livrés avec FL Studio. Le script compile aussi deux petits outils : `build-local/flpdump`, qui décompose un `.flp`, et `build-local/renderwav`.

Après une compilation, le programme peut se vérifier lui-même sans périphérique audio :

```
LYNX.exe --selftest report.txt
```

Le rapport se termine par `result=OK`. Plus d'options sont décrites dans la page [Ligne de commande](/wiki/command-line/).

## L'arborescence du source

| Dossier | Ce qu'il contient |
|---|---|
| `src/core/` | Modèle, séquenceur, instruments, effets et import `.flp`. C++ pur, testable sans JUCE |
| `src/flp/` | Le lecteur du format `.flp` |
| `src/engine/` | Le moteur audio : périphérique, graphe, samples, plugins, fichiers de projet |
| `src/ui/` | L'interface : Channel Rack, Piano Roll, Playlist, Mixer, fenêtres |
| `third_party/` | CLAP et le décodeur WavPack |
| `tests/` | Les tests du noyau |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Architecture, état, feuille de route et analyse du comportement de FL Studio |

## Bon à savoir

- JUCE 8 ne prend pas officiellement en charge MinGW. Le script de compilation Zig contourne les différences. Les correctifs sont dans `cmake/mingw/` et `src/platform/MingwFixes.cpp`.
- Ne lancez qu'un seul `build-windows-zig.sh` à la fois. Deux compilations Zig simultanées peuvent se bloquer. Pour des compilations parallèles dans des worktrees séparés, donnez à chacune son propre cache avec `ZIG_GLOBAL_CACHE_DIR` et `ZIG_LOCAL_CACHE_DIR`.
- Certains moteurs lisent des sons et des tables intégrés dans votre propre installation de FL Studio. Rien de FL Studio n'est dans le dépôt.
- Une partie de la documentation de `docs/` est encore en portugais. Elle est en cours de traduction.
- Si vous distribuez une version modifiée, y compris comme service réseau, la licence vous oblige à publier son source sous la même licence.

## Voir aussi

- [L'environnement de développement complet (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE sous Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Le dépôt et son README](https://github.com/Ka1-S/LYNX-SHARE)
- [Ligne de commande](/wiki/command-line/)
- [Formats de fichiers](/wiki/file-formats/)
