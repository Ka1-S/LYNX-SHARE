#!/usr/bin/env bash
# Cross build of LYNX.exe (Windows x64) from Linux/WSL using Zig (clang + MinGW-w64).
# Needs neither Visual Studio nor CMake. The official build is still CMakeLists.txt.
#
# Variables: ZIG (path to zig), JUCE_DIR (JUCE 8 checkout), OUT (output folder), JOBS
set -euo pipefail
cd "$(dirname "$0")/.."

ZIG="${ZIG:-$HOME/opt/zig-x86_64-linux-0.14.1/zig}"
JUCE_DIR="${JUCE_DIR:-$HOME/opt/juce-8.0.4}"
OUT="${OUT:-build-win}"
JOBS="${JOBS:-$(nproc)}"
TARGET=x86_64-windows-gnu

# Single version source: project(LYNX VERSION x.y.z) in CMakeLists.txt
VERSION="$(sed -n 's/^project(LYNX VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt)"
[[ -n "$VERSION" ]] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }

mkdir -p "$OUT/juce" "$OUT/app" "$OUT/shim"

# MinGW headers are lowercase; JUCE includes some with capitals (on Windows it does not matter).
echo '#include <dbghelp.h>' > "$OUT/shim/Dbghelp.h"

MODULES=(juce_core juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra
         juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors juce_audio_utils juce_dsp juce_opengl)

{
    echo '#pragma once'
    echo '#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1'
    echo '#define JUCE_STANDALONE_APPLICATION 1'
    echo '#define JUCE_USE_CURL 0'
    echo '#define JUCE_WEB_BROWSER 0'
    echo '#define JUCE_USE_WINDOWS_MEDIA_FORMAT 0'
    echo '#define JUCE_PLUGINHOST_VST3 1'
    echo '#define JUCE_ASIO 1'
    echo '#define JUCE_USE_MP3AUDIOFORMAT 0' # MP3: src/engine/Mp3Format (minimp3), round 157b
    echo '#define JUCE_APPLICATION_NAME_STRING "LYNX"'
    echo "#define JUCE_APPLICATION_VERSION_STRING \"$VERSION\""
    for m in "${MODULES[@]}"; do echo "#define JUCE_MODULE_AVAILABLE_$m 1"; done
} > "$OUT/JuceConfig.h.new"
# only replace the file if it changed, so the module cache is not invalidated
cmp -s "$OUT/JuceConfig.h.new" "$OUT/JuceConfig.h" 2>/dev/null && rm "$OUT/JuceConfig.h.new" || mv "$OUT/JuceConfig.h.new" "$OUT/JuceConfig.h"

COMMON=(-target "$TARGET" -std=c++20 -O2 -DNDEBUG=1 -fno-sanitize=undefined
        -include "$OUT/JuceConfig.h" -include cmake/mingw/mingw_compat.h
        -I"$OUT/shim" -I"$JUCE_DIR/modules" -I"$JUCE_DIR/modules/juce_audio_processors/format_types/VST3_SDK" -Ithird_party/clap/include -Ithird_party/wavpack/include -Ithird_party/vorbis/include -Ithird_party/lame/include -Ithird_party/asiosdk/common -Ithird_party/sse2neon -Ithird_party/pocketpy -Ithird_party/monocypher -Ithird_party/onnxruntime -Isrc)
# The plugins (LYNX_BUILD_PLUGINS=1 below) link these objects with --gc-sections: with a section per function and per
# datum the linker drops what a plugin never reaches (round 180b measured 32 % on Linux). Plugin builds only, so the app's
# objects and their cache entries stay as they are (LYNX_PLUGIN_SECTIONS=0 turns it off).
if [[ "${LYNX_BUILD_PLUGINS:-0}" == 1 && "${LYNX_PLUGIN_SECTIONS:-1}" == 1 ]]; then
    COMMON+=(-ffunction-sections -fdata-sections)
fi

# juce_gui_basics, juce_audio_processors, juce_graphics and juce_events are compiled from patched copies
# (cmake/LynxPatchJuce.cmake: issue #11 drag & drop and menu fixes; issue #70 VST3 note ids; issue #63 VST3 note
# tuning; round 174e2 Direct2D image readback; issue #32 the message dispatch hook of the plugin window crash guard;
# juce_audio_devices: issue #134 the Windows Audio channel probe), so
# the JUCE checkout itself is never modified. The copies are only replaced when their patched sources change (module cache).
GUI_BASICS_SRC="$JUCE_DIR/modules/juce_gui_basics"
AUDIO_PROCESSORS_SRC="$JUCE_DIR/modules/juce_audio_processors"
GRAPHICS_SRC="$JUCE_DIR/modules/juce_graphics"
EVENTS_SRC="$JUCE_DIR/modules/juce_events"
AUDIO_DEVICES_SRC="$JUCE_DIR/modules/juce_audio_devices"
CMAKE="${CMAKE:-$(command -v cmake || ls "$HOME"/opt/cmake-*/bin/cmake 2>/dev/null | head -n 1)}"
if [[ -n "$CMAKE" ]]; then
    rm -rf "$OUT/juce-patched.new"
    mkdir -p "$OUT/juce-patched.new/modules"
    cp -r "$JUCE_DIR/modules/juce_gui_basics" "$JUCE_DIR/modules/juce_audio_processors" "$JUCE_DIR/modules/juce_graphics" "$JUCE_DIR/modules/juce_events" "$JUCE_DIR/modules/juce_audio_devices" "$OUT/juce-patched.new/modules/"
    "$CMAKE" -DJUCE_DIR="$OUT/juce-patched.new" -P cmake/LynxPatchJuce.cmake
    if diff -rq "$OUT/juce-patched.new" "$OUT/juce-patched" >/dev/null 2>&1; then
        rm -rf "$OUT/juce-patched.new"
    else
        rm -rf "$OUT/juce-patched" && mv "$OUT/juce-patched.new" "$OUT/juce-patched"
        rm -f "$OUT/juce/juce_gui_basics.o" "$OUT/juce/juce_audio_processors.o" "$OUT/juce/juce_graphics.o" "$OUT/juce/juce_events.o" "$OUT/juce/juce_audio_devices.o"
    fi
    GUI_BASICS_SRC="$OUT/juce-patched/modules/juce_gui_basics"
    AUDIO_PROCESSORS_SRC="$OUT/juce-patched/modules/juce_audio_processors"
    GRAPHICS_SRC="$OUT/juce-patched/modules/juce_graphics"
    EVENTS_SRC="$OUT/juce-patched/modules/juce_events"
    AUDIO_DEVICES_SRC="$OUT/juce-patched/modules/juce_audio_devices"
