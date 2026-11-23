# Release steps

Official release files come from GitHub Actions (`.github/workflows/release.yml`, see `docs/CODE_SIGNING.md`):
a tag push builds `LYNX.exe` from the tagged source, runs the self-test on Windows and attaches to a draft release
(titled "LYNX SHARE x.y.z"; the product is named LYNX SHARE since v0.1.20, while the file names below, `LYNX.exe` and the
settings folders keep "LYNX" so existing installs, links and AppImage updates keep working):

- `LYNX-x.y.z-setup-x64.exe` — per-user installer (NSIS, `resources/installer/LYNX.nsi`, no admin rights): installs to
  `%LOCALAPPDATA%\Programs\LYNX`, Start-menu shortcut, optional desktop shortcut, `.lynx` association, optional
  "Open with LYNX SHARE" for `.flp` (FL Studio stays the default program), uninstaller in *Apps & features*. The workflow
  also installs and uninstalls it silently as a check.
- `LYNX-x.y.z-windows-x64.zip` — portable build (extract and run), with `Demo.lynx`.
- `LYNX-x.y.z-x86_64.AppImage` — Linux, one file (no "linux" in the name: AppImage naming rule; embeds update information for a fixed address, `https://lynx-share.com/update/LYNX-x86_64.AppImage.zsync`, with its `.zsync` next to it. After publishing a release, run `scripts/publish-appimage-update.sh v<label>`: until then Linux update checks keep offering the previous release. The target lives only on the site, never in `website/`, and `scripts/deploy-site.sh` keeps it and reminds you when it is behind; the AppImage
  catalog warns otherwise). Built in an Ubuntu 22.04 container, so it needs only glibc 2.35; the job fails when
  `scripts/check-linux-compat.sh` finds a newer glibc symbol or a dependency on the system's libstdc++, or when
  `scripts/check-appimage-start.sh` sees it quit within 12 s offline. Carries AppStream metadata
  (`resources/linux/com.lynx_share.LYNX.appdata.xml`).
- `LYNX-x.y.z-linux-x86_64.tar.gz` — Linux folder with `install.sh` (per-user install).
- `LYNX-x.y.z-macos-universal.dmg` — macOS, ad-hoc signed.
- `LYNX-x.y.z-store.msix` — the Microsoft Store package (unsigned; the Store signs it), submitted by hand in Partner
  Center or by the workflow's Store job (`docs/STORE.md` › *Automatic Store submission*). The workflow installs a test-identity copy of it and runs LYNX SHARE inside the package (`docs/STORE.md`).
- `LYNX-x.y.z-sbom.spdx.json` — SPDX 2.3 software bill of materials of the third-party components (round 190nr), made by
  `scripts/make-sbom.sh` from THIRD_PARTY.md and the packages' `licenses/` folder; the script stops when a THIRD_PARTY.md
  row or a licence file has no SBOM entry.
- `LYNX.exe` and `SHA256SUMS.txt` (SHA-256 of every file above).
- The optional plugin download (round 188b, `.github/workflows/plugins.yml`, docs/PLUGIN_EXPORT.md): `LYNX-Plugins-x.y.z-win-x64.zip` and `LYNX-Plugins-x.y.z-win-x64.7z` (the same files, about 10 times smaller), `LYNX-Plugins-x.y.z-linux-x86_64.tar.xz`, `LYNX-Plugins-x.y.z-macos-universal.tar.xz` and their own `LYNX-Plugins-x.y.z-SHA256SUMS.txt`. Attached to the draft release by the `plugins-publish` job; not in `SHA256SUMS.txt`, and never picked by the updater.

`workflow_dispatch` (Actions → Release → Run workflow, or `gh workflow run release.yml --ref <branch>`) builds and
checks everything without a tag and keeps the files as workflow artifacts only.

Steps:
1. Bump the version in `CMakeLists.txt` (`project(LYNX VERSION x.y.z)`, the single source) and the download badge in
   `README.md`.
2. Locally: `scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"` → `0 failures`;
   `flock ~/.cache/lynx-winbuild.lock scripts/build-windows-zig.sh`; from a Windows folder `LYNX.exe --selftest r.txt`
   (`result=OK`) and `LYNX.exe --scantest <folder with a VST3/CLAP> r.txt` (a test CLAP: `tools/testplugin`).
