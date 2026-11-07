# Round 180a: project save and reopen (Discussion #92), Gear Lever (issue #48)

Evidence: /mnt/e/lynx-work/save180a/ and /mnt/e/lynx-work/issues180a/. "Observed" means run here; "read" means read in source only.

## Discussion #92
The reporter's cause is NOT established: their OS, version and way of reopening are unknown.

### Observed on the published 0.1.24 Linux build (real GUI, private Xvfb, fresh profile)
Saved files were never empty: first Ctrl+S on an untitled project (10 notes, 2 clips), steps only (6 notes), the "Save changes?" prompt, saving while playing, a non-ASCII folder with spaces.
Reopening worked by Recent, File > Open and an absolute command-line path.
Reproduced empty: a relative command-line path (no message, blank project).
A read-only folder gives "Could not save". A second start runs as a second process with its own window and file.
Also found: Save as "beat.v2" wrote "beat.lynx".

### Observed on Windows (real GUI, posted messages to LYNX's own windows)
0.1.24 with the built-in file dialog: same results. Saved files were never empty (first save, while playing, the "Save changes?" prompt, a project with a sample). Absolute path, toolbar Open and a non-ASCII folder reopened fine. A relative path opened blank with no message, as a first and as a second instance. "beat.v2" wrote "beat.lynx". A second start runs as a second process.
This round's build with the native dialog: the same paths pass, a relative path opens the file (first and second instance), and "beat.v2" writes "beat.v2.lynx". Saving while playing: the transport kept moving (6:1:4 to 7:4:3), and the file got a new time, 2 clips. "Open backup" after a forced stop, fresh profile: the backup opened with its 2 clips.
Without LYNX_TEST_MODE, the relative path was blank on 0.1.24 and opened on this build, so the test mode is not what changes it. The other Windows runs used LYNX_TEST_MODE.

### Observed on this round's Linux build (private Xvfb)
A relative path as first instance opens the file. "beat.v2" writes "beat.v2.lynx". Save as "My Beat" over an existing file shows "already exists" and leaves that file alone. Recent lists the project. Through `--open-event-test`: at start, and later with a clean project, the file opens. With unsaved changes it asks, and Save on an untitled project saves it and then opens the waiting file.
Not tested: a real Explorer double-click, the native dialog on 0.1.24, Recent and Save as on Windows (the File menu needs the foreground), .flp export and reopen, OneDrive, MSIX.

### Built-in Save dialog listed no files
Observed on 0.1.24 and this round's build, Linux: the list stayed empty after a wait and a mouse wheel, while the Open dialog listed the same files. juce_FileChooser.cpp:49 empties the file filter without `canSelectFiles`, and LYNX passed `saveMode` alone.
`chooserFlags` adds it for every save dialog. Tested after the change:
- Built-in, Linux: the list fills, and an existing file gives one warning, then Overwrite writes it.
- Native, Windows: a new name saves. Overwriting, with and without the extension typed, gives one "Confirm Save As" and no LYNX alert.
- zenity 4.2.1: a new name saves, and its arguments are the same as on 0.1.24. Overwriting was inconclusive under Xvfb with no window manager.
Not tested: kdialog, the macOS save panel. Whether this relates to Discussion #92 is unknown.

### Read, not run
macOS delivers Finder double-click, Open With and Dock drop through `application:openFile:` -> `anotherInstanceStarted` (juce_MessageManager_mac.mm). LYNX did not override it and JUCE's default is empty, so the file was dropped. This is a candidate for the reporter if they use a Mac.

## Changes
`anotherInstanceStarted` (before the UI: taken by the start-up open; afterwards: asks about unsaved changes, waits for a project still loading), relative start-up paths, `withProjectExtension`, read-back after save, and `save()`'s `afterSaved`, so a "Save as" dialog doesn't lose the action that triggered it.
`--open-event-test <ms> <file>` (LYNX_TEST_MODE only) sends a file through the macOS path on any OS.

## Best way?
- **FL:** no FL REA in this round. It is bug fixing with no new UI or behaviour to match; opening a file from the OS and keeping a dotted name are baseline expectations of any application.
- **Ableton:** not studied, for the same reason.
- **Users:** the report is D#92 itself.
- **Verdict:** nothing to propose. Reopening the last project automatically at start-up would be a possible option, and it is the user's call.

## Issue #48 (Gear Lever)
Gear Lever 4.6.2's unmodified `GithubUpdater.py`, run with GTK stubbed, against the real 0.1.23 AppImage and the live API, returned True (target 0.1.24). With a simulated HTTP 403 or a network error it returned False, shown as "No updates". Read only: versions before commit 40bef0d request /releases/latest-all, which returns 404 from the live API. Which of these hit the reporter is unknown.

The user chose a fixed update address with no GitHub API. Release flags are unchanged.

### Host: lynx-share.com, not a rolling release tag
- **Rolling tag:** needs no extra host, but it adds a permanent entry to the Releases list and to `/releases`, which the in-app updater and AppImages up to 0.1.24 (`latest-all`) read. Replacing an asset is delete plus upload, so the file can be missing for a moment.
- **lynx-share.com:** a one-line 302 in `update/.htaccess` on the site to the versioned `.zsync` on the release. It changes in one step, stores no binary in git, stays under our own domain, and the AppImage stays on GitHub. Downsides: the switch runs locally because CI has no Hostinger token, and if the site is down the check reports no update until it returns.

### How it works
`package-linux.sh` embeds `zsync|https://lynx-share.com/update/LYNX-x86_64.AppImage.zsync` and writes an absolute `URL:` in the `.zsync` (a relative one would resolve against lynx-share.com). The asset name keeps matching `LYNX-*-x86_64.AppImage.zsync`, so 0.1.24 and older still find it.
`publish-appimage-update.sh <tag>` switches the address only for a published release whose public `.zsync` has the right URL, Length and SHA-1 for its AppImage and whose AppImage embeds the address. It refuses to go to an older tag, uploads only that one file, and checks the live address afterwards.
The target lives only on the site. `deploy-site.sh` leaves `website/update` out of its archive and carries the target that is live right now. It refuses to deploy if it cannot read that target, reports an error if the target changed afterwards, and prints a reminder when it is behind the newest release.

### Tests
- Gear Lever 4.6.2 `StaticFileUpdater` (unmodified, GTK stubbed), through a local 302 to a `.zsync` with the absolute GitHub URL: a copy of 0.1.23 with its update info patched returns True, and `download()` gives SHA-1 00b62ea5... (0.1.24). The real 0.1.24 file returns False. Its regex finds the embedded `zsync|` string in `readelf` output.
- appimageupdatetool (continuous build) on the same setup: update available, then `-O` updated the file by delta (8.3 MB reused, 19.3 MB fetched), SHA-1 00b62ea5...
- Live: a 302 test path on lynx-share.com through the CDN gave 302, `cache-control: no-store`, and the right file.
- The script refuses 0.1.24 (relative URL) and a tag that does not exist.
- `package-linux.sh` with the new settings: embedded `zsync|https://lynx-share.com/update/LYNX-x86_64.AppImage.zsync`, absolute `URL:`, and the `.zsync` SHA-1 equals the AppImage.
- Not tested: the real Gear Lever GUI; the script's success path, which needs a release built this way.
