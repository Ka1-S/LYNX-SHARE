---
source: 8a120ab7de
---
모든 릴리스에는 [릴리스 페이지](https://github.com/Ka1-S/LYNX-SHARE/releases)에 Windows, macOS, Linux용 빌드가 있습니다. 파일은 태그가 붙은 소스에서 GitHub Actions로 빌드되며, SHA-256 값과 함께 `SHA256SUMS.txt`에 나열됩니다. [다운로드](/download/) 페이지에서 최신 파일로 연결됩니다.

## Windows에 설치

Windows 10 또는 11, 64비트.

1. `LYNX-<version>-setup-x64.exe`를 다운로드합니다.
2. 실행합니다. 현재 사용자에게만 설치되며 관리자 권한이 필요하지 않습니다.
3. 실행 파일에 아직 코드 서명이 없어서 SmartScreen이 경고를 표시할 수 있습니다. `More info` 항목을 선택한 다음 `Run anyway` 항목을 선택합니다.
4. 시작 메뉴나 바탕 화면 바로 가기에서 LYNX SHARE를 실행합니다.

설치 프로그램은 LYNX SHARE를 `.lynx` 프로젝트를 여는 프로그램으로 지정합니다. `.flp` 파일에는 FL Studio의 기본 프로그램을 바꾸지 않고 *연결 프로그램* 목록에 LYNX SHARE를 추가합니다.

다른 방법이 두 가지 있습니다.

| 방법 | 파일 또는 위치 | 동작 |
|---|---|---|
| 포터블 zip | `LYNX-<version>-windows-x64.zip` | 아무 곳에나 풀고 `LYNX.exe`를 실행합니다. 설치되는 것은 없습니다. |
| Microsoft Store | [Store의 LYNX SHARE](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Store가 설치하고 업데이트를 관리합니다. |

## macOS에 설치

macOS 11 Big Sur 이상. 유니버설 `LYNX-<version>-macos-universal.dmg` 하나로 Intel과 Apple 실리콘을 모두 지원합니다.

1. `.dmg`를 열고 LYNX SHARE를 *응용 프로그램* 폴더로 끌어다 놓습니다.
2. 이 앱은 Apple의 공증을 받지 않았고 Developer ID 서명도 없어서 처음 실행할 때 한 단계가 더 필요합니다. *응용 프로그램* 폴더에서 LYNX SHARE를 우클릭(또는 Control 클릭)하고 *열기* 를 선택한 다음 *열기* 를 한 번 더 선택합니다.
3. macOS 15 Sequoia 이상에서는 한 번 열어 보려고 시도한 뒤 *시스템 설정 › 개인정보 보호 및 보안* 으로 가서 *그래도 열기* 를 클릭합니다.

터미널을 쓰는 방법:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

처음 녹음할 때 macOS가 마이크 접근 권한을 요청합니다.

## Linux에 설치

glibc 2.35 이상의 x86_64(Ubuntu 22.04+, Debian 12+, Fedora 36+, 최신 Arch 또는 Manjaro). LYNX SHARE에는 X11 또는 XWayland가 필요합니다. 오디오는 PipeWire, JACK, ALSA를 거치며 이를 위해 따로 설치할 것은 없습니다.

AppImage(`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

일부 배포판에서는 AppImage를 실행하려면 libfuse2가 필요합니다(Ubuntu 22.04는 `libfuse2`, 24.04는 `libfuse2t64`, Arch는 `fuse2`, Fedora는 `fuse-libs`). 처음 시작할 때 설정 마법사가 LYNX SHARE를 애플리케이션 메뉴에 추가하고 `.lynx` 파일을 이 프로그램으로 열도록 제안합니다. 나중에 `옵션 › 일반 설정...` 메뉴에서 바꾸거나, 터미널에서 `./LYNX-*.AppImage --menu-entry on`(또는 `꺼짐`)으로 바꿀 수 있습니다.

libfuse2를 설치하고 싶지 않다면 `LYNX-<version>-linux-x86_64.tar.gz`를 사용합니다. 풀어서 `./install.sh`를 실행하세요. 현재 사용자의 `~/.local` 아래에 메뉴 항목, 아이콘, `.lynx` 파일 형식과 함께 LYNX SHARE가 설치됩니다.

## 설정이 저장되는 위치

| 시스템 | 설정 폴더 |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (또는 `$XDG_CONFIG_HOME` 아래) |

이 폴더에는 옵션, 선택한 언어(`language.txt`), 단축키(`keys.json`), 플러그인 폴더(`plugin-folders.txt`), 스캔한 플러그인 목록(`plugins.xml`)이 들어 있습니다.

## 업데이트

LYNX SHARE는 시작할 때 GitHub에 최신 버전 번호를 묻습니다. 사용자에 관한 정보는 전혀 전송되지 않습니다. 새 버전이 나오면 막대에 `LYNX SHARE <version> is available` 문구와 `새로운 기능` 버튼, 업데이트 버튼이 표시됩니다. `도움말 › 업데이트 확인...` 메뉴는 언제든 확인합니다.

| 빌드 | 업데이트 방법 |
|---|---|
| Windows 설치 프로그램 | `업데이트` 버튼이 새 설치 프로그램을 다운로드하고, 저장하지 않은 변경 사항을 묻고, 설치한 뒤 다시 시작합니다 |
| Windows 포터블 zip, macOS | `업데이트` 버튼이 새 파일을 다운로드하고 그 폴더에서 보여 줍니다 |
| Linux AppImage | `업데이트` 버튼이 AppImage 파일을 제자리에서 업데이트합니다. Gear Lever와 AppImageUpdate로 밖에서 같은 일을 할 수도 있습니다 |
| Linux `.tar.gz` | `다운로드...` 버튼이 다운로드 페이지를 엽니다 |
| Microsoft Store | Store가 업데이트합니다. 이 빌드에는 자체 업데이트 확인이 없습니다 |

확인을 끄려면 `옵션 › 일반 설정...` 메뉴를 열고 `자동으로 업데이트 확인` 옵션의 체크를 해제합니다.

## 제거

- Windows 설치 프로그램: Windows의 *앱 및 기능* 목록에서 LYNX SHARE를 제거합니다.
- 포터블 zip: 푼 폴더를 삭제합니다.
- Linux `.tar.gz`: `./install.sh --uninstall`을 실행합니다.

## 알아 두면 좋은 점

- 프로젝트의 코드 서명 신청이 승인될 때까지 Windows 빌드에는 서명이 없습니다. macOS 앱에는 임시(ad-hoc) 서명만 있습니다. Linux 패키지에는 서명이 없습니다.
- 다운로드한 파일을 확인하려면 SHA-256 값을 릴리스 페이지의 `SHA256SUMS.txt`에 있는 줄과 비교합니다.
- 설치 프로그램, zip, `.dmg`, `.tar.gz`에는 데모 프로젝트 `Demo.lynx`가 들어 있습니다.
- 업데이트 막대를 닫기 버튼으로 닫으면 그 버전은 건너뜁니다.

## 관련 문서

- [여기서 시작](/wiki/getting-started/)
- [오디오 설정](/wiki/audio-setup/)
- [문제 해결](/wiki/troubleshooting/)
- [다운로드](/download/)
- [업데이트](/updates/)
