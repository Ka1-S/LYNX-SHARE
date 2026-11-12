#!/usr/bin/env bash
# Builds and tests the core (without JUCE) with any C++20 compiler.
# Uses $CXX if set; otherwise tries Zig in ~/opt, then g++/clang++.
set -euo pipefail
cd "$(dirname "$0")/.."
# tests/tests.cpp is one big translation unit: at -O2 clang (zig 0.13's 18.1) needs more than the default 8 MB stack
# for it (a segfault, exit 139 / abort 134, with no message). Raise the soft limit (no root needed) to 1 GiB: a FINITE
# value, because with "unlimited" glibc gives new threads only the default 8 MB and zig compiles on worker threads.
ulimit -s 1048576 2>/dev/null || ulimit -s "$(ulimit -H -s)" 2>/dev/null || true

if [[ -z "${CXX:-}" ]]; then
    if [[ -x "$HOME/opt/zig-linux-x86_64-0.13.0/zig" ]]; then
        CXX="$HOME/opt/zig-linux-x86_64-0.13.0/zig c++"
    else
        CXX="$(command -v clang++ || command -v g++)"
    fi
fi

FLAGS="-std=c++20 -O2 -g -ffp-contract=off -Wall -Wextra -Wpedantic -Isrc -Ithird_party/wavpack/include -Ithird_party/vorbis/include -Ithird_party/lame/include -Ithird_party/sse2neon -Ithird_party/pocketpy -Ithird_party/monocypher"
CORE="src/flp/Flp.cpp src/core/Song.cpp src/core/MarkerPlan.cpp src/core/NoteEdit.cpp src/core/Recorder.cpp src/core/TakeRecording.cpp src/core/Comping.cpp src/core/Pdc.cpp src/core/WorkerPool.cpp src/core/TimeStretch.cpp src/core/Arpeggiator.cpp src/core/SynthPresets.cpp src/core/Slicer.cpp src/core/AudioToMidi.cpp src/core/Templates.cpp src/core/FlWrapper.cpp src/core/FlEffects.cpp src/core/Effector.cpp src/core/VintagePhaser.cpp src/core/EmphasizerFx.cpp src/core/EmphasisFx.cpp src/core/TransporterFl.cpp src/core/EmphasisEngine.cpp src/core/FreqShifterFl.cpp src/core/LowLifterFl.cpp src/core/FlLegacyFx.cpp src/core/PocketFx.cpp src/core/PocketMultiFx.cpp src/core/VisualFx.cpp src/core/FlExtraFx.cpp src/core/Scratcher.cpp src/core/Dvs.cpp src/core/Transmitter.cpp src/core/VideoPlayer.cpp src/core/FruityDance.cpp src/core/Reeverb2.cpp src/core/LuxeVerb.cpp src/core/IldspMath.cpp src/core/ConvolverFx.cpp src/core/EquoFx.cpp src/core/PitchShifterFl.cpp src/core/SimSynth.cpp src/core/Wasp.cpp src/core/Granulizer.cpp src/core/Drumaxx.cpp src/core/FruityFilters.cpp src/core/NewtoneFx.cpp src/core/EdisonFx.cpp src/core/VocalPitch.cpp src/core/LynxTune.cpp src/core/LynxVerbs.cpp src/core/NebulaCloud.cpp src/core/MixSuite.cpp src/core/VoiceMatch.cpp src/core/EchoSuite.cpp src/core/LovePhilter.cpp src/core/DistructorFx.cpp src/core/MultibandDelayFx.cpp src/core/FrequencySplitter.cpp src/core/Slayer.cpp src/core/FlangusFastDist.cpp src/core/FruityPhaser.cpp src/core/FruityDelayBank.cpp src/core/FruityMultiband.cpp src/core/Vocodex.cpp src/core/FruityVocoder.cpp src/core/DelphiMath.cpp src/core/MaximusFx.cpp src/core/ParametricEqFl.cpp src/core/ParametricEqLinear.cpp src/core/FruityBasicFx.cpp src/core/TransistorBass.cpp src/core/GrossBeat.cpp src/core/StereoFx.cpp src/core/FruityDelay.cpp src/core/FruityShapers.cpp src/core/ChorusFx.cpp src/core/FruityDynamics.cpp src/core/FlSamplers.cpp src/core/FlSynths.cpp src/core/FlSytrus.cpp src/core/FlGenerators.cpp src/core/PhysicalModels.cpp src/core/DrumSynthLive.cpp src/core/SytrusEngine.cpp src/core/Harmless.cpp src/core/HarmlessFx.cpp src/core/Harmor.cpp src/core/Gms.cpp src/core/Morphine.cpp src/core/Ogun.cpp src/core/Toxic.cpp src/core/ToxicFx.cpp src/core/FruitKick.cpp src/core/BassDrum.cpp src/core/MiniSynth.cpp src/core/Dx10.cpp src/core/Kepler.cpp src/core/PoiZone.cpp src/core/FlPatcher.cpp src/core/Patcher.cpp src/core/Vfx.cpp src/core/VfxSequencer.cpp src/core/Inflate.cpp src/core/WavPack.cpp src/core/Scheduler.cpp src/core/Performance.cpp src/core/ClipLauncher.cpp src/core/Patcherize.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp src/core/LoadTrace.cpp src/core/ModuleWarmup.cpp src/core/FlpImport.cpp src/core/FlpExport.cpp src/core/XmlLite.cpp src/core/Gzip.cpp src/core/AlsImport.cpp src/core/AlsExport.cpp src/flp/FlpWrite.cpp src/core/Effects.cpp src/core/EmberClassic.cpp src/core/RhythmPan.cpp src/core/VoxStrip.cpp src/core/WaveTraveller.cpp src/core/FruitySlicer.cpp src/core/Slicex.cpp src/core/SlicexEngine.cpp src/core/VideoTime.cpp src/core/VideoTrack.cpp src/core/Adr.cpp src/core/DialogueAlign.cpp src/core/FlmFormat.cpp src/core/FlmImport.cpp src/core/FlmEffects.cpp src/core/PocketFxC.cpp src/core/PocketFxD.cpp src/core/PocketFxB.cpp src/core/PocketFxA.cpp src/core/Fpc.cpp src/core/FpcInstrument.cpp src/core/FpcEnvelope.cpp src/core/Rack.cpp src/core/AlsDevices.cpp src/core/FlmNoteFx.cpp src/core/FlmDwSampler.cpp src/core/FlmSlicer.cpp src/core/SessionOps.cpp src/core/Groove.cpp src/core/AlsDevicesLot2.cpp src/core/RemoteMap.cpp"

