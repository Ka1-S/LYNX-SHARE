---
source: c82c79e958
---
LYNX SHARE에는 레이아웃이 세 가지 있습니다. Logic, Cubase, Reaper, Pro Tools, Studio One에서 오셨다면 **믹서를 곁에 둔 간단한 레이아웃**으로 시작하세요. 익숙한 방식대로 악기나 목소리마다 트랙 하나가 타임라인에 놓입니다. 패턴이 필요하면 FL Studio 방식의 패턴 레이아웃을 쓰면 됩니다.

## 첫 단계

1. **레이아웃 고르기.** LYNX SHARE를 처음 시작하면 환영 카드가 어디서 넘어오는지 묻습니다. `Logic, Cubase, Reaper, Pro Tools 또는 Studio One`을 클릭하면 간단한 레이아웃이 New track 카드와 함께 열리고, 첫 트랙과 함께 믹서가 나타납니다. 나중에 `보기 › 레이아웃 스타일 › 간단한 레이아웃` 메뉴로 언제든 전환할 수 있고, `F9`로 믹서를 엽니다.
2. **트랙 추가.** `+ 트랙 추가`에는 `보컬 / 오디오`, `악기`, `드럼`, `샘플러`, `기타 / 베이스`, `가져오기`, `루프`가 있습니다. 악기 트랙은 컴퓨터 키, 화면 키보드, MIDI 키보드로 연주하세요.
3. **녹음.** `옵션 › 오디오 설정...`에서 오디오 장치를 고른 다음 [내 목소리 녹음하기](/learn/record-voice/)를 참고하세요.

## 같은 점

- 오디오, MIDI, 오토메이션 클립이 있는 타임라인 위의 트랙.
- 루프 테이크와 컴핑을 지원하는 오디오 및 MIDI 녹음.
- 인서트, 센드, 사이드체인, 그룹, 딜레이 보정이 있는 믹서.
- 서드파티 플러그인: 모든 시스템에서 VST3와 CLAP, macOS에서 Audio Units, Linux에서 LV2, Windows와 Linux에서 VST2.
- 곡, 트랙별 스템, MIDI 내보내기.

## 다른 점

- **트랙과 믹서 인서트는 별개입니다.** 간단한 레이아웃에서는 새 트랙마다 자체 채널과 믹서 트랙이 생깁니다. 패턴 레이아웃에서는 FL Studio처럼 채널을 인서트에 직접 연결합니다.
- **패턴.** 패턴 레이아웃에서는 노트가 패턴에 들어 있고, 이 패턴을 플레이리스트에 배치합니다. 패턴 하나에 여러 악기를 담을 수 있습니다.
- **키는 FL Studio를 따릅니다.** `Ctrl+Z`는 한 단계를 실행 취소하고, 한 번 더 누르면 다시 실행합니다. `Ctrl+Alt+Z`는 더 앞의 상태로 되돌립니다.
- **Logic, Cubase, Reaper, Pro Tools 프로젝트 가져오기는 없습니다.** DAWproject 파일, 스템, MIDI로 프로젝트를 옮기세요(아래 참고).

## 키

| 동작 | 키 |
|---|---|
| 재생, 또는 정지하고 처음으로 | `Space` |
| 녹음 켜기와 끄기 | `R` |
| 믹서 | `F9` |
| 피아노 롤 | `F7` |
| 플레이리스트 | `F5` |
| 브라우저 | `Alt+F8` |
| 열기, 저장 | `Ctrl+O`, `Ctrl+S` |
| 실행 취소, 더 앞으로 | `Ctrl+Z`, `Ctrl+Alt+Z` |
| 오디오, MP3, MIDI 내보내기 | `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+Shift+M` |

`도움말 › 단축키`에 모든 키가 나와 있습니다. [단축키](/wiki/shortcuts/)를 참고하세요.

## 프로젝트 옮기기

- **Cubase, Studio One, Bitwig**: 그쪽에서 DAWproject 파일(`.dawproject`)로 저장하고 `파일 › 열기...`으로 여세요. 트랙, 폴더, 클립, 노트, 워프 마커가 있는 오디오, 오토메이션, 템포, 마커, 센드가 있는 믹서, 상태가 있는 VST3, VST2, CLAP 플러그인이 넘어옵니다. `파일 › 내보내기 › DAWproject (.dawproject)...`는 반대 방향입니다.
- **Logic, Reaper, Pro Tools**: 트랙마다 곡의 시작부터 오디오나 MIDI 파일로 내보내 한 폴더에 넣으세요. LYNX SHARE에서 `+ 트랙 추가`을 클릭하고 `가져오기` 옆의 `폴더...`를 클릭하면, 파일마다 처음부터 시작하는 트랙이 됩니다.
- **내보내기**: 내보내기 창은 믹서 인서트마다 또는 채널마다 파일 하나를 쓰고, `파일 › 내보내기 › MIDI 파일...`은 노트를 씁니다.

[파일 형식](/wiki/file-formats/)과 [내보내기](/wiki/exporting/)를 참고하세요.

## 알아 두기

- Logic은 Apple Inc.의 상표이고, Cubase는 Steinberg Media Technologies, Reaper는 Cockos Incorporated, Pro Tools는 Avid Technology, Studio One은 PreSonus Audio Electronics, Bitwig는 Bitwig GmbH의 상표입니다. LYNX SHARE는 독립된 프로그램이며, 이들과 제휴하거나 보증을 받지 않았습니다.
- 클립 실행 레이아웃은 타임라인 옆에 장면이 있는 클립 그리드를 더합니다. [레이아웃](/wiki/layouts/)을 참고하세요.

## 관련 문서

- [레이아웃](/wiki/layouts/)
- [믹서](/wiki/mixer/)
- [녹음](/wiki/recording/)
- [FL Studio에서 넘어오기](/wiki/coming-from-fl-studio/)
- [배우기: 튜토리얼](/learn/)
