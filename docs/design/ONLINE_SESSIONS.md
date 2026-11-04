# LYNX SHARE Online Sessions — design

Status: design (round 167r, 2026-11-01); **phase P1 implemented in round 168a** (see "Decisions" and §9 P1); **live
sessions (P2) over the LYNX SHARE relay implemented in round 169b** (§9 "P2 as built"). This is a LYNX-own feature: FL
Studio 2026 has no live collaboration (see §0). Code references point at the tree as of `origin/main` 4a5f74fc.

## The idea in one paragraph

Like hosting a game: the **host** opens a project and picks *Session › Start online session*; LYNX SHARE shows an
invite code such as `LYNX-7F3K-Q9WM`. A friend picks *Session › Join online session*, pastes the code, the host
clicks *Accept*, and the friend's LYNX SHARE downloads the project and its samples in the background. From then on
both work in the same project at once. Each person gets **his own area** (his channels, Playlist tracks and mixer
inserts, in his colour) and records **with his own audio interface and his own plugins, locally**: nobody plays over
the network, so nothing a person records is delayed or glitched by the internet. What travels is the edits and the
finished takes. A part made with a plugin the other person does not own is heard by the others as audio that the
owner's computer renders for them. When the session ends, the host has the complete project on his disk.

What it is **not**: a real-time jam. Two people playing in time over the internet would hear each other 30–100 ms
late (more across continents), which is unplayable for music. LYNX SHARE takes the opposite approach: every take is
recorded against the local playback, on the local clock, and placed on the shared timeline to the sample.

---

## 0. FL Studio has no equivalent (evidence)

Static check of FL Studio 2026's own binaries (no FL launch; strings only, as the rounds' REA rules allow for
"does this exist" questions):

| Binary | Strings (ASCII + UTF-16) | Collaboration-related hits |
|---|---|---|
| `FL64.exe` | 3 251 | none |
| `FLEngine_x64.dll` | 237 135 | `collab`: 0, `invite`: 0, `webrtc` / STUN / ICE / SDP: 0 |
| `FLMManaged.dll` | 79 376 | only the Microsoft Graph SDK's own types (`DriveItemInviteRequest`, `InviteParticipantsOperation…`) bundled for OneDrive access, no FL feature |