# (the generated test chunks include tests/*.inc; the same flags everywhere keep one cached object per core file)
FLAGS="$FLAGS -iquote tests"

mkdir -p build-local build-local/wavpack
# workflow shell (key map, workspace layout, rack operations)
CORE="$CORE src/core/KeyMap.cpp src/core/PresetCycle.cpp src/core/Sfz.cpp src/core/Workspace.cpp src/core/RackOps.cpp src/core/PlaylistTrackOps.cpp src/core/ChannelMiscFx.cpp src/core/MidiImport.cpp"
# workflow P2 (note tools, mixer operations, history labels, browser search)
CORE="$CORE src/core/RollingAudioLog.cpp src/core/ChordReadout.cpp src/core/NoteTools.cpp src/core/MidiTools.cpp src/core/PianoRollTools.cpp src/core/EventEdit.cpp src/core/AudioEdit.cpp src/core/AudioEditTools.cpp src/core/FlAutoSlicer.cpp src/core/MixerOps.cpp src/core/History.cpp src/core/SearchQuery.cpp src/core/SearchIndex.cpp src/core/SearchService.cpp src/core/BrowserView.cpp src/core/EffectPresets.cpp src/core/PluginPresets.cpp src/core/ParamHelp.cpp"
# Slicex as a channel (round 190l)
CORE="$CORE src/core/SlicexInstrument.cpp"
# Loop Starter, local (round 168b)
CORE="$CORE src/core/LoopStarter.cpp"
# Simple workflow: adding a track (round 184a)
CORE="$CORE src/core/SimpleTracks.cpp src/core/KeyFollow.cpp src/core/GamepadMap.cpp src/core/ExportBundle.cpp"
# Simple workflow: one-knob effects, chain presets, the sound picker (round 190bu)
CORE="$CORE src/core/GoalKnobs.cpp src/core/ChainPresets.cpp src/core/SoundLibrary.cpp"
# MIDI controller profiles and the controller layer (round 190aw)
CORE="$CORE src/core/ControllerProfile.cpp src/core/ControllerSession.cpp"
# start-up sound (procedural)
CORE="$CORE src/core/StartupSound.cpp"
# speech synthesizer: .speech files, LYNX voice (round 168b)
CORE="$CORE src/core/Speech.cpp src/core/SpeechRules.cpp"
# FL's 3x Osc engine (REA round 47)
CORE="$CORE src/core/ThreeOsc.cpp"
# FL Studio Mobile's GMSynth (round 190ab)
CORE="$CORE src/core/GmsMobile.cpp"
# Pocket SuperSaw: FL Studio Mobile's SuperSaw (round 190by)
CORE="$CORE src/core/PocketSuperSaw.cpp"
# FL Studio integration: plugin database and sample relocation (read-only)
CORE="$CORE src/core/FlPluginDb.cpp"
# missing files of an opened project: search and relocation (round 81)
CORE="$CORE src/core/MissingFiles.cpp"
# the plugin picker's catalog (round 151)
CORE="$CORE src/core/PluginCatalog.cpp"
CORE="$CORE src/core/PluginDbFolder.cpp"
CORE="$CORE src/core/FolderWatcher.cpp"
# project safety: backups, versions, packages (REA round 118)
CORE="$CORE src/core/ProjectSafety.cpp"
# Online Sessions P1: stable ids, parts, stand-ins (round 168a)
CORE="$CORE src/core/SessionIds.cpp src/core/SessionParts.cpp src/core/StandIns.cpp"
# Online Sessions P2: live sessions — entity replication, authority, replicas, invite code + encryption (round 169b)
CORE="$CORE src/core/LiveSession.cpp src/core/LiveCrypto.cpp"
# Online Sessions P5-A1: remote plugins (round 190ai)
CORE="$CORE src/core/RemotePlugins.cpp"
# LYNX substitutes for missing third-party plugins (round 98)
CORE="$CORE src/core/PluginSubstitutes.cpp"
# settings pages, plugin manager and shortcut manager logic (round 82)
CORE="$CORE src/core/Preferences.cpp src/core/MidiSync.cpp"
# user themes: the palette derived from an accent and a background (round 162d)
CORE="$CORE src/core/ThemePalette.cpp"
# layouts of the native plugin editors (round 93)
CORE="$CORE src/core/PluginLayout.cpp src/core/StateParams.cpp"
# the drum / physical-model generators' controls and LYNX bank states (round 168p)
CORE="$CORE src/core/DrumControls.cpp src/core/BankStates.cpp src/core/DrumBanks.cpp"
# spectral waveforms (band analysis, multi-resolution peak cache)
CORE="$CORE src/core/WaveAnalysis.cpp"
# FL's Plucked!, Sawer and BeepMap engines (REA round 65)
CORE="$CORE src/core/Plucked.cpp src/core/BeepMap.cpp src/core/Sawer.cpp"
# FL's sampler interpolation and FL Keys (REA round 69)
CORE="$CORE src/core/FlResampler.cpp src/core/FlKeys.cpp"
# FL's Hardcore (REA round 71)
CORE="$CORE src/core/HardcoreFx.cpp"
# FL's DirectWave (REA round 72)
CORE="$CORE src/core/DirectWave.cpp"
# LYNX Soundfont: SF2 / SF3 player (round 158; TinySoundFont + stb_vorbis in third_party)
CORE="$CORE src/core/SoundFont.cpp src/core/SoundFontLoad.cpp src/core/SoundFontIndex.cpp"
# Ogg Vorbis decoding as FL does (REA round 77)
CORE="$CORE src/core/OggVorbis.cpp"
# export options, MP3 encoding, plugin performance monitor, clip properties / arrangements (round 84)
CORE="$CORE src/core/ExportOptions.cpp src/core/Mp3Encoder.cpp src/core/Mp3Decoder.cpp src/core/PluginMonitor.cpp src/core/PlaylistOps.cpp src/core/QuickRender.cpp src/core/ProjectContents.cpp src/core/TempoDetect.cpp src/core/AutoWarp.cpp src/core/ScoreLog.cpp src/core/ChannelLoops.cpp src/core/PatternBlocks.cpp src/core/ChannelBurn.cpp src/core/AutoSampler.cpp"
# LYNX's Facet EQ and Apex Limiter (round 99)
CORE="$CORE src/core/FacetEq.cpp src/core/ApexLimiter.cpp src/core/Mastering.cpp src/core/MasterLooks.cpp"
# LYNX's dynamics suite and FabFilter state translation (round 108)
CORE="$CORE src/core/DynamicsSuite.cpp src/core/FabFilterStates.cpp"
# LYNX's voice tools: Quell Denoiser, Gemini Doubler (round 119)
CORE="$CORE src/core/VoiceTools.cpp"
# MIDI CC automation of hosted plugins, Formula Controller (round 133, issue #3)
CORE="$CORE src/core/MidiControls.cpp src/core/MidiOut.cpp src/core/FormulaController.cpp"
# controller links: FL's mapping formula and smoothing (round 190bt)
CORE="$CORE src/core/LinkFormula.cpp"
CORE="$CORE src/core/VoiceShift.cpp"
# FL engines usable in native projects (round 142, issue #5)
CORE="$CORE src/core/NativeEngines.cpp src/core/LynxBanks.cpp src/core/ModelParams.cpp src/core/ToxicBank.cpp src/core/PoiZoneBank.cpp src/core/ModelParamsSynths.cpp src/core/PluginLayoutSynths.cpp src/core/HarmlessBank.cpp src/core/Dx10Bank.cpp src/core/SimSynthBank.cpp src/core/BeepMapBank.cpp src/core/GranulizerBank.cpp src/core/EngineLayouts.cpp"
# FL's Layer channel and the round 164 controllers
CORE="$CORE src/core/Layer.cpp src/core/XyController.cpp src/core/FlCurve.cpp src/core/ControlSurface.cpp src/core/VoltageController.cpp"
# crash / freeze reports: consent, anonymiser, report JSON, GitHub issue URL (round 159)
CORE="$CORE src/core/CrashReport.cpp"
# in-app update check: versions, release list, flavours, checksums, release notes (round 166b)
CORE="$CORE src/core/UpdateCheck.cpp"
# the Browser's "Find similar samples" (round 190as)
CORE="$CORE src/core/SampleSimilarity.cpp"
# the embedded Python (pocketpy) and FL's VFX Script (round 165d)
CORE="$CORE src/core/Python.cpp src/core/VfxScript.cpp src/core/EdisonScript.cpp src/core/ScriptDialog.cpp src/core/PianoRollScript.cpp"
# WavPack decoder (C)
if [[ -z "${CC:-}" ]]; then
    case "$CXX" in
        *zig*) CC="${CXX/c++/cc}" ;;
        *clang++*) CC="${CXX/clang++/clang}" ;;
        *g++*) CC="${CXX/g++/gcc}" ;;
        *) CC=cc ;;
    esac
