# Live set (.als) format — Live 11 / 12 (round 175a)

Clean-room notes for LYNX's .als import/export. Evidence tags:
- **[DATA]**: read from the sets that ship with Live 12 Trial (19 sets decompressed into `/mnt/e/lynx-work/ableton/xml/`: `DefaultLiveSet`, the Core Library templates, lessons and the Live 12 Suite demo song, 60 KB–27 MB of XML). Schema skeletons came from the scripts in the round's scratchpad (`skel.py`, `tree.py`, `where.py`, `devs.py`).
- **[BIN]**: string and RTTI tables of `Ableton Live 12 Trial.exe` (Live 12.4). `live-exe-strings.txt` holds the ASCII strings and `live-exe-strings-u16.txt` the UTF-16 ones. The serializer writes each class member `mFoo` as the element `<Foo>`, so the member-name lists give the element names of records the shipped sets never contain, such as third-party plugins.
- **[LIVE]**: checked in the running Live 12.4.16 Trial (sets saved by Live, or LYNX-written sets opened in it).
- **[TODO-LIVE]**: still has to be checked in the running app after "resume". Save a set with a VST2, a VST3 and a warped vocal, and re-open LYNX's exported sets.

## 1. Container
- An `.als` file is **gzip** (RFC 1952) around UTF-8 XML. No other wrapper. **[DATA]**
- Root element: `<Ableton MajorVersion="5" MinorVersion="12.0_12117" SchemaChangeCount="10" Creator="Ableton Live 12.1d1" Revision="…">`. Live 11 writes `MinorVersion="11.0_…"`. Live refuses a set without `MinorVersion` ("MinorVersion attribute missing from 'Ableton' root node", and "Unsupported MinorVersion ($1)" for unknown ones). **[DATA][BIN]**
- Scalars use one form throughout: `<Name Value="…"/>`. Booleans are `true`/`false`, numbers use `.` as decimal point at full precision, and enums are integers. **[DATA]**
- `Id` attributes number list items (unique per list). `NextPointeeId` (at the LiveSet top) must be greater than every `AutomationTarget`/`ModulationTarget`/`Pointee` Id in the file. Automation refers to those Ids. **[DATA]**
- `LomId`, `LomIdView`, `*ListWrapper LomId`, `ViewData` (JSON), `ViewState*`, `IsContentSelectedInDocument` and the scroll/zoom values are UI state, and `0`/`{}` are safe defaults. **[DATA]**

## 2. Time
- Every arrangement/clip time is in **beats (quarter notes), as a double**. LYNX ticks = beats × 960. **[DATA]**
- Automation events may carry `Time="-63072000"`. That is the "before the song" point which holds the initial value. **[DATA]**
- Tempo: `LiveSet/MainTrack/DeviceChain/Mixer/Tempo/Manual` (BPM). Its `MidiControllerRange` is 60–200, while the stored range is 10–999. Tempo automation is an `AutomationEnvelope` on MainTrack whose `PointeeId` = `Tempo/AutomationTarget@Id`. **[DATA]**
- Time signature: `MainTrack/DeviceChain/Mixer/TimeSignature/Manual` = `(numerator − 1) + 99 × log2(denominator)`. For example 4/4 = 201, 3/4 = 200 and 6/8 = 302. Range 0–494: numerator 1–99, denominator 1–16. Clips carry their own `TimeSignature/TimeSignatures/RemoteableTimeSignature{Numerator,Denominator,Time}`. **[DATA]**
- **Live 11** names the main track `MasterTrack`, and **Live 12** names it `MainTrack` (`AutoColorPickerForReturnAndMasterTracks` → `…AndMainTracks`). Live 12 still knows the old names (`MasterTrack/MasterChain/…` legacy paths in the binary). **[DATA][BIN]**

## 3. LiveSet top level (selected)
| Element | Meaning |
|---|---|
| `Tracks` | `MidiTrack`, `AudioTrack`, `GroupTrack`, `ReturnTrack` in display order (returns last) |
| `MainTrack` (11: `MasterTrack`) | master channel. Its Mixer holds Tempo and TimeSignature |
| `PreHearTrack` | browser preview track (ignore) |
| `Locators/Locators/Locator{Time, Name, Annotation, IsSongStart}` | arrangement markers |
| `Scenes/Scene{Name, Color, Tempo, IsTempoEnabled, TimeSignatureId, IsTimeSignatureEnabled, FollowAction}` | Session scene rows. ClipSlot *i* of every track belongs to Scene *i* |
| `Transport{LoopOn, LoopStart, LoopLength, CurrentTime}` | arrangement loop (beats) |
| `GroovePool/Grooves/Groove` | grooves (each holds a MIDI clip pattern, Base, Quantization, Timing, Random, Velocity) |
| `ScaleInformation{Root, Name}`, `InKey` | Live 12 project scale. `Name` = index into the scale chooser (C Bhairav = Root 0, Name 22 in a Live 12.4 save); LYNX's project key round-trips through it since round 190o (`core/LiveScales.h`, REA_rodada190o.md §4) |
| `SendsPre/SendPreBool` | per return: send is pre-fader |
| `Grid`, `ViewStates`, `SequencerNavigator` | UI state |