2b. Performance (every release; LYNX must stay faster and lighter than FL Studio): on a quiet machine (no builds),
   `FL=1 OPENTIME=1 LOAD_MAX=6 CORES=7-11 scripts/perf-gate.sh compare` with the Linux app and perfbench built
   (`build-linux/`). It measures the synthetic big projects of `core/PerfProjects.h` (synth100, inserts50, stream,
   big200: 200 channels / 120 inserts / 2,000 clips / 200 automation clips, automation: 400 dense automation clips)
   and FL Studio's Thief and Exoplanet plus Live's Patience (from the local installs, never committed): audio CPU at
   64 / 128 / 256 samples, import / build / rebuild time and peak memory (`tools/perfbench --gate`), `.lynx` save and
   load (`LYNX --perfgen`), open time, memory and paint per frame of every view (`LYNX --paintbench`) and the start-up
   with the project (`LYNX --opentime`), against `tests/perf/baseline-local.json`; any regression past its threshold
   fails (`scripts/perf_gate.py` prints the table). CI runs the synthetic part on every push (*Tests* › App ubuntu ›
   *Performance gate*, `tests/perf/baseline-ci-ubuntu.json`). Then the Windows comparison with FL Studio and Live:
   `scripts/perf-vs-fl.sh startup|open|play` (start-up, project open, RAM, playback CPU / fps; `scripts/perf-vs-fl-sum.py`).
   A baseline changes only for a reviewed, intended change: `PROFILE=<name> scripts/perf-gate.sh update`, committed
   with the reason. Paint cells: where the CPU counter can be read (Linux), `--paintbench` also counts the instructions
   per frame, and a cell whose instructions the run and the baseline both have is gated on them (5 %), its time only
   reported (the time divided by the calibration moves with the runner's memory system); add a CPU model's counts with
   `PERF_GATE_ONLY_METRICS=paint_instr scripts/perf_gate.py update <results> <baseline>` (every other number stays).
   Wall-clock cells (since 0.1.31): the audio CPU share is held to its instructions per sample and the edit snapshot
   (median of 12 edits) to its instruction count the same way, so a slower time whose count stayed in its limit is
   reported, not failed. A time failure counts when its instruction cell moved too, or when the interleaved A/B against
   main confirms it: build main's perfbench, then `BASE_PERFBENCH=<main's perfbench> FL=1 CORES=7-11 scripts/perf-gate.sh
   ab` (main and this build alternately, at least 5 rounds, median against median, `summary-ab.md`).
   A UI change (a round, before it lands): `scripts/paint-gate.sh <LYNX before> <LYNX after>` on FL's Thief AND Live's
   Patience (an .flp and an .als: the FL and the Live paths of every view; since round 190pb, after a design round gated
   on Thief alone let Patience's Channel rack and Clip-launch arrangement get 44 % and 72 % dearer), instruction counts per
   view and frame kind, 1 % per cell, whole-frame pixels equal unless the change means them to differ (`PIXELS=any`).
3. Optional local package check: `scripts/make-installer.sh` (defaults to `build-win/LYNX.exe`) writes the installer,
   the zip and `SHA256SUMS.txt` to `dist/`. NSIS: `makensis` on PATH, `$MAKENSIS`, or the NSIS zip release unpacked
   into `~/opt/nsis-<version>` (its `makensis.exe` runs through WSL interop). Quick installer check:
   `setup.exe /S /D=C:\some\test\folder`, then `C:\some\test\folder\Uninstall.exe /S`.
4. `git tag vx.y.z-alpha && git push origin vx.y.z-alpha`; when the workflow finishes, edit the draft release notes
   and publish it.
5. Microsoft Store: with the `MSSTORE_*` secrets set, the workflow's *Microsoft Store submission* job has already sent
   `LYNX-x.y.z-alpha-store.msix` as a new submission (`docs/STORE.md` › *Automatic Store submission*); without them,
   upload it by hand (`docs/STORE.md` › *Updating the Store version*).