fi
for f in third_party/wavpack/src/*.c; do
    obj="build-local/wavpack/$(basename "$f" .c).o"
    [[ -f "$obj" && "$obj" -nt "$f" ]] || $CC -c -O2 -w -DHAVE___BUILTIN_CLZ=1 -Ithird_party/wavpack/include -Ithird_party/wavpack/src "$f" -o "$obj"
done
WAVPACK="$(ls build-local/wavpack/*.o | tr '\n' ' ')"
# libogg + libvorbis decoder (C, BSD; third_party/vorbis): no FMA contraction, so the float order stays libvorbis'
mkdir -p build-local/vorbis
for f in third_party/vorbis/src/*.c; do
    [[ "$(basename "$f")" == lookup.c ]] && continue # (included by lsp.c)
    obj="build-local/vorbis/$(basename "$f" .c).o"
    [[ -f "$obj" && "$obj" -nt "$f" ]] || $CC -c -O2 -w -ffp-contract=off -include third_party/vorbis/include/lynx_vorbis_prefix.h -Ithird_party/vorbis/include -Ithird_party/vorbis/src "$f" -o "$obj"
done
WAVPACK="$WAVPACK $(ls build-local/vorbis/*.o | tr '\n' ' ')"
# LAME's libmp3lame (MP3 encoder, LGPL; third_party/lame, configured by its src/config.h)
mkdir -p build-local/lame
for f in third_party/lame/src/*.c; do
    obj="build-local/lame/$(basename "$f" .c).o"
    [[ -f "$obj" && "$obj" -nt "$f" ]] || $CC -c -O2 -w -DHAVE_CONFIG_H -Ithird_party/lame/include -Ithird_party/lame/src "$f" -o "$obj"
done
WAVPACK="$WAVPACK $(ls build-local/lame/*.o | tr '\n' ' ')"
# pocketpy (C11, MIT; third_party/pocketpy, LYNX's changes marked "LYNX"): sandboxed (no os / files), interruptible
mkdir -p build-local/pocketpy
obj=build-local/pocketpy/pocketpy.o
[[ -f "$obj" && "$obj" -nt third_party/pocketpy/pocketpy.c && "$obj" -nt third_party/pocketpy/pocketpy.h ]] || $CC -c -O2 -w -std=c11 -DPK_ENABLE_OS=0 -DPK_ENABLE_WATCHDOG=1 -Ithird_party/pocketpy third_party/pocketpy/pocketpy.c -o "$obj"
WAVPACK="$WAVPACK $obj"
# Monocypher (BSD-2 / CC0; third_party/monocypher): the live sessions' encryption (round 169b)
mkdir -p build-local/monocypher
obj=build-local/monocypher/monocypher.o
[[ -f "$obj" && "$obj" -nt third_party/monocypher/monocypher.c ]] || $CC -c -O2 -w -Ithird_party/monocypher third_party/monocypher/monocypher.c -o "$obj"
WAVPACK="$WAVPACK $obj"
# zig links its compiler_rt, whose archive member also defines WEAK cos/sin/exp/log (double and x87 long double). When a
# link needs any builtin of that member (128-bit division...), those copies replace glibc's libm, whose last bits the
# x87 bit-exact goldens hold: the "no-LTO failures" of multiband_delay_exact and the Kepler tests (round speed185a; the
# LTO links resolve to glibc). So a zig -fno-lto link on Linux puts GCC's libgcc.a ahead of compiler_rt: the builtins
# come from libgcc and the math from glibc. LYNX_ZIG_LIBGCC=0 turns it off.
case "$CXX" in
    *zig*-fno-lto*)
        if [[ "$(uname -s)" == Linux && "${LYNX_ZIG_LIBGCC:-1}" != 0 ]]; then
            libgcc="$(gcc -print-libgcc-file-name 2> /dev/null || true)"
            if [[ -f "$libgcc" ]]; then WAVPACK="$WAVPACK $libgcc"; else echo "note: no libgcc.a: zig's compiler_rt math replaces glibc's libm (x87 goldens may fail)" >&2; fi
        fi ;;
esac
# g++ / clang++ (not zig, which caches each object itself): every source is compiled once per build, in parallel, and
# the tests and tools link the objects (scripts/cxx-once.sh; it saves ~90 % of a CI "Core" job). LYNX_COMPILE_ONCE=0: off.
case "$CXX" in
    *zig*) ;;
    *)
        if [[ "${LYNX_COMPILE_ONCE:-1}" != 0 ]]; then
            export LYNX_CXX_ONCE_REAL="$CXX" LYNX_CXX_ONCE_RUN="$$-$(date +%s)"
            mkdir -p build-local/obj
            rm -f build-local/obj/*/.done-*
            CXX=scripts/cxx-once.sh
        fi ;;
