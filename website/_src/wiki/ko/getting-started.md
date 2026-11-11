---
source: a6d9063ec3
---
LYNX SHARE는 Windows, macOS, Linux에서 쓸 수 있는 무료 오픈 소스 DAW입니다. 패턴을 중심으로 작업합니다. 채널 랙과 피아노 롤에서 패턴을 만들고, 플레이리스트에서 배치하고, 믹서에서 믹스합니다. FL Studio 프로젝트(`.flp`)와 Ableton Live 세트(`.als`)를 열 수 있고, 자체 형식인 `.lynx`로 저장합니다.

알파 버전입니다. 버그가 있을 수 있으니 자주 저장하세요.

## 실행하기

1. 아래 표에서 사용 중인 시스템이 지원되는지 확인합니다.
2. 다운로드해서 설치합니다. 시스템별 단계는 [설치](/wiki/install/)에 있습니다.
3. 실행합니다. 짧은 설정 마법사가 언어를 고르고, 오디오 출력을 테스트하고, 플러그인 폴더를 나열하며, 그 폴더는 백그라운드에서 스캔됩니다. 건너뛰어도 되고, 나중에 `옵션 › 설정 마법사(언어, 오디오, 플러그인 폴더)...` 메뉴에서 다시 열 수 있습니다.
4. 소리가 나지 않으면 [오디오 설정](/wiki/audio-setup/)으로 갑니다.
5. [창 둘러보기](/wiki/tour/)를 읽고 [첫 곡 만들기](/wiki/first-song/)를 해 보세요.

## 필요한 사양

| 시스템 | 요구 사항 |
|---|---|
| Windows | Windows 10 또는 11, 64비트 |
| macOS | macOS 11 Big Sur 이상, Intel 또는 Apple 실리콘 |
| Linux | glibc 2.35 이상의 x86_64(Ubuntu 22.04+, Debian 12+, Fedora 36+, 최신 Arch 또는 Manjaro), X11 또는 XWayland |

오디오 인터페이스는 필요하지 않습니다. LYNX SHARE는 시스템이 이미 쓰고 있는 사운드 장치로 재생합니다. FL Studio도 필요하지 않습니다. 같은 컴퓨터에 설치되어 있으면 LYNX SHARE가 그 샘플을 찾아내고, `.flp` 프로젝트에 같은 플러그인 파일을 사용합니다.

서드파티 플러그인은 선택 사항입니다. LYNX SHARE에는 [카탈로그](/instruments-and-effects/)에 나열된 자체 악기와 이펙트가 있습니다.

## 작업 저장하기

`Ctrl+S`는 저장, `Ctrl+Shift+S`는 다른 이름으로 저장, `Ctrl+Alt+S`는 새 버전 저장입니다(Song_2, Song_3 등). 자동 저장도 있으며 `파일 › 자동 저장 폴더 열기` 메뉴와 `파일 › 마지막 백업으로 되돌리기...` 메뉴에서 접근합니다.

`파일 › 템플릿으로 새로 만들기` 메뉴에는 시작점이 네 가지 있습니다. Empty, Basic beat, Trap with 808, House입니다.

## 데모 프로젝트

Windows 설치 프로그램과 zip, macOS `.dmg`, Linux `.tar.gz`에는 데모 프로젝트 `Demo.lynx`가 들어 있습니다. `파일 › 열기...` (`Ctrl+O`) 메뉴로 엽니다. Windows에서는 설치 폴더의 `LYNX.exe` 옆에 있습니다.

터미널에서 직접 만들 수도 있습니다. [명령줄](/wiki/command-line/)을 참고하세요.

```
LYNX.exe --showcase out.lynx
```

## 다음에 볼 곳

| 하고 싶은 일 | 읽을 문서 |
|---|---|
| 각 창 익히기 | [채널 랙](/wiki/channel-rack/), [피아노 롤](/wiki/piano-roll/), [플레이리스트](/wiki/playlist/), [믹서](/wiki/mixer/), [브라우저](/wiki/browser/) |
| 다른 프로그램의 프로젝트 열기 | [FL Studio에서 넘어오기](/wiki/coming-from-fl-studio/), [Ableton Live에서 넘어오기](/wiki/coming-from-ableton/) |
| VST, CLAP, LV2, AU 플러그인 쓰기 | [플러그인](/wiki/plugins/) |
| 키 찾아보기 | [단축키](/wiki/shortcuts/) |
| 문제 해결 | [문제 해결](/wiki/troubleshooting/)과 [FAQ](/faq/) |

## 알아 두면 좋은 점

- 프로그램 파일은 Windows에서 `LYNX.exe`, macOS와 Linux에서 `LYNX`입니다. 설정 폴더 이름도 `LYNX`입니다.
- Windows와 macOS에서는 첫 실행 때 시스템 경고가 나올 수 있습니다. Windows 빌드에는 아직 코드 서명이 없고 macOS 빌드는 공증을 받지 않았기 때문입니다. 무엇을 클릭해야 하는지는 [설치](/wiki/install/)에 있습니다.
- LYNX SHARE가 지원하지 않는 언어를 쓰는 시스템에서는 인터페이스가 기본적으로 영어입니다. `옵션 › 일반 설정...` 메뉴에서 바꿀 수 있습니다. 새 언어는 다시 시작한 뒤에 적용됩니다.
- FL Studio 프로젝트의 FLEX 팩은 암호화되어 있어 열지 않습니다. 해당 채널에는 프리셋 이름에 따라 고른 신스 패치가 적용됩니다.

## 관련 문서

- [설치](/wiki/install/)
- [오디오 설정](/wiki/audio-setup/)
- [창 둘러보기](/wiki/tour/)
- [첫 곡 만들기](/wiki/first-song/)
- [다운로드](/download/)
