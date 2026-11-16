# REA round 190jf: B17 folder groups (Playlist + Mixer) and B22 DAWproject

Static evidence (no FL / Live session was driven in this round): the FL Studio online manual (playlist.htm, fetched
2026-11-16), FL 2026's change log (`Help/WhatsNew.htm` of the installed FL Studio 2026), strings of `FLEngine_x64.dll`
(`/mnt/e/lynx-work/community190hq/rea/flengine-strings.txt`), earlier REA rounds (104, 168f, 186c, 190ft, 190iu), the
FL MIDI-scripting API stubs, and the DAWproject repository (B22, §2). Confidence: [BIN] binary / change log, [MAN] manual
only, [INF] inferred.

## 1. B17 — Playlist folders

### What FL has
| Feature | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Groups ("Group with above track", one level, header + grouped tracks, collapse arrow, Auto color group, Collapse / Expand all) | REA 104, 168f [BIN]; manual: "Makes the Playlist track a sub-track of the one above it" [MAN] | yes (rounds 104, 168f) | unchanged (nest-aware) |
| Group mute: **Alt+click** the mute LED of any track of a group toggles mute for the grouped tracks | manual › Mute / Solo: "(Alt/Opt+Click) the Mute icon for any member of the group", "(Alt + Left click) Toggles mute for the grouped tracks" [MAN]; change log 17106 "MIDI scripting: added an inGroup parameter to playlist.muteTrack" [BIN] | **missing** (header mute greys only the header, REA 104) | `noteedit::muteGroup`: every track of the group (header included) takes the clicked track's flipped state |
| Group solo: **Alt+right-click** the LED | manual: "(Alt + Right click) Toggles solo for the grouped tracks" [MAN]; 16358 "scripts can now solo track groups in the playlist", 11573 "Collapsed grouped tracks in the playlist are muted when soloing the parent track with Alt+Right-click" (fixed: they play) [BIN] | **missing** | `noteedit::soloGroup`: the group plays, every other track is muted; on a group already alone everything plays (or, with General › "Restore previous state after solo", what played before), as the track solo |
| **View › "Hide collapsed grouped tracks"** | manual (Playlist menu › View, between "Resize all tracks" and "Show fade previews"): "Collapsed grouped tracks will be hidden"; DLL `EEHideCollapsedAction`, `HideCollapsedSubTracks`, "Hiding collapsed tracks\|This option hides collapsed Playlist tracks. The parent track will show a down arrow icon to open hidden tracks, and will reflect their activity in its own activity meter. Proceed?" [BIN] | **missing** (members always 7 px rows) | Playlist menu › View › "Hide collapsed grouped tracks" (saved with the UI options), FL's question before it switches on; the members of a collapsed group get no row |
| Reorder: Shift+wheel over a header, Move up / down; grouping by dragging a row's bottom edge up past the top | manual [MAN], REA 190ft [BIN] | yes | unchanged |
| Nested groups | none: FL's groups have one level (manual calls grouped tracks "nested" only in that sense; IL forum wishlists p=2071198, p=2067040 ask for "groups in groups") | — | LYNX extra, see below |

Not done from FL: the parent's activity meter for hidden tracks; the "Hidden clips|Some of the clips you are about to %s
are on hidden tracks. Proceed?" question (LYNX never edits clips of hidden rows by a box selection: they have no row).