esac
# The tests: tests/tests.cpp with its .inc files is split at build time into N translation units compiled in parallel
# (scripts/split-tests.py; same tests, same order, same checks). LYNX_TESTS_SPLIT=0 builds the single TU as before,
# LYNX_TESTS_SPLIT=<n> sets the number of chunks (default: the CPUs this build may use, 2 to 12).
split="${LYNX_TESTS_SPLIT:-auto}"
if [[ "$split" == auto ]]; then
    split="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    (( split < 2 )) && split=2
    (( split > 12 )) && split=12
fi
tests_single() { $CXX $FLAGS $CORE $WAVPACK tests/tests.cpp -o build-local/lynx_tests 2> >(grep -v 'glibc version' >&2); }
if [[ "$split" -ge 2 ]] && command -v python3 > /dev/null && python3 scripts/split-tests.py tests/tests.cpp build-local/split "$split"; then
    CHUNKS="build-local/split/lynx_split_main.cpp $(ls build-local/split/chunk*.cpp | tr '\n' ' ')"
    rc=0
    case "$CXX" in
        *zig*) # zig compiles every source of one command in parallel (and caches each object)
            $CXX $FLAGS $CORE $WAVPACK $CHUNKS -o build-local/lynx_tests 2> build-local/split/errors.log || rc=$? ;;
        *)
            pids=""
            for f in $CHUNKS; do
                $CXX $FLAGS -c "$f" -o "${f%.cpp}.o" 2> "${f%.cpp}.log" &
                pids="$pids $!"
            done
            for p in $pids; do wait "$p" || rc=$?; done
            # (not errors.log itself: a second run in the same build-local would otherwise cat it into itself)
            find build-local/split -maxdepth 1 -name '*.log' ! -name 'errors.log' -exec cat {} + > build-local/split/errors.log
            [[ $rc == 0 ]] && { $CXX $FLAGS $CORE $WAVPACK $(echo "$CHUNKS" | sed 's/\.cpp/.o/g') -o build-local/lynx_tests 2>> build-local/split/errors.log || rc=$?; } ;;
    esac
    grep -v 'glibc version' build-local/split/errors.log >&2 || true
    if [[ $rc != 0 ]]; then
        # a failure only the split can cause (a definition the splitter did not make inline...): build the single TU
        if grep -qE 'multiple definition|duplicate symbol|redefinition of|lynxSplit|LYNX_TEST_|LYNX_SPLIT' build-local/split/errors.log; then
            echo "note: the split test build failed (see above), building tests/tests.cpp as one unit (LYNX_TESTS_SPLIT=0)" >&2
            tests_single
        else
            exit $rc
        fi
    fi