else
    echo "warning: cmake not found; JUCE's drag & drop fixes (cmake/LynxPatchJuce.cmake) are not applied" >&2
fi

# Shared object cache (2026-11-02): JUCE and the third-party C libraries are the same in every worktree, so each object
# is compiled once per key into $LYNX_OBJ_CACHE (default ~/.cache/lynx-objcache) and copied from there by every other
# build. The key hashes the zig version, the target, the whole compile command (with $OUT normalised), JuceConfig.h,
# mingw_compat.h, the patched juce_gui_basics and the third-party sources. Concurrent builds: one compiles a key under
# its flock, the others wait and copy; objects enter the cache by an atomic rename. LYNX_OBJ_CACHE=off disables it.
OBJ_CACHE="${LYNX_OBJ_CACHE:-$HOME/.cache/lynx-objcache}"
ZIG_VERSION="$("$ZIG" version)"
tree_hash() { (cd "$1" && find . -type f | LC_ALL=C sort | xargs -d '\n' sha256sum | sha256sum | cut -c1-64); }
# cached <obj> <key material...> -- <compile command writing <obj>...>
cached() {
    local obj="$1" parts=()
    shift
    while [[ "$1" != -- ]]; do parts+=("$1"); shift; done
    shift
    if [[ "$OBJ_CACHE" == off ]]; then "$@"; return; fi
    local cmd="$*" key name
    key="$(printf '%s\n' "$ZIG_VERSION" "$TARGET" "${parts[@]}" "${cmd//$OUT/@OUT@}" | sha256sum | cut -c1-32)"
    name="$(basename "$obj" .o)-$key"
    mkdir -p "$OBJ_CACHE"
    # a hit replays the compiler's warnings and refreshes the entry's date (entries unused for 10 days are pruned)
    # (-s, not -f: an empty object left by a crash or a full disk is not a hit; it is compiled again and replaced)
    if [[ -s "$OBJ_CACHE/$name.o" ]] && cp "$OBJ_CACHE/$name.o" "$obj.tmp$$" && mv "$obj.tmp$$" "$obj"; then
        touch "$OBJ_CACHE/$name.o"; cat "$OBJ_CACHE/$name.err" 2>/dev/null >&2 || true; return 0
    fi
    (
        flock 9
        if [[ -s "$OBJ_CACHE/$name.o" ]]; then cp "$OBJ_CACHE/$name.o" "$obj.tmp$$" && mv "$obj.tmp$$" "$obj" && { cat "$OBJ_CACHE/$name.err" 2>/dev/null >&2 || true; }; exit; fi
        "$@" 2> "$obj.err$$"; rc=$?
        cat "$obj.err$$" >&2
        [[ $rc == 0 ]] || { rm -f "$obj.err$$"; exit $rc; }
        [[ -s "$obj" ]] || { rm -f "$obj.err$$"; echo "error: $obj is empty after a successful compile (disk full?)" >&2; exit 1; }
        mv "$obj.err$$" "$OBJ_CACHE/$name.err"
        cp "$obj" "$OBJ_CACHE/.$name.$$" && mv "$OBJ_CACHE/.$name.$$" "$OBJ_CACHE/$name.o"
    ) 9> "$OBJ_CACHE/$name.lock"
}
# cached_pp <obj> -- <compile command>: for the app sources, keyed by the preprocessed source (-E -P: every header it
# includes, every macro), so an unchanged file is copied from the cache in any worktree and a header change recompiles
# exactly the files that see it. (zig does not cache -c compiles: before this, every Windows build recompiled ~260 files.)
cached_pp() {
    local obj="$1" pp=() skip=0 a h
    shift 2
    if [[ "$OBJ_CACHE" == off ]]; then "$@"; return; fi
    for a in "$@"; do
        if [[ $skip == 1 ]]; then skip=0; continue; fi
        case "$a" in -c) ;; -o) skip=1 ;; *) pp+=("$a") ;; esac
    done
    h="$("${pp[@]}" -E -P -w -o - 2>/dev/null | sha256sum | cut -c1-64)"
    # (no output: the preprocessor failed, let the compiler report it)
    [[ -n "$h" && "$h" != e3b0c44298fc1c14* ]] || { "$@"; return; }
    cached "$obj" "pp=$h" -- "$@"
}
JUCE_KEY="$JUCE_DIR $(cat "$OUT/JuceConfig.h" cmake/mingw/mingw_compat.h "$OUT/shim/Dbghelp.h" "$JUCE_DIR/modules/juce_core/system/juce_StandardHeader.h" | sha256sum | cut -c1-64)"
GUI_BASICS_KEY="$(tree_hash "$GUI_BASICS_SRC")"
AUDIO_PROCESSORS_KEY="$(tree_hash "$AUDIO_PROCESSORS_SRC")"
GRAPHICS_KEY="$(tree_hash "$GRAPHICS_SRC")"
EVENTS_KEY="$(tree_hash "$EVENTS_SRC")"
AUDIO_DEVICES_KEY="$(tree_hash "$AUDIO_DEVICES_SRC")"
export ZIG OUT JUCE_DIR TARGET GUI_BASICS_SRC AUDIO_PROCESSORS_SRC GRAPHICS_SRC EVENTS_SRC AUDIO_DEVICES_SRC OBJ_CACHE ZIG_VERSION JUCE_KEY GUI_BASICS_KEY AUDIO_PROCESSORS_KEY GRAPHICS_KEY EVENTS_KEY AUDIO_DEVICES_KEY
export -f cached cached_pp
mkdir -p "$OBJ_CACHE" && find "$OBJ_CACHE" -maxdepth 1 -type f -mtime +10 -delete 2>/dev/null || true
export COMMON_STR="${COMMON[*]}"

