---
source: a79e43f031
---
LYNX SHARE는 프로젝트를 `.lynx` 파일로 저장합니다. 다른 프로그램의 프로젝트도 열고, 흔히 쓰는 오디오 파일을 불러오며, 오디오와 MIDI, 다른 프로그램용 프로젝트를 내보냅니다. 표에 0.1.26 알파의 모든 형식이 있습니다.

다른 프로그램 형식으로 연 프로젝트는 가져온 것입니다. 이때 `파일 › 저장`는 `.lynx` 파일을 어디에 쓸지 묻고, 원본 파일은 절대 바뀌지 않습니다.

## 다른 프로그램의 프로젝트 열기

1. `파일 › 열기...`(`Ctrl+O`)을 선택합니다.
2. `.flp`, `.als`, `.flm`, `.mid`, `.zip` 파일을 고릅니다.
3. `.flp`라면 `도움말 › Last FL Studio import report`를 선택해 근사치로 처리했거나 빠진 항목을 확인합니다.
4. `파일 › 저장`를 선택하고 `.lynx` 파일을 쓸 위치를 고릅니다.

## 형식

| 형식 | 열기 | 저장 / 내보내기 | 참고 |
|---|---|---|---|
| `.lynx` | `파일 › 열기...` | `파일 › 저장`, 자동 저장 포함 | LYNX SHARE 고유의 프로젝트 형식(JSON) |
| `.zip` | `파일 › 열기...`이 폴더에 풀고 그 안의 프로젝트를 엽니다 | `파일 › 내보내기 › 샘플 포함 프로젝트(.zip)...` | FL Studio의 압축 루프 패키지도 엽니다 |
| `.lynxpack` | 요약을 보여 준 뒤 엽니다. 파트는 호스트의 프로젝트에 병합됩니다 | `파일 › 온라인 세션 › 프로젝트 보내기...` | [온라인 세션](/wiki/online-sessions/)에서 보낸 프로젝트 또는 파트입니다. 서드파티 플러그인 파트는 오디오 렌더링과 플러그인 설정으로 전달됩니다 |
| `.flp` | `파일 › 열기...` | `파일 › 내보내기 › FL Studio 프로젝트...`, FL Studio 2026 프로젝트 또는 압축 루프 패키지 | 기본 플러그인은 정확도가 제각각인 재현 엔진으로 재생됩니다. FLEX 팩은 열지 않습니다. 내보낼 때 LYNX 전용 악기와 이펙트는 오디오로 렌더링하거나 가장 가까운 FL 플러그인으로 바꾸고, 모듈레이터는 뺍니다. [FL Studio에서 넘어오기](/wiki/coming-from-fl-studio/)를 참고하세요 |
| `.flm` | `파일 › 열기...` | 불가 | FL Studio Mobile 프로젝트. 일부만 가져옵니다. MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, Slicer, DirectWave 외의 악기는 내장 신스로 재생됩니다 |
| `.als` | `파일 › 열기...`, Live 11과 12의 세트 | `파일 › 내보내기 › Ableton Live 세트 (.als)...`, Live 12 또는 Live 11용 | 가져올 때 LYNX SHARE에 대응하는 것이 없는 Live 디바이스는 빠집니다. 내보낼 때 LYNX SHARE 자체의 악기와 이펙트는 변환되지 않고, 이펙트 파라미터 오토메이션과 인서트 간 라우트는 빠집니다. [Ableton Live에서 넘어오기](/wiki/coming-from-ableton/)를 참고하세요 |
| `.mid`, `.midi` | `파일 › 열기...`은 새 프로젝트를 만듭니다. `파일 › 가져오기 › MIDI 파일...`과 피아노 롤은 현재 프로젝트로 가져옵니다 | `파일 › 내보내기 › MIDI 파일...`(`Ctrl+Shift+M`) | |
| `.wav` | 샘플 또는 오디오 클립으로 | `파일 › 내보내기 › Wave 파일...`, 16비트, 24비트, 32비트 부동소수점 | 디더, 템포 정보, 루프 마커, 플레이리스트 마커는 선택 사항입니다 |
| `.mp3` | 샘플 또는 오디오 클립으로 | `파일 › 내보내기 › MP3 파일...` | 고정 비트레이트 |
| `.ogg` | 샘플 또는 오디오 클립으로 | `파일 › 내보내기 › OGG 파일...` | |
| `.flac` | 샘플 또는 오디오 클립으로 | `파일 › 내보내기 › FLAC 파일...`, 압축 0~8 | 어느 단계든 무손실 |
| `.aif`, `.aiff` | 샘플 또는 오디오 클립으로 | 불가 | |
| `.wv` | 샘플로 | 불가 | FL Studio 팩에 들어 있는 WavPack 샘플 |
| `.sf2`, `.sf3` | LYNX Soundfont로 재생 | 불가 | SoundFont |
| `.fst` | 브라우저에서 | 불가 | FL Studio 기본 플러그인의 프리셋. LYNX SHARE에 엔진이 있는 플러그인만 해당 |
| `.fsc` | 피아노 롤과 이벤트 에디터 | 피아노 롤과 이벤트 에디터 | FL Studio 스코어 및 오토메이션 파일 |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | 호스팅된 플러그인의 프리셋 메뉴에서 `프리셋 파일 불러오기...` | `프리셋 저장...`와 `프리셋 파일 내보내기...` | 서드파티 플러그인 프리셋. `.fxp`와 `.fxb`는 VST2 플러그인용, `.vstpreset`은 VST3 플러그인용이며, 플러그인이 LYNX SHARE 안에서 실행 중일 때만 쓸 수 있습니다 |
| 비디오 파일 | `파일 › 가져오기 › 비디오 파일...`이 플레이리스트에 비디오를 놓습니다 | 내보내기 창의 `이 믹스로 비디오도 쓰기` | 가져오기는 Windows와 macOS에서 시스템 디코더를 쓰고 Linux에서는 FFmpeg이 필요합니다. 내보내기에는 FFmpeg이 필요합니다 |

