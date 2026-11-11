---
source: 98829adcb8
---
LYNX SHARE는 창을 열지 않고도 실행할 수 있습니다. 옵션 하나는 프로젝트를 오디오로 렌더링하고, 다른 옵션들은 FL Studio 프로젝트나 Ableton Live 세트로 변환하며, 테스트용 옵션도 여러 개 있습니다. 프로젝트 자체의 검사에도 같은 옵션을 씁니다.

프로그램 파일은 Windows에서 `LYNX.exe`, macOS와 Linux에서 `LYNX`입니다. macOS의 전체 경로는 `/Applications/LYNX.app/Contents/MacOS/LYNX`입니다. 옵션은 모든 시스템에서 같습니다.

## 프로젝트를 파일로 렌더링하기

1. 프로그램이 있는 폴더에서 터미널을 엽니다. Windows에서는 설치 폴더이거나 zip을 푼 폴더입니다.
2. 자신의 프로젝트와 출력 파일로 `LYNX.exe --export project.flp out.flac`을 실행합니다.
3. 프로그램이 종료될 때까지 기다립니다. 창은 열리지 않습니다.
4. 출력 파일 옆에 쓰인 `out.flac.txt`를 읽습니다. 가져온 프로젝트라면 무엇이 변환되었고, 근사되었고, 제외되었는지, 어떤 플러그인을 찾았고 어떤 것이 없었는지가 나열됩니다.

## 프로젝트 열기

```
LYNX.exe project.lynx
```

창과 함께 LYNX SHARE를 시작하고 파일을 엽니다. `.lynx`, `.flp`, `.als`, `.flm`, `.mid`를 받습니다. 상대 경로는 현재 폴더 기준입니다.

## 일상적으로 쓰는 옵션

| 옵션 | 동작 |
|---|---|
| `--export <project> <out>` | 꼬리까지 포함해 곡 전체를 렌더링하며, 내보내기 창과 같은 방식입니다. `<out>`의 확장자가 형식을 정합니다. `.wav`(24비트), `.flac`, `.ogg`, `.mp3`(320 kbps) |
| `--export ... --pattern` | 현재 패턴만 렌더링합니다 |
| `--export ... --insert N` | 믹서 인서트 N의 출력만 렌더링합니다 |
| `--export ... --rate N` | 다른 샘플 레이트(Hz)로 렌더링합니다 |
| `--stems <project> <base.wav>` | 채널이 있는 믹서 인서트마다 WAV 하나를 씁니다. `base - 1.wav`, `base - 2.wav` 등입니다 |
| `--export-flp <project> <out.flp>` | FL Studio 프로젝트를 씁니다. `out.zip`을 주면 압축된 루프 패키지를 씁니다 |
| `--export-flp ... --substitute` | LYNX 전용 플러그인을 오디오로 렌더링하지 않고 가장 비슷한 FL 플러그인으로 바꿉니다 |
| `--export-als <project> <folder>` | Live 12용 Ableton Live 세트를 `<folder>/<name> Project/<name>.als`에 씁니다 |
| `--export-als ... --live11` | Live 11용 세트를 씁니다 |
| `--devices <report.txt>` | 이 컴퓨터의 오디오 드라이버 종류와 장치를 나열합니다 |
| `--showcase <out.lynx>` | LYNX SHARE 자체 데모 프로젝트를 씁니다 |
| `--lang <id>` | 이번 실행에서만 다른 인터페이스 언어를 씁니다. 예: `--lang pt-BR` |
| `--menu-entry on` 또는 `꺼짐` | Linux AppImage 전용: 애플리케이션 메뉴에 LYNX SHARE를 추가하거나 제거합니다 |

`--export`, `--stems`, `--export-flp`는 프로젝트로 `.lynx`, `.flp`, `.als`를 받습니다. `--export-als`는 `.flm`도 받습니다.

예시:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## 테스트용 옵션

LYNX SHARE 자체를 점검하기 위한 옵션입니다. 버그 보고에 유용합니다.

| 옵션 | 동작 |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | 오디오 장치 없이 셀프 테스트를 실행합니다. 통과하면 보고서가 `result=OK`로 끝납니다 |
| `--snapshot <prefix> [project]` | 인터페이스와 창을 접두사를 붙인 이름의 PNG 파일로 그립니다. 이 위키의 그림도 이렇게 만들었습니다 |
| `--render <project> <out.wav> <seconds>` | 현재 패턴을 지정한 초만큼 24비트 WAV로 렌더링합니다 |
| `--plugintest <plugin> <report.txt>` | VST3 또는 CLAP 플러그인 하나를 로드해 테스트합니다 |
| `--sandboxtest <plugin> <report.txt>` | 멈춘 플러그인도 포함해 플러그인을 별도 프로세스에서 테스트합니다 |
| `--scantest <folder> <report.txt>` | 폴더의 플러그인을 별도 프로세스에서 스캔합니다 |
| `--fldb <report.txt> [project.flp]` | 여기서 찾은 FL Studio 설치, 그 플러그인 데이터베이스, 프로젝트의 플러그인 일치 결과를 보고합니다 |
| `--devicetest <type> <device> <report.txt>` | 오디오 드라이버를 열어 1초 동안 무음을 재생하고 결과를 보고합니다 |
| `--uiperf <project> <report.txt>` | 믹서, 플레이리스트, 채널 랙의 그리기 시간을 측정합니다 |

예시:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## 알아 두면 좋은 점

- 결과는 터미널이 아니라 파일로 나갑니다. `--export`는 `<out>.txt`, `--export-flp`는 `<out.flp>.txt`를 쓰고, `--export-als`는 폴더에 `export-als.txt`를, `--stems`는 `<base.wav>.txt`를 씁니다.
- 프로그램은 표준 입력을 읽습니다. 셸 루프에서는 호출마다 `< /dev/null`을 붙이세요.
- `--snapshot`은 환경 변수 `LYNX_SNAPSHOT_SIZE`로 달리 지정하지 않으면 1280x720으로 그립니다. 예를 들어 `1920x1080`을 지정할 수 있습니다.
- `--lang`은 `옵션 › 일반 설정...` 메뉴에 나열된 언어의 ID를 받습니다. 예를 들어 `en`, `pt-BR`, `de`, `es`, `fr`, `ja`입니다. 알 수 없는 ID는 무시됩니다.
- `--export`는 내보내기 창과 같은 렌더링 경로를 쓰지만, 창의 다른 옵션은 적용되지 않습니다. 디더, 비트 깊이, 노멀라이즈가 필요하면 창을 사용하세요.
- 소스에는 테스트 옵션이 훨씬 더 많습니다. 내부용이어서 버전마다 바뀔 수 있습니다.

## 관련 문서

- [내보내기](/wiki/exporting/)
- [파일 형식](/wiki/file-formats/)
- [소스에서 빌드](/wiki/build-from-source/)
- [문제 해결](/wiki/troubleshooting/)