echo "Compiling JUCE modules (cached)..."
# the modules and the extra units JUCE's CMake compiles along with them, in one parallel pool
for m in "${MODULES[@]}" juce_core_CompilationTime juce_graphics_Harfbuzz juce_graphics_Sheenbidi; do echo "$m"; done | xargs -P "$JOBS" -I{} bash -c '
    m={}; obj="$OUT/juce/$m.o"
    if [[ ! -f "$obj" || ( "$OUT/JuceConfig.h" -nt "$obj" && "$m" != juce_graphics_Sheenbidi ) ]]; then
        echo "  JUCE $m"
        extra=""
        # JUCE ASIO uses MSVC __try/__except: clang accepts it with -fms-extensions, but on MinGW it does not catch
        # (a driver crashing while being opened/closed is not contained in this build; the MSVC build keeps the guard)
        [[ "$m" == juce_audio_devices ]] && extra="-fms-extensions -DJUCE_DONT_AUTOLINK_TO_WIN32_LIBRARIES=1"
        src="$JUCE_DIR/modules/$m/$m.cpp"
        key="$JUCE_KEY"
        [[ "$m" == juce_gui_basics ]] && src="$GUI_BASICS_SRC/$m.cpp" && key="$JUCE_KEY $GUI_BASICS_KEY"
        [[ "$m" == juce_audio_processors ]] && src="$AUDIO_PROCESSORS_SRC/$m.cpp" && key="$JUCE_KEY $AUDIO_PROCESSORS_KEY"
        [[ "$m" == juce_graphics ]] && src="$GRAPHICS_SRC/$m.cpp" && key="$JUCE_KEY $GRAPHICS_KEY"
        [[ "$m" == juce_events ]] && src="$EVENTS_SRC/$m.cpp" && key="$JUCE_KEY $EVENTS_KEY"
        [[ "$m" == juce_audio_devices ]] && src="$AUDIO_DEVICES_SRC/$m.cpp" && key="$JUCE_KEY $AUDIO_DEVICES_KEY"
        case "$m" in
            juce_core_CompilationTime) src="$JUCE_DIR/modules/juce_core/$m.cpp" ;;
            juce_graphics_Harfbuzz) src="$JUCE_DIR/modules/juce_graphics/$m.cpp" ;;
        esac
        if [[ "$m" == juce_graphics_Sheenbidi ]]; then
            cached "$obj" "$JUCE_DIR" -- "$ZIG" cc -target "$TARGET" -O2 -w -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -I"$JUCE_DIR/modules" -c "$JUCE_DIR/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$obj"
        else
            cached "$obj" "$key" -- $ZIG c++ $COMMON_STR $extra -w -c "$src" -o "$obj"
        fi
    fi'