What FL's network features are, from the same strings: **FL Cloud** (`ToolsFLCloudAccountActionExecute`,
`ToolsFLCloudPluginsActionExecute`, `ToolsFLCloudMasteringActionExecute`, `ToolsFLCloudDistributionActionExecute`,
`UploadSamplesToCloudActionExecute`, `FileSaveAndBackupToCloudActionExecute`, `TCloudStorageBackupsFolderNodeHelper`:
sounds, plugins, mastering, distribution, project backup storage), SoundCloud upload (`TSoundCloudUploader`) and **FL
Studio Remote / Image-Line Remote** (`ILRemoteServer`, "Enable connecting to the FL Studio Remote app on iOS and
Android": a phone as a controller on the LAN). None of them lets two people work in one project. Method and counts:
`docs/analise/REA_rodada167r.md`.

So Online Sessions are LYNX-own, like the mixer Mono button: the workflow is designed for LYNX, not copied.

---

## 1. How LYNX edits a song today, and how that becomes a shared session

### 1.1 What exists (grounding)

- **One document, one thread.** `AudioEngine` owns the `Song` (`src/core/Song.h`); only the UI thread touches it.
  Every change goes through `AudioEngine::editSong (std::function<void (Song&)>, bool undoable)`
  (`src/engine/AudioEngine.cpp` ~2159; about 470 call sites in the UI) or through an engine method that changes
  `song` directly; **all of them end in `publishSong()`**, which bumps `songStamp` and builds an immutable `RackState`
  for the audio thread (`SnapshotExchange<RackState>`). `publishSong()` is therefore the one choke point where every
  change to the song is visible.
- **Undo is whole-song snapshots.** `undoStack` / `redoStack` are `std::deque<Song>`; one mouse press = one step
  (`lastUndoPress`). `core/History.h` names a step by comparing two snapshots (`describeChange`).
- **Edits are opaque lambdas**, not operations: there is no operation log to replay.
- **References are indices.** `Note::channel`, `Clip::pattern / track / channel / automation`,
  `ChannelDef::mixerTrack / group`, `MixerSend`, `EffectSlot::sidechain / sendTracks`, `AutomationTarget::index /
  slot`, `Automation::source`, `PlaylistTrack::mixerTrack / channel`, `ChannelLoop::channel`, `MidiLink` /
  `InitialValue` targets. `Song::removeChannel`, `removePattern`, `removeAutomation`, `RackOps::moveChannels`,
  `MixerOps::moveTracks` renumber them.
- **The file format is JSON** (`engine/ProjectFile.*`, `toJson` / `fromJson`, `currentVersion = 1`, additive keys,
  defaults omitted, values clamped when read). `ProjectFile::mixerTrackToVar` already serialises one insert alone.
- **Hosted plugin parameters do not live in the Song.** `PluginRef::state` (base64) is filled by
  `AudioEngine::capturePluginStates()` before a save; the live state is inside the plugin instance.
- **Offline renders use their own engine.** `ExportJob` (`engine/Exporter.h`) builds a private `AudioEngine` with
  its own instruments and plugin instances on the UI thread and renders on a background thread. "Consolidate track"
  (`trackmodes::makeConsolidateSong` / `applyConsolidate`) already renders a solo copy of the song and swaps the
  result in as an audio clip.

### 1.2 Decision: host-authoritative state replication of entities

Rewriting 470 edit sites into operations is not realistic, and an opaque lambda cannot be replayed on another
machine. Instead the session layer **observes the result of each edit** and replicates **entity states**:

1. **Stable ids** (new, phase 1). `ChannelDef`, `Pattern`, `Automation`, `Arrangement` and `Clip` get a
   `std::uint64_t uid`; `Song` gets `projectId` (random 128-bit, the project's lineage) and `nextUid`. A
   `uids::normalize (Song&)` pass after every edit (in `editSong` and `publishSong`) gives a fresh uid to any entity
   whose uid is 0 or repeats an earlier one in the same list — so the 470 call sites need no change: a duplicated
   channel is a copy with the same uid, the pass renames the later one. Uids are saved in `.lynx` (needed by the
   phase 1 "merge part" too) and assigned to `.flp` imports by the same pass. A uid is `participantTag << 40 | counter`
   so two people can create entities at the same time without collisions.
2. **Positional slots stay positional.** Mixer inserts (1…500, `maxMixerTracks`) and Playlist rows are numbered slots
   in FL and LYNX, not list items. They keep their index on the wire. Operations that **renumber** them
   (`MixerOps::moveTracks`, moving Playlist tracks, deleting inserts in the middle) are **host-only during a session**
   (greyed for guests with the hint "Only the host can reorder inserts during a session").
3. **Wire form = the `.lynx` JSON with index references replaced by uids.** A new `Song::forEachIndexRef (kind, fn)`
   visitor collects in one place the references that `removeChannel` & co. already know about; a test fills every
   reference field of a `Song` and checks the visitor reaches each one (so a field added later cannot be forgotten
   silently). Derived fields are not sent: `Clip::crossfadeIn/Out` (recomputed by `clipops::updateCrossfades`),
   `MixerTrack::flInput`, `ForeignPlugin` leftovers.
4. **Entities and their granularity** (the unit that is diffed, owned, and conflict-checked):

   | Entity | Key | Body |
   |---|---|---|
   | Globals | — | `bpm`, `beatsPerBar`, `swing`, `masterPitch`, `info`, project switches (`autoCrossfades`, `playTruncatedNotes`, `fastDeclick`, `panTriangular`, `cleanMixer`, `substitutesBypassed`, `performanceMode`), `channelGroups` |
   | Channel | uid | the `ChannelDef` JSON (plugin state may be an attached blob, §4.3) |
   | Pattern header | uid | name, length, colour, loop settings, event lanes |
   | **Pattern notes slice** | (pattern uid, channel uid) | the notes of one channel in one pattern |
   | Insert | index | `ProjectFile::mixerTrackToVar` |
   | Playlist row | index | `PlaylistTrack` |
   | Clip | uid | the `Clip` (arrangement uid, track row, refs as uids) |
   | Automation | uid | the `Automation` |
   | Arrangement | uid | name, loop, **markers** (tempo / time signature / punch markers live here) |
   | Links | — | `midiLinks`, `initialValues` (small lists, replaced whole) |

   The **notes slice** is the key to working in the same pattern without stepping on each other: FL-style patterns
   hold notes of many channels; the host's notes for his channels and the guest's notes for his channels are
   different entities and merge without conflict.
5. **Local view state is never replicated** (each person has his own): `currentPattern`, `currentArrangement`
   (the wire form stores every arrangement's clips by arrangement uid regardless of which one is open — the
   `playlist`/`arrangements[i].clips` swap of `Song::selectArrangement` is undone on the way out and redone on the way
   in), `mode` (pattern/song), `loopStart`/`loopEnd`, Playlist row `height`/`collapsed`, selections, open windows,
   zoom, `info.workSeconds` (host only).
6. **Patches are per-entity JSON merge patches** (RFC 7386) computed between the entity's JSON before and after.
   This has a useful property for mixed builds: a client only sends the keys it changed, so keys it does not know
   (a newer LYNX's fields) survive on the host.

### 1.3 The edit loop

Every peer (the host too) runs a `SessionReplica` attached to its `AudioEngine`:

```
  confirmed C (host revision r) ── + pending local patches P1..Pn ──> working song W (= engine.song)
```

- **Detect.** `publishSong()` sets a dirty flag. A 30 Hz UI timer (and the end of every mouse gesture) diffs `W`
  against a shadow copy entity by entity (`operator==` per entity; patterns compare their note vectors; a full diff of
  a large project is well under 1 ms), and turns the differences into one patch: `PROPOSE {clientSeq, baseRev = r,
  entities…}`. A knob drag therefore sends ~30 small patches per second, coalesced, never one per mouse move.
- **Host orders.** The host applies proposals in arrival order. For each one it:
  1. checks **permissions** on every entity the patch touches (§2) — with one allowance: *ripples*. Some edits change
     entities other than the one the user touched (`followPatternLengths` moves clips when a pattern's length
     changes; `Song::removeChannel` deletes the channel's notes and audio clips everywhere). The host re-runs the
     same `Song` function on the primary change and accepts the ripple when it matches what it computed itself;
  2. checks **conflicts**: if an entity in the patch was changed by another participant's commit after `baseRev`,
     that entity is rejected (the others in the same patch still apply). Entities are small and owned by one person
     (§2), so this only happens when two people with rights edit the same thing within the same ~100 ms;
  3. applies, runs the same normalisations every peer runs (`uids::normalize`, `followPatternLengths`,
     `clipops::updateCrossfades` — derived data is recomputed, never trusted), increments the revision, appends the
     commit to its **session journal** (§8.3) and broadcasts `COMMIT {rev, origin, clientSeq, patch}`.
- **Peers rebase.** On a commit from someone else: `C' = C + commit`, `W' = C' + P1..Pn` (re-applying the pending
  merge patches entity by entity), then `engine.applyRemote (W')`. On the commit of one's own `Pi`: drop it from the
  pending list. On `REJECT {clientSeq, entities, reason}`: drop it, recompute `W`, and the hint bar says what was
  undone ("Pattern 3: Pedro changed it at the same time — your edit was undone"). The user's change never silently
  vanishes.
- **Applying a remote state** is a new `AudioEngine::applyRemote (Song)`: like `setSong` without clearing the
  history, without `modified = false`, keeping the local view fields, and through the normal `publishSong()` so
  `RackState::build` reuses the voices and plugin instances of unchanged channels (`lastPublishedShape`). A changed
  `PluginRef::state` of a plugin that is open here is pushed into the live instance (`setStateInformation`) instead of
  recreating it.
- **While this peer is recording or the transport plays a take, remote commits are queued**, not applied, so the
  local playback the musician is recording against does not change under him; they are applied at stop (the presence
  bar shows "3 changes waiting"). An option lets a listener apply them live.

### 1.4 Undo per user

The whole-song `undoStack` would undo other people's work, so in a session the engine switches to a
`SessionHistory`: each local gesture stores, per entity it touched, the entity's JSON before and after. **Undo**
builds a patch that restores the *before* state of each entity whose current state still equals its *after* state
(nobody changed it since); entities someone else changed since are skipped and named in the hint bar ("Can't undo
Pattern 3: Pedro edited it since"). Redo is the same in reverse. Undo and redo are ordinary proposals to the host.
The History panel (`ui/HistoryPanel`) lists only one's own steps (named with `describeChange` on the entity pair),
with others' edits as grey, non-clickable lines ("Pedro: Notes in Pattern 3"). Leaving the session keeps the history
as normal whole-song snapshots again.

### 1.5 Late join, reconnect, host leaving

- **Late join.** The host sends `SNAPSHOT {rev, wire song (gzip), participants, ownership}` and then live commits
  from `rev` on. A large project's JSON is a few MB, under 1 MB compressed. Media follows separately (§3).
- **Reconnect.** A peer that drops keeps its working copy and its pending patches (it can keep editing its own area
  offline; the patches queue). On reconnect it sends `RESUME {participantId, lastRev, pending…}`; the host keeps the
  last 4 096 commits (or 30 min) in memory and on disk: within that window it replays the missing commits, otherwise
  it sends a fresh snapshot, and the pending patches go through the normal conflict check against their old
  `baseRev`.
- **Host leaves.** *End session* ends it for everyone: each guest is offered **Save my part** (his entities and
  media as a part package, §9 P1) and, if the host allowed it, **Save a copy** of the whole project. **Host crash**:
  §8.3. **Hand-over** to another participant (who would become the authority and get the host's media) is a phase 4
  item; until then the host's computer is the session.

### 1.6 Version mismatch

The handshake carries `protocolVersion`, the LYNX version and build commit. Phases 2–3 require the **same LYNX
SHARE version** on every computer: the rendering of LYNX's own engines changes between versions, and a guest whose
LYNX lacks a field would still keep it (merge patches, §1.2.6) but could not play it. A mismatch is refused with a
clear message: "Pedro uses LYNX SHARE 0.1.22, you use 0.1.23. Both must use the same version — Help › Check for
updates." Later the rule can relax to "same protocol version" with a warning. (The Microsoft Store build updates
through the Store, so a Store user and a GitHub user can differ for a few days — the message says so.)

---

## 2. Areas, roles and permissions

### 2.1 Participants and roles

Each participant has an id (a hash of his long-term public key, §6.4, so he is the same person when he reconnects),
a display name and a colour. Roles:

| Role | Can |
|---|---|
| **Host** | everything; accepts joiners; grants permissions; owns the globals and the Master; ends the session |
| **Editor** (default for an accepted friend) | create things (they become his), edit his own area, record, chat |
| **Listener** | hear, follow the host's transport, see presence, chat; no edits (downloads the media and the stand-ins only) |

### 2.2 What "his area" is

Each entity has an **owner** (a participant id), stored with the session and saved in the `.lynx` as an optional
`session` block (participants + owner per uid/index), so a later session with the same people restores the areas:

| Thing | Owner | Notes |
|---|---|---|
| Channel | its creator | a guest's new channel takes a free insert **from his reserved insert range** (below) |
| Notes of channel C in pattern P | **the owner of C** | anyone can write notes for his own channels into any pattern; that slice is his |
| Pattern header (name, length, colour, loop, event lanes) | its creator | the host owns the patterns that existed before the session |
| Mixer insert | the participant whose range it is in / who claimed it | effects, fader, pan, EQ, sends, input, record arm: the owner. Master (0): host |
| Routing into someone else's insert | needs *Edit everything* | a guest may send to the Master and to his own inserts |
| Playlist rows | reserved ranges (below) | a guest's rows form a **track group** named after him, in his colour (`PlaylistTrack::grouped` header) |
| Clip | the owner of the row it is on | moving a clip to another person's row needs rights on both |
| Automation clip | the owner of its target (channel/insert) | tempo automation is a global |
| Globals: tempo, time signature, swing, master pitch, project settings, markers, arrangements list | host | grantable as *Tempo & project settings* |
| Renumbering (move inserts / rows / channels order) | host | see §1.2.2 |

**Reserved ranges** avoid two people grabbing the same insert or row at the same moment: when a guest joins, the
host assigns him a block of inserts (e.g. 40–59, after the highest insert in use, grown on demand) and a block of
Playlist rows (e.g. the 16 rows after the last used one, in his group). In a session `Song::claimFreeInsert`,
`trackmodes::freeMixerTrack` and the take-track choice (`takes::` "a new track after the last one used") look in the
participant's own range first.

### 2.3 Permissions the host can toggle per participant

*Edit own area* (on for editors) · *Edit everything* (off) · *Tempo & project settings* (off) · *Save a copy at the
end* (default: open question Q1) · *Role* (editor / listener). Turning a permission off never deletes anything.

Trying to edit something one does not own does nothing and the hint bar says "Owned by Ana — ask her for access";
the host gets a one-click *Give Pedro access to Pattern 3* in the participant list.

---

## 3. Media: samples, recordings, stand-ins

### 3.1 Identity and dedupe

Media are content-addressed: **id = BLAKE2b-256 of the file bytes** (streaming; Monocypher, §6.4; `core/Sha256.h`
only hashes a whole buffer). On the wire, sample paths (`ChannelDef::samplePath`, `SampleZone`, `EffectSlot::
samplePath`, audio clips' channels) become `media:<id>/<original file name>`; each peer maps an id to a local file.
Before downloading anything a peer checks whether it already has the bytes:

1. its own media cache (§3.3);
2. **the same file in its own FL Studio Data folder**: the host's path is matched relative to FL's `Data` folder
   (`engine/FlStudio` already locates the installation), compared by size and hash — FL's factory packs are then
   never downloaded when both people have FL;
3. LYNX's own bundled content (the same version ships the same files).

### 3.2 Transfer

- Files are cut into **1 MiB chunks**; a manifest `MEDIA_MANIFEST {id, size, kind, name, chunk hashes}` lets the
  receiver verify every chunk, resume after a drop at the first missing chunk, and fetch chunks from any peer that
  has them (in a 3-person session the second guest can get samples from the first).
- **Priority**: edits and presence first, always (media chunks are ≤ 64 KiB on the wire and interleaved below the
  state stream, so an edit never waits behind a sample); then the media the playback needs soonest (samples of the
  channels audible near this peer's playhead), then the rest. Downloads pause while this peer records.
- **Bandwidth**: a 3-minute 24-bit 48 kHz stereo take is ~52 MB raw, ~30 MB as FLAC. P2 sends WAV as is; P3 adds
  "compress recordings for transfer" (lossless FLAC through JUCE's encoder; the published media is then the FLAC file,
  with the WAV kept as a local alias on the recorder's side). A per-session upload cap in Session settings protects
  slow lines.
- Until a sample has arrived, its clips show a grey waveform with a progress ring and play silence; nothing fails.

### 3.3 Where the files go

- **Guest**: a session folder under the profile's cache, `platform::cacheFolder ("Sessions")/<session id>/` plus a
  shared content-addressed store `cacheFolder ("SessionMedia")/<aa>/<id>.<ext>`. On Windows `cacheFolder` is
  `%APPDATA%\LYNX\…` (roaming); big media do not belong in a roaming profile, so the implementation adds
  `platform::localCacheFolder` (`%LOCALAPPDATA%\LYNX`, the Store package's LocalCache) for these. Test runs use
  `LYNX_PROFILE_DIR` as for every other path.
- **Host**: starting a session requires a saved project (offer *Save as…*). Guests' takes and samples are stored in
  the project folder, `<project>/Session media/<participant name>/`, and the host's Song points at them — so after
  the session the project is complete and self-contained, and *File › Export › Project data files*
  (`ProjectPackage::exportZip`) works on it as usual.
- **Cleanup**: the guest store has an LRU budget (`core/CacheBudget.h`, default 20 GB, Session settings); *Leave
  session* asks "Keep the downloaded files for next time?"; *Settings › General › Clear session files* empties it.

---

## 4. Plugins

### 4.1 Who has what

When someone joins, and whenever a third-party plugin is added, the peers exchange **only the plugins the project
uses** (never the whole plugin list — privacy): `PLUGIN_QUERY [{format, uniqueId, deprecatedUid, name,
manufacturer, version}]` → `PLUGIN_ANSWER [{have, version}]`. The match is by format + unique id (+ name and vendor
as fallback), the way `PluginHost::findForeign` already matches FL's plugins — **not** by `PluginRef::id`: JUCE's
`createIdentifierString` contains a hash of the plugin's file path, which differs between two computers. Cross-format
matches (owner VST3, peer VST2 of the same plugin) count as "have, different format": the state may not carry over
(`adoptFlPlugins`' `withoutState` case), so they default to the stand-in.

### 4.2 Stand-ins: the owner renders, the others hear

For every part that needs a plugin a peer does not have (same plugin and same version = he plays it himself):

- **Channel stand-in** (an instrument plugin): the owner's computer renders the channel's output **before its mixer
  insert**. The peer keeps the channel, its insert, its fader, pan, sends and the insert's built-in effects live;
  only the instrument is replaced by the audio.
- **Insert stand-in** (an effect plugin on an insert): the owner renders that insert's output after its effect chain,
  before the fader; the peer bypasses that insert's chain and plays the audio into the fader. The render depends on
  everything routed into the insert, so it is re-rendered when an input changes.
- **Pattern stems, not a timeline stem (default).** A channel stand-in is rendered **per pattern** (the channel's
  notes in that pattern, the pattern's length plus the tail until silence, at most 4 bars). The peer's engine plays
  the stem of a pattern wherever a clip of that pattern plays, from the clip's offset, in song and in pattern mode.
  So when anybody **moves, copies or trims clips, no re-render is needed**. A **timeline stem** mode (the whole song)
  is used automatically when the part has song-level automation of the plugin's parameters, or when the owner picks
  it (for plugins with free-running LFOs or long glides across clips).
- **Engine side**: a local-only `InstrumentKind::standIn` (never saved, created by the session's local projection of
  the Song) plays the stems from the Scheduler's pattern clip starts. **Song side** (phase 1, also saved):
  `ChannelDef::standIns {pattern uid → media id, source hash, length}` and `MixerTrack::standIn`, so a project sent
  to someone (P1) and the host's final project keep the renders as a fallback.
- **Freshness.** The *source hash* covers everything the render depends on (the channel/insert JSON incl. plugin
  state, the notes slice, tempo map, swing, sample rate, plugin version). Every peer can compute it from the wire
  state, so a peer *knows* when a stand-in is stale and shows it ("updating…") while still playing the previous one.
- **When the owner renders**: debounced 2 s after the last change, on a low-priority worker, with a private
  `ExportJob`-style engine and **its own plugin instances** created from the state (never the live instances the
  musician is playing). Paused while the owner plays or records; resumed at stop. Plugins that refuse a second
  instance (some licence systems) fall back to rendering with the live instance when the transport is stopped.
- **Native engines are the advantage.** LYNX's own instruments and effects — including the FL engines LYNX rebuilt
  (Sytrus, Harmor, 3x Osc, Fruity effects…, state in `ChannelDef::pluginState` / `EffectSlot::state`) — are LYNX code:
  with the same LYNX version they sound identical on every computer and need no stand-in. The exception is an engine
  that reads files from the local FL installation (e.g. BooBass's DLL tables checked by `sha256Hex`): it is treated
  like a plugin ("needs FL Studio's data"), and FL's binaries are never transferred.

### 4.3 Plugin states

Third-party states are **opaque blobs** (`PluginRef::state`, `ForeignPlugin::state`): stored, hashed, forwarded,
never parsed or decompiled. Parameter moves of a hosted plugin do not touch the Song, so the owner's LYNX captures
the state (`PluginHost`'s `captureState`) 2 s after the last parameter change or when the plugin window closes, and
sends it as an entity change. A state over 64 KiB (sample-based instruments can be MBs) travels as a media blob
referenced by hash, not inline in the patch.

### 4.4 Latency / PDC

Each computer plans its own delay compensation (`core/Pdc.h`) for what it plays. Stems are rendered
latency-compensated (as exports are), so a stand-in has no latency of its own and lines up on the peer; the owner's
plugin latency only affects the owner's local PDC plan. A peer that has the plugin plays it with its own reported
latency, compensated locally. Nothing about latency crosses the network.

---

## 5. Recording, end to end

1. Pedro arms an insert of his range, picks his input (`MixerTrack::inputChannel`), sets count-in / punch as
   always. Punch ranges come from arrangement markers (`punchRanges (markers)`), which are host-owned globals; in a
   session a guest's **own time selection** acts as his local punch range (local view state, not a marker), so he
   does not need the host to place markers for him.
2. He presses record. The transport is **his own**: it runs on his clock, he hears his local playback (the host's
   parts as real engines or stand-ins), the count-in and the metronome locally. Remote commits are queued (§1.3), so
   nothing changes under him mid-take; media downloads and stand-in renders pause.
3. The take is written by the existing crash-safe writer (`core/TakeRecording.h`: header rewritten every second, ring
   that never loses the timeline) and placed with the existing compensation (`takes::Latency` = round trip + manual +
   Master PDC; `takes::place` → start/offset/offsetFraction on the shared tick timeline, to the sample; loop passes
   and punch parts as today). Ticks are the shared coordinate: the clip lands exactly where he played it on everyone's
   computer, whatever their sample rate or audio driver.
4. At stop: the take's clip is a normal patch (`PROPOSE`: new audio clip channel + clip on his row), published at
   once; the file is hashed and uploaded in the background. Others see the clip immediately as "arriving 34 %" and
   hear it when it has arrived.
5. A MIDI take into his plugin channel is just notes (a few KB), and his stand-in is re-rendered for the others.

Drop mid-take: nothing happens to the take — it is local. It uploads when the connection is back (§8.1).

**Following along.** By default every transport is independent. *Follow host transport* (presence bar toggle)
starts / stops / locates this peer's playback when the host's does (±network delay, irrelevant for listening), for
"listen to this" moments. It is not used while recording.

---

## 6. Networking

### 6.1 Transport layers

> **As built (round 169b): the relay first.** Live sessions go through the LYNX SHARE relay
> (`wss://relay.lynx-share.com`, SHARE_API.md §3) over one WebSocket per computer; LAN-TCP and ICE (below) are not
> built. Why, in this order:
>
> 1. **It works for everyone on day one.** A WebSocket to port 443 passes every home router, carrier-grade NAT,
>    mobile hotspot and company firewall that lets a browser out; TCP to a host's port needs port forwarding or UPnP,
>    and ICE hole punching fails for 10–20 % of connections (§9.1) — exactly the cases a first test with a friend
>    hits, with nothing LYNX can do about it.
> 2. **Nothing new to ship or trust.** The operating system's own TLS and WebSocket (WinHTTP on Windows; the system
>    libcurl ≥ 7.86 on Linux / macOS, loaded at run time) — no libjuice, KCP, miniupnpc or listening socket, no
>    Windows Firewall prompt, no new attack surface on the user's computer (nobody can connect to it).
> 3. **Privacy**: the participants never learn each other's IP address (only the relay sees them), which a direct
>    connection always reveals (§6.5).
> 4. **It costs nothing extra.** The relay runs on the VPS the project already has (the user's decision 5 kept the
>    free path; the VPS is not a new bill) and a session is small: edits are a few KB, media go once per person.
>    The relay is blind: it routes by a one-byte peer number and never sees the key (§6.4 as built).
> 5. **The session protocol does not care.** The messages (§6.2 as built) ride on a `livelink::Connection`; a direct
>    LAN / ICE link can later carry the same sealed frames, as an optimisation for big media on one network.
>
> The cost: every frame makes two hops (≈ 2 × the one-way latency to the relay, ~30–80 ms in Brazil), which only
> delays when an edit appears on the others' screens — takes are always recorded locally (§5).

```
  UI / SessionReplica / MediaStore / StandIns
        │  messages (§6.2) on 3 logical streams: state (reliable, ordered) · media (reliable, low priority) · presence (unreliable)
  SessionLink  ── Noise_XXpsk3 encryption + framing (§6.4)
        │
  ┌─────┴────────────────────────────────┐
  TcpLink (P2: LAN / direct IP)   IceLink (P3: UDP via ICE + reliable stream)   RelayLink (P4: TURN)
```

- **P2 — `TcpLink`**: JUCE `StreamingSocket` (no new dependency). LAN discovery by a UDP broadcast beacon (JUCE
  `DatagramSocket`, port 47800) so the Join dialog lists "Ana's session on this network"; *Direct IP* (`host:port`)
  for people who forward a port. Optional automatic port mapping with **miniupnpc** (BSD-3) so "direct" works through
  many home routers without opening ports by hand.
- **P3 — `IceLink`**: **libjuice** (MPL-2.0, plain C, no dependencies, ICE + STUN + TURN client, Windows/macOS/Linux)
  does the NAT traversal / UDP hole punching; on top, a reliable ordered stream with **KCP** (MIT, one C file) for
  the state and media streams, raw datagrams for presence. Both build with the zig/MinGW cross toolchain like the other
  C libraries in `third_party/`.
- **Considered**: *libdatachannel* (MPL-2.0, full WebRTC data channels) — heavier (usrsctp + a DTLS library +
  C++17), worth it only if a browser listener page is ever wanted; it would replace IceLink behind the same
  `SessionLink` interface. *ENet* (MIT) — a fine reliable-UDP library but it owns its socket, which conflicts with
  ICE's hole-punched socket. JUCE alone has no NAT traversal.
- Licences: MPL-2.0 files combine with the AGPL-3.0 (MPL §3.3, "Secondary License"); MIT/BSD are permissive. All go
  into `THIRD_PARTY.md`.

### 6.2 Messages

Framed: `u32 length | u8 type | u8 stream | payload`; JSON payloads (the same `juce::var` code as `.lynx`), gzip
above 4 KiB; media chunks binary.

| Group | Messages |
|---|---|
| Handshake | `HELLO {protocol, lynxVersion, build, name, colour, caps}` · `KNOCK` (host UI: accept/deny) · `WELCOME {participantId, role, permissions, ranges, session}` · `DENY {reason}` · `BYE {reason}` · `PING/PONG` |
| State | `SNAPSHOT {rev, song, participants, owners}` · `PROPOSE {clientSeq, baseRev, patch}` · `COMMIT {rev, origin, clientSeq, patch}` · `REJECT {clientSeq, entities, reason}` · `RESUME {participantId, lastRev}` |
| Session | `ROLE`, `GRANT/REVOKE {participant, permission, entity?}`, `RANGES {inserts, rows}`, `END` |
| Media | `MEDIA_MANIFEST {id, size, kind, name, chunkHashes}` · `MEDIA_HAVE [ids]` · `MEDIA_WANT {id, chunks}` · `MEDIA_CHUNK {id, index, bytes}` |
| Plugins | `PLUGIN_QUERY` · `PLUGIN_ANSWER` · `STANDIN_READY {entity, pattern?, media, sourceHash}` · `STANDIN_REQUEST {entity}` |
| Presence | `PRESENCE {playhead, playing, recording {insert, from}, focus {window, entity}, selection}` (5 Hz, unreliable) · `CHAT {text}` · `TRANSPORT {play/stop/locate}` (follow mode) |

### 6.3 Rendezvous on lynx-share.com (P3)

lynx-share.com is Hostinger **Single Web Hosting**: static files plus PHP (the crash-report API
`website/api/crash.php` / `_common.php` already runs there, storing files outside `public_html`, rate-limiting by a
salted daily hash of the address). Shared hosting has **no long-lived processes or WebSockets**, and PHP requests are
time-limited, so the rendezvous is a tiny **mailbox over HTTPS with long polling**, which is all ICE needs (a few
messages per join):

| Endpoint | Does |
|---|---|
| `POST /api/session/open.php {protocol}` | creates a room; answers `{room: "7F3K", hostToken, expires}` (room id from the server, 4 base-32 chars; the token proves later calls are the host's) |
| `POST /api/session/post.php {room, from, to, blob}` | drops one message (≤ 8 KB) into a participant's mailbox |
| `GET /api/session/poll.php?room&me&after=seq` | returns waiting messages, holding the request up to ~20 s if there are none (bounded below the host's PHP time limit — to be measured before P3, *not deployed now*) |
| `POST /api/session/close.php {room, hostToken}` | deletes the room |

- **The server learns nothing useful**: every blob is encrypted with a key derived from the full invite code, whose
  second half the server never sees (§6.4); it stores rooms as small files that expire after 12 h without a host poll;
  no IP address is stored (the existing salted daily hash only counts requests: rooms per address per day, joins per
  room, message rate, size caps, at most 8 participants).
- **STUN**: public STUN servers (Google's / Cloudflare's), configurable in Session settings. They see the address of
  whoever asks, like any web request.
- If the rendezvous is down, LAN and direct IP still work (they never use it).
- Nothing is deployed in this round; P3 adds the PHP files to `website/api/` and deploys them with the usual
  `scripts/deploy-site.sh` when the user says so.

### 6.4 Encryption and the invite code

> **As built (round 169b)** — simpler than the PAKE + Noise plan below, because the relay (not a direct socket) is
> the only way in and every peer is let in by the host by hand:
>
> - **Invite code** `LYNX-RRRRR-RRRRR-SSSS-SSSS-SSSS-SSSS`: the relay's room id (10 characters a-z 2-7) and an
>   **80-bit secret** (16 characters) the host's LYNX SHARE makes with the system's random generator and never sends
>   anywhere. Typing is forgiving (case, spaces, dashes, a pasted link; 0/1/8 read as o/i/b).
> - **Keys**: Argon2id (Monocypher, 8 MiB, 3 passes, a fixed salt) of the secret → a master key; BLAKE2b keyed
>   with it gives the **relay room token** (domain "relay room token") and the **session key** (domain "session
>   key:" + room). The host creates the room with that token (`POST /v1/rooms {"token"}`), so the relay checks
>   tokens without ever getting the secret, and nobody can get from the token to the key (different domains of a
>   hash, and an 80-bit secret behind Argon2id is out of reach of offline guessing).
> - **Frames**: `[0x4C][1][nonce 24][MAC 16][XChaCha20-Poly1305(sender id 8, counter 8, payload)]`, additional data
>   "LYNX SHARE live session v1". The sender id is random per connection; a frame whose counter is not above the
>   last one from that sender is dropped (replay); a frame changed on the way or sealed with another key is dropped.
> - **Limits, honestly**: everyone with the code shares one key, so a participant could pose as another one inside
>   the session (they were let in by the host anyway); there is no forward secrecy (a code that leaks later decrypts
>   a recording of the traffic — the code dies with the session); no per-install identity keys yet. A PAKE + Noise
>   handshake (below) fixes these and fits behind the same `Sealer` / `Opener` when direct links come.

- **Invite code** `LYNX-7F3K-Q9WM`: `7F3K` = the room id (server-side lookup), `Q9WM` = a 20-bit secret made by the
  host's LYNX and never sent to the server. The **whole code** is the password of a PAKE (CPace over X25519 with
  Monocypher's `crypto_elligator_map` as the hash-to-curve), whose result is the pre-shared key of a **Noise
  `XXpsk3_25519_ChaChaPoly_BLAKE2b`** handshake. A short code is enough because a PAKE allows only one online guess
  per connection attempt: the host's LYNX refuses after 5 failed handshakes and every joiner must also be **accepted
  by the host by hand** (knock). The code dies when the session ends.
- **Identity**: each install has a long-term X25519 key pair in the settings folder; its short fingerprint (`ab12 ·
  cd34`) is shown in the knock dialog and the participant list. A friend the host marked *Trusted* can rejoin a
  resumed session without a new code.
- **Primitives**: **Monocypher** (BSD-2 / CC0, one C file, audited) — X25519, XChaCha20-Poly1305, BLAKE2b, Elligator.
  The Noise state machine is ~400 lines, tested against the Noise specification's test vectors; it must get a careful
  review before release. Every frame after the handshake is AEAD-encrypted; LAN and direct-IP sessions use the same
  handshake, with the same code typed by hand.

### 6.5 Privacy, Store, firewall

- **What the others see**: in a direct (P2/P3) connection, **the participants see each other's IP address** — this
  is inherent in peer-to-peer and must be said in the Join and Start dialogs ("Only invite people you trust: people in
  your session can see your IP address"). A relay (P4) would hide it.
- **What is sent, only after the user starts or joins a session**: the project (Song JSON), its media, the third-
  party plugin identities the project uses (not the plugin list), display name and colour, presence and chat. To the
  rendezvous: encrypted mailbox blobs. To STUN: a binding request.
- **`PRIVACY.md`** changes from "exactly two things" to three: *Online sessions (only when you start or join one)* —
  what is sent, to whom (the participants you accept, the lynx-share.com rendezvous, STUN servers), the IP visibility,
  that nothing is stored on the server, and how files are kept/removed. The "only uses of a network API" list gains the
  session sources.
- **Microsoft Store** (for the user in Partner Center, at the release that ships P2/P3):
  - *Privacy policy* URL stays the same (the updated PRIVACY.md);
  - *"Does your app access, collect or transmit personal information?"* stays **Yes**; the explanation adds: "Online
    sessions (started by the user) transmit the project, its audio files, a display name and the IP address to the
    participants the user accepts; the rendezvous server stores nothing";
  - `resources/msix/AppxManifest.xml.in` declares `internetClientServer` and `privateNetworkClientServer` (a
    full-trust app is not sandboxed by them, but the Store listing shows the capabilities honestly);
  - `docs/STORE.md`'s "the Store build's only network use is the crash report" sentence is updated.
- **Windows firewall**: the first time LYNX SHARE listens (LAN host, UDP socket), Windows asks to allow it; the Start
  dialog tells the user to allow *Private networks* (and *Public* only if he wants direct IP from outside).

---

## 7. UI (LYNX style: dark, red accent; all strings in the 15 languages)

- **SESSION menu** (new top-level menu after TOOLS; FL's bar has none, this is LYNX-own): *Start online session…* ·
  *Join online session…* · *Invite…* (shows the code again) · *Participants…* · *Session settings…* · *Leave session* /
  *End session* · separator · *Send project to a friend…* · *Open a friend's part…* (P1).
- **Start dialog**: session name (the project's), mode (*Internet* (P3) / *This network only* / *Direct IP*), "Only
  invite people you trust" note, *Start*. Then the **Invite dialog**: the code in large monospace, *Copy code*, *Copy
  link* (`https://lynx-share.com/join#7F3K-Q9WM`: the fragment never reaches the server; the page opens
  `lynx://join/…` or shows the code to paste), "Ends when you end the session".
- **Join dialog**: code field that formats itself (`LYNX-____-____`), the sessions found on this network listed
  below it, name + colour (remembered), *Join* → "Waiting for Ana to accept…" (cancel).
- **Knock toast** on the host: "**Pedro** wants to join · key ab12 · cd34 — [Accept as editor] [Listener] [Deny]".
- **Presence bar** (right part of the toolbar): one round avatar per participant (initial, his colour) with a state
  glyph: red dot = recording (tooltip "Recording on insert 41 from bar 17"), ▶ playing, pencil = editing (window +
  thing), ↓ 43 % = downloading; a connection dot (green / amber / red) with *LAN* / *Direct* / *Relay* and the round
  trip in ms; "3 changes waiting" while recording. Click → the **participant list** popover: role dropdown,
  permission switches (§2.3), *Trusted*, *Remove from session*, fingerprint.
- **Area colouring**: a 3 px stripe in the owner's colour on channel buttons (Channel Rack), insert strip headers
  (Mixer) and his Playlist track group header; the hint bar names the owner. Others' things are not dimmed (it is one
  song), just not editable.
- **Plugin badges** on a channel button / effect slot: amber *stand-in* (hover: "Serum 2 isn't installed here — you
  hear Pedro's render, updated 12 s ago"), spinning *updating*, grey *waiting for render*, blue *version differs*
  (click: "Use my Serum 2.0.17 instead of Pedro's render").
- **Download progress**: a slim bar under the toolbar, "Downloading the project: 412 MB of 1.3 GB — samples near the
  playhead first" (pause / speed limit); grey waveforms with a ring on clips not arrived yet.
- **Chat**: a small panel opened from the presence bar (and *SESSION › Chat*) with the session's messages; voice is
  P4.
- **Session settings**: name/colour, default role for new joiners, stand-in mode default, upload limit, session
  files budget and location, STUN servers, "apply remote changes while recording" (off).

---

## 8. Failure cases

### 8.1 A peer drops (also mid-take)
Its LYNX keeps running on its working copy; a take in progress finishes and stays local; edits to its own area queue
as pending patches. The presence bar shows "Reconnecting…" (back-off 1 s → 30 s); `RESUME` replays (§1.5). The host
marks him grey; his parts keep playing from the last state and stand-ins.

### 8.2 Conflicting edits
Entity-level conflict check (§1.3): the later edit on the same entity is rejected and named in the hint bar; the
earlier one wins. With areas, conflicts need two people *with rights* on the same entity within ~100 ms. Globals
(tempo) are host-only by default, so the most disruptive conflicts cannot happen.

### 8.3 Host crash
The host journals every commit (`autosave/session-<id>.journal`, appended before the commit is broadcast) next to
the periodic autosave (`core/ProjectSafety.h` backups). On restart LYNX offers "Resume the online session with Pedro?"
→ autosave + journal replay restore the last committed revision exactly, the room is reopened with the **same code**
(P3: the host token is kept with the journal), and the guests' `RESUME` brings them back with their pending patches.
Guests meanwhile show "Host lost — waiting for Ana (your edits are kept)".

### 8.4 Slow peers
Edits are small and always first; a peer whose state backlog exceeds a threshold gets a fresh snapshot instead of a
long replay; presence is thinned for him; media obey the priority and the upload cap. A slow peer never slows the
host's own editing (the host applies and commits locally first).

### 8.5 Others
Disk full on a guest (downloads pause, message) · a plugin crashes on the owner's side (the last good stand-in
stays) · different sample rates (stems carry their rate; the sample loader resamples) · a guest without FL Studio
(FL Data samples are downloaded, FL-data-dependent engines become stand-ins) · two hosts' LYNX in test mode on one
machine (§10) · clock differences (irrelevant: no shared clock is used).

---

## 9. Phases and rough effort

Effort in LYNX rounds (one agent round ≈ one focused day), including tests, the 15 languages and docs.

| Phase | What the user gets | Main work | Effort |
|---|---|---|---|
| **P1 — Send project / my part** (no network) | *Send project to a friend…*: a `.lynxpack` (zip) with the project, every sample and **stand-ins of the third-party plugin parts**, plus the list of plugins used — the friend can open and play it even without those plugins. He records his part and uses *Send my part back*: a small package with only what he created (his channels, notes slices, rows, inserts, takes, his plugin states and stand-ins). *Open a friend's part…* merges it into the host's project, in his colour. Useful alone, over WhatsApp / Drive. | uids + `normalize` + saved; `forEachIndexRef`; owners `session` block; wire/entity diff + merge (the same code P2 uses); stand-in render (per pattern, per insert) via an `ExportJob` engine; `InstrumentKind::standIn`; extends `ProjectPackage::exportZip` | **2–3** |
| **P2 — Live session on LAN / direct IP** | Start/Join on the same network or by IP (+ UPnP), live edits, areas, permissions, presence, chat, background media, live stand-ins, per-user undo, reconnect, host journal | `SessionReplica`, `SessionHistory`, `applyRemote`, host ordering & checks, media store & chunk transfer, TcpLink + Noise/CPace (Monocypher), plugin query, UI (menu, dialogs, presence bar, badges) | **5–6** |
| **P3 — Invite code over the internet** | The `LYNX-7F3K-Q9WM` flow from anywhere, no router setup for most connections | libjuice + KCP `IceLink`, rendezvous PHP on lynx-share.com (deploy only when the user OKs), link page, `lynx://` handler, PRIVACY.md / Store changes, FLAC transfer | **3** |
| **P4 — Optional extras** | Relay for the connections that fail; voice chat; host hand-over; mixed-version sessions | TURN (`RelayLink`, libjuice already speaks TURN), Opus voice (BSD-3), hand-over protocol | **3+**, and the relay **may cost money** (§9.1) |

### P1 as built (round 168a)

User-facing steps: [USER_GUIDE.md](../USER_GUIDE.md#online-sessions-make-a-song-with-a-friend-phase-1-packages).
Where it differs from the plan above:

- **Menu**: *File › Online session ›* Send project... / Send my part back... / Merge a part... (a received
  `.lynxpack` also opens with *File › Open...*; a part opened that way is merged). Each guest's colour is picked
  automatically (blue, green, amber, purple) instead of a colour field.
- **Code**: `core/SessionIds` (uids, `normalize`, the index-reference visitors), `core/SessionParts` (`invite`,
  `guestCopy`, `extract`, `merge`, `keepOnlyPart`), `core/StandIns` (what is rendered, how it is cut, the projection
  the engine plays), `engine/SessionPack` (render through `ExportJob`, package write / open / verify, plugin identity
  matching, media adoption), `ui/SessionDialogs`. The package is its own format (not `ProjectPackage::exportZip`):
  `manifest.json` + `project.lynx` + `media/<sha256>.<ext>`.
- **No `InstrumentKind::standIn`**: stand-ins are data on the channel / insert (`ChannelDef::standIns`,
  `MixerTrack::standIns`). Where the plugin is missing, the audio engine plays a *projection* of the song (never saved
  or shown): the channel becomes hidden audio-clip channels at its pattern clips (Song mode) or its loop stem (Pattern
  mode); an insert's timeline stem replaces its chain and inputs (Song mode). The original plugin and its state stay in
  the project untouched.
- **Pattern stems** come from one render of the channel per package (each pattern once + 4 bars of tail, then twice;
  the loop stem is the second pass); a channel with song-level automation of its plugin, or a song with tempo
  automation, gets a timeline stem. Stems are 32-bit float WAV at 44.1 kHz, rendered at playback quality; an unchanged
  part (same source hash) is not rendered again.
- **Merge** is `parts::merge` (by uid, the guest's area only, moved inserts / rows when the host used them meanwhile),
  one `AudioEngine::editSong` = one undo step. The P2 wire diff is not written yet; P2 can reuse `merge` for late joins.
- **Plugin matching** on the other PC: format + unique id (else name + vendor) from the manifest; another version asks
  "play the renders" or "use my version".
- Tests: `tests/Session168aTests.inc` (uids, reference visitors, invite ranges, part round trip without clobbering,
  stand-ins play like the part within 1e-4 in Song and Pattern mode, insert stand-ins, streaming SHA-256) and
  `--selftest` `online_session=` (pack with a real `ExportJob` render, hash dedupe, open on another folder, uid
  persistence through save / load, part back, adopt media, merge).

### P2 as built (round 169b): live sessions over the relay

- **Menu**: *File › Online session ›* Start live session... (your name; guests may edit only their own channels,
  patterns, inserts and rows, or everything; what guests keep at the end) → the invite code (*Show invite code...*,
  Copy) · Join live session... (name + code; the open project is saved or let go first) · End / Leave live session.
  A knock asks the host "{name} wants to join your live session" (Let them in / Not now). The toolbar shows a red
  **LIVE** badge and everyone online (their colour dot, name, "(you)", "rec" while recording, files still arriving).
- **Transport**: the relay first (§6.1 as built), one WebSocket per computer (`engine/LiveLink`: WinHTTP / libcurl),
  one room of ≤ 4 peers created by the host with a token derived from the code (SHARE_API.md §3.1). Encryption: §6.4
  as built.
- **Wire** (`engine/LiveSessions`): relay payload `[1][sealed]` or `[2][u32 id][u16 i][u16 n][part]` (frames over
  512 KiB are split; ≤ 64 MiB per message). Sealed plain text: `J` JSON, `Z` gzip JSON (above 4 KiB), `M` a media
  chunk `[sha256 hex 64][u32 index][u32 count][u64 size][≤ 256 KiB]`. JSON messages: `hello {name, version,
  protocol}` · `deny {reason: version|full|declined}` · `snapshot {rev, tag, keepFull, editEverything, song,
  manifest}` · `propose {seq, base, patch}` · `commit {rev, origin, seq, patch, rejected, reason}` · `people {list}`
  · `presence {playing, recording, pos}` · `want {sha}` · `resync` · `bye` · `end {keepFull}`. A patch is `{set:
  [keys], del: [keys], song: <the carrier as .lynx JSON>}`.
- **Replication** (`core/LiveSession`): host-authoritative entity replication exactly as §1.2–§1.4: entities by uid
  (globals, session, links, order, arrangement, channel, pattern header, notes slice, automation, clip, insert,
  row); patches carry the changed entities plus id-only stubs; the host's `Authority` checks permissions (§2.2) and
  conflicts (the earlier edit wins, ripples allowed) and commits in the form every peer receives (the codec = the
  JSON round trip); every peer's `Replica` rebases its unconfirmed edits on each commit and adopts the host's rounding.
  Commits wait while the computer records (applied at the stop). A missing revision asks for a fresh snapshot.
- **Late join**: the snapshot is the confirmed song with its samples as `lynx-media:<sha256>/<name>` tokens and the
  plugin identities (P1's manifest code: `resolvePlugins` maps them to this computer's plugins).
- **Media**: a received song first gets every token turned into `<session media folder>/<sha>.<ext>`, then P1's
  `sessionpack::confine` (anything else outside that folder / FL's factory data becomes a missing file), then a sha
  this computer already has becomes its own file; the rest is asked for (`want`). Only a file this computer turned
  into that hash itself is ever sent; pieces arrive in order into a `.part` file that must hash to the sha before it
  is used; the host forwards a guest's file to the others as soon as it has it. Media wait while > 1 MiB of other
  frames is queued. Folders: the host's `<project>/Session media/Live session` (Documents/LYNX/Session media/... when
  not saved), a guest's Documents/LYNX/Projects/Received/Live sessions/<room>.
- **Undo per user**: in a session `AudioEngine` keeps its own steps (`before`, `after`) and undoes with
  `live::selectiveRevert`: only what that step changed and nobody changed since; the hint bar names what was left.
- **Leaving**: a guest's *Leave* tells the host (`bye`) and keeps only their part (`parts::keepOnlyPart`) unless the
  host allowed a full copy; the host's *End* sends `end`, closes the room (`DELETE`); a host that drops is seen by the
  relay's `leave` and handled like an end. A guest that drops disappears from everyone's list; their area stays.
- **Version**: `hello.protocol` must equal `live::protocolVersion`, else `deny version` (decision 2).
- **Tests**: `tests/Live169bTests.inc` (diff/apply by uid, permissions and ripples, conflicts, 24 seeded delayed
  networks of 1–3 guests converge, codec rounding adopted, commits held while recording, undo per user, invite code
  and frames); `--selftest` `live_session=` (every entity of the selftest's rich project through the .lynx JSON:
  what the format does not keep, stable on a second pass, 3 peers through the real codec converge, keys);
  `LYNX --livetest report.txt [--relay URL]`: host + 3 guests in one process over the real relay (wrong code refused,
  media both ways and forwarded, everyone's edits, a refused edit undone, undo per user, a late guest, a guest
  leaving, the host ending).
- **Not yet (later phases)**: LAN / direct links and ICE; reconnect / resume of a dropped guest (a new join gets a
  fresh snapshot); host journal and hand-over; live stand-ins over the network (stand-in renders are not sent, a
  part whose plugin is missing here is silent until P1's package flow); chat, follow-transport, per-participant
  permission toggles after the start; PAKE / Noise and identity keys (§6.4); hashing big samples off the message
  thread; FLAC media transfer.

### 9.1 What is free and what might cost money

- **Free**: P1, P2, P3 entirely. The rendezvous runs on the Hostinger plan the user already pays for (tiny traffic:
  a few KB per join); public STUN is free; P2P traffic goes directly between the participants' connections.
- **Might cost later (only the user decides, nothing is bought)**: about **10–20 % of connections** cannot be
  punched through (symmetric / carrier-grade NAT, common on mobile 4G/5G and some ISPs' CGNAT) and need a **TURN
  relay**, which carries the whole session's traffic (media included). Options when P3's real numbers are known:
  (a) run **coturn** on the Hostinger **KVM 2 VPS** the user already has (no new bill, uses its bandwidth; the VPS
  currently serves something else and must not be touched without the user's say-so); (b) a paid managed TURN
  service (billed per GB relayed; prices to be checked at that time); (c) stay free and, when a direct connection
  fails, offer P1's *Send project* instead. Until then LYNX shows: "Couldn't connect directly. Try *This network only*,
  *Direct IP*, or *Send project*."

### 9.2 Tests

- **Unit (Linux local build, `tests/`)**: uid normalisation (duplicates, imports, every create path through a random
  edit fuzzer); `forEachIndexRef` coverage; entity diff ⇄ merge round trip (`apply(diff(a, b), a) == b` on the demo
  songs and fuzzed songs); permission checks incl. ripples; conflict rejection; selective undo; Noise/CPace against the
  spec vectors; chunk manifest / resume; source-hash stability.
- **Convergence**: a host and 2–3 replicas **in one process over an in-memory link** with a `LossyLink` decorator
  (delay 0–300 ms, jitter, 5 % loss, reordering, disconnects), random edit scripts on all sides: at the end every
  replica's wire JSON equals the host's, and no accepted edit is lost.
- **Two real LYNX instances on one machine**: `LYNX_PROFILE_DIR` + `LYNX_TEST_MODE=1` per instance, loopback TCP,
  a `--session-script` command line (host and join, scripted edits, a scripted take from a WAV input, end, compare
  the host's saved project with the expected one) — the same style as `--selftest`. P3 adds the rendezvous against a
  local `php -S` server, never the live site.
- **Manual (user + friend)**: one LAN session, one internet session; a checklist in `docs/MANUAL_TESTING.md`.

---

## Decisions (the user, 2026-11-01)

The open questions of §10 are answered; where this section and the text above differ, this section wins.

1. **What a guest keeps at the end: the host decides, per person.** Default = only his own part; the host can allow a
   full copy of the project (*Send project…* and, in P2, the participant list). In P1 the choice travels in the
   package (`to.keepFull`) and the guest's LYNX SHARE honours it after *Send my part back* (it keeps only his part
   unless the host allowed a full copy). LYNX SHARE is open source, so this is a courtesy the program keeps, not DRM;
   the Send dialog says so.
2. **Same LYNX SHARE version on every computer** for live sessions (P2/P3): accepted (§1.6). P1 packages only note
   the version they were made with (`lynxVersion` in the manifest) and open in any later version.
3. **At most 4 participants** for now (host included): one constant, `session::maxParticipants = 4`
   (`core/SessionIds.h`); raising it later must not need more than changing that constant.
4. **Testing**: the user will test live sessions (P2/P3) later with his girlfriend's PC and her own plugins. Only
   legitimately installed / licensed plugins or active trials are used; licensing is never bypassed. P1 is tested on
   one machine with two fresh `LYNX_PROFILE_DIR`s (host and guest).
5. **Connectivity: the free path only** — Hostinger rendezvous + public STUN, P2P, LAN / direct IP. A paid relay or
   a VPS relay only if usage grows; not now (§9.1 option (c) until then).

## 10. Open questions for the user

1. **Copy for the guest**: at the end, may a guest save a **full copy** of the host's project by default, or only
   **his own part** unless the host allows more? (Proposed: only his part by default; the host can tick *Save a
   copy* per person.)
2. **Same version**: is it OK that both must use **exactly the same LYNX SHARE version** in live sessions (P2/P3)?
   It keeps everything sounding identical; the update bar makes it one click (Store users may lag a few days).
3. **Session size**: how many people at most — **4** (proposed) or up to 8?
4. **Your friend's setup**: does he have FL Studio installed (then FL's factory samples are never downloaded), and
   which third-party plugins does he use most? (Helps test stand-ins first with the plugins you two really use.)
5. **Relay later**: when P3 shows how often direct connections fail for you two, would you rather (a) use your
   existing VPS for a relay, (b) consider a paid relay, or (c) stay free with *Send project* as the fallback? No need
   to decide now.
