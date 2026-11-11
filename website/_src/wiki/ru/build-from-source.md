---
source: edc71061e6
---
LYNX SHARE написан на C++20 с JUCE 8 и собирается через CMake. Исходники лежат на [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) под лицензией GNU Affero General Public License v3.0. Релизные файлы GitHub Actions собирает из помеченных тегами исходников, так что эта страница нужна только тем, кто хочет изменить код или собрать программу для себя.

## Сборка в Linux

1. Установите зависимости из списка ниже.
2. Получите исходники командой `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` и перейдите в папку `lynx`.
3. Настройте проект командой `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Соберите командой `cmake --build build`.

Зависимости в Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Сборка в Windows с Visual Studio

Нужны Visual Studio 2022 Build Tools с компонентом «Разработка классических приложений на C++», CMake 3.22 или новее и Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Сборка для Windows из WSL или Linux

Так ведётся разработка самого проекта: программа для Windows кросс-компилируется с помощью Zig, без Visual Studio.

```
./scripts/build-windows-zig.sh
```

Результат лежит в `build-win/LYNX.exe`.

| Требование | Версия | Место по умолчанию |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE можно скачать так:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Если ваши пути другие, задайте переменные окружения `ZIG` и `JUCE_DIR`.

## Сборка в macOS

Нужны утилиты командной строки Xcode (`xcode-select --install`), CMake и Ninja, например из `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` даёт универсальное приложение. Уберите параметр, чтобы собрать только для текущего Mac.

## Запуск тестов ядра

Ядро (модель, секвенсор, инструменты, эффекты и импорт `.flp`) написано на чистом C++ и тестируется без JUCE любым компилятором C++20.

```
./scripts/build-local.sh ["folder with .flp files"]
```

Прогон должен закончиться строкой `N checks, 0 failures`. Если указать папку `Data` установки FL Studio 2026, скрипт разберёт и проекты `.flp`, которые поставляются с FL Studio. Скрипт также собирает два небольших инструмента: `build-local/flpdump`, который выводит содержимое `.flp`, и `build-local/renderwav`.

После сборки программа может проверить себя без аудиоустройства:

```
LYNX.exe --selftest report.txt
```

Отчёт заканчивается строкой `result=OK`. Другие параметры описаны на странице [Командная строка](/wiki/command-line/).

## Структура исходников

| Папка | Что в ней |
|---|---|
| `src/core/` | Модель, секвенсор, инструменты, эффекты и импорт `.flp`. Чистый C++, тестируется без JUCE |
| `src/flp/` | Читатель формата `.flp` |
| `src/engine/` | Аудиодвижок: устройство, граф, сэмплы, плагины, файлы проектов |
| `src/ui/` | Интерфейс: Channel Rack, Piano Roll, Playlist, Mixer, окна |
| `third_party/` | CLAP и декодер WavPack |
| `tests/` | Тесты ядра |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Архитектура, состояние, дорожная карта и анализ поведения FL Studio |

## Что полезно знать

- JUCE 8 официально не поддерживает MinGW. Скрипт сборки на Zig обходит различия. Исправления лежат в `cmake/mingw/` и `src/platform/MingwFixes.cpp`.
- Запускайте только один `build-windows-zig.sh` за раз. Две сборки Zig одновременно могут зависнуть. Для параллельных сборок в отдельных рабочих копиях дайте каждой свой кэш через `ZIG_GLOBAL_CACHE_DIR` и `ZIG_LOCAL_CACHE_DIR`.
- Некоторые движки читают встроенные звуки и таблицы из вашей собственной установки FL Studio. В репозитории нет ничего из FL Studio.
- Часть документации в `docs/` пока на португальском. Её переводят.
- Если вы распространяете изменённую версию, в том числе как сетевой сервис, лицензия требует опубликовать её исходники под той же лицензией.

## Связанные страницы

- [Полная среда разработки (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE в Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Репозиторий и его README](https://github.com/Ka1-S/LYNX-SHARE)
- [Командная строка](/wiki/command-line/)
- [Форматы файлов](/wiki/file-formats/)