### What LYNX adds (OD1 → B17, recommended option taken: folders that fold the Playlist and the Mixer together)
- **Nested folders** (`PlaylistTrack::nest`): a grouped track's depth is `1 + nest`; it belongs to the nearest track above
  with a smaller depth. Every existing group function became nest-aware and is unchanged for one level
  (`folders190jf_flat_groups_behave_as_before`). Track menu (grouped tracks only, after FL's Ungroup): "Nest under the
  track above (a group in the group)", and for nested tracks "Move out of its group (one level up)". A nested header has
  its own fold arrow; its folder's block is drawn from its indent.
- **Alt+drag a track header** (LYNX; FL's Alt+press on the header is a plain click, its drag does nothing): moves the
  track with its folder. Dropped among a folder's tracks it joins that folder; after a folder's last track it stays
  outside; dragging 24 px right / left = one level deeper / out. A red line + a frame round the folder show where.
  One undo step. `core/FolderDrag.h`, `noteedit::moveFolder`.
- **Group bus for any group** ("routed to a group bus if the user wants"): the track menu's "Group plays through its own
  mixer track" (round 190cg, Clip-launch only until now) is offered for every group; for a nested folder the innermost
  folder's members sum into its header's insert, and a sub-folder with its own insert plays into the outer bus through
  it (`arrange::busMembers`).
- **Mixer**: "Mixer groups follow the Playlist" (round 186c) folds / separates the inserts of each top-level folder
  (mixer groups have one level: a nested folder's inserts are part of the outer mixer group).
- **Saved**: `.lynx` key `playlistTrackNest` ([index, nest]) beside the old `playlistTrackGroups` rows, so an older LYNX
  still opens the groups one level deep. `.flp` export flattens the nesting (FL's one level). **`.als` import** keeps
  Live's nested group tracks (TrackGroupId chain) and their fold state.

### Best way?
- **FL:** one level, Alt+click group mute / solo, Hide collapsed grouped tracks.
- **Live 12:** group tracks nest, a group is always a bus, folding hides the members (no thin rows); users complain that
  nested group headers cannot collapse in some views (F1808154, F1820232).
- **Users:** "groups in groups" (FL wishlists), fewer clicks to move tracks into groups.
- **Verdict:** FL-exact by default (one level unless the user nests; FL's Alt gestures; Hide option off); nesting, the
  Alt+drag and the bus for every group are LYNX additions that change nothing until used.

## 2. B22 — DAWproject

- **Format and licence:** DAWproject by Bitwig and PreSonus, https://github.com/bitwig/dawproject — `Project.xsd`,
  `MetaData.xsd`, the README and a Java reference library. **MIT licence** ("Copyright (c) 2020 Bitwig", checked in the
  repository's LICENSE). LYNX's reader / writer is clean-room from the XSDs and the README (no Java code taken); the
  README's example project is a test fixture with the MIT notice (`tests/data/dawproject/BitwigReadmeProject.inc`). The
  repository ships no example `.dawproject` file (only `test-data/white-glasses.wav`).
- **Programs that read / write it:** Bitwig Studio, Studio One, Cubase / Nuendo, Cubasis, VST Live, n-Track (the README's
  list). FL Studio and Live do not.
- **Container:** ZIP with `project.xml`, `metadata.xml`, `audio/…`, `plugins/…` (`core/ZipLite.h`: stored + deflate read,
  CRC, ZIP64; stored write with a fixed date, so the output is deterministic).
- **What maps** (both directions unless said): tracks with notes / audio / automation, folder tracks (nested, → LYNX
  folders of §1), channel roles master / effect / submix, volume (linear / dB), pan, mute, solo, sends (pre / post),
  VST3 (`.vstpreset` Comp / Cont, byte-exact), VST2 (`.fxp` / `.fxb`), CLAP (raw state stream — assumption), LYNX's own
  devices (`deviceVendor="LYNX"`, the project file's JSON of the channel / effect, so LYNX → DAWproject → LYNX keeps every
  field), clips (time, duration, playStart, loops unrolled, fades, nested clips), warps (exact), automation points on
  volume / pan / send / device parameter (lanes under the row), tempo map (tempo markers; the clip goes back to its row),
  time signature and its automation, markers, metadata (title, artist, genre, website, comment). Import only: built-in
  Compressor / Limiter / NoiseGate / Equalizer → Crest / Apex / Sentry / Facet EQ (approximated, reported), scenes /
  clip slots → the clip grid.
- **What does not map** (always listed: the export's notes, the import's report):
  - Import: note effects, analyzers, unknown built-in devices, a second instrument, Mute / Enabled / built-in device
    parameter automation, MIDI and per-note expressions, notes' MIDI channels, send pan, disabled sends, marker colours,
    VCA role (an ordinary insert), other metadata, external plug-in states.
  - Export: the clip grid, other arrangements, song loop, VCAs, Pattern Blocks, patterns not on the Playlist, muted notes,
    note pan / fine pitch / slide / porta / Mod X/Y / chance, clip gain / pan / pitch / reverse / envelopes, automation of
    channel volume / pan, effect mix, VCA, level adjustment or route to the Master, automation curves (straight lines),
    clip LFOs, controller links, insert-to-Master levels, sends to deleted inserts, effect side-chains, arpeggiators,
    inserts no track plays through.
  - Structural: a channel's own volume / pan folds into its track's Channel; inserts several tracks share become submix
    tracks, so a re-import gives each track its own insert; automation clips come back merged per target (as lanes).
- **Best way?** FL and Live have no DAWproject. Users moving between Bitwig / Studio One / Cubase and FL-style workflows
  ask for an open exchange format (pain point #46). Verdict: a LYNX-own import / export next to .flp / .als / .flm, with
  the not-mapped list shown after every export.