## 그 밖의 내보내기

| 명령 | 기능 |
|---|---|
| 내보내기 창의 `파일` 옵션 | 스템: 믹서 인서트마다, 채널마다, 또는 악기만 있는 채널마다 파일 하나 |
| `파일 › 내보내기 › 모든 Playlist 트랙` | 플레이리스트 트랙마다 파일 하나 |
| `파일 › 내보내기 › 모든 Playlist 트랙 그룹` | 트랙 그룹마다 파일 하나 |
| `파일 › 내보내기 › 마스터링...` | 프로젝트를 렌더링하고 라우드니스 목표에 맞춰 마스터링합니다. 컴퓨터 안에서 처리됩니다 |
| `파일 › 내보내기 › 프로젝트 본...` | 채널과 이펙트 프리셋, 스코어를 폴더에 씁니다 |

내보내기 창은 곡, 현재 패턴, 플레이리스트 선택 영역을 렌더링합니다.

## 명령줄에서

Windows에서 프로그램은 `LYNX.exe`입니다. macOS와 Linux에서는 `LYNX`입니다.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems`, `--export-flp`는 입력으로 `.als`도 받습니다.

## 알아 두기

- 30초보다 긴 오디오 파일은 디스크 캐시에서 스트리밍합니다.
- 저장할 때 `.flp`, `.als`, `.flm`은 절대 쓰지 않습니다. 내보내기 명령이 새 파일을 씁니다.
- `.wv` 파일은 읽기 전용입니다.
- `.zip`은 먼저 폴더에 풀리고, 그 안의 프로젝트가 열립니다.

## 관련 문서

- [프로젝트 가져오기](/wiki/importing-projects/)
- [내보내기](/wiki/exporting/)
- [명령줄](/wiki/command-line/)
- [FL Studio에서 넘어오기](/wiki/coming-from-fl-studio/)
- [Ableton Live에서 넘어오기](/wiki/coming-from-ableton/)