APP_SOURCES=(src/Main.cpp
             src/engine/AudioEngine.cpp src/engine/AudioEngineSession.cpp src/engine/ChannelRackProcessor.cpp src/engine/Metronome.cpp
             src/engine/SampleLibrary.cpp src/engine/WavPackFormat.cpp src/engine/Mp3Format.cpp src/engine/ProjectFile.cpp src/engine/ProjectPackage.cpp src/engine/PluginHost.cpp src/engine/PluginSandbox.cpp src/engine/SandboxWorker.cpp src/engine/Bridge32Test.cpp src/engine/Exporter.cpp src/engine/FlStudioExport.cpp src/engine/StreamPrefetcher.cpp src/engine/DllAsioDevice.cpp src/engine/PipeWireDevice.cpp src/platform/LinuxRealtime.cpp src/engine/ClapFormat.cpp src/engine/Vst2Format.cpp src/engine/MidiFileIo.cpp src/engine/AudioLogger.cpp src/engine/PreviewPlayer.cpp
             src/ui/MainComponent.cpp src/ui/PluginKeysTest.cpp src/ui/ChannelRackComponent.cpp src/ui/PianoRollComponent.cpp src/ui/PlaylistComponent.cpp src/ui/PatternBlocksView.cpp src/ui/ClipGridComponent.cpp src/ui/GroovePoolComponent.cpp src/ui/DetailView.cpp src/ui/ClipAudioPanel.cpp src/ui/SessionMixer.cpp src/ui/MapMode.cpp src/ui/MidiToolsPanel.cpp src/ui/MixerComponent.cpp src/ui/QuickPick.cpp src/ui/PluginWindow.cpp src/ui/PluginPresetMenu.cpp src/ui/ProjectLoading.cpp src/ui/WindowOwner.cpp src/ui/ParametricEqEditor.cpp src/ui/FacetEqEditor.cpp src/ui/ProEditors.cpp src/ui/PluginHeader.cpp src/ui/PluginSkin.cpp src/ui/ExportDialog.cpp src/ui/FlStudioExportDialog.cpp src/ui/AlsExportDialog.cpp src/ui/StemExtract.cpp src/ui/AudioToMidi.cpp src/ui/AutoSamplerDialog.cpp src/ui/AutomationUi.cpp src/ui/ChannelSettings.cpp src/ui/BrowserComponent.cpp src/ui/Theme.cpp src/ui/Icons.cpp src/ui/I18n.cpp src/ui/EditorTools.cpp
             src/core/RollingAudioLog.cpp src/core/ChordReadout.cpp src/core/Song.cpp src/core/MarkerPlan.cpp src/core/Scheduler.cpp src/core/ClipLauncher.cpp src/core/SessionOps.cpp src/core/DrawMode.cpp src/core/RemoteMap.cpp src/core/Groove.cpp src/core/Performance.cpp src/core/Patcherize.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp src/core/LoadTrace.cpp src/core/ModuleWarmup.cpp
             src/core/FlpImport.cpp src/core/FlpExport.cpp src/core/XmlLite.cpp src/core/Gzip.cpp src/core/AlsImport.cpp src/core/AlsDevices.cpp src/core/AlsDevicesLot2.cpp src/core/AlsDevicesLot3.cpp src/core/AlsExport.cpp src/core/FlmFormat.cpp src/core/FlmImport.cpp src/core/FlmEffects.cpp src/core/FlmNoteFx.cpp src/core/FlmDwSampler.cpp src/core/FlmSlicer.cpp src/flp/FlpWrite.cpp src/core/Effects.cpp src/core/NoteEdit.cpp src/core/Recorder.cpp src/core/TakeRecording.cpp src/core/Comping.cpp src/core/Pdc.cpp src/core/WorkerPool.cpp src/core/TimeStretch.cpp src/core/Arpeggiator.cpp src/core/SynthPresets.cpp src/core/Slicer.cpp src/core/AudioToMidi.cpp src/core/Templates.cpp src/core/FlWrapper.cpp src/core/FlEffects.cpp src/core/Effector.cpp src/core/VintagePhaser.cpp src/core/EmphasizerFx.cpp src/core/EmphasisFx.cpp src/core/TransporterFl.cpp src/core/EmphasisEngine.cpp src/core/FreqShifterFl.cpp src/core/LowLifterFl.cpp src/core/FlLegacyFx.cpp src/core/PocketFx.cpp src/core/PocketMultiFx.cpp src/core/PocketFxC.cpp src/core/PocketFxD.cpp src/core/PocketFxE.cpp src/core/PocketReverb.cpp src/core/PocketFxB.cpp src/core/PocketFxA.cpp src/core/Reeverb2.cpp src/core/LuxeVerb.cpp src/core/IldspMath.cpp src/core/ConvolverFx.cpp src/core/EquoFx.cpp src/core/PitchShifterFl.cpp src/core/SimSynth.cpp src/core/Wasp.cpp src/core/Granulizer.cpp src/core/Drumaxx.cpp src/core/FruityFilters.cpp src/core/NewtoneFx.cpp src/core/EdisonFx.cpp src/core/VocalPitch.cpp src/core/LynxTune.cpp src/core/LynxVerbs.cpp src/core/NebulaCloud.cpp src/core/MixSuite.cpp src/core/VoiceMatch.cpp src/core/EchoSuite.cpp src/core/LovePhilter.cpp src/core/DistructorFx.cpp src/core/MultibandDelayFx.cpp src/core/FrequencySplitter.cpp src/core/Slayer.cpp src/core/FlangusFastDist.cpp src/core/FruityPhaser.cpp src/core/FruityDelayBank.cpp src/core/FruityMultiband.cpp src/core/Vocodex.cpp src/core/FruityVocoder.cpp src/core/DelphiMath.cpp src/core/MaximusFx.cpp src/core/ParametricEqFl.cpp src/core/ParametricEqLinear.cpp src/core/FruityBasicFx.cpp src/core/TransistorBass.cpp src/core/GrossBeat.cpp src/core/StereoFx.cpp src/core/FruityDelay.cpp src/core/FruityShapers.cpp src/core/ChorusFx.cpp src/core/FruityDynamics.cpp src/core/FlSamplers.cpp src/core/FlSynths.cpp src/core/FlSytrus.cpp src/core/FlGenerators.cpp src/core/PhysicalModels.cpp src/core/DrumSynthLive.cpp src/core/SytrusEngine.cpp src/core/Harmless.cpp src/core/HarmlessFx.cpp src/core/Harmor.cpp src/core/Gms.cpp src/core/Morphine.cpp src/core/Ogun.cpp src/core/Toxic.cpp src/core/ToxicFx.cpp src/core/FruitKick.cpp src/core/BassDrum.cpp src/core/MiniSynth.cpp src/core/Dx10.cpp src/core/Kepler.cpp src/core/PoiZone.cpp src/core/FlPatcher.cpp src/core/Patcher.cpp src/core/Vfx.cpp src/core/VfxSequencer.cpp src/core/Inflate.cpp src/core/WavPack.cpp src/flp/Flp.cpp src/core/EmberClassic.cpp src/core/RhythmPan.cpp src/core/VoxStrip.cpp src/core/Rack.cpp src/core/Fpc.cpp src/core/FpcInstrument.cpp src/core/FpcEnvelope.cpp
             src/platform/MingwFixes.cpp)