## 4. Tracks
All track kinds share **[DATA]**:
- `@Id` (unique).
- `Name{EffectiveName, UserName}`: use `UserName` when it is not empty. Live puts `#` in a name for auto numbering.
- `Color` (index into Live's 70-colour palette).
- `TrackGroupId` (= the `@Id` of the enclosing `GroupTrack`, −1 = none) and `TrackUnfolded`.
- `AutomationEnvelopes/Envelopes/AutomationEnvelope`.
- `TakeLanes/TakeLanes/TakeLane{Name, ClipAutomation/Events/(MidiClip|AudioClip)}`: Live 11+ comping lanes.
- `LinkedTrackGroupId`.
- `Freeze`.
- `DeviceChain`, which contains:
  - `AudioInputRouting`/`MidiInputRouting`/`AudioOutputRouting`/`MidiOutputRouting{Target, UpperDisplayString, LowerDisplayString}`. Output targets:
    - `AudioOut/Main`;
    - `AudioOut/GroupTrack` (to its group);
    - `AudioOut/External/S0` (hardware pair 1/2);
    - `AudioOut/None`;
    - `AudioOut/Track.<Id>/TrackIn` / `…/DeviceIn…` (to another track or a sidechain).
  - `Mixer` (§6).
  - `MainSequencer` (clips, §5).
  - `FreezeSequencer` (frozen audio).
  - `DeviceChain/Devices` (§7).

Kind specifics:
- `MidiTrack`: `PitchbendRange`, `IsTuned`, MPE settings. Notes go to its instrument.
- `AudioTrack`: arrangement clips in `MainSequencer/Sample/ArrangerAutomation/Events/AudioClip`.
- `MidiTrack`: arrangement clips in `MainSequencer/ClipTimeable/ArrangerAutomation/Events/MidiClip`.
- Session clips (both): `MainSequencer/ClipSlotList/ClipSlot[i]/ClipSlot/Value/(MidiClip|AudioClip)`. An empty slot has an empty `Value`. `HasStop` marks the slot's stop button.
- `GroupTrack`: no clips. Children route to it with `AudioOut/GroupTrack`.
- `ReturnTrack`: its index among the return tracks = index of the matching `TrackSendHolder` in every track's `Mixer/Sends`.

## 5. Clips
Common (MidiClip and AudioClip) **[DATA]**:
- `@Time`: the clip's position in the arrangement (beats). Session clips have `Time="0"`.
- `CurrentStart`, `CurrentEnd`: arrangement span (beats).
- `Loop{LoopStart, LoopEnd, StartRelative, LoopOn, OutMarker, HiddenLoopStart, HiddenLoopEnd}` in clip-content beats:
  - `LoopOn` true: playback starts at `LoopStart + StartRelative` and loops `[LoopStart, LoopEnd)` until `CurrentEnd`.
  - `LoopOn` false: it plays from `LoopStart` up to `OutMarker` (end marker).
- `Name`, `Color`, `Disabled` (clip deactivated = muted).
- Session settings: `LaunchMode` (0 trigger, 1 gate, 2 toggle, 3 repeat), `LaunchQuantisation` (0 = global), `Legato`, `Ram`.
- `GrooveSettings/GrooveId` (−1 none), `VelocityAmount`.
- `FollowAction{FollowTime, IsLinked, LoopIterations, FollowActionA, FollowActionB, FollowChanceA, FollowChanceB, JumpIndexA, JumpIndexB, FollowActionEnabled}`.
- `Envelopes/Envelopes/ClipEnvelope{EnvelopeTarget/PointeeId, Automation/Events}`: clip automation and modulation, in clip time.
- `IsWarped`, `TakeId`.

MidiClip **[DATA]**:
- `Notes/KeyTracks/KeyTrack{MidiKey, Notes/MidiNoteEvent{Time, Duration, Velocity, OffVelocity, NoteId, [VelocityDeviation, Probability, IsEnabled]}}`. Times are relative to the clip content (beats). Velocity is 0–127, as a float.
- `Notes/PerNoteEventStore/EventLists/PerNoteEventList{@NoteId, @CC}`: MPE per-note pitch bend/slide/pressure.
- `BankSelectCoarse/Fine`, `ProgramChange` (−1 none).
- `ScaleInformation`, `IsInKey`.

AudioClip **[DATA]**:
- `SampleRef/FileRef`:
  - `RelativePathType` (0 none, 1 relative to the set folder, 3 user library, 5 Live pack / Core Library), `RelativePath`, `Path` (absolute, as saved), `Type` (2 = file), `LivePackName`, `LivePackId`, `OriginalFileSize`, `OriginalCrc`.
  - Next to FileRef: `DefaultDuration` (frames), `DefaultSampleRate`.
  - Live 11 sets may hold `RelativePathElement` lists instead of `RelativePath`, and the reader accepts both.
- `WarpMode`:
  - 0 Beats, 1 Tones, 2 Texture, 3 Re-Pitch, 4 Complex, 6 Complex Pro. 5 is not used. In the shipped sets: 573× Beats, 35× Complex, 28× Re-Pitch, 6× Complex Pro.
  - Per-mode parameters: `GranularityTones`, `GranularityTexture`, `FluctuationTexture`, `TransientResolution`, `TransientLoopMode`, `TransientEnvelope`, `ComplexProFormants`, `ComplexProEnvelope`.
- `WarpMarkers/WarpMarker{@SecTime, @BeatTime}`: the mapping between sample seconds and clip beats, piecewise linear between markers. Live writes one extra marker past the last real one to give the final segment's rate. `IsWarped=false`: the clip plays at its own speed, and Loop/Start values are in **seconds**.
- `PitchCoarse` (semitones), `PitchFine` (cents), `SampleVolume` (linear gain).
- `Fades{FadeInLength, FadeOutLength, FadeIn/OutCurveSkew/Slope}`, in beats in the arrangement.
- `Onsets/UserOnsets` (transient markers), `Sync`, `HiQ`, `IsSongTempoLeader`.

## 6. Mixer (`DeviceChain/Mixer`) [DATA]
| Element | Meaning | LYNX |
|---|---|---|
| `Volume/Manual` | linear gain, 0.000316 (−70 dB = −inf) … 1.99526 (+6 dB); 1 = 0 dB | `MixerTrack::volume = gain × unityFader` |
| `Pan/Manual` | −1…1 (`PanMode` 1 = split stereo: `SplitStereoPanL/R`) | `pan` |
| `Speaker/Manual` | track on (false = muted) | `muted = !on` |
| `SoloSink` | solo | `solo` |
| `Sends/TrackSendHolder[i]{Send/Manual, EnabledByUser}` | send level to return *i*, linear gain (0.000316 = off) | `MixerSend` |
| `CrossFadeState` | DJ crossfader A/B | dropped |
| `Tempo`, `TimeSignature`, `GlobalGrooveAmount`, `CrossFade` | main track only | song |

Every automatable parameter has the form `{Manual, [MidiControllerRange{Min,Max}], AutomationTarget@Id, ModulationTarget@Id}`. Automation: `AutomationEnvelope{EnvelopeTarget/PointeeId = AutomationTarget@Id, Automation/Events/(FloatEvent|BoolEvent|EnumEvent){@Time, @Value}}`. Float events are linear between points (Live 11+ curves: `CurveControl1X/Y`, `CurveControl2X/Y` on an event). **[DATA]**

## 7. Devices
`DeviceChain/DeviceChain/Devices/*`, in signal order. Every device has `On{Manual}`, `UserName`, `LastPresetRef`, `SourceContext` and its parameters (same automatable form). **[DATA]**

Native devices found in the shipped sets, with counts **[DATA]**:
- Instruments:
  - OriginalSimpler 330, Operator 25, InstrumentVector 23, Drift 23, MultiSampler 3;
  - StringStudio 8, UltraAnalog 7, InstrumentMeld 2, LoungeLizard 2, Tension, Collision.
- Racks:
  - InstrumentGroupDevice 129, AudioEffectGroupDevice 115, DrumGroupDevice 12, MidiEffectGroupDevice 2.
  - Each has `Branches/(InstrumentBranch|AudioEffectBranch|DrumBranch|MidiEffectBranch){DeviceChain/…/Devices, MixerDevice, KeyRange, VelocityRange, BranchSelectorRange, ReceivingNote (drum pad)}` and `MacroControls.0–15`.
- Audio effects:
  - Eq8 181, Compressor2 149, Reverb 98, Delay 63, MultibandDynamics 63, AutoFilter(2) 50, StereoGain 38, Hybrid 34, DrumBuss 33, Saturator 29, GlueCompressor 24, Overdrive 22, GrainDelay 15, Limiter 14, FilterEQ3 12, Gate 12, Echo 12, Spectral 12, Roar 11, ChannelEq 10, Chorus2 10, AutoPan 9, Amp 9, Cabinet 7, Chorus 6, Redux2 6, Vinyl 5, Vocoder 3, Transmute 10, Erosion, Pedal, Tube, FilterDelay, BeatRepeat, Phaser, Flanger, FrequencyShifter, AutoShift, Tuner, SpectrumAnalyzer.
- MIDI effects: MidiVelocity 17, MidiArpeggiator 13, MidiChord 6, MidiScale 5, MidiPitcher, MidiNoteLength.
- Max devices: MxDeviceAudioEffect, MxDeviceMidiEffect, MxDeviceInstrument (patch embedded or referenced). Black box: kept by reference only.

Simpler (`OriginalSimpler`) **[DATA]**:
- `Player/MultiSampleMap/SampleParts/MultiSamplePart{KeyRange, VelocityRange, RootKey, Detune, Volume, Panorama, SampleStart, SampleEnd, SustainLoop, ReleaseLoop, SampleRef, SampleWarpProperties{WarpMarkers, WarpMode, IsWarped…}}`;
- `Player/PlaybackMode` (0 classic, 1 one-shot, 2 slicing), `Player/SlicingStyle`, `Player/SlicingSensitivity`;
- `Envelope`, `Filter`, `Lfo` blocks.

Sampler (`MultiSampler`) uses the same `MultiSampleMap` with many parts. Drum Rack: `DrumBranch/BranchInfo/ReceivingNote` = `128 − MIDI note` of the pad (92 ↔ C1 = 36) **[DATA]**, confirmed against the pad names and clip notes of the shipped kits.

Third-party plugins **[BIN]**. The shipped sets contain none. Member names come from the binary's UTF-16 member table, next to RTTI `AVstPluginInfo`, `AVst3PluginInfo`, `AVst3Preset`, `AVstPreset`, `AAuPluginInfo`:
```
PluginDevice (VST2/VST3)  |  AuPluginDevice (macOS AU)
  On, ParameterList/PluginFloatParameter{ParameterName, ParameterId, VisualIndex, ParameterValue{Manual…}}
  PluginDesc
    VstPluginInfo { Path, PlugName, UniqueId, Inputs, Outputs, NumberOfParameters, NumberOfPrograms,
                    Flags, Category, VstVersion, IsShellClient, Preset/VstPreset{…, Buffer (hex text), ProgramNumber, ParameterSettings} }
    Vst3PluginInfo { Name, Uid{Fields.0..3 (signed int32 each)}, DeviceType (1 instrument, 2 effect),
                    Preset/Vst3Preset{ ProcessorState (hex text), ControllerState (hex text), MpeEnabled, ParameterSettings } }
    AuPluginInfo { ComponentType, ComponentSubType, ComponentManufacturer, ComponentFlags, Manufacturer, Name,
                    Preset/AuPreset{ Buffer… } }
  MpeEnabled, ParameterSettings/PluginParameterSettings{ParameterId, VisualIndex, MacroControlIndex}
```
- The VST2 `Buffer` is the plugin's `effGetChunk` data (or the parameter bank).
- **[LIVE] VST2 layout written by Live 12.4.16** (kHs Gain VST2):
  - `VstPluginInfo{WinPosX, WinPosY, NumAudioInputs, NumAudioOutputs, IsPlaceholderDevice, Path (absolute, '/'), PlugName, UniqueId (the VST2 id as a decimal int: 1802725230 = 'ksgn'), Inputs, Outputs, NumberOfParameters, NumberOfPrograms, Flags (1280: Live's own bits, not effFlags), Preset/VstPreset{OverwriteProtectionNumber, MpeEnabled, MpeSettings, ParameterSettings, IsOn, PowerMacroControlIndex, PowerMacroMappingRange{Min,Max}, IsFolded, StoredAllParameters, DeviceLomId, DeviceViewLomId, IsOnLomId, ParametersListWrapperLomId, Type, ProgramCount, ParameterCount, ProgramNumber, Buffer, Name, PluginVersion, UniqueId, ByteOrder (2), PresetRef}, Version, VstVersion (2400), IsShellClient, Category}`.
  - `Type` is the fxp four-character code: 1178747752 = 'FBCh' (bank chunk). `Buffer` is then the raw chunk as hex text; kHs Gain's starts `504B0304`, a zip.
  - LYNX reads 'FBCh'/'FPCh' as a chunk. Any other type is read as parameter floats; their byte order (ByteOrder) is still **[TODO-LIVE]**.
- VST3 `ProcessorState`/`ControllerState` are the component and controller `IBStream` bytes, which are exactly what LYNX's `flVst3State` pair carries.
- The four `Fields.N` are the FUID's four 32-bit words in its string order. For kHs Gain, Fields.0 = −1481959855 = 0xA7AB1651 is the first eight hex digits of the class id; `BranchDeviceId` reads `device:vst3:audiofx:a7ab1651-…`. JUCE's normalised `uniqueId` is `Σ Fields·31ⁿ`, and it equals the plugin list's `878ab867` **[LIVE]**. LYNX: `alsVst3ClassId`/`alsVst3Fields` convert to and from the COM memory layout of `ForeignPlugin::classId`.
- **[LIVE] layout written by Live 12.4.16** (kHs Gain VST3 on an audio track, saved by Live):
```
PluginDevice{ IsExpanded, BreakoutIsExpanded, On{Manual, AutomationTarget, MidiCCOnOffThresholds}, ModulationSourceCount,
  Pointee, LastSelectedTimeableIndex, LastSelectedClipEnvelopeIndex, LastPresetRef, LockedScripts, IsFolded,
  ShouldShowPresetName, UserName, Annotation,
  SourceContext/Value/BranchSourceContext{OriginalFileRef, BrowserContentPath, LocalFiltersJson, PresetRef, BranchDeviceId "device:vst3:audiofx:<uid>"},
  MpePitchBendUsesTuning, ViewData,
  PluginDesc/Vst3PluginInfo{ WinPosX, WinPosY, NumAudioInputs, NumAudioOutputs, IsPlaceholderDevice,
    Preset/Vst3Preset{ OverwriteProtectionNumber, MpeEnabled, MpeSettings, ParameterSettings, IsOn, PowerMacroControlIndex,
      PowerMacroMappingRange, IsFolded, StoredAllParameters, Uid{Fields.0..3}, DeviceType, ProcessorState, ControllerState, Name, PresetRef },
    Name, Uid{Fields.0..3}, DeviceType (2 = audio effect) },
  MpeEnabled, MpeSettings, ParameterList, AxisX, AxisY, SideChain{…} }
```
- `ProcessorState`/`ControllerState` are element **text**: uppercase hex, 40 bytes (80 digits) per line, indented. They hold the raw `IComponent::getState` / `IEditController::getState` bytes. kHs Gain's starts `01000000 0E020000 504B0304…`, which is the plugin's own header followed by a zip.
- Blobs are written as hex text inside the element, line-wrapped with whitespace. The reader ignores whitespace.

## 8. What LYNX maps (summary)
| Live | LYNX |
|---|---|
| MidiTrack + instrument | channel (instrument) + mixer insert + Playlist track |
| MidiClip (arrangement) | its own pattern (the clip's notes) + pattern clip; looped clip = clip longer than the pattern (stretched clips loop) |
| AudioTrack + AudioClip | audioClip channel per sample + audio clip; `PitchCoarse/Fine` → clip pitch; `SampleVolume` → clip gain; warp → clip `stretch` (one rate between first and last marker), or, when the clip is stretched offline (keeps its pitch, file length known), the channel stretches the file to its content beats; markers that bend the tempo stay as the channel's warp markers and each segment is stretched at its own rate in one pass (round 175a, M1; export writes them back) |
| GroupTrack | mixer insert. Children route to it (`toMaster` off + send) and their Playlist tracks are grouped under a header track |
| ReturnTrack + sends | mixer insert + `MixerSend` from every track with level > −70 dB |
| Mixer Volume/Pan/Speaker/Solo | insert volume/pan/mute/solo |
| Tempo / TimeSignature + automation | `Song::bpm`, `beatsPerBar` + tempo automation clip; signature changes → markers |
| Locators | Playlist markers |
| Session clips / Scenes | the clip grid (round 181c, section 13; the default by the user's choice of 2026-11-08); Settings › General › Workflow can put them on the Playlist after the arrangement instead, one section per scene (a marker per scene) |
| Volume/Pan/Send/plugin-param automation | automation clips on the matching target (round 190ed, §18: rack macros, devices inside racks through a free macro, VST2 plugin parameters, a Simpler's filter; VST3 parameters listed) |
| PluginDevice | `PluginRef.foreign` (vst2 id / vst3 class id + state), resolved like an .flp's plugins |
| Native devices | the LYNX engine in the same role with its settings translated (slice N3, section 14: Simpler, Sampler zones, Drum Rack pads, racks, EQ Eight, Compressor, Glue Compressor, Utility, Reverb; lot 2 §15; lot 3 §18: EQ Three, Drum Buss, Auto Filter's modulation, Operator without FM, Drift); every other device is dropped and listed in the import report |

## 9. What Live's loader requires (found by experiment, 2026-11-06) [LIVE]
Live crashes on a set that is merely well-formed. It either exits or shows "A serious program error". A set written by another program must follow these rules:
1. **Complete objects.** Every object has all of its children, in Live's own order. Missing members are not defaulted.
2. **Unique ids.** Every `Id` on `Pointee`, `*Target` (AutomationTarget, ModulationTarget, …) and `ControllerTargets.N` element is unique.
3. **`NextPointeeId`.** It must be greater than the largest id in the file, or Live reports "NextPointeeId is too low: X must be bigger than Y".
4. **Envelopes.** An `AutomationEnvelope` whose `EnvelopeTarget/PointeeId` names a missing id is fatal ("Invalid Pointee ID … in APtr …").
5. **Sends.** `Mixer/Sends` and the set's `SendsPre` have exactly one entry per ReturnTrack.
6. **Clip slots.** Each MIDI and audio track's `ClipSlotList`, and its `FreezeSequencer` `ClipSlotList`, has exactly one `ClipSlot` per Scene. Group and return tracks have none: their `FreezeSequencer` `ClipSlotList` is empty, and a return track's own `Mixer/Sends` holders are `EnabledByUser` false, as Live saves them (round 182b: Live 12.4 crashed with an access violation on exports whose return track broke this).
7. **Saving.** Live saves only inside a project folder: `<name> Project/<name>.als`, next to `Ableton Project Info/`. The exporter writes that layout.

LYNX's exporter (slice N2) therefore emits each element kind from its own generator. The generator writes every member name in Live's order with default values (format facts recorded here), then numbers the ids and sets `NextPointeeId`. It does not ship or copy Live's own files. A prototype (`tools/complete.py`) produced `probe/p1/probeD6.als`, which opens in Live with a MIDI clip, a warped audio clip and a locator.

## 10. Warp behaviour measured in Live 12.4 (2026-11-06) [LIVE]
The reference renders are in `warp-refs/`: one vocal, 6 warp modes, played at 0.8× speed, 1.25× speed and +5 st. LYNX's import was compared with them using `LYNX.exe --render` on single-track sets (`/mnt/e/lynx-work/ableton175a/render-compare.txt`).
- **Re-Pitch ignores the clip's transpose** (`PitchCoarse`/`PitchFine`). The tempo alone sets the pitch: +5 st renders at the source's 184 Hz. The import sets the clip pitch to 0 for WarpMode 3.
- **Level.** Every mode keeps the source level (±0.4 dB), except Complex Pro transposed +5 st, which is 3 dB quieter with a tilted spectrum (formant handling, Formants = 100). That case goes to N4.
- **Unity gain in LYNX.** A Playlist audio clip plays at unity when its channel volume is 10000/12800 (FL's default), so imported audio-clip channels are set to that value.
- **How the import matches today** (envelope correlation over the clip):
  - Re-Pitch: 1.000.
  - Complex / Complex Pro: 0.95–0.97.
  - Texture: 0.92–0.94.
  - Tones: 0.88–0.92.
  - Beats: 0.76–0.94.
  - The N4 own-DSP modes target these numbers.

## 11. Export (slice N2, 2026-11-06)
File › Export › "Ableton Live set (.als)..." and `LYNX.exe --export-als <in> <folder> [--live11] [--reference] [--scan]` write `<folder>/<name> Project/<name>.als`. The samples are copied to `Samples/Imported/` and referenced through `RelativePath`; with "Refer to them where they are" (`--reference`) they keep their absolute `Path`. The other way, `LYNX.exe --export-flp <set.als> <out.flp|zip>` opens a Live set and writes it as an FL Studio project (round 182b). `--export <set.als> <out.wav>` and `--stems <set.als> <base.wav>` render a set the same way.

**Skeletons.** Each element starts from its kind's skeleton in `src/core/AlsSkeleton.h`, which `tools/als_skeleton.py` generates from sets saved by Live (kept local, never committed):
- **Live 12:** the 12.4 shipped sets, plus two probe sets saved on this machine.
- **Live 11:** a public 11.0.12 set (MIT) holds most kinds. GroupTrack, AudioClip and the VST3 PluginDevice come from a public 11.0.12 set (MPL-2.0). Locator comes from a public 11.3.3 set.
- **Why real Live 11 samples:** the earlier Live 12→11 downgrade (`to11`) differed from the real Live 11 objects. GroupTrack missed 36 members and AudioClip had 7 wrong ones (for example `IsSongTempoMaster`, not `IsSongTempoLeader`). Only Locator matched.

**Rules found while exporting** [LIVE, Live 12.4]. These add to §9:
8. **Numbers stay numbers.** A member that holds a number never gets an empty value. `ScaleInformation/Name` is the scale index in Live 12 ("Unexpected value for int node" when empty) and a name such as "Major" in Live 11.
9. **Group slots.** A GroupTrack has one `GroupTrackSlot` per scene in its own `Slots` list, and an *empty* `FreezeSequencer/ClipSlotList`. Live 11.0–12.2 save it that way. With freeze slots present, Live 12.4 refuses the set: "List of group track freeze sequencer slots not empty".
9b. **List ids.** Items of one list have unique `Id`s ("Non-unique list ids"). The skeletons keep Live's own ids inside an object and reset only the object's own id.
10. **Note attributes.** Live 11 writes every `MidiNoteEvent` attribute: Time Duration Velocity VelocityDeviation OffVelocity Probability IsEnabled NoteId. Live 12 leaves out the default ones but keeps the same order [DATA].
11. **Before-the-song event.** An envelope opens with the "before the song" event at −63072000. A point at time 0 *is* that event; it is not written twice [DATA].
12. **Routings point at this set.** Every routing `Target` is `AudioIn/…`, `AudioOut/…`, `MidiIn/…` or `MidiOut/…`. A track path (`p46251/p41230/p36996`) left over from the set a skeleton came from makes Live 12.4 crash on load with "A serious program error" (`std::out_of_range`, "invalid map<K, T> key" in Log.txt). The Live 11 GroupTrack skeleton carried one in its `AudioInputRouting`; the exporter now writes `AudioIn/External/S0` ("Ext. In", "1/2"), as Live saves groups. Found by bisecting the Live 11 export: ungrouped it loads, one plain group crashes, a real Live 11.3 group transplanted in loads, and the fixed group loads [LIVE, Live 12.4.16].
13. **Sample formats.** Live reads WAV, AIFF, FLAC, Ogg and MP3. It does not read WavPack (`.wv`) or Ogg-in-WAV (format tags 0x674F–0x6751, logged as "Unknown unsupported format 26447"). The exporter decodes such samples and writes them as 32-bit float `.wav` in `Samples/Imported/`, also with "Refer to them where they are" [LIVE].

**Mapping from LYNX:**
- **Tracks:**
  - A channel with notes, or with a plugin, becomes a MIDI track.
  - A Playlist track with audio clips becomes an audio track. Its gain is the channel gain relative to the unity value 0.78125.
  - An insert with one track folds into that track.
  - An insert shared by several tracks becomes a GroupTrack.
  - An insert with no track that other inserts route into (no Master output, single route) becomes a GroupTrack. Groups nest when they are routed into another bus.
  - Any other insert with no track that is fed by sends becomes a ReturnTrack.
- **Plugins:** loaded plugins export through `PluginHost::flWrapperRecord`, giving VST3 class id plus component/controller state, or the VST2 id plus chunk.
- **Not converted yet:** LYNX's own instruments and effects (N3). They are listed in the export report.

**Offline checks** (scratchpad `schema_check.py` / `loader_check.py`):
- The export is compared, element by element, with real sets of its version: parent/tag pairs, child layouts, attribute sets and value kinds.
- The §9 rules are checked on every exported file.
- References:
  - Live 11: two 11.0.12 sets (ML-Ableton, MIT), two abletoolz fixtures (11.0.0 and 11.0.12, MPL-2.0), and an 11.3.3 sample set.
  - Live 12: the 12.4 shipped sets and a 12.2.1 public set.

## 12. Warp modes (slice N4, 2026-11-06)
Reference: Live 12.4.16 renders of a test voice (`warp-refs/`, 48 kHz, clip warped 0.8×, 1.25×, and +5 st), compared by RMS-envelope correlation and mean spectral deviation (24 log bands).
- **Live's Beats, measured** (output→source time map by cross-correlation, forward and reversed): the audio is cut at transients only (no cut at every 1/16 on sustained material); each piece starts where the warp map puts its cut and plays at 1× speed. Faster: cut where the next piece begins. Slower: after its end the piece plays backwards, then forwards ("back and forth", `TransientLoopMode` 2), until the next piece. A tone is cut at its very start; a swell at its steepest rise.
- **LYNX `StretchMethod::beats`** (`TimeStretch.cpp` `beatsStretch`, `stretchdetail::beatsCuts`): LYNX's attack detector, each cut moved to the start of the 10 ms window before the steepest 10 ms RMS rise (−30…+50 ms); 16 ms linear crossfades (1–4 ms: clicks Live has not; 16 ms: 0.2–0.3 dB spectral deviation). Envelope correlation 0.90/0.98 (old transients preset) → 0.967/0.999.
- **Tones / Texture / Complex** use the spectral stretcher with 40 / 60 / 90 ms blocks, overlap 6, no attack splicing: 0.977 / 0.985 / 0.994 correlation, about 1 dB spectral deviation (2 dB before).
- Mapping: import WarpMode 0→beats, 1→tones, 2→texture, 4 and 6→complex, 3→Re-Pitch (no pitch keep); export beats/transients→0, tones/mono→1, texture→2, else 4.
- **Beats transposed** (round 175a, the Beats-transpose slice): Live resamples each piece by the clip's transpose (2^(st/12) faster, so higher and shorter), starts it at its own place on the grid and fills the gap back and forth. LYNX renders the same offline: `timeStretch (…, beats, semitones)` reads each piece with 4-point Hermite interpolation. An imported Beats clip with a transpose stores it in its channel's `sampler.stretchPitch` (saved in .lynx as `stretchPitch`). The stretched-sample cache keys on it, in memory and on disk, and the clip plays untransposed. On export the value is written back as `PitchCoarse`/`PitchFine`. Beats-up5 against Live: envelope correlation 0.754 → 0.944, spectral deviation 1.54 → 0.95 dB (the stretcher alone, outside the app). The other modes still transpose in real time.
- Open: the `TransientResolution`, `TransientEnvelope` and Texture `FluctuationTexture` parameters are not modelled.

## Live's .asd analysis files (round 175a, M2)

Live writes `<sample>.asd` next to each sample it has analysed (3,567 in the trial's Core Library). It uses the same binary
serialisation as `Preferences.cfg`: a schema of named records, then the data. Two records matter for auto-warp:

- `OnSets` (`Positions`, `TransitionEnergies`, `IsSet`): Live's transients. The data is a u32 count N, N u32 positions
  (sample frames, increasing), then N f32 energies.
- `WarpMarker` (`SecTime`, `BeatTime`, both f64): the warp markers Live stores for the sample. Data records are 0x0A
  "WarpMarker", a u32, then the two doubles. The loops store their start (0, 0) and one marker that gives the rate.

Read by `/mnt/e/lynx-work/ableton175a/m2/asd.py`. Measured against LYNX on the Core Library loops (`m2/onsets.txt`,
`m2/warp.txt`):

- Transients (±10 ms): LYNX's attack detector matches Live with F 0.81 on drums, but only 0.31 on tonal and 0.36 on
  textures. Live marks many soft onsets that LYNX doesn't. Lowering the threshold to 3 or 4.5 dB changed nothing.
- Tempo: Live uses the tempo in the file name for 148 of 156 named loops. For the others it mostly takes the length as
  a power of two of beats (33 of 47).
- Beat 0: the file's start, for 195 of 203 loops.
- LYNX's auto-warp agrees with Live's stored tempo for 162 of 203 loops without names, 148 of 156 with them, and on the
  start for 194 of 203.

## 13. Session view ↔ clip grid (round 181c, 2026-11-07)
Launch numbers read in Live 12's own Clip View on a set we wrote [LIVE: docs/analise/REA_rodada181a.md, 181b]:
- `LaunchQuantisation` 0…14 = Global, None, 8 Bars, 4 Bars, 2 Bars, 1 Bar, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 = `LaunchQuant`.
- `LaunchMode` 0…3 = Trigger, Gate, Toggle, Repeat = `LaunchMode`.
- `FollowActionA` 0…9 = No Action, Stop, Again, Previous, Next, First, Last, Any, Other, Jump = `FollowKind`. [INFERRED] `FollowActionB` uses the same numbers. `JumpIndexA/B`: Live shows the stored number itself, 1-based (5 shows "5", 0 an empty field) [LIVE, 181c]; LYNX's jump row = the number − 1. [INFERRED] it counts scenes from the top, not clips of the group.
- `GlobalQuantisation` (LiveSet) has no Global entry: it is the clip number − 1 (9 = 1/8; the default 4 = 1 Bar).

Import (the default, the user's choice of 2026-11-08; `AlsImportOptions::sessionOnPlaylist`, Settings › General › Workflow › Session clips of Live sets: Playlist, keeps 175a's placement):
- Each MIDI or audio track's `ClipSlotList` slot *s* → grid column = the track's Playlist track, row *s*. One clip per slot.
- A looping clip → one pass of its loop region `[LoopStart, LoopEnd)` with the slot's Loop on; a start marker inside the loop (`StartRelative` ≠ 0) is reported as approximated. A one-shot → `[LoopStart + StartRelative, OutMarker)` with Loop off. MIDI clips get their own pattern, audio clips the same channel and warp handling as arrangement clips.
- `LaunchMode`, `LaunchQuantisation`, `Legato`, `Name`, `Color`, and `FollowAction{FollowActionEnabled, FollowActionA/B, FollowChanceA/B, JumpIndexA/B, IsLinked, LoopIterations, FollowTime}` → the slot's settings.
- An empty slot with `HasStop` false → no stop button.
- Scenes: `Name`, `Color`, `Tempo` when `IsTempoEnabled`, the numerator of `TimeSignatureId` when `IsTimeSignatureEnabled` (n/4 only; other denominators reported). Scene follow actions are reported (not built).
- A set whose slots are all empty adds no grid.

Export:
- The set gets one scene per grid row; every MIDI and audio track one `ClipSlot` per scene in the main and the freeze sequencer, a group one `GroupTrackSlot` per scene [§9 rule 6].
- A grid pattern clip → a MIDI clip in each channel track it has notes for (the arrangement's mapping); two grid clips of one channel in one scene keep the first (noted). A grid audio clip → the audio track of its Playlist track.
- Session clips have `Time` 0, the clip span as the loop region (`LoopOn` = the slot's Loop) and the slot's launch settings and follow actions. An empty slot without its LYNX stop button gets `HasStop` false (MIDI tracks use their channel's Playlist track).
- Scenes: name, colour, tempo and n/4 time signature with their enable flags; `GlobalQuantisation` = grid quantisation − 1.
- A song whose grid has clips but no Playlist clips exports no arrangement clip (the current-pattern fallback is for songs with neither).

## 14. Live's own devices → LYNX's own (slice N3, round 190bq, 2026-11-11)
Code: `src/core/AlsDevices.{h,cpp}` (the laws, one function per device) and `AlsImport.cpp` (`nativeEffectFrom`, the Simpler branch of `instrumentFrom`). Tests: `tests/AlsDevices190bqTests.inc`. Sources: the device elements of the 19 sets Live 12.4 ships (181 EQ Eight, 149 Compressor, 24 Glue Compressor, 38 Utility, 98 Reverb, 330 Simpler) [DATA], each parameter's own range in `MidiControllerRange` [DATA], Ableton's manual from memory [DOC]. Live was not run and its binary was not read in this round, so every statement about how Live *sounds* is [ASSUMED] until an A/B in Live; the list is in round 190bq's handoff and as [ASSUMED] tags in `AlsDevices.h` / `.cpp`.

A device parameter is `<Name><Manual Value="…"/>…</Name>`; switches are `true` / `false`; enumerations are indices.

| Live device (tag) | LYNX | Translated | Listed in the report |
|---|---|---|---|
| EQ Eight (`Eq8`) | Facet EQ | per band (curve A): on, type, frequency, gain, Q; Scale; output gain; L/R and M/S modes (curve B → right / side bands) | Adaptive Q on a shelf, a 48 dB/oct cut or a shelf outside the measured Q span, a ninth band in L/R or M/S, Q beyond Facet's range |
| Compressor (`Compressor2`) | Crest Compressor, Clean style | threshold, ratio (inf : 1 = Crest's limit), attack, release, auto release, knee, look-ahead, output gain, makeup switch, dry/wet, side-chain input track + gain + EQ + listen | RMS detection, values outside Crest's ranges, linear release, side-chain tap point, side-chain dry/wet; Expand mode is not translated (device dropped and listed) |
| Glue Compressor (`GlueCompressor`) | Crest Compressor, Bus style | threshold, ratio 2 / 4 / 10, attack and release steps, Auto release, range, makeup, dry/wet, oversampling, side chain | Soft clip |
| Utility (`StereoGain`) | Utility | gain, mute, phase L / R, channel mode, Mono, width, mid/side balance, balance | exact: gain within ±24 dB, mute, phase, channel mode, Mono, width and balance (in the matrix while its coefficients stay within ±2); approximated: wider matrices, gain beyond ±24 dB, mid/side balance; not applied: Bass Mono, DC filter |
| Reverb (`Reverb`) | Atrium | dry/wet, predelay, decay, size, input filter → low / high cut, Reflect / Diffuse → early/late, Chorus → late modulation, Spin → early modulation, shelves → bass / high decay multipliers, density → diffusion | Freeze, Stereo, Shape, Scale |
| Simpler (`OriginalSimpler`) | Sampler channel | (besides zones and volume envelope) transpose, volume, pan, filter type / frequency / resonance, filter and pitch envelopes, LFO → volume / pan / cutoff, voices, glide, One-Shot fades and Gate, Classic loop and region, Slice mode (stored points, by region), Warp (one tempo) | filter slope / circuit / drive / key tracking, velocity sensitivity, LFO → pitch, synced LFO rate, Spread, random pan, loop crossfade |

Laws that carry a unit conversion:
- **EQ Eight Q → Facet Q** (Facet's prototypes, `MEASURE_fabfilter.md` §1; Live's side measured in Live 12.4, round 190cw renders, fits in round 190cx): bell and notch `√2·Q` (Live = the textbook bell, 0.03…0.08 dB); 12 dB cuts `√2·Q` (textbook, 0.08 dB); shelves: Live's Q acts as the cookbook Q `0.71·(Q/0.71)^0.394` (Q 0.71 → 0.71, Q 2 → 1.068; best fits 0.709 / 1.068, measured on a +12 dB low shelf only), Facet `(Qs/0.7071)^(1/0.4697)`; 48 dB cuts: an 8th-order Butterworth at Q 0.71 with all four section Qs scaled by `1 + 0.2611·ln(Q/0.714)` (Q 0.5 / 0.71 / 1 → × 0.907 / 0.999 / 1.088, fit 0.14 dB; four equal sections are 2.2 dB off at best) — Facet holds it with its cut law "all sections" (`facet::cutLawParam`, set by the importer only; the default law and every older project are unchanged). Adaptive Q (bells): `Q × AdaptiveQFactor^(|gain|/2)` (1.12 in every saved device, ≈ Q·10^(|gain|/40); Q 2 at ±12 dB → 3.95, measured 3.97 / 3.95). Mode order 0…7 = low cut 48, low cut 12, low shelf, bell, notch, high shelf, high cut 12, high cut 48. Scale −200…200 % → Facet's 0…200 % with the gains turned over when negative.
- **Compressor**: Threshold is a linear gain (0.000316…1.995) → dB; Ratio's largest float = inf : 1; times in ms; look-ahead index 0 / 1 / 2 = 0 / 1 / 10 ms; side chain `AudioIn/Track.<id>/<tap>` → `EffectSlot::sidechain` = that track's insert.
- **Glue**: attack index → 0.01 / 0.1 / 0.3 / 1 / 3 / 10 / 30 ms; release index → 0.1 / 0.2 / 0.4 / 0.6 / 0.8 / 1.2 s, 6 = Auto; ratio index → 2 / 4 / 10; Range 70 dB = no limit.
- **Utility**: `out = gain · B · W · A · in`, A = phase switches then channel mode (0 Left, 1 Stereo, 2 Right, 3 Swap), W = Mono or `M ± width·S` with M = (L+R)/2, S = (L−R)/2, B = the balance, constant power with a boost: L × √2·cos((b+1)π/4), R × √2·sin((b+1)π/4) (+0.5 → 0.541 / 1.307). All three measured in Live 12.4 (round 190cw r13–r17, exact to 1e-4); the balance's place after the width stage is assumed (measured alone).
- **Simpler**: voices index 0…14 → 1…8, 10, 12, 14, 16, 20, 24, 32; envelope times ms → s; filter-envelope amount semitones → octaves; a Slice-mode Simpler plays its first slice on C1 in Live and on key 60 in LYNX, so its notes move up 24 keys on import.

Drum Rack pads keep the pad Simpler's sample, tuning, volume and pan only (one report line). A device element without its parameters (another Live version, a stub) is not translated and is dropped and listed as before. Since lot 2 (§15) automation and rack macro mappings follow a converted device's parameters where the parameter has a LYNX control.

**Export:** no Live-native device is written yet. `alsdev::liveUtilityOf` gives a LYNX Utility's exact Live settings (gain / mute / phase / channel mode / Mono, balance centred) and is tested, but a new device kind needs a skeleton and a load test in Live (§9), which this round could not run.

## 15. Live's own devices → LYNX's own, lot 2 (round 190cf, 2026-11-11)
Code: `src/core/AlsDevicesLot2.cpp` (laws and translators, declared in `AlsDevices.h`), `AlsDevices.cpp` (`effectFromLiveDevice` dispatch and the parameter links of lot 1's devices), `AlsImport.cpp` (`nativeEffectFrom`, `effectsFrom`, `rackDeviceFrom`, `midiEffectFrom`). Tests: `tests/AlsDevices190cfTests.inc`. Sources as in §14: the device elements and `MidiControllerRange` of the 19 shipped sets [DATA], Ableton's manual from memory [DOC]; Live was not run and its binary was not analysed (names from an older static string dump only), so enumeration orders and curve shapes marked [ASSUMED] in the code wait for an A/B in Live (list in the round's handoff).

| Live device (tag) | LYNX | Translated | Listed in the report |
|---|---|---|---|
| Delay (`Delay`) | Echoform, Clean mode | per side: synced sixteenths (1…6, 8, 16) as a note value, or the free time in ms; Link; ping-pong; feedback; freeze; band-pass → low cut + high cut; time modulation rate / depth; dry/wet | 5 sixteenths or an offset (held in ms at the set's tempo), filter modulation, transition mode, linear dry/wet law |
| Echo (`Echo`) | Echoform, Clean mode | per side: division / sixteenths with straight, triplet, dotted modes, or ms; link; feedback (inverted listed); channel mode stereo / ping-pong; input and output gain; high-pass / low-pass → low cut / high cut; modulation rate / depth; dry/wet | Mid/Side mode, filter resonance, Reverb, Noise, Wobble, Gate, Ducking, stereo width, envelope modulation |
| Multiband Dynamics (`MultibandDynamics`) | Prism Multiband, 2 or 3 bands | crossovers, per band: on, one action (above or below the threshold, as compression / limit / expansion / upward lift), thresholds with the band's input gain folded in, output gain, attack, release; Amount; Time; output | a band's second action, RMS detection, times beyond Prism's ranges, soft knee as a fixed 6 dB knee |
| Auto Filter (`AutoFilter`, `AutoFilter2`) | Filter | low-pass / high-pass / band-pass, cutoff (pitch → Hz in the older device), resonance → Q (§16), 12 / 24 dB slope, dry/wet | LFO, envelope follower, circuit / drive, band pass peak level, Q over 12; notch and morph types are not translated (device dropped and listed) |
| Saturator (`Saturator`) | Ember Saturator | drive (negative drive → input gain), curve → the fitted Ember curve with drive / output offsets (§16), output, dry/wet, auto gain off | Color filters, output clip, DC filter, the curve itself (named) |
| Overdrive (`Overdrive`) | Distortion | drive, tone, dry/wet | band-pass before the drive, Dynamics |
| Limiter (`Limiter`) | Apex Limiter, Clean style | gain, ceiling, release, look-ahead, stereo link, true-peak mode | negative gain, release above 1 s, auto release, 6 ms look-ahead, Soft Clip mode, Mid/Side routing, Maximize |
| Gate (`Gate`) | Sentry Gate, Classic style | threshold (linear → dB), attack, hold, release, floor → range, Flip → ducking, look-ahead, side-chain input + high pass / low pass | Return (hysteresis), other side-chain EQ shapes, side-chain dry/wet |
| Compressor in Expand mode (`Compressor2`, Model 2) | Sentry Gate, Upward style | threshold, expansion ratio 1 : n → lift of (n − 1) dB per dB over the threshold, attack, release, output gain | dry/wet, makeup, the release of the lifted gain |
| Channel EQ (`ChannelEq`) | EQ | low / mid / high gains (linear → dB), mid frequency, output | 80 Hz high pass, mid frequency outside 200 Hz … 5 kHz |
| Chorus-Ensemble (`Chorus2`), Chorus (`Chorus`) | Chorus | rate, amount → depth, feedback, dry/wet | Ensemble and Vibrato modes (named), width, warmth, high pass, output gain; the older device's two delay times |
| Auto Pan (`AutoPan`) | Auto-pan | only as a panner (phase 180°): synced rate → period in beats, free rate in Hz, amount → depth | LFO shape, offset, Spin, Invert; as a tremolo (phase 0° / 360°) it is not translated (device dropped and listed) |
| Redux (`Redux2`) | Lo-fi | sample rate → hold length, bit depth, dry/wet | Jitter, quantiser shape, pre / post filter |
| Arpeggiator (`MidiArpeggiator`) | the channel's arpeggiator | style → up / down / up-down / up & down (the ends twice) / random, synced rate (1/128 … 1/1) or free rate in beats, gate, steps of 12 semitones → octave range | other styles (nearest named), Repeats, Retrigger, velocity decay, Hold, Offset, Groove |
| Pitch, Velocity, Chord, Note Length (`MidiPitcher`, `MidiVelocity`, `MidiChord`, `MidiNoteLength`) | written into the notes of the track's clips | transpose with its key range; velocity range / out range in Clip, Gate and Fixed modes; chord shifts with their velocities; free note length | notes played live are not changed (one line each); Velocity's Drive / Compand / Random, Chord's Chance / Strum, Note Length on note-off or synced |

Where MIDI effects are read: before the instrument on a MIDI track, and at the head of the chain of a one-chain Instrument Rack. A Chord or Note Length behind an Arpeggiator cannot be written into the notes and stays unsupported; Scale, Random and MIDI effects inside multi-chain racks stay unsupported.

Laws that carry a unit conversion:
- **Echoform**: sixteenth counts index 0…7 → 1, 2, 3, 4, 5, 6, 8, 16 sixteenths; a time that is a note value × straight / dotted / triplet goes to the note sync, any other to ms at the set's tempo. Feedback gain g → Echoform value g / 2. Delay's band-pass (centre f, width w octaves) → low cut f·2^(−w/2), high cut f·2^(w/2). A linear dry/wet d → the equal-power mix value with the same wet : dry ratio, `(2/π)·atan(d / (1 − d))`.
- **Multiband Dynamics**: the saved "ratio" r is a slope. Above the threshold the output rises (1 + r) dB per dB: r = −0.75 → 4 : 1, r = −1 → limit, r > 0 → upward expansion. Below it the output falls (1 − r) dB per dB: r > 0 lifts quiet signal (upward compression), r = 1 holds it at the threshold, r < 0 expands downwards. Amount scales r; Time scales attack and release. A band's input gain g moves its thresholds by −g at Prism's input and adds to its level.
- **Auto Filter** (resonance law superseded by §16): the older device's cutoff is a pitch, Hz = 440·2^((n − 69)/12). Resonance r (older device 0…1.25, new 0…1) → the cookbook Q = 0.7071·12.5^r, clamped to the Filter's 0.5 … 12. Measured in Live 12.4 (round 190cw r18 / r19, fitted in 190cx): a low pass at 1 kHz with r = 0.5 is the cookbook biquad at Q 2.49 (new) / 2.50 (older) within 0.02 dB, the two tags the same curve within 0.035 dB once the new device's Output (a linear gain: 0.5 = −6.02 dB, listed when not 1) is taken out; 0.7071 at r = 0 is assumed, so the law rests on one point.
- **Saturator** (curve mapping superseded by §16) curve type 0…7 (Live 12.4's menu, round 190cw): Analog Clip, Soft Sine, Bass Shaper, Medium Curve, Hard Curve, Sinoid Fold, Digital Clip, Waveshaper → Ember's tube, tape, tube, tape, transistor, fuzz, transistor, fuzz.
- **Gate / Compressor**: thresholds are linear gains → dB; Gate's floor ("Gain") −75 dB is "−inf dB" in Live 12.4 → Sentry's deepest floor (100 dB, a full gate), above it the floor in dB; Gate look-ahead index 0 / 1 / 2 = 0 / 1.5 / 10 ms; Limiter look-ahead index = 1.5 / 3 / 6 ms.
- **Redux → Lo-fi**: the Lo-fi holds n = 44100 / rate whole samples and quantises to 2^bits levels.
- **Arpeggiator**: synced-rate index 0…13 = 1/128, 1/96, 1/64, 1/48, 1/32, 1/24, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3, 1/2, 1/1 [DATA: default 8; DOC]; gate % → 0…1.

**Automation and macros.** Each translator also returns, per Live parameter it maps continuously, a link `{Live parameter path, LYNX parameter, law}`. An arrangement envelope whose `PointeeId` is such a parameter's `AutomationTarget` becomes an automation of that effect parameter with every point passed through the law; a rack macro mapped onto it (`KeyMidi`, channel 16) becomes a `RackMapping` whose ends are the law of the mapping's `Min` / `Max`. Mappings and envelopes onto a setting with no LYNX control are listed. Envelopes onto devices inside a rack and clip envelopes are still listed as unsupported.

## 16. Time signature changes, the loop switch and the tempo lane range (round 190cg, 2026-11-11)
- Signature changes: an `AutomationEnvelope` of the main track whose `PointeeId` is the `Id` of `DeviceChain/Mixer/TimeSignature/AutomationTarget`, holding `EnumEvent`s. The value is the signature code of §4 (99 × denominator exponent + numerator − 1, 201 = 4/4); the first event sits at −63072000 like every envelope [DATA]. Later events are taken to be in beats [inferred, needs a live check]. Import: each event after the start → a time-signature marker. Export: the start signature (with its denominator) in `TimeSignature/Manual`; the envelope is written only when the song has later signature markers.
- `Transport/LoopOn` is the loop switch; `LoopStart` / `LoopLength` hold the brace whether the loop is on or off [DATA: a new set has LoopOn false, LoopStart 8, LoopLength 16]. Import keeps the range and sets `Song::loopOff` from the switch; export writes the range whenever the song has a time selection.
- `Mixer/TempoAutomationViewBottom` / `Top` (60 / 200 in a new set) are the BPM range the tempo lane shows [DATA]. Kept both ways in `Song::tempoViewLow/High`; LYNX's lane does not draw with it yet.

## 17. Device laws refit to Live 12.4.16's renders (round 190da, 2026-11-12)

Round 190cz rendered 190 tracks in Live 12.4.16 (an impulse, a sweep, sines, noise and a 1 kHz staircase through one
device each, 48 kHz float); round 190da fitted the laws and ran every case through the importer and LYNX's engines
(before / after tables: `/mnt/e/lynx-work/als190cf/HANDOFF.md` § Round 190da). The laws now in `AlsDevices*.cpp`:

- **Auto Filter**: both tags are cookbook biquads at the cutoff, Q = 0.5 + 2x / (1 − x) (Q does not depend on the cutoff;
  low, high and band pass share it). Older device x = min(r, 0.9) (Q 0.5 … 18.5); new device x from its measured table
  (0.5 → 2.49, 0.9 → 17.86, 1 → 35.78; r above 1 acts as 1). The 24 dB/oct slope adds a second stage of the same type
  with Q2 = 0.5 + x / (1 + x): LYNX's Filter got an appended Slope parameter (0 = 12 dB/oct, every older project) that
  runs that pair. Live's band pass peaks at its Q (the cookbook "constant skirt" form); the Filter's at 0 dB (listed). Q
  over the Filter's 12 is listed.
- **EQ Eight**: 48 dB cut section scale 1 + 0.2632·ln(Q / 0.7071) (exact at Q 0.1 … 10, low and high cut); shelves: Live's
  Q is a fixed cookbook Q, the same for both shelves and every gain (13-point table Q 0.1 … 18, a quintic in ln Q);
  Adaptive Q: Q × AdaptiveQFactor^(|gain × Scale| − 6) on bells and, when AdaptiveQAffectsShelves, on the shelf's cookbook Q.
- **Utility**: mid/side balance m: mid × min(1, 2 − m), side × min(1, m); the balance comes after width / mono / mid-side.
- **Glue Compressor**: Range ≤ 40 stops the gain reduction at 1.1727·Range − 2.197 dB; from 50 up no limit (Crest's 60 dB).
  The detector weighting seen in the rendered preset is its own side-chain EQ (high pass 200 Hz), which the importer
  already carries.
- **Saturator**: Drive is a gain before a memoryless curve (Waveshaper excepted). Per type, the Ember curve and drive /
  output offsets fitted on the curves at drive 0 and +12 dB (`AlsDevicesLot2.cpp`, `saturatorFit`).
- **Reverb**: Live's 1 kHz T30 ≈ 0.91 × Decay → Atrium's decay 0.94 × Decay; the loop's high shelf (gain g) → Atrium's
  High mult 1 / (1 + 0.08·(−20 log10 g)·Decay). Atrium's fully wet tail is louder than Live's (listed with the measured sizes).

## 18. Live's own devices → LYNX's own, lot 3, and the envelopes of racks and plugins (round 190ed, 2026-11-12)
Code: `src/core/AlsDevicesLot3.cpp` (laws and translators, declared in `AlsDevices.h`), `AlsDevices.cpp` (dispatch),
`AlsImport.cpp` (rack / plugin envelopes, the Peak Controller links, the synths). No Live render of these devices
exists yet: every law that is not plain unit maths is [ASSUMED] and listed in the round's handoff
(`/mnt/e/lynx-work/als190ed/HANDOFF.md`, "needs live render").

| Live device (tag) | LYNX | Translated | Listed in the report |
|---|---|---|---|
| EQ Three (`FilterEQ3`) | Prism Multiband, 3 bands, minimum-phase (the crossover sum), no dynamics | FreqLo / FreqHi → crossovers, Slope → 24 / 48 dB/oct, GainLo / Mid / Hi (linear) → band levels (dB), kill switches and −inf gains → the bands that still sound are soloed | the pair (fitted), gains under −30 dB, all bands off (Prism's lowest output) |
| Drum Buss (`DrumBuss`) | an Audio Effect Rack "Drum Buss": Ember Saturator → Facet EQ → Crest Compressor (with Comp on) → Shell Channel, plus a "Dry" chain when Dry/Wet is under 100 % or can move (refit to Live's renders in round 190ek, §19) | Drive and its type → Ember's Pentode voicing at the measured small-signal gain per type and amount (+ the type's own EQ: Medium a bell, Hard a low shelf, in proportion to the drive); the device's own colour → three Facet bells and the output; Damping → Ember's 6 dB/oct high cut; Input Trim → Ember's input; Crunch → its mid band (two Facet shelves under the output's lift); Boom → a Facet bell at the Boom frequency and a resonant 12 dB/oct high pass (decay sets their gain / Q / place); Comp → Crest at 4 : 1, −19 dB, 1 / 200 ms, +11 dB; Transients → Shell's transient shaper (Amount, Invert below 0); Output → Shell's output; Dry/Wet → the chains' volumes. The rack's macros (Drive, Crunch, Damping, Transients, Boom, Boom Freq, Trim, Output, Dry/Wet) run through the imported values: Live's envelopes and macros reach them | every fitted part, the type's EQ at other drives, Boom's tail, Crunch's distortion, the transient shaper's tail lift, Soft's wave folding, negative transients under automation |
| Auto Filter's LFO / envelope follower / side chain | a Peak Controller (FL's internal-controller link) in front of the Filter — or at the end of the side-chain source track's insert — whose output 0 (peak + LFO) drives the Filter's cutoff through a 33-point curve | LFO rate (Hz at the set's tempo, or synced), shape, phase offset, amount; the follower's amount and release (a straight fall); the cutoff's base | amounts (law assumed), the follower's attack, stereo LFO offset, falling saw, inside a rack (no link possible) |
| Operator (`Operator`) | the built-in synth, only when no FM is in use: operator A alone (B … D off or at −inf), or algorithm 11's carriers (≤ 3) | waveforms Sine (0), White noise (21), User (22 → the synth's harmonic table), coarse / fine ratio → pitch, volume, A's envelope (ms → s), filter, glide, global volume / pan | the pair (fitted), envelope levels / slopes / loop, LFO, pitch envelope, spread, tone; with FM the device stays unsupported ("FM synthesis: no LYNX engine for it yet") |
| Drift (`Drift`) | the built-in synth | oscillator 1 / 2 types → nearest waves, transposes, detune, levels, noise; filter I / II (12 dB / 24 dB ladder), frequency, resonance, key tracking; envelope 1 (amp ADSR); voice mode mono; glide; volume | the pair (fitted), shape, high pass, envelope 2 / LFO / cycling envelope / matrix, drift, stereo / unison voices |

**Envelopes (arrangement automation).** Before lot 3 only envelopes on mixer, tempo, sends and the parameters of converted
devices on a track's own chain came in. Now also:
- a rack's own macro (Audio Effect Rack on an insert → `effectParam` slot parameter k; Instrument Rack on a channel →
  `channelParam` k; 0 … 127 → 0 … 1);
- a converted device inside a rack, a plugin inside a rack, a nested rack's macro: a free macro of the outer rack (no
  mapping, no name, not under an envelope itself) is mapped over the target's whole range, set to the target's value
  (nothing moves), excluded from Rand and variations and named after the parameter; the envelope drives that macro through
  the device's law;
- a VST2 plugin's parameter (`ParameterId` = its index [model]) on an insert or as the track's instrument;
- a Simpler's filter frequency / resonance → the Sampler's Cutoff / Resonance knobs.
(round 190ek) A VST3 plugin's parameter on an insert or as the track's instrument is carried by the plugin's own
ParamID: slot k = its place among the device's parameters with an id (`ParameterId` ≥ 0), the ParamIDs written into
the plugin's state as FL's record 4 so the plugin host puts slot k on the plugin's own parameter when it opens it; Live's
plain values are mapped linearly over the saved `MidiControllerRange` (exact for 0 … 1; another range is listed as
approximate). Inside a rack it stays listed. Envelopes of devices LYNX does not translate stay listed as "Automation of a
device parameter (track)".

## 19. Lot 3 refit to Live 12.4's renders (round 190ek, 2026-11-12)
Evidence: round 190ei's renders and saves (`/mnt/e/lynx-work/live190ei/`: RESULTS.md, CASES.md, renders, the VST3 save),
re-analysed with LYNX's own renders of the same devices (`/mnt/e/lynx-work/als190ek/`: measure3.cpp runs the importer's
translations on the same stimuli, compare3.py scores both against Live; dbfit / dbeval try every Ember curve on the
Drum Buss staircases). Live's devices were not decompiled; renders, the menus' shots, Live's own saves and the rate
labels read as plain strings from Live's engine binary were used.
- **Auto Filter (new device).** `Lfo_Amount` 94 semitones per 1.0 (0.5 → ±47 st), `Envelope_Amount` 96 semitones per
  1.0 at a full-scale peak; `Lfo_TimeMode` 0 Hz, 1 time (`Lfo_Time` s), 2 synced, 3 triplet (× 2/3), 4 dotted (× 1.5),
  5 sixteenths (`Lfo_Sixteenth`); `Lfo_SyncedRate` 0 … 21 = the older `BeatRate` table turned round (8 bars … 1/64:
  index 9 = 3/8 = 1.5 beats measured; the engine holds the same 22 labels for both).
- **Auto Filter (older device).** `ModHub` 0.94 semitone per unit at a full-scale peak (linear in the peak); LFO shapes:
  0 sine (0 rising at song time 0), 1 square (+ first), 2 triangle (from the bottom: the controller's is shifted half a
  cycle), 3 saw up, 4 saw down (alone: the rising saw with the curve turned over), 5 and 6 random steps four per cycle.
  The Peak Controller follows a sine's peak exactly; on noise it reads about 1.9 × Live's follower (block peak vs a
  smoothed one) and it has no attack (Live's side-chained kick peaks 80 ms after the hit).
- **Drift.** `Global_VoiceMode` 3 = Unison (menu: Poly, Mono, Stereo, Unison); 2 and 3 listed.
- **Drum Buss.** As in the §18 table; the laws and their fits are in `AlsDevices.h` (`namespace drumbuss`).
- **Unchanged and pinned by tests:** EQ Three (flat within 0.31 dB, Slope 0 / 1 = 24 / 48 dB/oct, kills, gains,
  FlatResponse has no effect), Operator's waves 0 / 21 / 22 and algorithm 10, Drift's oscillator maps, filter slopes and
  envelope 1.

