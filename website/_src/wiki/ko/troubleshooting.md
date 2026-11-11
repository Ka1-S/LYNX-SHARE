---
source: a3748d4a2a
---
아래의 각 제목이 문제 하나입니다. 해당하는 문제가 없으면 끝에 설명한 대로 보고해 주세요.

## 프로그램 시작

### Windows: 처음 실행할 때 SmartScreen 경고

실행 파일에 아직 코드 서명이 없어서 처음 실행할 때 SmartScreen이 경고를 표시할 수 있습니다.

1. `More info` 항목을 클릭합니다.
2. `Run anyway` 항목을 클릭합니다.

설치 프로그램에는 관리자 권한이 필요하지 않습니다.

### macOS: "열 수 없음"

이 앱은 Apple의 공증을 받지 않았고 Developer ID 서명도 없어서 처음 실행할 때 한 단계가 더 필요합니다.

1. LYNX SHARE를 `Applications` 폴더로 끌어다 놓습니다.
2. 우클릭(Control 클릭)하고 `열기` 항목을 선택합니다.
3. `열기` 버튼을 한 번 더 클릭합니다.

macOS 15 Sequoia 이상에서는 한 번 열어 보려고 시도한 뒤 `System Settings › Privacy & Security` 항목으로 가서 `Open Anyway` 버튼을 클릭합니다. 터미널을 쓰는 방법도 있습니다.

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

처음 녹음할 때 macOS가 마이크 접근 권한을 요청합니다.

### Linux: AppImage가 시작되지 않음

LYNX SHARE에는 glibc 2.35 이상의 x86_64 시스템(Ubuntu 22.04, Debian 12, Fedora 36, 최신 Arch 또는 Manjaro)과 X11 또는 XWayland가 필요합니다.