# workflow shell: core (key map, workspace layout, rack operations) and UI (workspace desktop, control contract)
APP_SOURCES+=(src/core/DrumControls.cpp src/core/BankStates.cpp src/core/DrumBanks.cpp)
APP_SOURCES+=(src/core/KeyMap.cpp src/core/PresetCycle.cpp src/core/Sfz.cpp src/core/Workspace.cpp src/core/RackOps.cpp src/core/PlaylistTrackOps.cpp src/core/ChannelMiscFx.cpp src/core/MidiImport.cpp src/ui/Workspace.cpp src/ui/NativeChild.cpp src/ui/ValueControl.cpp src/ui/Dialogs.cpp)
# Wave Traveller (round 167f)
APP_SOURCES+=(src/core/WaveTraveller.cpp src/ui/WaveTravellerEditor.cpp)
APP_SOURCES+=(src/core/FruitySlicer.cpp src/ui/FruitySlicerEditor.cpp)
# FL's Envelope / instrument settings page of the plugin voice processor (round 190f)
APP_SOURCES+=(src/ui/FlVoicePanel.cpp)
APP_SOURCES+=(src/core/Slicex.cpp src/core/SlicexEngine.cpp)
APP_SOURCES+=(src/core/SlicexInstrument.cpp)
APP_SOURCES+=(src/ui/SlicexEditor.cpp)
APP_SOURCES+=(src/ui/FpcEditor.cpp) # round 190q
# Windows file association (.lynx) and its icon
APP_SOURCES+=(src/platform/FileTypes.cpp src/platform/Paths.cpp src/platform/Package.cpp src/platform/NativeEmbed.cpp)
# SEH guard around calls into hosted plugins (round 111): the only unit built with -fms-extensions (__try / __except)
APP_SOURCES+=(src/platform/CrashGuard.cpp)
# workflow P2: core (note tools, mixer operations, history labels, browser search)
APP_SOURCES+=(src/core/NoteTools.cpp src/core/MidiTools.cpp src/core/MidiGenerate.cpp src/core/PianoRollTools.cpp src/core/EventEdit.cpp src/core/AudioEdit.cpp src/core/AudioEditTools.cpp src/core/FlAutoSlicer.cpp src/core/MixerOps.cpp src/core/History.cpp src/core/SearchQuery.cpp src/core/SearchIndex.cpp src/core/SearchService.cpp src/core/BrowserView.cpp src/core/EffectPresets.cpp src/core/PluginPresets.cpp src/core/ParamHelp.cpp)
# workflow P2: UI (note tool dialogs, tool windows, undo history, quick-add palette)
APP_SOURCES+=(src/ui/NoteToolDialogs.cpp src/ui/PianoRollScripts.cpp src/ui/MidiClipboard.cpp src/ui/AudioClipboard.cpp src/ui/EventEditor.cpp src/ui/AudioEditor.cpp src/ui/AudioEditorTools.cpp src/ui/ToolWindow.cpp src/ui/TempoTapper.cpp src/ui/TempoDetectionDialog.cpp src/ui/HistoryPanel.cpp src/ui/VersionsPanel.cpp src/ui/PluginsOffPanel.cpp src/ui/QuickAddPalette.cpp src/ui/PluginPickerView.cpp src/ui/PluginThumbs.cpp)
# start-up: splash screen and its procedurally synthesised sound
APP_SOURCES+=(src/core/StartupSound.cpp src/core/Speech.cpp src/core/SpeechRules.cpp src/ui/Splash.cpp src/ui/SpeechDialog.cpp)
# main window chrome: hint bar and toolbar widgets
APP_SOURCES+=(src/ui/HintBar.cpp src/ui/Toolbar.cpp)
# FL's 3x Osc engine (REA round 47)
APP_SOURCES+=(src/core/ThreeOsc.cpp)
# FL Studio Mobile's GMSynth (round 190ab)
APP_SOURCES+=(src/core/GmsMobile.cpp)
APP_SOURCES+=(src/core/PocketSuperSaw.cpp)
# onboarding: FL Studio integration (plugin database, sample relocation), background scan, setup wizard
APP_SOURCES+=(src/core/FlPluginDb.cpp src/engine/FlStudio.cpp src/ui/SetupWizard.cpp)
APP_SOURCES+=(src/core/PluginCatalog.cpp src/ui/PluginPicker.cpp)
APP_SOURCES+=(src/core/PluginDbFolder.cpp)
APP_SOURCES+=(src/core/FolderWatcher.cpp)
APP_SOURCES+=(src/core/MissingFiles.cpp src/core/ProjectSafety.cpp src/ui/MissingFilesDialog.cpp src/ui/SettingsDialog.cpp)
# Online Sessions P1: stable ids, parts, stand-ins, packages and their dialogs (round 168a)
APP_SOURCES+=(src/core/SessionIds.cpp src/core/SessionParts.cpp src/core/StandIns.cpp src/engine/SessionPack.cpp src/ui/SessionDialogs.cpp)
# Online Sessions P2: live sessions over the relay (round 169b)
APP_SOURCES+=(src/core/LiveSession.cpp src/core/LiveCrypto.cpp src/engine/LiveLink.cpp src/engine/LiveSessions.cpp src/engine/MediaPack.cpp src/ui/LiveChat.cpp src/engine/LiveTest.cpp src/ui/LiveSessionUi.cpp)
# Online Sessions P5-A1: remote plugins (round 190ai)
APP_SOURCES+=(src/core/RemotePlugins.cpp src/engine/LiveRemotePlugins.cpp src/ui/LiveRemoteUi.cpp)
# round 190c: the device-change and short-export self-tests (issues #134, #138)
APP_SOURCES+=(src/engine/DeviceChangeTest.cpp src/engine/ShortExportTest.cpp)
# round 190az: a CLAP plugin deleted on another thread while the message thread is blocked
APP_SOURCES+=(src/engine/ClapTeardownTest.cpp)
# LYNX substitutes for missing third-party plugins (round 98)
APP_SOURCES+=(src/core/PluginSubstitutes.cpp)
# settings pages (General/Audio/MIDI), plugin manager, shortcut manager (round 82)
APP_SOURCES+=(src/core/ThemePalette.cpp src/ui/ThemeSettings.cpp src/ui/ColourPicker.cpp src/ui/TrackIcons.cpp)
# Loop Starter, local (round 168b)
APP_SOURCES+=(src/core/LoopStarter.cpp src/ui/LoopStarterWindow.cpp)
APP_SOURCES+=(src/core/SimpleTracks.cpp src/core/KeyFollow.cpp src/ui/SimpleWorkflow.cpp src/ui/FinishDock.cpp) # Simple workflow (rounds 184a, 189k)
APP_SOURCES+=(src/core/GoalKnobs.cpp src/core/ChainPresets.cpp src/core/SoundLibrary.cpp src/ui/SimpleDock.cpp) # Simple dock: effects, sounds (round 190bu)
APP_SOURCES+=(src/core/GamepadMap.cpp src/ui/GamepadInput.cpp) # game controller control (round 184a, S16)
APP_SOURCES+=(src/core/ControllerProfile.cpp src/core/ControllerSession.cpp src/engine/ControllerIo.cpp src/ui/ControllerBridge.cpp) # MIDI controller profiles (round 190aw)
APP_SOURCES+=(src/core/ExportBundle.cpp src/ui/OtherAppsExport.cpp) # Simple workflow: Export for other apps (round 184a, S9)
APP_SOURCES+=(src/ui/LyricsPanel.cpp src/ui/ReferenceMatchPanel.cpp src/ui/StartFromPanel.cpp) # lyrics, reference match, start from (round 190ch)
APP_SOURCES+=(src/ui/FirstSongTour.cpp) # the Simple workflow's Welcome screen and first-song tour (round 190dv)
APP_SOURCES+=(src/core/Preferences.cpp src/core/MidiSync.cpp src/engine/MidiSyncIo.cpp src/ui/SettingsPages.cpp src/ui/PluginManager.cpp src/ui/ShortcutManager.cpp)
# spectral waveforms: band analysis, background cache and drawing
APP_SOURCES+=(src/core/WaveAnalysis.cpp src/ui/WaveformView.cpp)
# FL's Plucked!, Sawer and BeepMap engines (REA round 65)
APP_SOURCES+=(src/core/Plucked.cpp src/core/BeepMap.cpp src/core/Sawer.cpp)
# LYNX Soundfont: SF2 / SF3 player with a preset search (round 158, issue #6)
APP_SOURCES+=(src/core/SoundFont.cpp src/core/SoundFontLoad.cpp src/core/SoundFontIndex.cpp src/ui/SoundFontEditor.cpp)
APP_SOURCES+=(src/ui/VfxEditors.cpp)
# the Browser's "Find similar samples" (round 190as)
APP_SOURCES+=(src/core/SampleSimilarity.cpp)
# the embedded Python (pocketpy) and FL's VFX Script (round 165d)
APP_SOURCES+=(src/core/Python.cpp src/core/VfxScript.cpp src/core/EdisonScript.cpp src/core/ScriptDialog.cpp src/core/PianoRollScript.cpp)
# FL's sampler interpolation (REA round 69)
APP_SOURCES+=(src/core/FlResampler.cpp src/core/FlKeys.cpp)
# FL's Hardcore (REA round 71)
APP_SOURCES+=(src/core/HardcoreFx.cpp)
# LYNX's Facet EQ and Apex Limiter (round 99)
APP_SOURCES+=(src/core/FacetEq.cpp src/core/ApexLimiter.cpp src/core/Mastering.cpp src/core/MasterLooks.cpp)
# LYNX's dynamics suite and FabFilter state translation (round 108)
APP_SOURCES+=(src/core/DynamicsSuite.cpp src/core/FabFilterStates.cpp src/ui/DynamicsDisplays.cpp)
# LYNX's voice tools: Quell Denoiser, Gemini Doubler (round 119)
APP_SOURCES+=(src/core/VoiceTools.cpp)
# MIDI CC automation of hosted plugins, Formula Controller (round 133, issue #3)
APP_SOURCES+=(src/core/MidiControls.cpp src/core/MidiOut.cpp src/core/FormulaController.cpp src/ui/ControllerEditors.cpp src/ui/MidiOutEditor.cpp)
# controller links: Remote control settings window, mapping formula (round 190bt)
APP_SOURCES+=(src/core/LinkFormula.cpp src/ui/RemoteControl.cpp)
APP_SOURCES+=(src/core/VoiceShift.cpp)
# FL engines usable in native projects (round 142, issue #5)
APP_SOURCES+=(src/core/NativeEngines.cpp src/core/LynxBanks.cpp src/core/ModelParams.cpp src/core/ToxicBank.cpp src/core/PoiZoneBank.cpp src/core/ModelParamsSynths.cpp src/core/PluginLayoutSynths.cpp src/core/HarmlessBank.cpp src/core/Dx10Bank.cpp src/core/SimSynthBank.cpp src/core/BeepMapBank.cpp src/core/GranulizerBank.cpp src/core/EngineLayouts.cpp src/ui/NewtoneEditor.cpp)
APP_SOURCES+=(src/core/Layer.cpp src/ui/LayerEditor.cpp) # FL's Layer channel (round 164)
APP_SOURCES+=(src/core/XyController.cpp src/core/FlCurve.cpp src/core/ControlSurface.cpp src/ui/Controllers164.cpp) # X-Y (-Z) Controller, Control Surface, Dashboard (round 164)
APP_SOURCES+=(src/core/VoltageController.cpp src/ui/VoltageEditor.cpp) # Voltage Controller (round 164)
# FL's DirectWave (REA round 72)
APP_SOURCES+=(src/core/DirectWave.cpp)
# Ogg Vorbis decoding as FL does (REA round 77)
APP_SOURCES+=(src/core/OggVorbis.cpp)
# export options + MP3, plugin performance monitor, clip properties / arrangements, new project, touch keyboard (round 84)
APP_SOURCES+=(src/core/ExportOptions.cpp src/core/Mp3Encoder.cpp src/core/Mp3Decoder.cpp src/core/PluginMonitor.cpp src/core/PlaylistOps.cpp src/core/QuickRender.cpp src/core/ProjectContents.cpp src/core/TempoDetect.cpp src/core/AutoWarp.cpp src/core/ScoreLog.cpp src/core/ChannelLoops.cpp src/core/PatternBlocks.cpp src/core/ChannelBurn.cpp src/core/AutoSampler.cpp)
APP_SOURCES+=(src/ui/NewProjectDialog.cpp src/ui/ClipProperties.cpp src/ui/PluginMonitorWindow.cpp src/ui/TouchKeyboard.cpp)
# native plugin editors in FL's arrangement (round 93)
APP_SOURCES+=(src/core/PluginLayout.cpp src/core/StateParams.cpp src/ui/PluginEditors.cpp src/ui/PatcherEditor.cpp src/ui/RackEditor.cpp src/ui/DrumRackShot.cpp)
# crash / freeze reports: consent, anonymiser, upload, GitHub form (round 159)
APP_SOURCES+=(src/core/CrashReport.cpp src/ui/CrashReporter.cpp)
# in-app update check + Update button (round 166b)
APP_SOURCES+=(src/core/UpdateCheck.cpp src/ui/Updater.cpp)
# visual plugins: Wave Candy, Visualizer, video export (round 164c)
APP_SOURCES+=(src/core/VisualFx.cpp src/ui/VisualEditors.cpp)
# FL plugins LYNX lacked: Mute 2, Big Clock, Tuner, HTML NoteBook (round 165g)
APP_SOURCES+=(src/core/FlExtraFx.cpp src/core/Scratcher.cpp src/core/Dvs.cpp src/core/Transmitter.cpp src/ui/FlExtraEditors.cpp src/core/VideoPlayer.cpp src/core/VideoTime.cpp src/core/VideoTrack.cpp src/core/Adr.cpp src/core/DialogueAlign.cpp src/ui/VideoPlayerView.cpp src/ui/VideoMedia.cpp src/ui/VideoMediaWindows.cpp src/ui/VideoTracks.cpp src/ui/AdrWindow.cpp)
APP_SOURCES+=(src/core/VisualFx.cpp src/core/FruityDance.cpp src/ui/VisualEditors.cpp src/ui/VisualGl.cpp src/ui/DanceView.cpp)

