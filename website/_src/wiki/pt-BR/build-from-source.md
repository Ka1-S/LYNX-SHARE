---
source: edc71061e6
---
O LYNX SHARE é escrito em C++20 com JUCE 8 e compilado com CMake. O código-fonte está em [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE), sob a GNU Affero General Public License v3.0. Os arquivos das versões são compilados a partir do código com tag pelo GitHub Actions, então você só precisa desta página para alterar o código ou compilar por conta própria.

## Compilar no Linux

1. Instale as dependências listadas abaixo.
2. Baixe o código com `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` e entre na pasta `lynx`.
3. Configure com `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Compile com `cmake --build build`.

As dependências no Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Compilar no Windows com o Visual Studio

Exige o Visual Studio 2022 Build Tools com "Desktop development with C++", CMake 3.22 ou mais novo e Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Compilar para Windows a partir do WSL ou Linux

É assim que o próprio projeto é desenvolvido: o programa para Windows é compilado de forma cruzada com o Zig, sem Visual Studio.

```
./scripts/build-windows-zig.sh
```

O resultado é `build-win/LYNX.exe`.

| Requisito | Versão | Local padrão |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

O JUCE pode ser baixado com:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Defina as variáveis de ambiente `ZIG` e `JUCE_DIR` se os seus caminhos forem outros.

## Compilar no macOS

Precisa das ferramentas de linha de comando do Xcode (`xcode-select --install`), do CMake e do Ninja, por exemplo com `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` gera um app universal. Deixe-o de fora para compilar só para o Mac atual.

## Rodar os testes do core

O core (modelo, sequenciador, instrumentos, efeitos e importação de `.flp`) é C++ puro e é testado sem o JUCE, com qualquer compilador C++20.

```
./scripts/build-local.sh ["folder with .flp files"]
```

A execução deve terminar com `N checks, 0 failures`. Se você passar a pasta `Data` de uma instalação do FL Studio 2026, ele também lê os projetos `.flp` que acompanham o FL Studio. O script compila ainda duas ferramentas pequenas: `build-local/flpdump`, que despeja o conteúdo de um `.flp`, e `build-local/renderwav`.

Depois de compilar, o programa pode se verificar sem dispositivo de áudio:

```
LYNX.exe --selftest report.txt
```

O relatório termina com `result=OK`. Há mais opções na página [Linha de comando](/wiki/command-line/).

## A árvore do código

| Pasta | O que contém |
|---|---|
| `src/core/` | Modelo, sequenciador, instrumentos, efeitos e importação de `.flp`. C++ puro, testável sem o JUCE |
| `src/flp/` | O leitor do formato `.flp` |
| `src/engine/` | O motor de áudio: dispositivo, grafo, samples, plugins, arquivos de projeto |
| `src/ui/` | A interface: Channel Rack, Piano Roll, Playlist, Mixer, janelas |
| `third_party/` | O CLAP e o decodificador WavPack |
| `tests/` | Os testes do core |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Arquitetura, estado, roteiro e a análise do comportamento do FL Studio |

## Bom saber

- O JUCE 8 não suporta oficialmente o MinGW. O script de build com Zig contorna as diferenças. As correções estão em `cmake/mingw/` e `src/platform/MingwFixes.cpp`.
- Rode só um `build-windows-zig.sh` por vez. Dois builds com Zig ao mesmo tempo podem travar. Para builds em paralelo em worktrees separados, dê a cada um o próprio cache com `ZIG_GLOBAL_CACHE_DIR` e `ZIG_LOCAL_CACHE_DIR`.
- Alguns motores leem sons e tabelas embutidos da sua própria instalação do FL Studio. Nada do FL Studio está no repositório.
- Parte da documentação em `docs/` ainda está em português. Ela está sendo traduzida.
- Se você distribuir uma versão modificada, inclusive como serviço de rede, a licença exige que você publique o código-fonte dela sob a mesma licença.

## Veja também

- [O ambiente de desenvolvimento completo (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [O LYNX SHARE no Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [O repositório e o README](https://github.com/Ka1-S/LYNX-SHARE)
- [Linha de comando](/wiki/command-line/)
- [Formatos de arquivo](/wiki/file-formats/)
