# Round 181a: clip grid and launching (slice N5), what the behaviour is based on

Clean room: LYNX's own code and art. Sources are the reference DAW's published manual and its own shipped sets. No trademarks in the UI.
Tags: **[MANUAL]** published manual, **[DATA]** read from shipped sets, **[INFERRED]** deduced, not proven, **[OPEN]** unverified: LYNX's choice until checked in the running program.

## Launching
| Behaviour | Source | LYNX |
|---|---|---|
| Launch Quantisation: Global, None, 8/4/2/1 bars, 1/2, 1/2T … 1/32 | [MANUAL] Launching Clips 16.4 | `LaunchQuant` |
| A clip's quantisation number 0 = Global | [DATA] every default clip | `LaunchQuant::global` |
| Set-wide default 1 Bar is stored as `GlobalQuantisation` 4, so a clip's number is that + 1 and 1 = None | [INFERRED] from default sets (4 everywhere) and the label order | enum order; the .als mapping must confirm it |
| Trigger: up ignored. Gate: up stops. Toggle: next down stops. Repeat: retriggers at the clip quantisation rate while held | [MANUAL] 16.2 | `LaunchMode`; numbers 0…3 [DATA] ALS-FORMAT §5 |
| Gate's stop on release waits for the global quantisation | [OPEN] | as stated |
| After Repeat is released the clip plays on | [OPEN] | as stated |
| Legato takes over the play position of the clip played before on that track, also with quantisation off | [MANUAL] 16.3 | `ClipGridSlot::legato` |
| Grid counted from the song's start | [OPEN] | on the steady transport clock, which equals the song position until the song loops |
| Launch while stopped starts the transport | [OPEN] (commonly known) | `AudioEngine::launcherCommand` |
| A track's stop and Stop All wait for the global quantisation | [OPEN], the manual page is silent | as stated |
| In a scene launch each clip keeps its own quantisation | [OPEN] | as stated |

## Follow actions
| Behaviour | Source | LYNX |
|---|---|---|
| No Action, Stop, Play Again, Previous, Next, First, Last, Any, Other, Jump; two actions with chances | [MANUAL] 16.7 | `FollowKind` in that order |
| Numbers in a set follow that order: the default set stores A = 4 and B = 0, which reads as Next and No Action | [INFERRED] from the default only; the .als mapping must confirm it | |
| Linked: after the clip's end × the multiplier. Unlinked: after the Follow Action Time | [MANUAL] 16.7 | `ClipFollow::linked`, `iterations`, `ticks` |
| "Follow Actions circumvent global quantization but not clip quantization" | [MANUAL] 16.7 | `Player::follow` |
| A global switch for the whole set | [MANUAL] 16.7 | `ClipGrid::followActions` |
| A group is a run of filled slots; Next from the last wraps to the first | [OPEN], the manual says only "group" | `followGroup` |
| Follow actions on scenes | [MANUAL] 16.7 | not built yet |

## Scenes and the arrangement
| Behaviour | Source | LYNX |
|---|---|---|
| A Scene launch starts every clip in the row | [MANUAL] Session View | `launchScene` |
| Empty slots hold stop buttons, removable per slot so a scene leaves that track alone | [MANUAL] Session View; [DATA] `HasStop` | `setStopButton`, a hidden length-0 Clip so the mark follows its track |
| Session and arrangement clips of one track are mutually exclusive | [MANUAL] Session View | `Player::taken()` |
| A clip stop gives silence, the arrangement does not resume until Back to Arrangement | [MANUAL] Session View | taken stays set, also over a transport stop ([OPEN] for the transport stop) |
| Back to Arrangement acts at once | [OPEN] | `backToArrangement`: clips end, the Playlist resumes mid-clip in that block |

| Scene tempo and time signature apply when the scene is launched | [MANUAL] Session View | `ClipGridScene::tempo`, `beatsPerBar`; applied at the press, not at the grid point ([OPEN]) |
| With record on, launched clips are logged into the arrangement | [MANUAL] Session View | `updateLauncherRecording`: a pass becomes a Playlist clip; clip property, mixer and tempo logging are not built |

## Not built yet
Scene follow actions, velocity amount, select next scene on launch, drag and drop into slots, audio slots from the UI (the engine plays them), per-cell screen-reader names, .als mapping of the grid (with round 175a).

## Best way?
- **FL:** Performance mode launches clips placed before the Start marker on the Playlist (`core/Performance.h`, REA round 165m). Sync is per track, with independent trigger and position sync. There is no grid, scene or follow action.
- **Live:** a clip grid with per-clip launch settings. Reference for this slice.
- **Users:** the Session view is the most-cited reason to leave FL (research-web-2 §2.8, 6 sources). The Back to Arrangement state confuses people ([forum](https://forum.ableton.com/viewtopic.php?p=1756364)). Bitwig's launcher beside the arranger is praised ([SOS](https://www.soundonsound.com/reviews/bitwig-studio)).
- **Verdict:** follow Live's launch rules; Pattern workflow stays the default. Coordinator's decisions (2026-11-06; the user may still change them):
  1. Option "Give tracks back to the Playlist when the transport stops" (`ClipGrid::backOnStop`), off by default as in the manual, plus a visible state on silenced tracks and an easy way back.
  2. Clip-launch workflow shows the grid beside the Playlist by default, plus the [Grid|Song] flip. Tested at small window sizes.

## Why the grid does not reuse perf::Player directly
perf::Player finds clips by (track, start) in the Playlist zone and syncs per track, so it cannot express per-clip quantisation, legato or scenes without changing FL-exact code. The new Player keeps its shape (fixed arrays, one virtual clip per pass) and shares `scheduleSingleClip`.

## Cost when off
- `Song::clipGrid` is an empty optional. `Clip::gridSlot` sits in padding (checked at compile time against the layout, see the tests).
- `launcher::Runtime` is made only when a published song has a grid.
- The block's launcher test replaces the one Performance mode already had (`state.launchers`): the same number of conditions on the default path.