for lib in wavpack vorbis lame pocketpy monocypher; do declare "TP_KEY_$lib=$(tree_hash third_party/$lib)"; done

echo "WavPack (decoder, C)..."
mkdir -p "$OUT/wavpack"
for f in third_party/wavpack/src/*.c; do
    obj="$OUT/wavpack/$(basename "$f" .c).o"
    if [[ ! -f "$obj" || "$f" -nt "$obj" ]]; then
        cached "$obj" "$TP_KEY_wavpack" -- "$ZIG" cc -target "$TARGET" -O2 -w -DHAVE___BUILTIN_CLZ=1 -Ithird_party/wavpack/include -Ithird_party/wavpack/src -c "$f" -o "$obj"
    fi
done

echo "libogg + libvorbis (decoder, C)..."
mkdir -p "$OUT/vorbis"
for f in third_party/vorbis/src/*.c; do
    [[ "$(basename "$f")" == lookup.c ]] && continue # (included by lsp.c)
    obj="$OUT/vorbis/$(basename "$f" .c).o"
    if [[ ! -f "$obj" || "$f" -nt "$obj" ]]; then
        cached "$obj" "$TP_KEY_vorbis" -- "$ZIG" cc -target "$TARGET" -O2 -w -ffp-contract=off -include third_party/vorbis/include/lynx_vorbis_prefix.h -Ithird_party/vorbis/include -Ithird_party/vorbis/src -c "$f" -o "$obj"
    fi
done

echo "LAME libmp3lame (MP3 encoder, C)..."
mkdir -p "$OUT/lame"
for f in third_party/lame/src/*.c; do
    obj="$OUT/lame/$(basename "$f" .c).o"
    if [[ ! -f "$obj" || "$f" -nt "$obj" ]]; then
        cached "$obj" "$TP_KEY_lame" -- "$ZIG" cc -target "$TARGET" -O2 -w -DHAVE_CONFIG_H -Ithird_party/lame/include -Ithird_party/lame/src -c "$f" -o "$obj"
    fi
done

echo "pocketpy (embedded Python, C)..."
mkdir -p "$OUT/pocketpy"
obj="$OUT/pocketpy/pocketpy.o"
if [[ ! -f "$obj" || third_party/pocketpy/pocketpy.c -nt "$obj" || third_party/pocketpy/pocketpy.h -nt "$obj" ]]; then
    cached "$obj" "$TP_KEY_pocketpy" -- "$ZIG" cc -target "$TARGET" -O2 -w -std=c11 -DPK_ENABLE_OS=0 -DPK_ENABLE_WATCHDOG=1 -Ithird_party/pocketpy -c third_party/pocketpy/pocketpy.c -o "$obj"
fi

echo "Monocypher (live session encryption, C)..."
mkdir -p "$OUT/monocypher"
obj="$OUT/monocypher/monocypher.o"
if [[ ! -f "$obj" || third_party/monocypher/monocypher.c -nt "$obj" || third_party/monocypher/monocypher.h -nt "$obj" ]]; then
    cached "$obj" "$TP_KEY_monocypher" -- "$ZIG" cc -target "$TARGET" -O2 -w -Ithird_party/monocypher -c third_party/monocypher/monocypher.c -o "$obj"
fi

# the commit the build comes from (crash reports name it)
COMMIT="$(git rev-parse --short=10 HEAD 2>/dev/null || true)"
[[ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ]] && COMMIT="$COMMIT-dirty"
printf '#pragma once\n#define LYNX_GIT_COMMIT "%s"\n' "$COMMIT" > "$OUT/lynx_build.h"

# -Werror=return-type (round 190ci): a value-returning function or lambda that can end without a return is undefined
# behaviour that clang compiles to "fall into the next function". A merge that dropped one `return` line shipped that
# way in the 0.1.26 release candidate (the FPC editor's channel lookup: adding an FPC channel ended LYNX) with only a
# warning in the log. It is an error in LYNX's own sources.
echo "Compiling the app..."
printf '%s\n' "${APP_SOURCES[@]}" | xargs -P "$JOBS" -I{} bash -c '
    src={}; obj="$OUT/app/$(echo "$src" | tr / _).o"
    extra=""; [[ "$src" == src/platform/CrashGuard.cpp ]] && extra="-fms-extensions"
    [[ "$src" == src/ui/CrashReporter.cpp ]] && extra="-iquote $OUT"
    cached_pp "$obj" -- $ZIG c++ $COMMON_STR $extra -Wall -Wextra -Werror=return-type -c "$src" -o "$obj"'

# 2026-11-04: an object whose source left APP_SOURCES (another branch's build in this folder) must not be linked
# by the app/*.o glob below: it broke unrelated landings with undefined symbols.
declare -A KEEP_OBJ=(); for src in "${APP_SOURCES[@]}"; do KEEP_OBJ["$OUT/app/$(echo "$src" | tr / _).o"]=1; done
for obj in "$OUT"/app/*.o; do [[ -n "${KEEP_OBJ[$obj]:-}" ]] || { echo "removing stale object $obj"; rm -f "$obj"; }; done

echo "Resources (manifest, icons, version $VERSION)..."
OUT_ABS="$(cd "$OUT" && pwd)"
IFS=. read -r VMAJ VMIN VPAT <<< "$VERSION"
{
    echo '#pragma once'
    echo "#define LYNX_VERSION_MAJOR $VMAJ"
    echo "#define LYNX_VERSION_MINOR $VMIN"
    echo "#define LYNX_VERSION_PATCH $VPAT"
    echo "#define LYNX_VERSION_STR \"$VERSION\""
} > "$OUT/lynx_version.h"
( cd resources && "$ZIG" rc /i "$OUT_ABS" /fo "$OUT_ABS/LYNX.res" LYNX.rc )

echo "Linking..."
# only the objects of this tree's sources: an object left in $OUT/app by a build of another branch (a merge-train
# bisect step without that branch) must never be linked in
APP_OBJECTS=()
for src in "${APP_SOURCES[@]}"; do APP_OBJECTS+=("$OUT/app/$(echo "$src" | tr / _).o"); done
# (one list: the plugins of LYNX_BUILD_PLUGINS=1 below link the same app objects, so they need the same libraries)
APP_LIBS=(-lole32 -loleaut32 -luuid -lwinmm -lws2_32 -lshlwapi -lversion -lwininet -limm32 -lcomdlg32
          -lgdi32 -luser32 -lcomctl32 -lshell32 -ladvapi32 -lrpcrt4 -ldbghelp -lsetupapi -luxtheme -ldwmapi
          -ld2d1 -ldcomp -ldwrite -ldxgi -ld3d11 -ldxguid -lwsock32 -lopengl32 -lpropsys -lmfplat -lmfreadwrite -lmfuuid -lksuser -lavrt -lwinhttp)
"$ZIG" c++ -target "$TARGET" -O2 -o "$OUT/LYNX.exe" "$OUT"/juce/*.o "${APP_OBJECTS[@]}" "$OUT"/wavpack/*.o "$OUT"/vorbis/*.o "$OUT"/lame/*.o "$OUT"/pocketpy/*.o "$OUT"/monocypher/*.o "$OUT/LYNX.res" \
    -Wl,--subsystem,windows "${APP_LIBS[@]}"

ls -la "$OUT/LYNX.exe"

# LYNX ASIO driver (REA round 97), shipped next to LYNX.exe
OUT="$OUT" ZIG="$ZIG" scripts/build-lynx-asio.sh

# the 32-bit plugin bridge (round 176a), shipped next to LYNX.exe; it reuses the patched JUCE copies and the object cache
OUT="$OUT" ZIG="$ZIG" JUCE_DIR="$JUCE_DIR" JOBS="$JOBS" scripts/build-bridge32.sh

# LYNX's own plugins as VST3 / CLAP (round 180b, docs/PLUGIN_EXPORT.md): only with LYNX_BUILD_PLUGINS=1; the normal build stops above
if [[ "${LYNX_BUILD_PLUGINS:-0}" == 1 ]]; then
    PLUGIN_OBJECTS=()
    for src in "${APP_SOURCES[@]}"; do [[ "$src" == src/Main.cpp ]] || PLUGIN_OBJECTS+=("$OUT/app/$(echo "$src" | tr / _).o"); done
    mkdir -p "$OUT/plugin" "$OUT/plugins"
    PLUGIN_LIBS=("${APP_LIBS[@]}" -lpsapi)
    plugin_cxx() { # <obj> <source> [flags...]
        local obj="$1" source="$2"; shift 2
        cached_pp "$obj" -- $ZIG c++ $COMMON_STR -Isrc -DLYNX_EXPORT_VERSION="\"$VERSION\"" -DJUCE_MODULE_AVAILABLE_juce_audio_plugin_client=1 "$@" -c "$source" -o "$obj"
    }
    plugin_link() { # <output> <objects...>
        local output="$1"; shift
        "$ZIG" c++ -target "$TARGET" -O2 -shared -s -o "$output" "$@" "$OUT/plugin/ExportProcessor.o" "${PLUGIN_OBJECTS[@]}" "$OUT"/juce/*.o "$OUT"/wavpack/*.o "$OUT"/vorbis/*.o "$OUT"/lame/*.o "$OUT"/pocketpy/*.o "$OUT"/monocypher/*.o "$OUT/plugin/LynxPlugin.res" "${PLUGIN_LIBS[@]}" -Wl,--gc-sections
        rm -f "$(dirname "$output")"/*.lib "$(dirname "$output")"/*.pdb # (the linker's import library: not part of a plugin)
    }
    ( cd resources && "$ZIG" rc /fo "$OUT_ABS/plugin/LynxPlugin.res" LynxPlugin.rc )
    plugin_cxx "$OUT/plugin/ExportProcessor.o" src/plugin/ExportProcessor.cpp -Wall -Wextra
    plugin_cxx "$OUT/plugin/ClapEntry.o" src/plugin/ClapEntry.cpp -Wall -Wextra
    plugin_link "$OUT/plugins/LYNX Plugins.clap" "$OUT/plugin/ClapEntry.o"
    index=0 # (one table, src/plugin/plugins.def: every built entry; the index counts them, plugin/ExportCatalog.h)
    while IFS='~' read -r name synth code category built; do
        # built = 2: a VST3 of its own as well (1: in the CLAP only)
        [[ "$built" == 2 ]] || { index=$((index + 1)); continue; }
        dir="$OUT/plugins/$name.vst3/Contents/x86_64-win"
        mkdir -p "$dir"
        plugin_cxx "$OUT/plugin/Vst3Entry$index.o" src/plugin/Vst3Entry.cpp -w -DLYNX_EXPORT_INDEX=$index "-DJucePlugin_Name=\"$name\"" -DJucePlugin_IsSynth=$synth \
            -DJucePlugin_PluginCode=$code "-DJucePlugin_Vst3Category=\"$category\""
        plugin_link "$dir/$name.vst3" "$OUT/plugin/Vst3Entry$index.o"
        index=$((index + 1))
    done < <(sed -nE 's/^LYNX_PLUGIN \("([^"]+)", ([01]), [^,]+, [^,]+, "[^"]+", (0x[0-9a-f]+), "([^"]+)", ([12])\)$/\1~\2~\3~\4~\5/p' src/plugin/plugins.def)
    plugin_cxx "$OUT/plugin/plugexporttest.o" tools/plugexporttest.cpp -Wall -Wextra
    plugin_cxx_link=("$OUT/plugin/plugexporttest.o" "$OUT/plugin/ExportProcessor.o" "${PLUGIN_OBJECTS[@]}" "$OUT"/juce/*.o "$OUT"/wavpack/*.o "$OUT"/vorbis/*.o "$OUT"/lame/*.o "$OUT"/pocketpy/*.o "$OUT"/monocypher/*.o "$OUT/LYNX.res")
    "$ZIG" c++ -target "$TARGET" -O2 -o "$OUT/plugexporttest.exe" "${plugin_cxx_link[@]}" "${PLUGIN_LIBS[@]}"
    ls -la "$OUT"/plugins/*.clap "$OUT"/plugins/*/Contents/x86_64-win/*
fi