1. 아래 두 명령으로 파일을 실행 가능하게 만들고 실행합니다.
2. 그래도 시작되지 않으면 libfuse2를 설치합니다. 패키지 이름은 Ubuntu 22.04에서 `libfuse2`, Ubuntu 24.04에서 `libfuse2t64`, Arch에서 `fuse2`, Fedora에서 `fuse-libs`입니다.
3. 설치하고 싶지 않다면 `.tar.gz`를 다운로드해서 풀고 `./install.sh`를 실행합니다.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh`는 현재 사용자의 `~/.local` 아래에 메뉴 항목과 함께 LYNX SHARE를 설치합니다. `./install.sh --uninstall`은 제거합니다.

## 사운드

### 소리가 나지 않거나 오디오 장치가 잘못됨

1. `옵션 › 오디오 설정...` 메뉴를 엽니다.
2. 드라이버를 고릅니다.
3. 출력 장치를 고릅니다.

| 시스템 | 드라이버 |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO, 내장 LYNX ASIO 드라이버 |
| macOS | Core Audio |
| Linux | PipeWire(네이티브), JACK, ALSA. 처음 시작하면 LYNX SHARE는 이 중 장치가 있는 첫 번째 드라이버를 엽니다 |

ASIO는 Windows 드라이버 모델이어서 Windows에만 있습니다. `옵션 › 설정 마법사(언어, 오디오, 플러그인 폴더)...` 메뉴에서도 오디오 장치를 테스트합니다. [오디오 설정](/wiki/audio-setup/)을 참고하세요.

### Linux: 소리가 끊김

오디오 스레드는 실시간 우선순위로 실행되어야 합니다.

1. `옵션 › 오디오 설정...` 메뉴를 열고 `상태` 항목을 읽습니다. 오디오 스레드가 실시간 우선순위를 받았는지 표시됩니다.
2. 받지 못했다면 rtkit를 설치하거나(`sudo apt install rtkit`, `sudo dnf install rtkit`, `sudo pacman -S rtkit`), JACK 환경처럼 실시간 제한이 설정된 `오디오` 그룹에 사용자를 추가합니다.
3. 로그아웃했다가 다시 로그인합니다.

자세한 내용은 [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)에 있습니다.

## 플러그인

### 플러그인이 충돌함

플러그인만 멈추도록 플러그인을 자체 프로세스에서 실행합니다.

1. `옵션 › 일반 설정...` 메뉴를 엽니다.
2. `플러그인을 별도 프로세스에서 실행(충돌로부터 LYNX SHARE 보호)` 옵션을 켭니다.
3. 프로젝트를 다시 엽니다.

자체 프로세스에서 충돌했거나 응답을 멈춘 플러그인은 뮤트됩니다. 프로젝트를 저장하고 다시 열면 플러그인이 다시 로드됩니다. [서드파티 플러그인](/wiki/plugins/)을 참고하세요.

### 플러그인이 목록에 없음

1. `옵션 › 플러그인 스캔` 메뉴를 선택합니다.
2. 그래도 없으면 `옵션 › 플러그인 관리자...` 메뉴를 열고 `플러그인 폴더...` 버튼을 클릭해 플러그인의 폴더를 추가합니다.
3. `실패` 탭을 봅니다. 스캔에 실패한 플러그인 파일은 건너뜁니다. Windows에서는 이유가 `%APPDATA%\LYNX\scan-failures.log`에 있습니다.
4. `모두 다시 스캔...` 버튼을 클릭하면 실패한 파일을 다시 시도합니다.

### 프로젝트가 설치되지 않은 플러그인을 사용함

이펙트는 같은 역할을 하는 LYNX SHARE 이펙트로 대체되고, 설정은 프로젝트에 유지됩니다. `도움말 › 누락된 파일...` 메뉴에는 누락된 항목이 나열됩니다. [내장 악기와 이펙트](/wiki/built-in/)를 참고하세요.

### Linux에서 비디오가 동작하지 않음

비디오 트랙과 `Export with video` 옵션에는 FFmpeg가 필요합니다. LYNX SHARE가 설치 방법을 알려 줍니다.

## 충돌 보고서

![다음 시작 때 표시되는 충돌 보고서 질문: Send, Show what will be sent, Don't send](/assets/img/wiki/crash-report.webp)

충돌 보고서는 사용자가 선택해야 보내집니다. 충돌이나 멈춤이 있고 나면 다음에 시작할 때 보고서를 보낼지 묻습니다.

| 명령 | 동작 |
|---|---|
| `전송할 내용 보기` | 전송될 텍스트를 그대로 보여 줍니다 |
| `대신 GitHub에 보고` | 미리 채워진 GitHub 이슈 양식을 엽니다. 파일은 직접 첨부합니다 |
| `보고서 폴더 열기` | 컴퓨터에 있는 보고서 파일을 보여 줍니다 |
| `Include file names (projects, samples)` | 끄면 각 파일의 확장자만 전송됩니다 |
| `묻지 않고 항상 보내기` | 앞으로의 보고서를 묻지 않고 보냅니다 |

`옵션 › 일반 설정...` 메뉴의 `충돌 보고서` 옵션은 `Ask`(기본값), `항상 보내기`, `보내지 않음` 중에서 정합니다.

보고서에는 LYNX SHARE 버전, 운영 체제, CPU, 그래픽 카드와 드라이버, 화면 크기, 오디오 드라이버와 장치, 충돌 또는 멈춤 파일, 세션의 마지막 60단계, 로드된 서드파티 플러그인의 이름과 버전이 들어 있습니다.

오디오, 프로젝트, 샘플, 컴퓨터 이름, 사용자 이름은 들어 있지 않습니다. 서버는 IP 주소를 저장하지 않습니다. 전송된 ID는 설정 폴더의 `crash-reports\sent.log`에 나열됩니다. 보고서를 삭제하고 싶다면 그 ID를 적어 이슈를 여세요. 전체 내용은 [개인정보 처리방침](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md)에 있습니다.

## 문제 보고

![Report a problem 창: 설명 상자, 선택 사항인 이메일 주소, Send 버튼](/assets/img/wiki/report-problem.webp)

1. `도움말 › 문제 보고...` 메뉴를 선택합니다.
2. 무슨 일이 있었는지 설명합니다. 이메일 주소는 선택 사항이며 답변을 받고 싶을 때만 적습니다.
3. 문제가 충돌이었다면 `최신 충돌 / 멈춤 파일과 이번 세션의 단계 첨부` 옵션을 체크한 채로 둡니다.
4. `전송할 내용 보기` 버튼을 클릭해 텍스트를 확인합니다.
5. `보내기` 버튼을 클릭합니다. 그 전에는 아무것도 전송되지 않습니다.

[github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues)에서 이슈를 열 수도 있습니다.

## 알아 두면 좋은 점

- 충돌 후에는 프로젝트의 백업 사본이 LYNX SHARE의 자동 저장 폴더에 있습니다.
- 충돌에 플러그인이 관련되어 있으면, 질문에서 앞으로 그 플러그인을 별도 프로세스에서 실행하도록 제안합니다.
- 설정 폴더는 Windows에서 `%APPDATA%\LYNX`, macOS에서 `~/Library/Application Support/LYNX`, Linux에서 `~/.config/LYNX`입니다.

## 관련 문서

- [오디오 설정](/wiki/audio-setup/)
- [서드파티 플러그인](/wiki/plugins/)
- [설치](/wiki/install/)
- [FAQ](/faq/)
- [충돌 방지](/why/#crash)