else
    [[ "$split" -ge 2 ]] && echo "note: the test split is not available, building tests/tests.cpp as one unit" >&2
    tests_single
fi
# The tools: zig keys its per-file object cache on the output file name, so every tool that links the core recompiled
# all of it (7 times per cold build), and each one ran its own single-threaded LTO link, one after the other. With zig
# the lines below only queue the tools (build-local/.bin/cxx): each is linked as build-local/.bin/<tool>/lynx_tests
# (the tests' name, so the cached core objects are reused) and they are built in parallel before the tests run.
TOOLS_QUEUE=
case "$CXX" in
    *zig*)
        mkdir -p build-local/.bin
        TOOLS_QUEUE=build-local/.bin/queue
        rm -f "$TOOLS_QUEUE"
        cat > build-local/.bin/cxx << EOF
#!/usr/bin/env bash
args=(); out=
while [[ \$# -gt 0 ]]; do [[ "\$1" == -o ]] && { out="\$2"; shift 2; continue; }; args+=("\$1"); shift; done
[[ -n "\$out" ]] || exec $CXX "\${args[@]}"
d="build-local/.bin/\$(basename "\$out")"; mkdir -p "\$d"
{ printf '%q ' $CXX "\${args[@]}" -o "\$d/lynx_tests"; printf '2> >(grep -v "glibc version" >&2) && mv -f %q %q\n' "\$d/lynx_tests" "\$out"; } >> $TOOLS_QUEUE
EOF
        chmod +x build-local/.bin/cxx
        CXX=build-local/.bin/cxx ;;
esac
$CXX $FLAGS src/flp/Flp.cpp tools/flpdump.cpp -o build-local/flpdump 2> >(grep -v 'glibc version' >&2)

$CXX $FLAGS $CORE $WAVPACK tools/renderwav.cpp -o build-local/renderwav 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/OggVorbis.cpp $(ls build-local/vorbis/*.o | tr '\n' ' ') tools/oggdecode.cpp -o build-local/oggdecode 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/comparewav.cpp -o build-local/comparewav 2> >(grep -v 'glibc version' >&2)
# LYNX effects offline, the LYNX side of the third-party parity harness (round tp172b)
$CXX $FLAGS $CORE $WAVPACK tools/fxrender.cpp -o build-local/fxrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/vocodexrender.cpp -o build-local/vocodexrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Harmor.cpp src/core/HarmlessFx.cpp tools/harmorrender.cpp -o build-local/harmorrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/gmsrender.cpp -o build-local/gmsrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Morphine.cpp tools/morphinerender.cpp -o build-local/morphinerender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/FruitKick.cpp src/core/FlangusFastDist.cpp tools/fruitkickrender.cpp -o build-local/fruitkickrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/BassDrum.cpp tools/bassdrumrender.cpp -o build-local/bassdrumrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Toxic.cpp src/core/ToxicFx.cpp tools/toxicrender.cpp -o build-local/toxicrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/ogunrender.cpp -o build-local/ogunrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/MiniSynth.cpp tools/minisynthrender.cpp -o build-local/minisynthrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/SimSynth.cpp tools/simsynthrender.cpp -o build-local/simsynthrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Dx10.cpp tools/dx10render.cpp -o build-local/dx10render 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Kepler.cpp src/core/IldspMath.cpp tools/keplerrender.cpp -o build-local/keplerrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/TransistorBass.cpp tools/tbassrender.cpp -o build-local/tbassrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/PoiZone.cpp tools/poizonerender.cpp -o build-local/poizonerender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/bankrender.cpp -o build-local/bankrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/FruityDelay.cpp tools/fdelayrender.cpp -o build-local/fdelayrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/distructorrender.cpp -o build-local/distructorrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/ThreeOsc.cpp tools/threeoscrender.cpp -o build-local/threeoscrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Slayer.cpp tools/slayerrender.cpp -o build-local/slayerrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/wavetravellerrender.cpp -o build-local/wavetravellerrender 2> >(grep -v 'glibc version' >&2)

# (the tools queued above: build them in parallel, at most half the cores at once, an LTO link takes ~4 GB)
if [[ -n "$TOOLS_QUEUE" && -f "$TOOLS_QUEUE" ]]; then
    # 2026-11-04: in parallel the tools deadlocked inside zig's shared object cache (every zig process waiting on a
    # manifest lock another one held, 0 % CPU for hours). One at a time by default; LYNX_TOOLS_JOBS=n to try more.
    tools_jobs=${LYNX_TOOLS_JOBS:-1}
    if [[ "${LYNX_TOOLS_WITH_TESTS:-0}" == 1 ]]; then
        # (round speed185a) the tests don't use the tools: link them (single-threaded LTO links, ~8 min) while the tests run
        rm -f build-local/tools.rc
        ( xargs -P "$tools_jobs" -d '\n' -I{} bash -c '{}' < "$TOOLS_QUEUE" > build-local/tools.log 2>&1; echo $? > build-local/tools.rc; rm -f "$TOOLS_QUEUE" ) &
        tools_pid=$!
    else
        xargs -P "$tools_jobs" -d '\n' -I{} bash -c '{}' < "$TOOLS_QUEUE"
        rm -f "$TOOLS_QUEUE"
    fi
fi
# LYNX_TEST_JOBS=<n> (or "auto": the CPUs) runs the tests in parallel shards, then the timing-sensitive ones alone
# (scripts/run-tests.sh: same tests, same checks, the same "N checks, M failures" line). Unset or 1: one process, as before.
tests_rc=0
case "${LYNX_TEST_JOBS:-1}" in
    1) ./build-local/lynx_tests "$@" || tests_rc=$? ;;
    auto) scripts/run-tests.sh build-local/lynx_tests "$@" || tests_rc=$? ;;
    *) scripts/run-tests.sh -j "$LYNX_TEST_JOBS" build-local/lynx_tests "$@" || tests_rc=$? ;;
esac
if [[ -n "${tools_pid:-}" ]]; then
    wait "$tools_pid" || true
    if [[ "$(cat build-local/tools.rc 2> /dev/null)" != 0 ]]; then
        grep -v 'glibc version' build-local/tools.log >&2 || true
        echo "build-local: the tools did not build (build-local/tools.log)" >&2
        exit 1
    fi
fi
exit $tests_rc
