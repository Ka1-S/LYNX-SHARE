---
source: edc71061e6
---
LYNX SHARE는 JUCE 8을 쓴 C++20으로 작성했고 CMake로 빌드합니다. 소스는 [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE)에 GNU Affero General Public License v3.0으로 공개되어 있습니다. 릴리스 파일은 태그가 붙은 소스를 GitHub Actions로 빌드한 것이므로, 코드를 수정하거나 직접 빌드할 때만 이 페이지가 필요합니다.

## Linux에서 빌드

1. 아래에 나온 의존성을 설치합니다.
2. `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx`로 소스를 받고 `lynx` 폴더로 이동합니다.
3. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`로 구성합니다.
4. `cmake --build build`로 빌드합니다.

Ubuntu 24.04의 의존성은 다음과 같습니다.

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Windows에서 Visual Studio로 빌드

"C++를 사용한 데스크톱 개발"이 포함된 Visual Studio 2022 Build Tools, CMake 3.22 이상, Git이 필요합니다.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## WSL이나 Linux에서 Windows용으로 빌드

프로젝트 자체가 이 방식으로 개발됩니다. Visual Studio 없이 Zig로 Windows 프로그램을 크로스 컴파일합니다.

```
./scripts/build-windows-zig.sh
```

결과물은 `build-win/LYNX.exe`입니다.

| 요구 사항 | 버전 | 기본 위치 |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE는 다음 명령으로 받을 수 있습니다.

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

경로가 다르면 환경 변수 `ZIG`와 `JUCE_DIR`을 설정합니다.

## macOS에서 빌드

Xcode 명령줄 도구(`xcode-select --install`), CMake, Ninja가 필요합니다. 예를 들어 `brew install cmake ninja`로 설치할 수 있습니다.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`는 유니버설 앱을 만듭니다. 현재 Mac용으로만 빌드하려면 빼세요.

## 코어 테스트 실행

코어(모델, 시퀀서, 악기, 이펙트, `.flp` 가져오기)는 순수 C++이며, C++20 컴파일러만 있으면 JUCE 없이 테스트할 수 있습니다.

```
./scripts/build-local.sh ["folder with .flp files"]
```

실행은 `N checks, 0 failures`로 끝나야 합니다. FL Studio 2026이 설치된 곳의 `Data` 폴더를 주면 FL Studio에 포함된 `.flp` 프로젝트도 파싱합니다. 이 스크립트는 작은 도구 두 개도 빌드합니다. `.flp`를 덤프하는 `build-local/flpdump`와 `build-local/renderwav`입니다.

빌드한 뒤에는 오디오 장치 없이도 프로그램이 스스로 점검할 수 있습니다.

```
LYNX.exe --selftest report.txt
```

보고서는 `result=OK`로 끝납니다. 더 많은 옵션은 [명령줄](/wiki/command-line/) 페이지에 있습니다.

## 소스 트리

| 폴더 | 내용 |
|---|---|
| `src/core/` | 모델, 시퀀서, 악기, 이펙트, `.flp` 가져오기. JUCE 없이 테스트할 수 있는 순수 C++ |
| `src/flp/` | `.flp` 형식 리더 |
| `src/engine/` | 오디오 엔진: 장치, 그래프, 샘플, 플러그인, 프로젝트 파일 |
| `src/ui/` | 인터페이스: 채널 랙, 피아노 롤, 플레이리스트, 믹서, 창 |
| `third_party/` | CLAP과 WavPack 디코더 |
| `tests/` | 코어 테스트 |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | 아키텍처, 현황, 로드맵, FL Studio 동작 분석 |

## 알아 두기

- JUCE 8은 MinGW를 공식 지원하지 않습니다. Zig 빌드 스크립트가 그 차이를 우회합니다. 수정 사항은 `cmake/mingw/`와 `src/platform/MingwFixes.cpp`에 있습니다.
- `build-windows-zig.sh`는 한 번에 하나만 실행하세요. Zig 빌드를 동시에 두 개 돌리면 멈출 수 있습니다. 별도의 워크트리에서 병렬로 빌드하려면 `ZIG_GLOBAL_CACHE_DIR`과 `ZIG_LOCAL_CACHE_DIR`로 각각 자체 캐시를 지정합니다.
- 일부 엔진은 내장 사운드와 테이블을 사용자의 FL Studio 설치에서 읽습니다. 저장소에는 FL Studio의 것이 하나도 들어 있지 않습니다.
- `docs/`의 문서 일부는 아직 포르투갈어로 되어 있으며, 번역 중입니다.
- 수정한 버전을 배포하면 네트워크 서비스로 제공하는 경우에도, 라이선스에 따라 같은 라이선스로 그 소스를 공개해야 합니다.

## 관련 문서

- [전체 개발 환경(SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [Linux에서의 LYNX SHARE(LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [저장소와 README](https://github.com/Ka1-S/LYNX-SHARE)
- [명령줄](/wiki/command-line/)
- [파일 형식](/wiki/file-formats/)
