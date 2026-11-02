# Round 167v — first-run window layout (v0.1.22)

User report (relayed by the owner, pt-BR): "quem baixa o LYNX SHARE, a tela inicial abre com o Channel Rack em tela
cheia, meio bagunçado. Não tem como abrir na Playlist, todo organizado? Um padrão de primeira."
("whoever downloads LYNX SHARE: the first screen opens with the Channel Rack full screen, kind of messy. Can't it open
on the Playlist, all organised? A proper first-run default.")

## 1. Reproduction (0.1.21 release zip, fresh LYNX_PROFILE_DIR, Windows 1920x1080 at 100 %)

Shots in `E:\lynx-work\firstrun167v\shots\` (`/mnt/e/lynx-work/firstrun167v/shots/`), script
`C:\Users\kauan\lynx-test\firstrun167v\run.ps1` (+ `/mnt/e/lynx-work/firstrun167v/gui.sh`, `gui2.sh`).

| run | what we saw |
|---|---|
| `before-test-start.png` (test mode, 1705x947 window = 88 % of the work area) | Browser left, Playlist, Channel Rack floating at x≈520, 720 px wide: fine on this screen |
| `before-test-1366x728.png` (window 1366x728) | the rack (690 px) covers most of the Playlist's arrangement |
| `before-notest-run1-start.png` (normal mode, first start) | setup wizard over the desktop; same layout behind it |
| `before-notest-run2-start.png` (normal mode, second start, same profile) | **the Browser covers the Playlist's pattern picker** ("ist" title, picker cut): the saved layout is broken |

Causes:
1. **Small / scaled desktops.** The main window opens at 88 % of the work area and is not maximized. At 125 %/150 %
   Windows scaling (most laptops: 1366x768 @125 % = 1093x614 logical, 1920x1080 @150 % = 1280x720 logical) the
   workspace is ~945–1130 px wide. `Layout::makeDefault` gave the rack min(720, desktop − Browser − 32) px and slid it
   left over the Playlist's track names when it did not fit: at 945 px the rack ran from x=236 to x=929, i.e. it
   covered the whole Playlist (track names and arrangement) — the "Channel Rack em tela cheia".
2. **Saved layout skew.** The workspace is built at the component's design size (1180x642) and then sized to the
   screen; a saved layout was parsed at the first size (minimum widths clamped there) and then *scaled*, and any
   scaling of a non-pristine layout clamped the Browser to its 220 px minimum while the Playlist's left edge scaled
   on. The result (saved again at exit) has the Browser wider than where the Playlist starts (0.1865 vs 0.1545 of the
   width in the shot's `workspace.txt`), so every later start shows the Browser over the Playlist's pattern picker.

## 2. FL Studio's default arrangement (evidence)

- FL 2026 has **View › Arrange windows › Desktop (default)** (also Desktop (alternate), Tablet (default/alternate),
  custom layouts): menu strings `Arrange windows`, `Desktop (default)`, `Desktop (alternate)`, `&Save custom layout`
  in FLEngine_x64.dll; actions `SetDefaultDesktopLayoutAction`, `SetAlternateDesktopLayoutAction`,
  `SetCustomLayoutAction`, `SaveCustomLayoutAction`, `RealignWindowsAction` [STR]. WhatsNew.htm: "11627 View > Arrange
  windows > Desktop (default) doesn't work correctly when scaling is at 150%".
- `SetDefaultDesktopLayoutActionExecute` (published method → code **0xF1BE40**) calls the arrange routine
  **0x11FAB80** twice (modes 3 and 0) [DECOMP, `tmp/firstrun167v/d1.c`]. What it does:
  - one window is moved to the desktop's top-left corner (0, 0) keeping its size (0x103AA50 = SetBounds) — by role
    the Browser [inferred: FL docks the Browser at the left];
  - the window at global 0x16BA808 gets SetBounds(x = min(that window's right edge, …), 0, desktop width − x, desktop
    height): the background window filling everything right of it at full height — by role the Playlist [inferred];
  - every other open, undocked floating window gets a restored size of **2/3 of the desktop width × 3/4 of its
    height** (0x85C3C0 / 0x85C400, loop over the form list at 0x16BA7E8);
  - the window at 0x16BD450 is placed at left = 16·scale, top = desktop height − its height − 32·scale, width =
    desktop width − 32·scale (a full-width strip at the bottom).
  The globals have no symbols and their forms were not resolved within this round (0x16BA808 is written by the
  generic plugin-form creator 0xE34450; read-only use of round 167t's analysed Ghidra project). Not driven live:
  applying an FL layout changes the user's FL window arrangement, which the rules forbid. LYNX already had this
  arrangement (Browser left, Playlist filling the rest, Channel Rack floating, Mixer and Piano Roll closed); what
  was wrong is how it scaled, below.
- FL's main form (`TFruityLoopsMainForm` DFM) is `BorderStyle = bsNone`: FL draws its own caption and fills the
  screen; LYNX keeps a native title bar.

## 3. What LYNX does now

| item | before | now |
|---|---|---|
| main window, first start | 88 % of the work area, centred, never remembered | **maximized** on the first start (no `main-window.txt`), so the Playlist gets the screen; its place and maximized state are remembered (`main-window.txt` in the settings folder). LYNX_TEST_MODE keeps the fixed-size window scripted checks expect (`LYNX_TEST_MAXIMIZE=1` tests the first start) |
| Channel Rack in the default layout (first start, View › Reset layout) | min(720, desktop − Browser − 32) wide; on a small desktop slid over the track names | to the right of the Playlist's pattern picker and track names whenever at least 420 px fit there (its steps scroll sideways), at most 720 px; only on a desktop under ~900 px does it move left; `fitChannelRack` never widens it past the desktop's right edge |
| Browser + Playlist | scaled separately; the Browser's minimum width pushed it over the Playlist | the Playlist starts where the Browser ends after any resize (`Layout::browserDocksPlaylist` / `redockPlaylist`) |
| a restored layout | parsed at the design size, then scaled (clamps compounded) | re-read from the saved text for every new window size until a panel is moved by hand |
| an already broken `workspace.txt` (Browser over the Playlist) | kept broken forever | repaired when read (the Playlist starts where the Browser ends); nothing else in a saved layout changes |

Existing users: their saved layout is used as before; the only change to it is the repair above when the Browser
overlaps the Playlist. Users without `main-window.txt` (everyone updating from 0.1.21) get the maximized window once,
then whatever they leave it as.

## 4. Tests

`tests/FirstRun167vTests.inc`: default layout at 945…2560 px desktops (rack beside the track names, inside the
desktop, 420–720 px), Browser/Playlist docking across resizes (and an undocked Playlist left alone), the 0.1.21
broken `workspace.txt` repaired at two sizes, a sound layout read back unchanged.

## Where I stopped

- done: reproduction (0.1.21 zip), FL REA (0xF1BE40 / 0x11FAB80), core + UI changes, tests (standalone run: 48 + 83
  checks pass).
- in progress: Windows build with the change (`OUT=build-win-firstrun`), GUI check before/after at 1920x1080 and
  1366x728 / 945x440 window sizes.
- next: remove the temporary `ltrace` (LYNX_LAYOUT_TRACE) in src/ui/Workspace.cpp, build-local, notes, land.
- rerun: `/mnt/e/lynx-work/firstrun167v/gui.sh <exe win path> <tag> <test 0|1> [WxH,…]`,
  `/mnt/e/lynx-work/firstrun167v/gui2.sh <exe> <tag> <test>` (two starts on one profile).
