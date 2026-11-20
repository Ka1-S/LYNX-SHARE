#include "core/PluginSubstitutes.h"
#include "core/VoiceMatch.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>

#include "core/ApexLimiter.h"
#include "core/Effects.h"
#include "core/FabFilterStates.h"
#include "core/FacetEq.h"
#include "core/KiloheartsDsp.h"
#include "core/WavesApiEq.h"
#include "core/FlEffects.h"
#include "core/FlPluginDb.h"
#include "core/FlWrapper.h"
#include "core/LimiterLaw.h"
#include "core/LynxVerbs.h"
#include "core/LynxTune.h"
#include "core/MixSuite.h"
#include "core/LowLifterFl.h"
#include "core/VocalPitch.h"
#include "core/VoiceTools.h"
#include "core/VoiceShift.h"
#include "core/VoxStrip.h"
#include "core/EchoSuite.h"
#include "core/Reeverb2.h"
#include "core/DynamicsSuite.h"
#include "core/ZipLite.h"

namespace lynx
{
namespace
{
// The built-in table (format: docs/PLUGIN_FALLBACK.md). Seeded with the third-party plugins of tmp/gf/PLUGIN_QUEUE.md
// and common mixing staples, mapped to EXISTING LYNX effects. Identifiers marked [DATA] were read from real projects'
// FL wrapper records; the others match by name. Parameter maps only where the saved state shows the layout and a
// public manual gives the meaning (each one says so). Modules added later (pitch corrector, vocal tools...) append
// their own entries here or through registerPluginSubstitutes.
constexpr std::string_view builtInTable = R"TABLE(
# ---------------------------------------------------------------- reverbs -> LYNX's reverbs (round 100)
# Valhalla DSP: the saved state is a JUCE XML block whose attributes are the plugin's normalised (0..1) VST3 values
# [DATA, round 98]. Round 100 measured each parameter's value -> unit law and enumeration breakpoints on the official
# demos (black-box, REA_rodada100 §0-§6) [EXEC]; LYNX's own reverbs use the same laws, so most maps are identities;
# enumerations use "steps", and Atrium's decay (LYNX's own law) a "curve". The Mix controls are equal-power
# dry / wet inside the effect (measured), so the slot mix stays at 100 %.
[valhalla-vintageverb]
product = ValhallaVintageVerb
vendor = Valhalla DSP
lynx = Epoch Verb
# [DATA] class ID from FL wrapper records of real projects
vst3 = 7654535665657633616c68616c6c6176
names = ValhallaVintageVerb
# round 190mb: the reference's own level change away from 48 kHz (Epoch's hidden rateLaw, #21). Only a translated
# VintageVerb runs it
replaced = #21=1
map = xml:Mix -> Mix
map = xml:PreDelay -> Predelay
map = xml:Decay -> Decay
map = xml:Size -> Size
map = xml:Attack -> Attack
map = xml:BassMult -> Bass mult
map = xml:BassXover -> Bass xover
map = xml:HighShelf -> High shelf
map = xml:HighFreq -> High freq
map = xml:EarlyDiffusion -> Early diffusion
map = xml:LateDiffusion -> Late diffusion
map = xml:ModRate -> Mod rate
map = xml:ModDepth -> Mod depth
map = xml:HighCut -> High cut
map = xml:LowCut -> Low cut
map = xml:ColorMode -> Color | steps 0:0 0.6662:0.5 0.9995:1
map = xml:ReverbMode -> Mode | steps 0:0 0.0828:0.0476 0.1245:0.0952 0.1662:0.1429 0.2078:0.1905 0.2495:0.2381 0.2912:0.2857 0.3328:0.3333 0.3745:0.381 0.4162:0.4286 0.4578:0.4762 0.4995:0.5238 0.5412:0.5714 0.5828:0.619 0.6245:0.6667 0.6662:0.7143 0.7078:0.7619 0.7495:0.8095 0.7912:0.8571 0.8328:0.9048 0.8745:0.9524 0.9162:1 0.9578:0

[valhalla-room]
product = ValhallaRoom
vendor = Valhalla DSP
lynx = Atrium
names = ValhallaRoom
# round 190ly: a translated ValhallaRoom runs Atrium's dense 32-line late tank (hidden reaLaw, #24; round 190ls). The owner's
# perf rule: new Atriums keep the light 8-line tank (+48 % CPU for +0.18 of score is only worth it in the ValhallaRoom role)
replaced = #24=1
map = xml:mix -> Mix
map = xml:predelay -> Predelay
map = xml:HiCut -> High cut
map = xml:earlyLateMix -> Early/late
map = xml:lateSize -> Late size
map = xml:lateCross -> Late cross
map = xml:lateModRate -> Late mod rate
map = xml:lateModDepth -> Late mod depth
map = xml:RTBassMultiply -> Bass mult
map = xml:RTXover -> Bass xover
map = xml:RTHighMultiply -> High mult
map = xml:RTHighXover -> High xover
map = xml:earlySize -> Early size
map = xml:earlyCross -> Early cross
map = xml:earlyModRate -> Early mod rate
map = xml:earlyModDepth -> Early mod depth
map = xml:earlySend -> Early send
map = xml:diffusion -> Diffusion
map = xml:space -> Space
map = xml:LoCut -> Low cut
map = xml:decay -> Decay | curve 0:0 0.0002:0.2254 0.0005:0.2646 0.001:0.2987 0.002:0.3372 0.004:0.3807 0.008:0.4297 0.012:0.4613 0.019:0.4999 0.03:0.5415 0.05:0.5921 0.08:0.6429 0.13:0.6999 0.2:0.7546 0.3:0.8101 0.45:0.8696 0.65:0.9274 1:1
map = xml:type -> Type | steps 0:0 0.1662:0.0909 0.2495:0.1818 0.3328:0.2727 0.4162:0.3636 0.4995:0.4545 0.5828:0.5455 0.6662:0.6364 0.7495:0.7273 0.8328:0.8182 0.9162:0.9091 0.9995:1

[valhalla-plate]
product = ValhallaPlate
vendor = Valhalla DSP
lynx = Foil Plate
names = ValhallaPlate
map = xml:Mix -> Mix
map = xml:PreDelay -> Predelay
map = xml:Decay -> Decay
map = xml:Size -> Size
map = xml:Width -> Width
map = xml:ModRate -> Mod rate
map = xml:ModDepth -> Mod depth
map = xml:LowEQFreq -> Low EQ freq
map = xml:LowEQGain -> Low EQ gain
map = xml:HighEQFreq -> High EQ freq
map = xml:HighEQGain -> High EQ gain
map = xml:Type -> Type | steps 0:0 0.0828:0.0909 0.1245:0.1818 0.1662:0.2727 0.2078:0.3636 0.2495:0.4545 0.2912:0.5455 0.3328:0.6364 0.3745:0.7273 0.4162:0.8182 0.4578:0.9091 0.4995:1 0.5412:0

[valhalla-supermassive]
product = Valhalla Supermassive
vendor = Valhalla DSP
lynx = Nebula
names = ValhallaSupermassive
# round 190mb: the classic networks' per-mode tone and level (Nebula's hidden toneLaw, #15). Only a translated
# Supermassive runs it
replaced = #15=1
map = xml:Mix -> Mix
map = xml:Delay_Ms -> Delay
map = xml:DelayWarp -> Warp
map = xml:Feedback -> Feedback
map = xml:Density -> Density
map = xml:Width -> Width
map = xml:LowCut -> Low cut
map = xml:HighCut -> High cut
map = xml:ModRate -> Mod rate
map = xml:ModDepth -> Mod depth
map = xml:DelaySync -> Sync | steps 0:0 0.4995:0.3333 0.7495:0.6667 0.9995:1
map = xml:DelayNote -> Note | steps 0:0 0.1453:0.1667 0.287:0.3333 0.4287:0.5 0.5745:0.6667 0.7162:0.8333 0.8578:1
map = xml:Mode -> Mode | steps 0:0 0.0828:0.0476 0.1245:0.0952 0.1662:0.1429 0.2078:0.1905 0.2495:0.2381 0.2912:0.2857 0.3328:0.3333 0.3745:0.381 0.4162:0.4286 0.4578:0.4762 0.4995:0.5238 0.5412:0.5714 0.5828:0.619 0.6245:0.6667 0.6662:0.7143 0.7078:0.7619 0.7495:0.8095 0.7912:0.8571 0.8328:0.9048 0.8745:0.9524 0.9162:1 0.9578:0

[valhalla-shimmer]
product = ValhallaShimmer
vendor = Valhalla DSP
lynx = Halo
names = ValhallaShimmer
# the reference saves bare parameterN attributes (0..1); the order is its VST3 parameter list [EXEC]
map = xml:parameter0 -> Mix
map = xml:parameter1 -> Shift
map = xml:parameter2 -> Feedback
map = xml:parameter3 -> Diffusion
map = xml:parameter4 -> Size
map = xml:parameter5 -> Low cut
map = xml:parameter6 -> High cut
map = xml:parameter7 -> Mod rate
map = xml:parameter8 -> Mod depth
# round 135: its four reverb modes (Mono / three stereo sizes, steps at 0.25 / 0.5 / 0.75 [EXEC]) -> Halo's same four
map = xml:parameter9 -> Reverb mode | steps 0:0.125 0.25:0.25 0.5:0.375 0.75:0.625
map = xml:parameter10 -> Shift mode | steps 0:0 0.25:0.25 0.45:0.5 0.65:0.75 0.85:1
# round 190it: its colour turns Dark between 0.479 and 0.521 (refcap030's colour sweep), so at 0.5
map = xml:parameter11 -> Color | steps 0:0 0.5:1

[valhalla-futureverb]
product = ValhallaFutureVerb
vendor = Valhalla DSP
lynx = Horizon
names = ValhallaFutureVerb
map = xml:Mix -> Mix
map = xml:Width -> Width
map = xml:Echo_Ms -> Echo time
map = xml:EchoFeedback -> Echo feedback
map = xml:EchoSpread -> Echo spread
map = xml:EchoDrive -> Echo drive
map = xml:EchoDetune -> Echo detune
map = xml:EchoLevel -> Echo level
map = xml:ReverbDecay -> Decay
map = xml:ReverbSize -> Size
map = xml:ReverbAttack -> Attack
map = xml:ReverbEarlyLate -> Early/late
map = xml:ReverbDensity -> Density
map = xml:ReverbLevel -> Reverb level
map = xml:ModRate -> Mod rate
map = xml:ModDepth -> Mod depth
map = xml:LowEQFreq -> Low EQ freq
map = xml:HighEQFreq -> High EQ freq
map = xml:EchoSync -> Echo sync | steps 0:0 0.4995:0.3333 0.7495:0.6667 0.9995:1
map = xml:EchoNote -> Echo note | steps 0:0 0.1662:0.2 0.3328:0.4 0.4995:0.6 0.6662:0.8 0.8328:1
map = xml:Routing -> Routing | steps 0:0 0.4995:1
# round 183b: its modes sit at (k + 1) / 24 (its factory presets) and switch at those exact points (measured: 0.0833
# plays Room, 0.08333 Chamber; 0.3333 Frozen, 0.33333 Nonlin)
map = xml:EchoMode -> Echo mode | steps 0:0 0.083333:0.0909 0.125:0.1818 0.166667:0.2727 0.208333:0.3636 0.25:0.4545 0.291667:0.5455 0.333333:0.6364 0.375:0.7273 0.416667:0.8182 0.458333:0.9091 0.5:1 0.541667:0
map = xml:ReverbMode -> Reverb mode | steps 0:0 0.083333:0.1429 0.125:0.2857 0.166667:0.4286 0.208333:0.5714 0.25:0.7143 0.291667:0.8571 0.333333:1 0.375:0
map = xml:Color -> Color | steps 0:0 0.1662:0.3333 0.2495:0.6667 0.3328:1

# ---------------------------------------------------------------- other reverbs -> Reeverb 2 / LuxeVerb
[fabfilter-pro-r]
product = FabFilter Pro-R
vendor = FabFilter
lynx = LuxeVerb
names = FabFilter Pro-R; FabFilter Pro-R 2; Pro-R; Pro-R 2
mix = 1
# round tp172b: a Pro-R 2 state opens LuxeVerb's Space engine at its settings (core/SpaceVerb.h)
translate = pro-r2

[soundtoys-littleplate]
product = Little Plate
vendor = Soundtoys
lynx = Foil Plate
names = LittlePlate; Little Plate
# round 129 (docs/PARITY.md): LYNX's plate reverb plays a plate's role (was Reeverb 2); its state is encoded: a
# 2 s plate, 100 % wet, at the slot's dry / wet 25 % as before. Round 150: in the measured plate voicing (the
# trial's onset and decay per band, REA_rodada150a §7). Round 152: the editor opens in the one-knob Plate view
preset = Decay=0.3; Mix=1; Voicing=1; View=1
mix = 0.25

[waves-reverbs]
product = Waves reverb (RVerb / H-Reverb / TrueVerb)
vendor = Waves
lynx = Reeverb 2
waves = RVerb; H-Reverb; TrueVerb
# round 190js: their states are translated (decay, pre-delay, the wet level by the decay, each reverb's measured tone,
# H-Reverb's ducking, TrueVerb's direct path and levels; docs/PARITY.md "Round 190js"), the slot at 100 %. Without a
# readable state: Reeverb 2 wet at the slot's dry / wet 25 % as before
translate = waves-reverb
preset = Dry=0; Wet=0.8
mix = 0.25

# ---------------------------------------------------------------- delays -> Fruity Delay 3 (FL's "Default" preset)
[waves-h-delay]
product = H-Delay
vendor = Waves
lynx = Echoform
# [DATA] class ID (stereo component) and <PluginName> from real projects. Round 119: LYNX's echo (was Fruity Delay 3
# at FL's default); the RealWorld list is translated (sync host / BPM / ms, note, time, feedback, ping-pong or
# stereo, mix, output, filters), measured on the official trial (docs/analise/REA_rodada119.md §2). Without a
# readable state: a clean quarter-note ping-pong at 30 % feedback
vst3 = 48545356444268532d64656c61792073
waves = H-Delay
translate = waves-h-delay
preset = Mix=1; Style=0.9; Sync L=0.625; Note L=0.45; Sync R=0.625; Note R=0.45; Feedback=0.0465; Mode=0.5417; Age=0; Mod depth=0; Width=1; Pairs=1; Output=0.4611

[waves-supertap]
product = SuperTap
vendor = Waves
# round 190jh: LYNX's multi-mode echo (was Fruity Delay 3 at FL's default, which added echoes to a SuperTap whose taps are
# off): the state's taps, feedback, tap filter and direct level are translated onto Echoform's clean two-line echo,
# measured on refcap182's SuperTap programme captures (docs/PARITY.md "Round 190jh"). Without a readable state: the
# dry signal (SuperTap's default: the direct path on, every tap off)
lynx = Echoform
waves = SuperTap
translate = waves-supertap
preset = Mix=0

[soundtoys-echoboy]
product = EchoBoy
vendor = Soundtoys
# round 129 (docs/PARITY.md): LYNX's multi-mode echo plays EchoBoy's role. Round 136: her states play Echoform's Tape
# voicing (the loop structures measured on the official trial, REA_rodada136 §4), recognised by the hash of the encoded
# preset (not decoded here); any other state: round 129's tape echo, 1/4 note at the project tempo, 25 % feedback, 25 % mix
# (round 147: in the Studio reel voicing — EchoBoy's default style, measured black-box, REA_rodada147a)
lynx = Echoform
preset = Mode=0.0415; Sync L=0.6248; Note L=0.4498; Feedback=0.125; Mix=0.25; Echo voicing=0.064516
# [DATA] VST2 unique ID from real projects
vst2 = StEB
names = EchoBoy
translate = soundtoys-echoboy

[soundtoys-echoboy-jr]
product = EchoBoy Jr
vendor = Soundtoys
# round 190is: its own entry (it shared EchoBoy's): EchoBoy's fallback in the voicings' revision 0 (#108). Revision 1's echo
# lag is EchoBoy's (8 samples at 48 kHz), EchoBoy Jr's ~3, and on its steady tones the lag moved the echo's phase against
# the dry (refcap030: 5 mode / sr96 files a step lower). Its own measured default (Studio Tape, Wide, 100 ms, feedback
# 0.30, mix 40 %) was tried as the fallback and kept out: its blocks (mostly mix 25 … 30 %) scored lower
lynx = Echoform
preset = Mode=0.0415; Sync L=0.6248; Note L=0.4498; Feedback=0.125; Mix=0.25; Echo voicing=0.064516; #108=0
names = EchoBoyJr; EchoBoy Jr
translate = soundtoys-echoboy

[soundtoys-primaltap]
product = PrimalTap
vendor = Soundtoys
# round 129: Echoform (as EchoBoy), a darker lo-fi tape echo. Round 190is: Little PrimalTap has its own entry now; the state
# stays encoded (not decoded: a change of one control rewrites the payload from that point to its end). PrimalTap's own
# measured default (two taps at 0.5 beat, no feedback, mix 50 %, band-limited) was tried as the fallback and kept out:
# refcap030's mode / pairs blocks (95 / 120 ms, mix 30 %) scored lower with it (3.67 -> 3.04, 3.37 -> 3.00)
lynx = Echoform
preset = Mode=0.125; Sync L=0.6248; Note L=0.4498; Feedback=0.125; Mix=0.25
names = PrimalTap
# round 190kv: its factory presets (recognised by the payload's fingerprint) play their own settings on Echoform
translate = soundtoys-factory

[soundtoys-little-primaltap]
product = Little PrimalTap
vendor = Soundtoys
# round 190is: its own entry (it shared PrimalTap's). Any state plays Little PrimalTap's default as measured on refcap030:
# 113 ms (the echo 6 samples later), no feedback, mix 50 % on a linear law (dry and echo −6 dB: Echoform's equal-power Mix
# at 2/3 with the Output at −4.8 dB), a steep low-pass at 12.3 kHz (Age) and its droop (High cut), no LFO
lynx = Echoform
preset = Mix=0.666667; Output=0.367466; Time L=0.113125; Mode=0.541667; Mod depth=0; Age=0.221; High cut=0.45; Feedback=0
names = Little PrimalTap; LittlePrimalTap
# round 190kv: its factory presets (recognised by the payload's fingerprint) play their own settings
translate = soundtoys-factory

# Valhalla delay / modulation plugins -> LYNX's delay / modulation suite (round 101). Their saved state is a JUCE
# XML block holding every parameter as its normalised value [DATA, EXEC]; the LYNX effects use the same value laws,
# measured black-box on the official demos (docs/analise/REA_rodada101.md §2-§6), so most settings map one to one.
# [derived] VST3 class IDs: Steinberg's VST2-compatible UID ("VST" + the VST2 ID found in the state's FBCh header +
# the first 9 letters of the lower-case name), in FL's byte order; the same rule reproduces ValhallaVintageVerb's ID
# read from real projects.
[valhalla-delay]
product = ValhallaDelay
vendor = Valhalla DSP
lynx = Echoform
vst3 = 64545356614c7679616c68616c6c6164
names = ValhallaDelay
mix = 1
# round 190ly: the ValhallaDelay role's loop laws (Echoform's hidden valhallaLaw, #115 since the 0.1.30 rollup step 10): diffusion inside the echo time,
# the measured loop gain over Feedback. Only a translated ValhallaDelay runs them
replaced = #115=1
map = xml:Mix -> Mix
# styles: Single, Dual, Ratio, PingPong, Quad onto Echoform's order (Single, Ratio, Quad, Dual, Ping-pong)
map = xml:DelayStyle -> Style | steps 0:0.1 0.19:0.7 0.39:0.3 0.59:0.9 0.79:0.5
map = xml:DelayLSync -> Sync L
map = xml:DelayLNote -> Note L
map = xml:DelayL_Ms -> Time L
map = xml:DelayRSync -> Sync R
map = xml:DelayRNote -> Note R
map = xml:DelayR_Ms -> Time R
map = xml:DelaySpread -> Spread
map = xml:DelaySpacing -> Spacing
map = xml:DelayRatio -> Ratio
# round 137: the quad taps and Repeat / Swell (Echoform has them since round 135w; the same on / off values)
map = xml:TapA -> Tap A
map = xml:TapB -> Tap B
map = xml:TapC -> Tap C
map = xml:TapD -> Tap D
map = xml:RepeatSwell -> Swell
map = xml:Feedback -> Feedback
map = xml:Width -> Width
map = xml:DriveIn -> Drive
map = xml:Age -> Age
map = xml:Diffusion -> Diffusion
map = xml:DiffSize -> Diffusion size
map = xml:LowCut -> Low cut
map = xml:HighCut -> High cut
map = xml:ModRate -> Mod rate
map = xml:ModDepth -> Mod depth
map = xml:Wow -> Wow
map = xml:Flutter -> Flutter
map = xml:FreqShift -> Shift
map = xml:FreqDetune -> Shift detune
map = xml:PitchShift -> Pitch
map = xml:PitchDetune -> Pitch detune
map = xml:Ducking -> Ducking
# round 137: Era (Past below 2/3, Present, Future at the top; measured on the demo's own value texts)
map = xml:Era -> Era | steps 0:0 0.6662:0.5 0.9995:1
# the 17 modes (index floor (24 v)) onto Echoform's 12 characters: Tape, HiFi, BBD, Digital, Ghost (shifter), Pitch,
# RevPitch (reverse), BBDuck (bucket), Clarity (clean), DuckTape (tape), PitchDuck (pitch), LoFi, Quartz (clean),
# PhaserDDL (phaser), RichPitch (detune), ChromeTape (tape), Analog; the "duck" variants' built-in ducking is not kept
map = xml:Mode -> Mode | steps 0:0.041667 0.0828:0.208333 0.1245:0.291667 0.1662:0.458333 0.2078:0.708333 0.2495:0.791667 0.2912:0.875 0.3328:0.291667 0.3745:0.541667 0.4162:0.041667 0.4578:0.791667 0.4995:0.125 0.5412:0.541667 0.5828:0.625 0.6245:0.958333 0.6662:0.041667 0.7078:0.375 0.7495:0.041667
# round 190ca measured the Tape mode's echoes of drums, music and a vocal 2.1 / 1.2 / 1.8 dB louder than the reference's
# while a single impulse came out 0.9 dB quieter (the level laws differ with the signal) and set Output -1.67 dB. Round
# 190dl: Echoform's measured tape stage (Law = Measured, every new instance) has that level law itself (a record gain
# with memory, fitted on the same renders), so the translation leaves Output at 0 dB again

[valhalla-freqecho]
product = ValhallaFreqEcho
vendor = Valhalla DSP
lynx = Spiral Echo
vst3 = 4654535645717668616c68616c6c6166
names = ValhallaFreqEcho
mix = 1
map = xml:wetDryMix -> Mix
map = xml:shift -> Shift
map = xml:delay -> Delay
map = xml:sync -> Sync
map = xml:feedback -> Feedback
map = xml:lowCut -> Low cut
map = xml:highCut -> High cut
map = xml:stereo -> Stereo

[valhalla-spacemodulator]
product = ValhallaSpaceModulator
vendor = Valhalla DSP
lynx = Orbit
vst3 = 535453564d707664616c68616c6c6173
names = ValhallaSpaceModulator
mix = 1
# round 190mb: the reference's wet level per mode on Orbit's voices (Orbit's hidden smLaw, #7). Only a translated
# SpaceModulator runs it. (Its mode order, read back from the plug-in, is Orbit's: the Mode map stays direct)
# round 190mj: and the reference's own voices per mode (Orbit's hidden voiceLaw, #8), measured on click-train probes
replaced = #7=1; #8=1
map = xml:wetDry -> Mix
map = xml:rate -> Rate
map = xml:depth -> Depth
map = xml:feedback -> Feedback
map = xml:manual -> Manual
map = xml:Mode -> Mode

[valhalla-ubermod]
product = ValhallaUberMod
vendor = Valhalla DSP
lynx = Lattice Mod
vst3 = 7554535665627672616c68616c6c6175
names = ValhallaUberMod
mix = 1
map = xml:Mix -> Mix
map = xml:Depth -> Depth
map = xml:StereoWidth -> Width
map = xml:Delay -> Delay
map = xml:Feedback -> Feedback
map = xml:Spread -> Spread
map = xml:Slope -> Slope
map = xml:Skew -> Skew
map = xml:Random -> Random
map = xml:TapGain -> Tap gain
map = xml:Diffusion -> Diffusion
map = xml:DiffSize -> Diffusion size
map = xml:DiffModRate -> Diffusion rate
map = xml:DiffModDepth -> Diffusion depth
map = xml:LowCut -> Low cut
map = xml:HighCut -> High cut
map = xml:SpatialXover -> Crossover
map = xml:DetuneRate -> Detune rate
map = xml:DetuneDepth -> Detune depth
map = xml:VibratoRate -> Vibrato rate
map = xml:VibratoDepth -> Vibrato depth
map = xml:OverMod -> Over-mod
map = xml:DriveInGain -> Drive in
map = xml:DriveOutGain -> Drive out
map = xml:DriveNoiseGain -> Noise
map = xml:FeedbackRotate -> Rotate
map = xml:SmoothingTime -> Smoothing
map = xml:ColorMode -> Colour
map = xml:type -> Taps
map = xml:DiffEnable -> Diffusion on
map = xml:Drive -> Drive on
map = xml:DrivePrePost -> Drive post
map = xml:Speed -> Half speed
# round 137: InputPan (0-8, the same value law; its routes measured black-box, REA_rodada137 §4)
map = xml:InputPan -> Input pan
# round 144: DelaySync (floor (17 v): free, straight, dotted, triplet groups — measured, REA_rodada144a §1), the same value law
map = xml:DelaySync -> Delay sync

[fabfilter-timeless]
product = FabFilter Timeless
vendor = FabFilter
# round 190kw: LYNX's multi-mode echo (was Fruity Delay 3 at FL's default, 2.37 / 10 on refcap030's Timeless 3 bank): a
# Timeless 3 state (component state "FFBS", 1007 values) is translated onto Echoform's Clean mode with the line law
# measured on the reference (hidden #109: its dry / wet law, read filter and wide limit): delay time / sync / time pan,
# feedback (ping pong, pan, cross mix), mix, stereo width, wet level / pan, the filters in use, drive, diffuse, freeze
# (docs/PARITY.md "Round 190kw"). Without a readable state (Timeless 2): the reference's default, a single 350 ms echo
# without feedback at Mix 50 % (the dry at unity)
lynx = Echoform
names = FabFilter Timeless 2; FabFilter Timeless 3; Timeless 2; Timeless 3
translate = timeless3
preset = Mode=0.541667; Style=0.1; Sync L=0; Sync R=0; Time L=0.35; Time R=0.35; Feedback=0; Mix=0.5; Width=1; Low cut=0.002764; High cut=1; Age=0; Mod depth=0; Wow=0; Flutter=0; Drive=0; #109=1

# ---------------------------------------------------------------- limiters -> Limiter (ceiling -0.3 dB)
[waves-l-series]
product = Waves L1 / L2 / L3
vendor = Waves
lynx = Limiter
waves = L1; L2; L3; L3-16; L3-LL Multi; L4
# round 190jh: an L2 state's RealWorld list is translated (threshold = the input gain, out ceiling, release; ARC = the
# Limiter's shortest release), measured on refcap182's L2 programme captures (docs/PARITY.md "Round 190jh")
translate = waves-l2

[fabfilter-pro-l]
product = FabFilter Pro-L
vendor = FabFilter
lynx = Apex Limiter
names = FabFilter Pro-L; FabFilter Pro-L 2; Pro-L; Pro-L 2
# round tp172b: a Pro-L 2 state (component "FabF", 32 values, measured black-box) is translated: gain, style, look-ahead,
# attack, release, channel links, oversampling, true peak, dither, noise shaping, DC filter, side chain, output level
translate = pro-l2

# ---------------------------------------------------------------- compressors -> Compressor
[waves-rcomp]
product = Renaissance Compressor
vendor = Waves
lynx = Compressor
# [DATA] class ID (stereo component) and <PluginName> from real projects
vst3 = 5554535643527253636f6d7072657373
waves = RComp
# round 113: its RealWorld list is translated (threshold, ratio, attack, release, gain; measured on the official
# trial, docs/analise/REA_rodada113.md §3c). Without a readable state: a gentle default (2:1 above -15 dBFS, auto gain)
translate = waves-rcomp
preset = Threshold=0.75; Ratio=0.0526; Auto gain=1

[waves-cla-2a]
product = CLA-2A
vendor = Waves
lynx = Glow Leveler
# [DATA] class ID (stereo component) and <PluginName> from real projects
vst3 = 4c545356324163536c612d3261207374
waves = CLA-2A
# LYNX's optical leveler (round 96). Round 113: gain, peak reduction and comp / limit are translated from the
# RealWorld list (measured on the official trial, REA_rodada113 §3c); without a readable state light leveling
preset = Peak reduction=0.30; Gain=0.067
translate = waves-cla-2a

[waves-cla-76]
product = CLA-76
vendor = Waves
lynx = Crest Compressor
waves = CLA-76
# round 190ky: Crest's FET law (a feedback limiter, LYNX's own model of the structure REA showed in the reference's DSP,
# fitted on our demo-mode probes: docs/PARITY.md "Round 190ky", docs/analise/REA_rodada190ky.md); the RealWorld list is
# translated (Input / Output by their measured gains, attack, release, ratio, revision, comp off, mix, trim). Was the Bus
# style fitted around one factory state (rounds 127 / 148 / 190jh). Without a readable state: a light 4:1 at Waves' defaults
translate = waves-cla-76
preset = Style=0.9; Threshold=0.6; Ratio=0.546; Knee=0.13; Range=0.53; Auto gain=1

# round 190kt: the console bus compressor on Crest's Bus style with its console law (a feedback compressor, LYNX's own
# model of the structure REA showed in the reference's DSP, fitted on our demo-mode probes: docs/PARITY.md "Round 190kt",
# docs/analise/REA_rodada190kt.md). Was Compressor's classic voicing (round 190jh). Without a readable state: Crest's
# Bus style at its top threshold (the reference's default, +15, reduces nothing on programme)
[waves-sslcomp]
product = SSLComp
vendor = Waves
lynx = Crest Compressor
waves = SSLComp
translate = waves-sslcomp-bus
preset = Style=0.9; Threshold=1

[waves-compressors]
product = Waves compressor (CLA-3A / C1)
vendor = Waves
lynx = Compressor
waves = CLA-3A; C1 comp; C1comp
preset = Threshold=0.75; Ratio=0.0526; Auto gain=1
# round 190jh: an SSLComp state's RealWorld list is translated (threshold, ratio, attack, release, make-up, mix),
# measured on refcap182's SSLComp programme captures; the others keep the preset
translate = waves-sslcomp

[fabfilter-pro-c]
product = FabFilter Pro-C
vendor = FabFilter
lynx = Crest Compressor
names = FabFilter Pro-C 2; FabFilter Pro-C 3; Pro-C 2; Pro-C 3
# LYNX's style compressor (round 108), fitted to our black-box measurements (MEASURE_fabfilter §2); a Pro-C 3 state
# (component state "FFBS", 100 values) is translated: style, threshold, ratio, knee, range, attack, release, hold,
# look-ahead, character, side-chain EQ (3 bands), levels, mix, oversampling. Pro-C 2 states keep the defaults.
translate = pro-c3
# auto gain on by default (round 107): keeps the level when the state is not readable
preset = Auto gain=1

[fabfilter-pro-mb]
product = FabFilter Pro-MB
vendor = FabFilter
lynx = Prism Multiband
names = FabFilter Pro-MB; Pro-MB
# LYNX's 4-band dynamics (round 108, MEASURE_fabfilter §6 / §12.4): the enabled bands are laid out low → high on
# Prism's four bands (gaps become bypassed bands), with threshold, ratio, range, mode, knee, attack / release %,
# level, solo, look-ahead, mix and levels. Without a readable state the bands start at a gentle -12 dB range.
translate = pro-mb

[fabfilter-pro-g]
product = FabFilter Pro-G
vendor = FabFilter
lynx = Sentry Gate
names = FabFilter Pro-G; Pro-G
# LYNX's gate / expander (round 108, MEASURE_fabfilter §7): every setting of the state is translated (style,
# thresholds, ratios, range, attack, release, hold, knee, look-ahead, side-chain filters, wet / dry, output)
translate = pro-g

# ---------------------------------------------------------------- EQs and channel strips -> Parametric EQ (flat)
[waves-ssl-channel]
product = SSL E-Channel
vendor = Waves
lynx = Ridge Strip
# [DATA] class ID (stereo component) and <PluginName> from real projects. Round 123: Ridge Strip's "E" voicing is the
# console measured black-box on the official trial (every EQ band / filter from impulse responses, the feedback
# compressor from stairs, bursts and her takes), so the RealWorld list maps one to one (REA_rodada123.md)
vst3 = 5354535648437353736c6368616e6e65
waves = SSLChannel
translate = waves-ssl-channel

# round 190mm: the G-series channel (no entry before: such projects lost the plug-in). Its RealWorld list has the E
# channel's layout (55 = LMF ÷3, 56 = HMF ×3; LF / HF are shelves only), translated onto Ridge Strip's measured E
# console like the SSL E-Channel; scored on our demo-mode captures (docs/analise/REA_rodada190mm.md §4)
[waves-ssl-g-channel]
product = SSL G-Channel
vendor = Waves
lynx = Ridge Strip
waves = SSLGChannel
translate = waves-ssl-g-channel

[ssl-native-channel]
product = SSL Native Channel Strip / 4K E
vendor = Solid State Logic
lynx = Ridge Strip
names = SSL Native Channel Strip 2; SSL Native Channel Strip; SSL 4K E; SSL 4K B; SSL 4K G; Vocalstrip 2; SSL Native Vocalstrip 2
# round tp172c: a Channel Strip 2 state (the same XML form) to Ridge's Modern voicing, measured black-box on its trial
# (E / G EQ, filters, compressor, gate); 4K G (Black) and 4K B (Brown; its stepped compressor, slice 4) to the 4K voicing
# round 145: a Vocalstrip 2 state (XML, the original's own units) is translated to Ridge's Vocal strip voicing, measured
# black-box on the official trial (de-esser, de-ploser, EQ, compander, order; REA_rodada145 §2); round 148: a 4K E state
# (the same XML form) to Ridge's 4K voicing, measured on the 4K E trial (REA_rodada148 §2); the other strips' states are
# not read (Ridge at its defaults)
translate = ssl-vocalstrip-2

# round 145: the rest of Complete Access (Slate Digital, Solid State Logic, LiquidSonics, sonible, Harrison; official
# trials on this PC) by role only — their states are not read (not in her corpus, not measured): LYNX's effect for
# the same job at its defaults, so a project that used them still plays a sensible chain
[complete-access-strips]
product = channel strips (Slate Virtual Mix Rack)
vendor = Complete Access (role)
lynx = Ridge Strip
names = Virtual Mix Rack; VBC Rack

# round tp172c6: SSL Native Drumstrip -> Ridge's gate and trims; tp172c8: onto Shell Channel, LYNX's drum channel strip
# (gate, transient shaper, low / high enhancers, listen-mic compressor, process order, dry / wet)
[ssl-drumstrip]
product = Drumstrip (drum channel strip)
vendor = Solid State Logic (Complete Access)
lynx = Shell Channel
names = SSL Native Drumstrip v6; SSL Native Drumstrip; Drumstrip
translate = ssl-drumstrip

# round tp172c6: SSL Guitarstrip -> Ridge's own engine (EQ, filters, compressor) with the DRIVE section on the Guitar amp /
# Bass amp models, measured black-box on its trial; tp172c8: onto Fret Channel, LYNX's guitar / bass channel strip (phase
# correction, EQ, compressor, amp with its voicings, output mix)
[ssl-guitarstrip]
product = Guitarstrip (guitar / bass channel strip)
vendor = Solid State Logic (Complete Access)
lynx = Fret Channel
names = SSL Guitarstrip; Guitarstrip
translate = ssl-guitarstrip

# round tp172c6: Slate VCC Channel -> the console's measured model (the drive and the trims read from its state);
# tp172c7: onto Kiln Channel, LYNX's console channel (console, drive, trims, noise, group, group bypass, main gains)
[vcc-channel]
product = VCC Channel (console emulation)
vendor = Slate Digital (Complete Access)
lynx = Kiln Channel
names = VM VCC Channel; VCC Channel
translate = vcc-channel

# round tp172c5: Harrison 32Classic and MPC channel strips -> Ridge's own engine (brown curve), their states read and
# translated in the originals' units by laws fitted black-box on the official trials (EQ bands and filters, compressor
# with the 32C's auto make-up, gate / expander, trims); tp172c6: the 32C's saturator on Ridge's DRIVE section (Saturate);
# the MPC's de-esser / denoiser are not reproduced yet
[harrison-strips]
product = Harrison 32Classic / MPC channel strips
vendor = Harrison
lynx = Ridge Strip
names = Harrison 32C; Harrison_32ClassicChannelStrip; Harrison 32Classic; Harrison 32Classic Channel Strip; Harrison_MPCChannelStrip; Harrison MPC Channel Strip
translate = harrison-strip

[complete-access-bus-comps]
product = bus / smart compressors (SSL Bus Compressor 2, G3, VBC, sonible smart:comp, pure:comp)
vendor = Complete Access (role)
lynx = Crest Compressor
# round 190ar: the references with a fitted setting map load at the mapped settings (core/FittedMaps.inc)
translate = fitted-map
names = SSL Native Bus Compressor 2; SSL 360 Link Bus Compressor; SSL Native X-Comp v6; SSL Fusion HF Compressor; VBC FG-Grey; VBC FG-MU; VBC FG-Red; smartcomp3; smartcomp2; purecomp; learncomp; Harrison_LookaheadCompressor; Harrison_MPCCompressor; VM FG-Stress; VM FG-Bomber

[complete-access-vintage-comps]
product = vintage compressor models (Slate FG-116 / FG-76 / FG-401 / FG-A / FG-Dynamics, SSL X-ValveComp)
vendor = Complete Access (role)
lynx = Compressor
translate = fitted-map
names = VM FG-116; VM FG-116 Modern; VM FG-116 Vintage; VM FG-76; VM FG-401; VM FG-A; VM FG-Dynamics; VM The Monster; SSL Native X-ValveComp v6; MO-TT

[complete-access-optos]
product = optical levelers (Slate FG-2A, SD-3A, Custom Opto)
vendor = Complete Access (role)
lynx = Glow Leveler
names = VM FG-2A; VM SD-3A; VM Custom Opto

[complete-access-limiters]
product = limiters (Slate FG-X 2, SSL X-Limit, sonible smart:limit, pure:limit)
vendor = Complete Access (role)
lynx = Apex Limiter
translate = fitted-map
names = FG-X 2; FG-X; SSL X-Limit; smartlimit; purelimit; learnlimit; Bus Clipper

[eiosis-aireq]
product = AirEQ (Eiosis AirEQ, Slate VM AirEQ Air / Earth)
vendor = Eiosis (Slate Digital)
lynx = Facet EQ
# round 190ln: the state is translated in physical units (cuts, Earth / Air shelves, five bells / shelves, strength, mid /
# side placement, trims); laws and shapes measured on the references' renders, structure read statically
# (docs/analise/REA_rodada190ln.md §3). Without a readable state: Facet flat, as before
translate = eiosis-aireq
names = Eiosis AirEQ; Eiosis AirEQ 5.1; AirEQ; VM AirEQ Air; VM AirEQ Earth

[slate-infinity-eq]
product = Infinity EQ
vendor = Slate Digital
lynx = Facet EQ
# round 190ln: the state is translated in physical units (the first eight enabled bands of 24: cuts, shelves, bells by
# width, notch, tilt, dynamics, channel placement, output); laws and shapes measured on the reference's renders
# (docs/analise/REA_rodada190ln.md §4). Without a readable state: Facet flat, as before
translate = slate-infinity-eq
names = Inf EQ; Infinity EQ

[complete-access-eqs]
product = EQs (Slate FG-73 / FG-N / FG-S / SD-PE1 / CS-EQ, SSL X-EQ 2, sonible smart:EQ)
vendor = Complete Access (role)
lynx = Facet EQ
names = VM FG-73; VM FG-N; VM FG-S; VM SD-PE1; VM CS-EQ; VM CS-Lift; SSL Native X-EQ 2; SSL Fusion Violet EQ; smartEQ4; smartEQ3; pureeq; learneq; Harrison_LegacyEQ; Harrison_MasteringEQ

[complete-access-deessers]
product = de-essers (SSL DeEss, Slate FG-DS 902, Eiosis E2Deesser, sonible smart:deess)
vendor = Complete Access (role)
lynx = Hush De-esser
names = SSL DeEss; VM FG-DS 902; Eiosis E2Deesser; smartdeess; puredeess; Harrison_De-Esser

[complete-access-gates]
product = gates (SSL X-Gate, Slate Gate Classic / Drums, Harrison Spectral Gate)
vendor = Complete Access (role)
lynx = Sentry Gate
translate = fitted-map
names = SSL X-Gate; VM Gate Classic; Harrison_SpectralGate

# round 190dm: the three-band bus compressor stands in on Prism Multiband (three bands on its crossovers, set alike), at its
# setting map's default (fitted black-box on its probes, /mnt/e/lynx-work/dyn190dm/fit): every bank setting played
# through this preset scores 4.25 / 2.76 (tp172c / programme) against Crest's defaults 2.50 / 2.36. (MO-TT stays on the
# Compressor: on Prism its map wins at matched settings, but its default as a stand-in scores 2.92 / 2.48 against 3.08 / 2.56.)
[three-band-bus-compressor]
product = three-band bus compressor (SSL G3 MultiBusComp)
vendor = Complete Access (role)
lynx = Prism Multiband
names = SSL G3 MultiBusComp
preset = Bands=0.4; Band layout=0; Crossover 1=0.216712; Crossover 2=0.828259; Slope=0.5; Mix=1; Processing=1; B1 threshold=0.97449; B1 ratio=0.47828; B1 range=0.0; B1 knee=0.5; B1 attack=0.33953; B1 release=0.53006; B1 level=0.48994; B2 threshold=0.97449; B2 ratio=0.47828; B2 range=0.0; B2 knee=0.5; B2 attack=0.33953; B2 release=0.53006; B2 level=0.48994; B3 threshold=0.97449; B3 ratio=0.47828; B3 range=0.0; B3 knee=0.5; B3 attack=0.33953; B3 release=0.53006; B3 level=0.48994

# round 190dm: the drum gate stands in on Sentry Gate's transient trigger, at the reference's default settings (its laws
# measured on its probes: threshold -60 dB, range 100 dB, release 500 ms, hold 46.75 ms, Mid shape, sensitivity 5): every
# bank setting played through this preset scores 4.12 / 3.41 (tp172c / programme) against the level gate's 2.25 / 1.73
[drum-gate]
product = drum gate (Slate Gate Drums)
vendor = Complete Access (role)
lynx = Sentry Gate
names = VM Gate Drums
preset = Trigger=1; Threshold=0; Range=1; Attack=0.1316; Release=0.46416; Hold=0.43243; Knee=0; Release shape=0.5; Sensitivity=0.44444

[complete-access-reverbs]
product = reverbs (LiquidSonics Lustrous Plates / Seventh Heaven / Cinematic Rooms, Slate VerbSuite Classics, SSL FlexVerb / PlateVerb / SpringVerb, sonible smart:reverb)
vendor = Complete Access (role)
lynx = Foil Plate
names = Lustrous Plates; Seventh Heaven; Seventh Heaven Professional; Cinematic Rooms; Cinematic Rooms Professional; Illusion; VerbSuite Classics; SSL Native FlexVerb; SSL PlateVerb; SSL SpringVerb; SSL GateVerb; smartreverb2; smartreverb; pureverb; learnreverb; Vocal Reverb
preset = Decay=0.3; Mix=1; Law=0
mix = 0.25
# round 190le: a Lustrous Plates state (its plain ValueTree) gets round 190ca's fitted Foil setting (translateLustrousPlates);
# the other devices keep the role preset or their default line below
translate = lustrous-plates
# round 190ca: four of these devices at their own default setting, from Foil setting maps fitted on physical measurements of
# each device's probe renders (decay per band, onset, dry gain, wet energy and band balance, echo density, L/R correlation,
# band limit; the lab folder nebula190ca/maps, fitverb.py). FlexVerb and VerbSuite default to 100 % wet, the two SSL plates
# to an even mix. Both shelves sit at 700 Hz: equal gains there are a flat wet gain (Foil has no wet level control).
# round 190cm: FlexVerb and VerbSuite keep the role's 25 % wet (mix=0.25 = the slot's dry/wet, the effect itself all wet as
# in `preset`): no state of theirs is translated, so the mix a project used is unknown, and before round 190ca their
# stand-in opened at 25 % — an import must not suddenly get wetter. The fitted character (decay, size, EQ, type) stays.
default = SSL PlateVerb | Mix=0.2687; Predelay=0.4168; Decay=0.3028; Size=1.0000; Width=0.5000; Mod rate=0.0000; Mod depth=1.0000; Low EQ freq=0.7420; Low EQ gain=0.7917; High EQ freq=0.0606; High EQ gain=1.0000; Type=0.2900; Low cut=0.3268; Voicing=1.0000
default = SSL SpringVerb | Mix=0.1933; Predelay=0.0000; Decay=0.4877; Size=1.0000; Width=0.5000; Mod rate=0.0000; Mod depth=0.2309; Low EQ freq=0.7420; Low EQ gain=1.0000; High EQ freq=0.0606; High EQ gain=0.9915; Type=0.3750; Low cut=0.3764; Voicing=0.0000
default = SSL Native FlexVerb | Mix=1.0000; Predelay=0.0000; Decay=0.0541; Size=0.2500; Width=0.4694; Mod rate=0.0000; Mod depth=0.0768; Low EQ freq=0.7420; Low EQ gain=0.8524; High EQ freq=0.0606; High EQ gain=0.9149; Type=0.8750; Low cut=0.5996; Voicing=0.0000; mix=0.25
default = VerbSuite Classics | Mix=1.0000; Predelay=0.1351; Decay=0.2780; Size=1.0000; Width=0.5000; Mod rate=0.0000; Mod depth=0.0000; Low EQ freq=0.7420; Low EQ gain=0.2242; High EQ freq=0.0606; High EQ gain=0.5054; Type=0.2900; Low cut=0.0000; Voicing=1.0000; mix=0.25

[complete-access-delays]
product = delays (SSL X-Echo / X-Delay, Slate Stellar Echo SD-201, Harrison 3D Delay)
vendor = Complete Access (role)
lynx = Echoform
# round 190dl: these devices keep the Classic law (the measured tape stage is ValhallaDelay's; on the bank it lowered
# the multi-head tape row's tail score 2.72 -> 2.28)
preset = Mode=0.0415; Sync L=0.6248; Note L=0.4498; Feedback=0.125; Mix=0.25; Law=0
names = SSL Native X-Echo; SSL X-Delay; Stellar Echo SD-201; Harrison_3DDelay

# round 190lb: D16 Group Repeater (a tape / analog delay modeller, 0.1 .. 1000 ms per channel, feedback with its own
# loop filters; the public manual, BLACKBOX_rodada190lb §7). Its code is PACE-encrypted and its trial expired before
# any capture, and no saved state of it was available: spec-based, unscored. It plays the delay role's quarter-note
# preset (as the other tape-style delays above do); its own settings are not translated
[d16-repeater]
product = Repeater
vendor = D16 Group
lynx = Echoform
preset = Mode=0.0415; Sync L=0.6248; Note L=0.4498; Feedback=0.125; Mix=0.25; Law=0
names = Repeater

[complete-access-saturators]
product = saturators (Slate Heatwave / Revival, SSL Fusion Vintage Drive / X-Saturator, Harrison Tape Saturator)
vendor = Complete Access (role)
lynx = Ember Saturator
translate = fitted-map
names = Heatwave; VM Revival; SSL Fusion Vintage Drive; SSL Native X-Saturator v6; SSL Fusion Transformer; Harrison Tape Saturator

[complete-access-tuners]
product = pitch correction (Slate MetaTune)
vendor = Complete Access (role)
lynx = LYNX Tune
names = MetaTune

[waves-s1-imager]
product = S1 Imager
vendor = Waves
lynx = Stereo Shaper
# round 124: the width / rotation / gain matrix measured on the official trial is set exactly on the Stereo Shaper's
# matrix (docs/analise/REA_rodada124_extras.md §4); without a readable state the slot stays neutral
waves = S1 Imager; S1 Imager Stereo
translate = waves-s1-imager

[waves-c4]
product = C4 Multiband Parametric Processor
vendor = Waves
lynx = Prism Multiband
waves = C4
# round 190mg: the four-band dynamics role (no entry before: such projects lost the plug-in). Its RealWorld list is
# translated onto Prism's four bands, and Prism's hidden C4 law runs the C4's own structure (in-phase crossovers with its
# Q, the rational gain law with its knee, look-ahead and two-stage release), fitted on our Waves-demo probes
# (docs/analise/REA_rodada190mg.md §2)
translate = waves-c4

# round 190mm (scheps): the multi-module channel strip role (no entry before: such projects lost the plug-in). Its five
# modules (Pre, Gate, DS2, EQ, Comp), their on / off and the module order are read from the state and run by Ridge
# Strip's hidden omni law: LYNX's own model of each module, fitted on our Waves-demo probes (EQ shapes, filters, thump,
# saturation, the compressor types with their automatic make-up; docs/analise/REA_rodada190mm_scheps.md)
[waves-scheps-omni]
product = Scheps Omni Channel
vendor = Waves
lynx = Ridge Strip
waves = Scheps Omni Channel; Scheps Omni Channel 2
translate = waves-scheps-omni

# ---------------------------------------------------------------- round 190mm (sub-round dyn): Waves without a stand-in
# before (such projects lost the plug-in). Each law is LYNX's own model of the structure REA read in the reference's DSP,
# every constant fitted on our demo-mode probes (docs/analise/REA_rodada190mm_dyn.md)
[waves-rvox]
product = Renaissance Vox
vendor = Waves
lynx = Compressor
waves = Renaissance Vox; RVox
# the one-knob vocal compressor on Compressor's hidden Vox law: the threshold with its automatic make-up, the gate and
# the output gain. Without a readable state: Compressor at a 0 dBFS threshold (the reference's default compresses nothing)
translate = waves-rvox
preset = Threshold=1

[waves-rdeesser]
product = Renaissance DeEsser
vendor = Waves
lynx = Hush De-esser
waves = RDeEsser; Renaissance DeEsser
# the split / wide-band de-esser on Hush's hidden Renaissance de-esser law (its crossover, the threshold that rises with
# the level below the band, the Renaissance smoother): threshold, range, frequency, high-pass / band-pass, split /
# wide-band. Without a readable state: Hush's own engine at its defaults
translate = waves-rdeesser

[waves-c6]
product = C6 Multiband Compressor
vendor = Waves
lynx = Prism Multiband
waves = C6; C6-SideChain
# the C4's four crossover bands on Prism's C4 law (round 190mg) and the C6's two floating dynamic bands on the law's hidden
# floating bands (a band-pass of the crossovers' sum through the same gain law and smoother)
translate = waves-c6

[waves-api-2500]
product = API-2500
vendor = Waves
lynx = Compressor
waves = API-2500
# the VCA bus compressor on Compressor's hidden bus law: threshold, ratio, attack, release (incl. variable), knee,
# thrust, type (feed-forward / feedback), make-up (auto / manual), mix, trim. Was the generic Waves compressor entry
# (Compressor preset). Without a readable state: that preset
translate = waves-api-2500
preset = Threshold=0.75; Ratio=0.0526; Auto gain=1

[waves-rbass]
product = Renaissance Bass
vendor = Waves
lynx = Low Lifter
waves = Renaissance Bass; RBass
# the psycho-acoustic bass enhancer on Low Lifter's hidden bass law: its crossover at Frequency, the harmonics of the band
# below it at Intensity, the original bass in / out, the output gain. Without a readable state: Low Lifter's default
translate = waves-rbass

[waves-maxxbass]
product = MaxxBass
vendor = Waves
lynx = Low Lifter
waves = MaxxBass
# the same bass law in the MaxxBass role: input gain, frequency, original bass gain, harmonics gain
translate = waves-maxxbass

[waves-vitamin]
product = Vitamin
vendor = Waves
lynx = Aether
waves = Vitamin
# the five-band harmonic enhancer on Aether's hidden Vitamin law: the four crossovers, each band's gain, the direct gain
translate = waves-vitamin

[waves-ar-plates]
product = Abbey Road Reverb Plates
vendor = Waves
lynx = Foil Plate
waves = Abbey Road Reverb Plates; Abbey Road Plates
# round 190mg: the plate role (no entry before: such projects lost the plug-in); its decay (the plate and Damper as the
# reference combines them), pre-delay, treble and bass cut on Foil Plate's measured plate voicing, 100 % wet
translate = waves-ar-plates
preset = Mix=1; Voicing=1

[waves-atlas]
product = Atlas Reverb
vendor = Waves
lynx = Atrium
waves = Atlas Reverb
# round 190mg: the room / hall role (no entry before); its decay and pre-delay on Atrium, 100 % wet
translate = waves-atlas
preset = Mix=1

[waves-puigtec]
product = PuigTec EQP1A
vendor = Waves
lynx = Facet EQ
waves = PuigTec EQP1A; PuigTec EQP-1A; EQP1A
# round 190lf: the Pultec-style program EQ role (no entry before: such projects lost the plug-in). Its RealWorld list
# is translated onto eight Facet bands (low boost / atten shelves, the high boost bell, the high atten shelf and the
# reference's own flat response), every value fitted on our Waves-demo probes (docs/analise/REA_rodada190lf.md §3)
translate = waves-puigtec

[waves-eqs]
product = Waves EQ (SSL EQ)
vendor = Waves
lynx = Parametric EQ
# round 190mm: Renaissance EQ and Q10 have their own entries below
waves = SSLEQ

# round 190mm: Renaissance EQ (REQ 2 / 4 / 6) and Q10 on Facet EQ's Waves design law — every band shape identified on our
# demo-mode impulse responses (docs/analise/REA_rodada190mm.md §2: the bell's prescribed-Nyquist-gain design, the shelves'
# shared-s-term law, the resonant shelves, the 18 dB/oct cuts; Q10's bandwidth-prewarped bells, first-order shelves,
# Butterworth cuts), the RealWorld list translated band by band (no entry before: a flat Parametric EQ)
[waves-req]
product = Renaissance Equalizer (REQ)
vendor = Waves
lynx = Facet EQ
waves = REQ; REQ 2; REQ 4; REQ 6
translate = waves-req

# round 190mm: the API-550A / 550B / 560 EQs on Facet EQ (no entry before): every control position probed on our demo-mode
# impulse responses and fitted with one Facet band each (core/WavesApiEq.h; docs/analise/REA_rodada190mm.md §3), plus the
# 550s' own flat response; the RealWorld list is translated band by band
[waves-api-550]
product = API 550A / 550B
vendor = Waves
lynx = Facet EQ
waves = API-550; API-550A; API-550B
translate = waves-api-550

# round 190mm: Renaissance Channel (no entry before) — its compressor is the Renaissance Compressor's, so it plays on
# Compressor's Renaissance law (round 190mg) with the law's hidden channel EQ (the Renaissance EQ's bands, Facet's Waves
# design law) before / after it as routed; the gate is not carried (docs/analise/REA_rodada190mm.md §4)
[waves-rchannel]
product = Renaissance Channel (RChannel)
vendor = Waves
lynx = Compressor
waves = RChannel
translate = waves-rchannel

[waves-api-560]
product = API 560
vendor = Waves
lynx = Facet EQ
waves = API-560
translate = waves-api-560

[waves-q10]
product = Q10 Equalizer
vendor = Waves
lynx = Facet EQ
waves = Q10
translate = waves-q10

[fabfilter-pro-q]
product = FabFilter Pro-Q
vendor = FabFilter
lynx = Facet EQ
names = FabFilter Pro-Q 2; FabFilter Pro-Q 3; FabFilter Pro-Q 4; Pro-Q 2; Pro-Q 3; Pro-Q 4
# round 124: a Pro-Q 4 state (component state "FFBS", 600 values) is translated band by band (shape, frequency, gain,
# Q, slope; processing mode, gain scale, output) — Facet's shapes are fitted to the official trial, so her curves
# null against the reference (docs/analise/REA_rodada124_extras.md §2). Other versions keep a flat curve.
translate = pro-q4

# ---------------------------------------------------------------- vocal tools -> LYNX's mixing suite (round 96)
[waves-deesser]
product = DeEsser
vendor = Waves
lynx = Hush De-esser
waves = DeEsser; DeEsser Stereo
# round 113: frequency, threshold, split / wideband, side-chain filter and monitor are translated from the RealWorld
# list (measured on the official trial, REA_rodada113 §3c); without a readable state a gentle default (5.5 kHz,
# -20 dBFS, at most 4 dB)
translate = waves-deesser
# round 165x: 5.5 kHz on the extended law's 2 … 20 kHz scale (0.487 on the old one)
preset = Frequency=0.4393; Threshold=0.667; Range=0.167

[fabfilter-pro-ds]
product = FabFilter Pro-DS
vendor = FabFilter
lynx = Hush De-esser
names = FabFilter Pro-DS; Pro-DS
# round 148: her two states (Single Vocal, wide band) -> Hush's measured vocal voicing (REA_rodada148 §3)
translate = pro-ds

[harrison-deesser]
product = Harrison De-Esser
vendor = Harrison
lynx = Hush De-esser
names = Harrison De-Esser; Harrison DeEsser

# round trans189d (issue #94): the two-knob transient designer role -> Strike Shaper (level-independent attack / sustain,
# output, parallel mix, limiter, side-chain filter); its states are not read (no trial on this PC): Strike at its defaults
[spl-transient-designer]
product = Transient Designer
vendor = SPL
lynx = Strike Shaper
names = SPL Transient Designer; SPL Transient Designer Plus; Transient Designer Plus; SPL TD Plus; UAD SPL Transient Designer; SPL Transient Designer (UAD)

[slate-fresh-air]
product = Fresh Air
vendor = Slate Digital
lynx = Aether
names = Fresh Air; FreshAir
# round 113: its two air amounts are read from the state (freshAirAmounts); round 124: they drive Aether's dynamic
# air model, measured black-box on the official trial to match it sample for sample (REA_rodada124 §3)
translate = fresh-air

# ---------------------------------------------------------------- Kilohearts (round 190ln)
# The kHs snapins (kHs Essentials) and Faturator. Their state is a ZIP with state.json whose model holds every setting in
# the plug-in's own units (core/KiloheartsStates.inc); one translator ("kilohearts") sets the stand-in to those units.
# Round 190ln mapped them from the plug-ins' parameter texts and manuals (Slate's unlicensed builds rendered only their
# input); round 190nb measured the licensed kHs Essentials (Kilohearts' own build) and refitted the maps (REA_rodada190nb).
# Disperser plays on Facet EQ's hidden all-pass cascade (its structure read statically). Round 190nb: Ring Mod, Reverser,
# Resonator, Phase Distortion and Shaper play Kilohearts laws hidden in Effector (below).
[kilohearts-disperser]
product = Disperser
vendor = Kilohearts
lynx = Facet EQ
names = Disperser
translate = kilohearts

[khs-gain]
product = kHs Gain
vendor = Kilohearts
lynx = Utility
names = kHs Gain
translate = kilohearts

[khs-channel-mixer]
product = kHs Channel Mixer
vendor = Kilohearts
lynx = Utility
names = kHs Channel Mixer
translate = kilohearts

[khs-stereo]
product = kHs Stereo
vendor = Kilohearts
lynx = Utility
names = kHs Stereo
translate = kilohearts

[khs-haas]
product = kHs Haas
vendor = Kilohearts
lynx = Stereo Shaper
names = kHs Haas
translate = kilohearts

[khs-filter]
product = kHs Filter
vendor = Kilohearts
lynx = Facet EQ
names = kHs Filter
translate = kilohearts

[khs-3-band-eq]
product = kHs 3-Band EQ
vendor = Kilohearts
lynx = Facet EQ
names = kHs 3-Band EQ
translate = kilohearts

[khs-formant-filter]
product = kHs Formant Filter
vendor = Kilohearts
lynx = Facet EQ
names = kHs Formant Filter
translate = kilohearts

[khs-ladder-filter]
product = kHs Ladder Filter
vendor = Kilohearts
lynx = Filter
names = kHs Ladder Filter
translate = kilohearts

[khs-nonlinear-filter]
product = kHs Nonlinear Filter
vendor = Kilohearts
lynx = Filter
names = kHs Nonlinear Filter
translate = kilohearts

[khs-compressor]
product = kHs Compressor
vendor = Kilohearts
lynx = Compressor
names = kHs Compressor
translate = kilohearts

[khs-compactor]
product = kHs Compactor
vendor = Kilohearts
lynx = Compressor
names = kHs Compactor
translate = kilohearts

[khs-dynamics]
product = kHs Dynamics
vendor = Kilohearts
lynx = Compressor
names = kHs Dynamics
translate = kilohearts

[khs-limiter]
product = kHs Limiter
vendor = Kilohearts
lynx = Limiter
names = kHs Limiter
translate = kilohearts

[khs-clipper]
product = kHs Clipper
vendor = Kilohearts
lynx = Fruity Soft Clipper
names = kHs Clipper
translate = kilohearts

[khs-gate]
product = kHs Gate
vendor = Kilohearts
lynx = Sentry Gate
names = kHs Gate
translate = kilohearts

[khs-transient-shaper]
product = kHs Transient Shaper
vendor = Kilohearts
lynx = Strike Shaper
names = kHs Transient Shaper
translate = kilohearts

[khs-distortion]
product = kHs Distortion
vendor = Kilohearts
lynx = Distortion
names = kHs Distortion
translate = kilohearts

[kilohearts-faturator]
product = Faturator
vendor = Kilohearts
lynx = Distortion
names = Faturator
translate = kilohearts

[khs-bitcrush]
product = kHs Bitcrush
vendor = Kilohearts
lynx = Lo-fi
names = kHs Bitcrush
translate = kilohearts

[khs-frequency-shifter]
product = kHs Frequency Shifter
vendor = Kilohearts
lynx = Frequency shifter
names = kHs Frequency Shifter
translate = kilohearts

[khs-pitch-shifter]
product = kHs Pitch Shifter
vendor = Kilohearts
lynx = Pitch shift
names = kHs Pitch Shifter
translate = kilohearts

[khs-chorus]
product = kHs Chorus
vendor = Kilohearts
lynx = Chorus
names = kHs Chorus
translate = kilohearts

[khs-ensemble]
product = kHs Ensemble
vendor = Kilohearts
lynx = Chorus
names = kHs Ensemble
translate = kilohearts

[khs-phaser]
product = kHs Phaser
vendor = Kilohearts
lynx = Fruity Phaser
names = kHs Phaser
translate = kilohearts

[khs-flanger]
product = kHs Flanger
vendor = Kilohearts
lynx = Fruity Flanger
names = kHs Flanger
translate = kilohearts

[khs-reverb]
product = kHs Reverb
vendor = Kilohearts
lynx = Reeverb 2
names = kHs Reverb
translate = kilohearts

[khs-delay]
product = kHs Delay
vendor = Kilohearts
lynx = Echoform
names = kHs Delay
translate = kilohearts

[khs-dual-delay]
product = kHs Dual Delay
vendor = Kilohearts
lynx = Echoform
names = kHs Dual Delay
translate = kilohearts

[khs-tape-stop]
product = kHs Tape Stop
vendor = Kilohearts
lynx = Pocket Tape Stop
names = kHs Tape Stop
translate = kilohearts

[khs-comb-filter]
product = kHs Comb Filter
vendor = Kilohearts
lynx = Pocket Comb Filter
names = kHs Comb Filter
translate = kilohearts

[khs-trance-gate]
product = kHs Trance Gate
vendor = Kilohearts
lynx = Volume gate
names = kHs Trance Gate
translate = kilohearts

# round 190nb: the five snapins without a LYNX effect of their own play Kilohearts laws hidden in FL's Effector
# (core/KiloheartsDsp.h), read statically in HeartCore 2.4 and measured on the licensed kHs Essentials.
[khs-ring-mod]
product = kHs Ring Mod
vendor = Kilohearts
lynx = Effector
names = kHs Ring Mod
translate = kilohearts

[khs-reverser]
product = kHs Reverser
vendor = Kilohearts
lynx = Effector
names = kHs Reverser
translate = kilohearts

[khs-resonator]
product = kHs Resonator
vendor = Kilohearts
lynx = Effector
names = kHs Resonator
translate = kilohearts

[khs-phase-distortion]
product = kHs Phase Distortion
vendor = Kilohearts
lynx = Effector
names = kHs Phase Distortion
translate = kilohearts

[khs-shaper]
product = kHs Shaper
vendor = Kilohearts
lynx = Effector
names = kHs Shaper
translate = kilohearts

[soundtoys-decapitator]
product = Decapitator
vendor = Soundtoys
lynx = Ember Saturator
# [DATA] VST2 unique ID from real projects
vst2 = StDc
names = Decapitator
# round 136: Ember's Classic voicing, measured black-box on the official trial (REA_rodada136 §1). Its state is
# encoded (not decoded here); her 63 saved states are known by the hash of the encoded preset and translated to the
# settings the trial reports for them; any other state: style A, drive 2, output -3.2 dB (her usual light drive)
preset = Voicing=1; Classic style=0; Classic drive=0.2; Classic tone=0.5; Classic high cut=1; Classic mix=1; Classic output=0.866667
translate = soundtoys-decapitator

[fabfilter-saturn]
product = FabFilter Saturn
vendor = FabFilter
lynx = Ember Saturator
names = FabFilter Saturn; FabFilter Saturn 2; Saturn 2
preset = Drive=0.15
# a Saturn 2 state ("FFBS", 951 values) sets Ember's multiband mode (round 108): up to four bands with their
# crossovers, style family, drive, mix and level, the global mix and output
translate = saturn2

# ---------------------------------------------------------------- modulation
[soundtoys-panman]
product = PanMan
vendor = Soundtoys
lynx = Auto-pan
# [DATA] VST2 unique ID from real projects
vst2 = StPn
names = PanMan
# round 136: Auto-pan's Rhythm voicing (core/RhythmPan.h), measured black-box on the official trial (REA_rodada136
# §2). Its state is encoded (not decoded here); her 50 saved states are known by the hash of the encoded preset and
# translated to the pattern, rate / beats, width, smoothing, start phase and analog stage measured for them; any other
# state: her most used one (a 3.56 Hz left / right triangle over 151 degrees, analog). (Round 135's rhythmic-panner
# sources — free LFO, rhythm steps — stay available on the same effect; vox138 kept round 136's measured voicing for
# the substitute.)
preset = Depth=0.918; Source=0.5; Rate=0.8505; Smoothing=1; Analog=1; Voicing=1; Rhythm rate=0.8505; Sync=0; Width=0.7171; Offset=0.5; Rhythm smoothing=0.9925; Pattern=0; Amount=0.9981; Start phase=0.9917; Rhythm analog=0.5; Rhythm output=0.5
translate = soundtoys-panman

[soundtoys-littlealterboy]
product = Little AlterBoy
vendor = Soundtoys
lynx = LYNX Tune
# [DATA] VST2 unique ID from real projects
vst2 = StLa
names = Little AlterBoy; LittleAlterBoy; AlterBoy
# round 136 (REA_rodada136 §3): LYNX Tune as the voice shifter — Quantize = chromatic snapping at the fastest retune,
# Transpose = every note bypassed (pitch shift only), the formant shift, the measured Drive stage and the mix. Its
# state is encoded (not decoded here); her 52 saved states are known by the hash of the encoded preset; any other
# state: her most used one (Quantize, formant +1.2 st, drive 4.6). (Round 135's Voice Shift engine — the same role as
# its own effect — stays in LYNX's effect list; the substitute keeps round 136's measured model.)
preset = Scale=0; Retune=0; Humanize=0; Formant preserve=1; Formant shift=0.585714; Transpose=0.5; Mix=1; Drive stage=1; Drive=0.46; Output=0.5
translate = soundtoys-littlealterboy

# ---------------------------------------------------------------- vocal pitch correction -> LYNX Tune (round 95)
[antares-auto-tune]
product = Auto-Tune Pro
vendor = Antares
lynx = LYNX Tune
# [DATA] VST3 class ID from FL wrapper records of real projects (round 98). Round 113: the state's key and scale are
# read (autoTuneProKey); round 120: its retune speed, humanize and flex-tune too (measured on the official trial);
# without a key the project's key (another readable pitch corrector) or, failing that, a gentle chromatic correction
# (50 ms, humanize 50 %) instead of forcing C major
vst3 = 415453563854413175746f2d54756e65
names = Auto-Tune Pro; Auto-Tune Pro X; Auto-Tune Artist; Auto-Tune Access; Auto-Tune EFX+; Auto-Tune Hybrid; Auto-Tune
translate = auto-tune-pro
preset = Scale=0; Retune=0.3536; Humanize=0.5
mix = 1

# ---------------------------------------------------------------- noise suppressor / doubler (round 119)
[waves-ns1]
product = NS1
vendor = Waves
lynx = Quell Denoiser
# LYNX's one-knob noise suppressor, fitted to the official trial (docs/analise/REA_rodada119.md §3); the amount is
# read from the RealWorld list (value 229, %)
waves = NS1
translate = waves-ns1
preset = Amount=0.25

[waves-doubler]
product = Doubler
vendor = Waves
lynx = Gemini Doubler
# LYNX's two-voice doubler, laws measured on the official trial (REA_rodada119 §4); every control of the two-voice
# Doubler is translated from the RealWorld list (voices 3 and 4 of the four-voice one are left out)
waves = Doubler; Doubler2; Doubler4
translate = waves-doubler

[waves-tune]
product = Waves Tune Real-Time
vendor = Waves
lynx = LYNX Tune
waves = WavesTune Real-Time; Waves Tune Real-Time; WavesTune
names = WavesTune Real-Time; Waves Tune Real-Time
# round 120: its saved scale (root + note statuses) and its Note Transition time (= LYNX Tune's retune, measured)
translate = waves-tune
preset = Scale=0; Retune=0.3536; Humanize=0.5
mix = 1
)TABLE";

std::string lowerAscii (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (std::string_view s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1]))
        --b;
    return std::string (s.substr (a, b - a));
}

std::vector<std::string> splitList (std::string_view s, char separator)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == separator)
        {
            if (auto item = trim (s.substr (start, i - start)); ! item.empty())
                out.push_back (std::move (item));
            start = i + 1;
        }
    return out;
}

// a name key without the channel-layout words Waves and others append ("RComp Stereo", "L2 Mono")
std::string matchKey (const std::string& name)
{
    auto key = pluginNameKey (name);
    for (bool again = true; again;)
    {
        again = false;
        for (const std::string suffix : { "stereo", "mono" })
            if (key.size() >= suffix.size() + 2 && key.compare (key.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                key.resize (key.size() - suffix.size());
                again = true;
            }
    }
    return key;
}

std::optional<EffectKind> effectKindNamed (const std::string& name)
{
    const auto wanted = lowerAscii (trim (name));
    for (int k = 0; k <= (int) lastEffectKind; ++k) // the last EffectKind: extend when one is appended
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin || kind == EffectKind::parallel)
            continue;
        if (lowerAscii (effectInfo (kind).name) == wanted)
            return kind;
    }
    return std::nullopt;
}

int paramNamed (EffectKind kind, const std::string& name)
{
    const auto wanted = lowerAscii (trim (name));
    const auto& params = effectInfo (kind).params;
    for (std::size_t i = 0; i < params.size(); ++i)
        if (lowerAscii (params[i].name) == wanted)
            return (int) i;
    return -1;
}

bool parseFloat (const std::string& s, float& out)
{
    try
    {
        std::size_t used = 0;
        out = std::stof (s, &used);
        return used == s.size() && std::isfinite (out);
    }
    catch (...)
    {
        return false;
    }
}

bool parseHex16 (const std::string& s, std::array<std::uint8_t, 16>& out)
{
    std::string hex;
    for (auto c : s)
        if (std::isxdigit ((unsigned char) c))
            hex += c;
        else if (c != '-' && c != ' ')
            return false;
    if (hex.size() != 32)
        return false;
    for (std::size_t i = 0; i < 16; ++i)
        out[i] = (std::uint8_t) std::stoul (hex.substr (i * 2, 2), nullptr, 16);
    return true;
}

// "StEB" (four characters, as VST2 IDs are usually written) or a number ("0x53744542", "1400128834")
bool parseVst2Id (const std::string& s, std::uint32_t& out)
{
    if (s.size() == 4 && ! (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
    {
        out = ((std::uint32_t) (unsigned char) s[0] << 24) | ((std::uint32_t) (unsigned char) s[1] << 16)
              | ((std::uint32_t) (unsigned char) s[2] << 8) | (std::uint32_t) (unsigned char) s[3];
        return true;
    }
    try
    {
        std::size_t used = 0;
        const auto v = std::stoull (s, &used, 0);
        if (used != s.size() || v > 0xFFFFFFFFull)
            return false;
        out = (std::uint32_t) v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// "<source> -> <target> [| inLo inHi outLo outHi]"; source = vst2:<n> | xml:<attribute> | waves:<n>; target =
// "mix" or a parameter name of the LYNX effect
bool parseMap (const std::string& text, EffectKind kind, SubstituteParamMap& out)
{
    const auto arrow = text.find ("->");
    if (arrow == std::string::npos)
        return false;
    const auto source = trim (std::string_view (text).substr (0, arrow));
    auto rest = std::string (text.substr (arrow + 2));
    std::string range;
    if (const auto bar = rest.find ('|'); bar != std::string::npos)
    {
        range = rest.substr (bar + 1);
        rest = rest.substr (0, bar);
    }
    const auto target = trim (rest);

    const auto colon = source.find (':');
    if (colon == std::string::npos)
        return false;
    const auto type = lowerAscii (source.substr (0, colon));
    const auto arg = trim (std::string_view (source).substr (colon + 1));
    if (arg.empty())
        return false;
    if (type == "xml")
    {
        out.source = SubstituteParamMap::Source::xml;
        out.attribute = arg;
    }
    else if (type == "vst2" || type == "waves")
    {
        out.source = type == "vst2" ? SubstituteParamMap::Source::vst2 : SubstituteParamMap::Source::waves;
        float index = 0;
        if (! parseFloat (arg, index) || index < 0 || index != std::floor (index))
            return false;
        out.index = (int) index;
    }
    else
        return false;

    if (target == "mix") // lower case: the slot's dry/wet ("Mix" is an effect parameter, e.g. of LYNX's reverbs)
        out.toSlotMix = true;
    else if ((out.target = paramNamed (kind, target)) < 0)
        return false;

    if (const auto words = splitList (range, ' '); ! words.empty() && (lowerAscii (words[0]) == "curve" || lowerAscii (words[0]) == "steps"))
    {
        out.shape = lowerAscii (words[0]) == "curve" ? SubstituteParamMap::Shape::curve : SubstituteParamMap::Shape::steps;
        for (std::size_t i = 1; i < words.size(); ++i)
        {
            const auto colon = words[i].find (':');
            float x = 0, y = 0;
            if (colon == std::string::npos || ! parseFloat (words[i].substr (0, colon), x) || ! parseFloat (words[i].substr (colon + 1), y))
                return false;
            if (! out.points.empty() && x <= out.points.back().first)
                return false; // x must ascend
            out.points.push_back ({ x, y });
        }
        return out.points.size() >= (out.shape == SubstituteParamMap::Shape::curve ? 2u : 1u);
    }
    if (! range.empty())
    {
        std::vector<float> v;
        for (const auto& token : splitList (range, ' '))
        {
            float f = 0;
            if (! parseFloat (token, f))
                return false;
            v.push_back (f);
        }
        if (v.size() != 4 || v[0] == v[1])
            return false;
        out.inLo = v[0];
        out.inHi = v[1];
        out.outLo = v[2];
        out.outHi = v[3];
    }
    return true;
}

std::vector<SubstituteEntry>& registry()
{
    static std::vector<SubstituteEntry> entries = parseSubstituteTable (builtInTable);
    return entries;
}

std::size_t findBytes (const std::vector<std::uint8_t>& data, std::string_view what, std::size_t from = 0)
{
    if (what.empty() || data.size() < what.size())
        return std::string::npos;
    for (std::size_t i = from; i + what.size() <= data.size(); ++i)
        if (std::memcmp (data.data() + i, what.data(), what.size()) == 0)
            return i;
    return std::string::npos;
}

std::string xmlUnescape (const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '&')
        {
            out += s[i];
            continue;
        }
        const auto end = s.find (';', i);
        if (end == std::string::npos)
        {
            out += s[i];
            continue;
        }
        const auto entity = s.substr (i + 1, end - i - 1);
        if (entity == "amp") out += '&';
        else if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else
        {
            out += s.substr (i, end - i + 1);
        }
        i = end;
    }
    return out;
}

// the text of the active setup's <Parameters> block of a Waves preset chunk
std::string wavesParametersText (const std::vector<std::uint8_t>& state)
{
    const auto start = findBytes (state, "<PresetChunkXMLTree");
    if (start == std::string::npos)
        return {};
    const std::string xml (state.begin() + (std::ptrdiff_t) start, state.end());
    std::string setup = "SETUP_A";
    if (const auto a = xml.find ("<ActiveSetup>"); a != std::string::npos)
        if (const auto b = xml.find ("</ActiveSetup>", a); b != std::string::npos)
            setup = trim (std::string_view (xml).substr (a + 13, b - a - 13));
    const auto data = xml.find ("<PresetData Setup=\"" + setup + "\"");
    if (data == std::string::npos)
        return {};
    const auto params = xml.find ("<Parameters", data);
    if (params == std::string::npos)
        return {};
    const auto open = xml.find ('>', params);
    const auto close = xml.find ("</Parameters>", params);
    if (open == std::string::npos || close == std::string::npos || close < open)
        return {};
    return xml.substr (open + 1, close - open - 1);
}

float mapValue (const SubstituteParamMap& m, double value)
{
    if (m.shape == SubstituteParamMap::Shape::steps)
    {
        auto y = m.points.front().second;
        for (const auto& [px, py] : m.points)
            if (value >= (double) px - 1e-6)
                y = py;
        return std::clamp (y, 0.0f, 1.0f);
    }
    if (m.shape == SubstituteParamMap::Shape::curve)
    {
        const auto& p = m.points;
        if (value <= (double) p.front().first)
            return std::clamp (p.front().second, 0.0f, 1.0f);
        for (std::size_t i = 1; i < p.size(); ++i)
            if (value <= (double) p[i].first)
            {
                const auto t = (value - (double) p[i - 1].first) / ((double) p[i].first - (double) p[i - 1].first);
                return std::clamp ((float) ((double) p[i - 1].second + t * ((double) p[i].second - (double) p[i - 1].second)), 0.0f, 1.0f);
            }
        return std::clamp (p.back().second, 0.0f, 1.0f);
    }
    auto u = (value - (double) m.inLo) / ((double) m.inHi - (double) m.inLo);
    u = std::clamp (u, 0.0, 1.0);
    return (float) ((double) m.outLo + u * ((double) m.outHi - (double) m.outLo));
}

// --- binary writer / reader for foreignToText ----------------------------------------------------------------------
void putU32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (v >> (8 * i)));
}

void putBytes (std::vector<std::uint8_t>& out, const std::uint8_t* p, std::size_t n)
{
    putU32 (out, (std::uint32_t) n);
    out.insert (out.end(), p, p + n);
}

void putString (std::vector<std::uint8_t>& out, const std::string& s)
{
    putBytes (out, (const std::uint8_t*) s.data(), s.size());
}

struct BinReader
{
    const std::vector<std::uint8_t>& d;
    std::size_t pos = 0;
    bool ok = true;

    std::uint32_t u32()
    {
        if (pos + 4 > d.size())
        {
            ok = false;
            return 0;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= (std::uint32_t) d[pos + (std::size_t) i] << (8 * i);
        pos += 4;
        return v;
    }

    std::vector<std::uint8_t> bytes()
    {
        const auto n = u32();
        if (! ok || n > d.size() - pos)
        {
            ok = false;
            return {};
        }
        std::vector<std::uint8_t> v (d.begin() + (std::ptrdiff_t) pos, d.begin() + (std::ptrdiff_t) (pos + n));
        pos += n;
        return v;
    }

    std::string str()
    {
        const auto b = bytes();
        return std::string (b.begin(), b.end());
    }
};

constexpr std::uint32_t foreignTextMagic = 0x31464C4Cu; // "LLF1"

// --- translators of states whose layout is readable without a vendor library (round 113) ------------------------
using RawTranslator = int (*) (const std::vector<std::uint8_t>& state, EffectSlot& slot);

// Auto-Tune Pro: key and scale (autoTuneProKey) and the Auto-mode controls of its ParamsData (autoTuneProParams;
// [EXEC] REA round 120 §5: the X<n> entries are the plugin's parameter n in real units — 4 Retune Speed (ms),
// 61 Humanize, 90 Flex-Tune (0…100)). Measured black-box on the official trial: a held note is pulled to its target
// along a first-order curve whose time constant is 1.36 × the retune speed − 3 ms (LYNX Tune's retune is that time
// constant); Humanize and Flex-Tune map 1:1 onto LYNX Tune's (fitted to the same measurements, LynxTune.cpp).
// Round 127 (docs/specs/auto-tune-pro.md §3, §7.5): the remaining cheap controls, units from the trial's own
// parameter texts — X3 Detune (cents, A4 = 440·2^(c/1200); LYNX Tune's reference reaches ±39 c), X74 Transpose
// (semitones), X92 Wet-Dry Mix (%), X10 Input Type (Soprano, Alto-Tenor, Low Male, Instrument, Bass Instrument →
// LYNX Tune's ranges, the two instrument types on Instrument), X70 Formant Correction off → formants move with the
// pitch, X71 Throat Length (%: LYNX Tune's Throat length, measured in round 165hb),
// X11 Classic mode ("Auto-Tune 5": no formant correction, no Flex-Tune — the manual) and X9 Tracking (50 = LYNX
// Tune's default strictness; the rest of the law [model]). All 143 states of her main projects use Detune 0,
// Transpose 0, Mix 100 %, Alto-Tenor, Throat 100 %, Tracking 50 (round 120 §5), so these only matter elsewhere.
// Without a readable key the slot keeps the entry's gentle preset.
int translateAutoTuneEfx (const std::vector<std::uint8_t>& state, EffectSlot& slot);
std::string attributeIn (const std::string& text, std::size_t from, const std::string& name);

int translateAutoTunePro (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    if (slot.kind != EffectKind::lynxTune)
        return 0;
    if (const auto efx = translateAutoTuneEfx (state, slot); efx > 0)
        return efx;
    const auto key = autoTuneProKey (state);
    if (! key)
        return 0;
    slot.params[lynxtune::key] = (float) key->first / 11.0f;
    if (key->second >= vocal::scaleCount) // whole tone / diminished (round 138: modern scale lists): "More scales"
    {
        slot.params[lynxtune::scale] = 0.0f;
        slot.params[lynxtune::moreScales] = key->second == (int) vocal::Scale::wholeTone ? 0.5f : 1.0f;
    }
    else
        slot.params[lynxtune::scale] = (float) key->second / (float) (vocal::scaleCount - 1);
    const auto x = autoTuneProParams (state);
    auto has = [&x] (int n) {
        const auto it = x.find (n);
        return it != x.end() && std::isfinite (it->second);
    };
    auto at = [&x, &has] (int n, double fallback) { return has (n) ? x.at (n) : fallback; };
    int n = 2;
    auto set = [&slot, &n] (int param, double v) {
        slot.params[(std::size_t) param] = (float) std::clamp (v, 0.0, 1.0);
        ++n;
    };
    const auto retune = std::clamp (at (4, 20.0), 0.0, 400.0);
    set (lynxtune::retune, std::sqrt (std::clamp (1.36 * retune - 3.0, 0.0, 400.0) / 400.0));
    set (lynxtune::humanize, at (61, 0.0) / 100.0);
    const auto classic = at (11, 0.0) >= 0.5;
    set (lynxtune::flexTune, classic ? 0.0 : at (90, 0.0) / 100.0);
    if (has (3))
        set (lynxtune::reference, (440.0 * std::exp2 (std::clamp (at (3, 0.0), -100.0, 100.0) / 1200.0) - 430.0) / 20.0);
    if (has (74) && ! classic)
        set (lynxtune::transpose, (std::clamp (std::round (at (74, 0.0)), -12.0, 12.0) + 12.0) / 24.0);
    if (has (92))
        set (lynxtune::mix, at (92, 100.0) / 100.0);
    if (has (10))
    {
        static constexpr int rangeOf[5] { 0, 1, 2, 3, 3 };
        set (lynxtune::range, rangeOf[std::clamp ((int) std::lround (at (10, 1.0)), 0, 4)] / (double) (lynxtune::rangeCount - 1));
    }
    if (has (70) || classic)
        set (lynxtune::formantPreserve, ! classic && at (70, 1.0) >= 0.5 ? 1.0 : 0.0);
    // round 166c: X71 Throat Length → LYNX Tune's own Throat length (the law measured in REA_rodada165hb §2 lives in
    // the engine; round 138's half-size law on Formant shift, fitted on a synthetic vowel, is superseded)
    if (has (71) && ! classic)
        set (lynxtune::throatLength, lynxtune::throatValue (at (71, 100.0)));
    if (has (9))
        set (lynxtune::tracking, std::clamp (at (9, 50.0), 1.0, 100.0) * 0.4 / 50.0);
    // round 128: natural vibrato (X62, dB), ignore vibrato (X44), Create Vibrato (X5 shape, X7 rate, X6 cents, X46 / X50
    // onset delay / rate, ms) and the active scale's per-note Bypass / Remove edits (autoTuneProNoteEdits)
    if (has (62)) set (lynxtune::naturalVibrato, at (62, 0.0) / 24.0 + 0.5);
    if (has (44)) set (lynxtune::ignoreVibrato, at (44, 0.0) >= 0.5 ? 1.0 : 0.0);
    if (has (5)) set (lynxtune::vibratoShape, std::clamp (std::round (at (5, 0.0)), 0.0, 3.0) / 3.0);
    if (has (7)) set (lynxtune::vibratoRate, (at (7, 5.5) - 0.1) / 9.9);
    if (has (6)) set (lynxtune::vibratoDepth, at (6, 18.0) / 100.0);
    if (has (46)) set (lynxtune::vibratoDelay, at (46, 500.0) / 1500.0);
    if (has (50)) set (lynxtune::vibratoOnset, at (50, 500.0) / 1500.0);
    // round 166c: the created vibrato's Amplitude (X47, 0 … 30), Formant (X48, 0 … 100) and Variation (X49, 0 … 100),
    // and Transpose In Scale (X179 on / off) with Scale Transpose (X181, −7 … +7 steps)
    if (has (47)) set (lynxtune::vibratoAmplitude, at (47, 10.0) / 30.0);
    if (has (48)) set (lynxtune::vibratoFormant, at (48, 70.0) / 100.0);
    if (has (49)) set (lynxtune::vibratoVariation, at (49, 20.0) / 100.0);
    if (has (179) && ! classic) set (lynxtune::transposeInScale, at (179, 0.0) >= 0.5 ? 1.0 : 0.0);
    if (has (181)) set (lynxtune::scaleTranspose, lynxtune::scaleTransposeValue ((int) std::lround (std::clamp (at (181, 0.0), -7.0, 7.0))));
    if (const auto edits = autoTuneProNoteEdits (state, key->first, key->second))
        for (int k = 0; k < 12; ++k)
        {
            set (lynxtune::noteC + k, ((edits->second >> k) & 1u) != 0 ? 0.0 : 1.0);
            set (lynxtune::bypassC + k, ((edits->first >> k) & 1u) != 0 ? 1.0 : 0.0);
        }
    return n;
}

// Auto-Tune EFX+ (round 127; [DATA] her 6 states, 17 slots): an "<AUTOTUNEEFXPLUS>" element of <PARAM id="p…"
// value="…"/> in real units. pKey 0…11, pScale (0 Major, 1 Minor, 2 Chromatic — the same list as Auto-Tune Pro: every
// pScale 2 state has all twelve notes on, every pScale 1 state on key 11 holds B minor's notes), pNoteC … pNoteB = the
// notes that are targets (absolute pitch classes; one state adds F to B minor), pRetuneSpeed (ms, read with Auto-Tune
// Pro's law [model: not measured on EFX+]), pHumanize, pPitchShift (semitones), pFormant, pThroatLength (%),
// pInputType, pDryWetMix (%). Its multi-effect chain (Duet, Tube, Mutate, Vocode, filters) and Auto-Motion patterns
// have no LYNX Tune equivalent and are left out.
int translateAutoTuneEfx (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto root = findBytes (state, "<AUTOTUNEEFXPLUS");
    if (root == std::string::npos || slot.kind != EffectKind::lynxTune)
        return 0;
    const std::string text (state.begin() + (std::ptrdiff_t) root, state.end());
    std::map<std::string, double> v;
    for (auto p = text.find ("<PARAM "); p != std::string::npos; p = text.find ("<PARAM ", p + 7))
    {
        const auto id = attributeIn (text, p, "id");
        float value = 0;
        if (! id.empty() && parseFloat (attributeIn (text, p, "value"), value) && std::isfinite (value))
            v[id] = value;
    }
    auto at = [&v] (const char* id, double fallback) {
        const auto it = v.find (id);
        return it != v.end() ? it->second : fallback;
    };
    if (v.find ("pKey") == v.end() || v.find ("pScale") == v.end())
        return 0;
    const int key = std::clamp ((int) std::lround (at ("pKey", 0.0)), 0, 11);
    static constexpr const char* notes[12] { "pNoteC", "pNoteC#", "pNoteD", "pNoteD#", "pNoteE", "pNoteF",
                                             "pNoteF#", "pNoteG", "pNoteG#", "pNoteA", "pNoteA#", "pNoteB" };
    std::uint16_t mask = 0;
    for (int k = 0; k < 12; ++k)
        if (at (notes[k], 1.0) >= 0.5)
            mask = (std::uint16_t) (mask | (1u << k));
    const int theirs = (int) std::lround (at ("pScale", 2.0));
    auto scale = theirs == 0 ? vocal::Scale::major : (theirs == 1 ? vocal::Scale::minor : vocal::Scale::chromatic);
    if (mask == 0)
        mask = 0xfff;
    if (vocal::scaleMask (scale, key) != mask)
        scale = vocal::Scale::custom; // the note switches carry the exact targets
    auto& p = slot.params;
    p[lynxtune::key] = (float) key / 11.0f;
    p[lynxtune::scale] = (float) (int) scale / (float) (vocal::scaleCount - 1);
    for (int k = 0; k < 12; ++k)
        p[(std::size_t) (lynxtune::noteC + k)] = ((mask >> k) & 1u) != 0 ? 1.0f : 0.0f;
    const auto retune = std::clamp (at ("pRetuneSpeed", 20.0), 0.0, 400.0);
    p[lynxtune::retune] = (float) std::sqrt (std::clamp (1.36 * retune - 3.0, 0.0, 400.0) / 400.0);
    p[lynxtune::humanize] = (float) std::clamp (at ("pHumanize", 0.0) / 100.0, 0.0, 1.0);
    p[lynxtune::flexTune] = 0.0f;
    p[lynxtune::transpose] = (float) ((std::clamp (std::round (at ("pPitchShift", 0.0)), -12.0, 12.0) + 12.0) / 24.0);
    const auto formant = at ("pFormant", 1.0) >= 0.5;
    p[lynxtune::formantPreserve] = formant ? 1.0f : 0.0f;
    p[lynxtune::formantShift] = 0.5f;
    p[lynxtune::throatLength] = lynxtune::throatValue (at ("pThroatLength", 100.0)); // round 166c: the engine's throat
    static constexpr int rangeOf[5] { 0, 1, 2, 3, 3 };
    p[lynxtune::range] = (float) rangeOf[std::clamp ((int) std::lround (at ("pInputType", 1.0)), 0, 4)] / (float) (lynxtune::rangeCount - 1);
    p[lynxtune::mix] = (float) std::clamp (at ("pDryWetMix", 100.0) / 100.0, 0.0, 1.0);
    return 24;
}

// Waves Tune Real-Time: the key and scale saved in its state (wavesTuneScale; [DATA] every one of the 11 distinct
// states of her corpus names its scale — "Natural Minor" / "Major" — with the root and the twelve note statuses);
// a scale LYNX Tune has no name for becomes its custom scale with the same notes. RealWorld 20 = Speed, 21 = Note
// Transition (ms). [EXEC] Black-box (official trial, REA round 120 §2): a held mistuned note is pulled to the target
// along a first-order curve whose time constant is the Note Transition time (50 % / 90 % at 12 ms + 0.69 / 2.3 x NT for
// NT = 21 … 250 ms), which is LYNX Tune's retune law at retune = NT; Speed barely changes anything on sung notes (only
// below ~5 ms, slightly less vibrato), so it is not translated; no humanize.
std::optional<double> wavesValue (const std::vector<double>& rw, std::size_t i);

int translateWavesTune (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    if (slot.kind != EffectKind::lynxTune)
        return 0;
    int n = 0;
    if (const auto sc = wavesTuneScale (state))
    {
        slot.params[lynxtune::key] = (float) sc->key / 11.0f;
        slot.params[lynxtune::scale] = (float) sc->scale / (float) (vocal::scaleCount - 1);
        for (int k = 0; k < 12; ++k)
            slot.params[(std::size_t) (lynxtune::noteC + k)] = sc->scale != (int) vocal::Scale::custom || ((sc->mask >> k) & 1u) != 0 ? 1.0f : 0.0f;
        n += 2;
    }
    const auto rw = wavesRealWorldParams (state);
    if (const auto transition = wavesValue (rw, 21); transition && *transition >= 0.0)
    {
        slot.params[lynxtune::retune] = (float) std::sqrt (std::clamp (*transition, 0.0, 400.0) / 400.0);
        slot.params[lynxtune::humanize] = 0.0f;
        n += 2;
    }
    // round 135w: Speed is shown as saved; it only acts (Link off) where the trial showed an effect (below ~5 ms)
    if (const auto speed = wavesValue (rw, 20); speed && *speed > 0.0)
    {
        slot.params[lynxtune::speed] = lynxtune::speedValue (*speed);
        slot.params[lynxtune::speedLink] = *speed < 5.0 ? 0.0f : 1.0f;
        ++n;
    }
    // round 190mg: what the reference does not correct passes as the input itself (REA_rodada190mg_voc.md; hidden)
    slot.params[lynxtune::wavesLaw] = 1.0f;
    // RealWorld 22 = 100 in every state of her corpus, Correction % by its default [model]
    if (const auto amount = wavesValue (rw, 22); amount && *amount >= 1.0 && *amount <= 100.0)
        slot.params[lynxtune::correction] = (float) (*amount / 100.0);
    return n;
}

// Fresh Air: its two air amounts drive Aether's dynamic air model (round 124), which reproduces the official trial
// measured black-box (docs/analise/REA_rodada124.md §3): the same two parallel compressed high-passed bands, the same
// amount laws, so the amounts map one to one; Trim (the float at 2526 of the state, -12 … 0 dB) becomes the output.
// (Round 113 fitted a static shelf; the original is level-dependent: quiet highs get up to +12 dB, loud ones +3.)
int translateFreshAir (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace a = mixsuite::aether;
    const auto air = freshAirAmounts (state);
    if (! air || slot.kind != EffectKind::aether)
        return 0;
    slot.params[a::dynamic] = 1.0f;
    slot.params[a::midAir] = air->first;
    slot.params[a::highAir] = air->second;
    slot.params[a::air] = 0.0f;
    slot.params[a::harmonics] = 0.0f;
    slot.params[a::mix] = 1.0f;
    float trimDb = 0.0f;
    std::memcpy (&trimDb, state.data() + 2526, 4);
    if (! std::isfinite (trimDb) || trimDb < -12.0f || trimDb > 0.0f)
        trimDb = 0.0f;
    slot.params[a::output] = (trimDb + 12.0f) / 24.0f;
    return 3;
}

// --- Waves RealWorld lists (round 113). [EXEC] Black-box measurement of the official trials (WaveShell 17.1, tools/
// fxmeasure, headless): each control was moved alone and the value of the list that followed it (and its text) was
// recorded; then the plugin at her projects' states and LYNX's substitute were run on the same sine stairs (1 and
// 6 kHz, -60 .. 0 dBFS) and vocal takes and the substitute fitted (docs/analise/REA_rodada113.md §3c).
// The value at `i`, or nothing when the list is shorter / holds "*"
std::optional<double> wavesValue (const std::vector<double>& rw, std::size_t i)
{
    if (i < rw.size() && std::isfinite (rw[i]))
        return rw[i];
    return std::nullopt;
}

float logNorm (double v, double lo, double hi) { return (float) std::clamp (std::log (v / lo) / std::log (hi / lo), 0.0, 1.0); }
float linNorm (double v, double lo, double hi) { return (float) std::clamp ((v - lo) / (hi - lo), 0.0, 1.0); }

// Renaissance Compressor (28 values): 0 release ms, 1 threshold dB, 2 gain dB, 8 attack ms, 9 ratio, 17 ARC (0) / manual,
// 26 mix %, 27 trim dB.
// Round 123: Compressor's "vintage program" voicing is the compressor measured black-box on the official trial at her
// state (core/VoiceMatch.h; REA_rodada123 §3), taking threshold, ratio, attack, release and gain as they are.
int translateWavesRComp (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto threshold = wavesValue (rw, 1), ratio = wavesValue (rw, 9);
    if (slot.kind != EffectKind::compressor || ! threshold || ! ratio)
        return 0;
    slot.params[0] = linNorm (*threshold, -60.0, 0.0);
    slot.params[1] = linNorm (*ratio, 1.0, 20.0);
    slot.params[2] = logNorm (wavesValue (rw, 8).value_or (0.5), 0.1, 100.0);
    slot.params[3] = logNorm (wavesValue (rw, 0).value_or (160.0), 10.0, 1000.0);
    slot.params[4] = linNorm (wavesValue (rw, 2).value_or (0.0), 0.0, 24.0);
    slot.params[5] = 0.0f;
    slot.params[6] = (float) std::clamp (wavesValue (rw, 26).value_or (100.0) / 100.0, 0.0, 1.0); // mix %
    slot.params[7] = linNorm (wavesValue (rw, 27).value_or (0.0), -18.0, 18.0);                  // trim dB
    slot.params[8] = 1.0f;                                                                         // vintage voicing
    // round 135w: ARC / Manual (17), Electro / Opto (22), Warm / Smooth (16) [EXEC round 113 list]; the output limiter
    // the reference always has
    slot.params[9] = wavesValue (rw, 17).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    slot.params[10] = wavesValue (rw, 22).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    slot.params[11] = wavesValue (rw, 16).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    slot.params[12] = 1.0f;
    // round 150: the reference's own ranges (Compressor's "Ranges = RComp" and its RC parameters, REA_rodada150 §6):
    // ratio 0.5 … 50 (below 1 expands), gain −30 … +30, attack 0.5 … 500 ms, release 5 … 5000 ms, as they are
    const auto r = std::clamp (*ratio, 0.5, 50.0);
    slot.params[13] = 1.0f;
    slot.params[14] = (float) (r >= 1.0 ? 0.625 * (1.0 - std::log (r) / std::log (50.0)) : 0.625 + 0.375 * std::log (r) / std::log (0.5));
    slot.params[15] = linNorm (wavesValue (rw, 2).value_or (0.0), -30.0, 30.0);
    slot.params[16] = logNorm (wavesValue (rw, 8).value_or (16.0), 0.5, 500.0);
    slot.params[17] = logNorm (wavesValue (rw, 0).value_or (160.0), 5.0, 5000.0);
    // round 190mg: the vintage voicing's Renaissance law — LYNX's model of the reference's structure, every constant
    // fitted on our probes (core/VoiceMatch.h progcomp::Renaissance, docs/analise/REA_rodada190mg_ren.md); hidden, saved
    // while set (0 = the round-123 / 152 engine, every other Compressor)
    if (slot.params.size() > (std::size_t) compressorRenLaw)
        slot.params[(std::size_t) compressorRenLaw] = 1.0f;
    return 6;
}

// SSLComp (22 values; round 190jh [EXEC]: refcap182's states against the settings set through the trial's parameters):
// 0 threshold (−15 … +15, the console's dB), 1 ratio (0 = 2:1, 1 = 4:1, 2 = 10:1), 3 attack (0 … 5 = 0.1, 0.3, 1, 3, 10,
// 30 ms), 4 release (0 … 4 = 0.1, 0.3, 0.6, 1.2 s, Auto), 5 make-up dB, 19 mix %, 21 side-chain high-pass Hz (20 = off,
// not translated). Compressor's classic voicing takes them as they are, the threshold moved by sslCompThresholdDb to
// dBFS and Auto as sslCompAutoReleaseMs — fitted on refcap182's programme (D, X1 … X4 x drums, bass, guitar, vocal, mix:
// offsets −15 / −20 / −23 / −25 / −30 dB scored 2.64 / 4.20 / 5.56 / 5.40 / 3.08; Auto at 300 / 600 ms 5.56 / 5.68).
constexpr double sslCompThresholdDb = -23.0, sslCompAutoReleaseMs = 600.0;
int translateWavesSslComp (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto threshold = wavesValue (rw, 0), ratio = wavesValue (rw, 1);
    if (slot.kind != EffectKind::compressor || ! threshold || ! ratio || wavesPluginName (state) != "SSLComp")
        return 0;
    static constexpr double ratios[3] { 2.0, 4.0, 10.0 }, attacks[6] { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 };
    static constexpr double releases[5] { 100.0, 300.0, 600.0, 1200.0, sslCompAutoReleaseMs };
    const auto pick = [&] (std::size_t i, double fallback, int count) { return std::clamp ((int) std::lround (wavesValue (rw, i).value_or (fallback)), 0, count - 1); };
    slot.params[0] = linNorm (*threshold + sslCompThresholdDb, -60.0, 0.0);
    slot.params[1] = linNorm (ratios[pick (1, 1.0, 3)], 1.0, 20.0);
    slot.params[2] = logNorm (attacks[pick (3, 2.0, 6)], 0.1, 100.0);
    slot.params[3] = logNorm (releases[pick (4, 3.0, 5)], 10.0, 1000.0);
    slot.params[4] = linNorm (wavesValue (rw, 5).value_or (0.0), 0.0, 24.0);
    slot.params[5] = 0.0f;
    slot.params[6] = (float) std::clamp (wavesValue (rw, 19).value_or (100.0) / 100.0, 0.0, 1.0);
    return 7;
}

// SSLComp → Crest's Bus style with the console law (round 190kt [EXEC]; the state's RealWorld list as above, plus 8 IN
// (compression on), 20 trim dB). The console law takes the detector's threshold, loop gain, knee and RC time constants;
// the reference's knobs reach them through LYNX's fits on our demo-mode probes (rea190kt/lab: fitstatic2.py on 1 kHz
// stairs at 3 ratios × 4 thresholds, 0.02 … 0.2 dB rms; fitdyn.py / fitauto.py on level steps and drum bursts):
// per ratio step, threshold = knob + a cubic in the knob, loop gain and knee linear in the knob.
struct SslBusFit { double off, q1, q2, q3, loop, loopT, knee, kneeT; };
// a make-up on the compressed path only, blended with the dry path at `mix`, then a trim on both: Crest's mix and output
void consoleMix (EffectParams& p, double mix, double makeupDb, double trimDb)
{
    namespace c = dyn::crest;
    mix = std::clamp (mix, 0.0, 1.0);
    const auto mk = std::pow (10.0, makeupDb / 20.0), sum = (1.0 - mix) + mk * mix;
    p[c::mix] = (float) (mk * mix / sum);
    p[c::input] = 0.5f;
    p[c::output] = (float) std::clamp ((trimDb + 20.0 * std::log10 (sum) + 36.0) / 72.0, 0.0, 1.0);
}
constexpr SslBusFit sslBusFits[3] {
    { -35.4220, 0.2569, -0.0006, -0.2269, 2.6844, -0.0974, 10.0000, 0.6599 },
    { -26.5435, 0.4816, 0.0122, -0.2725, 4.3253, -0.5199, 2.7142, -0.9498 },
    { -22.0192, 0.4841, -0.0060, -0.2320, 14.5155, 0.1545, 1.5414, 0.3730 },
};
constexpr double sslBusAttackMs[6] { 0.047, 0.988, 1.645, 8.435, 30.81, 111.0 };   // the six attack positions' RC time constants
constexpr double sslBusReleaseMs[5] { 85.8, 127.6, 264.3, 560.5, 26.66 };  // 0.1 / 0.3 / 0.6 / 1.2 s and Auto's fast capacitor
int translateWavesSslCompBus (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace c = dyn::crest;
    const auto rw = wavesRealWorldParams (state);
    const auto threshold = wavesValue (rw, 0), ratio = wavesValue (rw, 1);
    if (slot.kind != EffectKind::crestCompressor || ! threshold || ! ratio || wavesPluginName (state) != "SSLComp")
        return 0;
    const auto pick = [&] (std::size_t i, double fallback, int count) { return std::clamp ((int) std::lround (wavesValue (rw, i).value_or (fallback)), 0, count - 1); };
    const auto& f = sslBusFits[pick (1, 1.0, 3)];
    const auto t = std::clamp (*threshold, -15.0, 15.0), u = t / 15.0;
    const auto thr = t + f.off + f.q1 * t + f.q2 * t * u + f.q3 * t * u * u;
    const bool in = wavesValue (rw, 8).value_or (1.0) >= 0.5;
    const auto release = pick (4, 3.0, 5);
    auto& p = slot.params;
    p[c::style] = c::styleValue (c::Style::bus);
    p[c::voicing] = 0.0f;
    p[c::busLaw] = 1.0f;
    p[c::threshold] = (float) std::clamp ((thr + 60.0) / 60.0, 0.0, 1.0);
    p[c::ratio] = in ? c::ratioValue (1.0 + f.loop * (1.0 + f.loopT * u)) : 0.0f;
    p[c::knee] = (float) std::clamp (f.knee * (1.0 + f.kneeT * u) * 8.685889638065035 / 2.0 / 72.0, 0.0, 1.0); // nepers → the knob
    p[c::range] = 1.0f;
    p[c::attack] = c::attackValue (sslBusAttackMs[pick (3, 2.0, 6)]);
    p[c::release] = c::releaseValue (sslBusReleaseMs[release]);
    p[c::autoRelease] = release == 4 ? 1.0f : 0.0f;
    p[c::hold] = p[c::lookahead] = p[c::maxLookahead] = 0.0f;
    p[c::character] = 0.0f;
    p[c::oversampling] = 0.0f;
    p[c::autoGain] = 0.0f;
    p[c::autoThreshold] = 0.0f;
    p[c::stereoLink] = 1.0f;
    p[c::linkMode] = c::linkModeValue (c::LinkMode::stereo);
    // the reference's make-up acts on the compressed path only (trim · (dry · (1 − mix) + wet · make-up · mix)); Crest's
    // output acts on both, so the blend and the output carry it
    consoleMix (p, wavesValue (rw, 19).value_or (100.0) / 100.0, wavesValue (rw, 5).value_or (0.0), wavesValue (rw, 20).value_or (0.0));
    for (int b = 0; b < c::scBands; ++b)
        p[c::scParam (b, 0)] = facet::shapeValue (facet::Shape::off);
    const auto hp = wavesValue (rw, 21).value_or (20.0);
    if (hp > 20.5) // the side-chain high-pass (12 dB/oct; 20 Hz = off)
    {
        p[c::scParam (0, 0)] = facet::shapeValue (facet::Shape::lowCut);
        p[c::scParam (0, 1)] = facet::hzValue (hp);
        p[c::scParam (0, 4)] = facet::slopeValue (12);
    }
    return 22;
}

// round 190jh: SuperTap's echo levels on Echoform (see translateWavesSuperTap), fitted on refcap182's programme
constexpr double superTapLossDb = 0.3, superTapHardDb = 3.0, superTapFeedback = 1.0, superTapHalfPi = 1.5707963267948966;

// L2 (16 values; round 190jh [EXEC]: refcap182's L2 states against the settings set through the trial's parameters):
// 0 release ms (the dial; 1 ms with ARC, its default), 1 threshold dB (-30 … 0), 2 out ceiling dB, 8 quantize, 13 ARC
// (1 on). Round 190lf (REA of the L2 DSP + our probes, docs/analise/REA_rodada190lf.md §2): the Limiter's L2 law
// (core/LimiterLaw.h). L2 limits the input at the threshold and adds the make-up ceiling − threshold (measured: the
// small-signal gain is exactly that, and 0.999 at Ceiling 0 dB) — the Limiter's Gain knob is that make-up (a threshold
// above the ceiling: the hidden trim takes the gain below 0 dB), its Ceiling the ceiling, ARC the law's programme-
// dependent release, else the Release knob (L2's dial: the 90 % recovery time, 10 … 1000 ms here). Round 190mg: IDR's
// requantizer (word length, dither, noise shaping) on the law's output.
int translateWavesL2 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace ll = limiterlaw;
    const auto rw = wavesRealWorldParams (state);
    const auto threshold = wavesValue (rw, 1), ceiling = wavesValue (rw, 2);
    if (slot.kind != EffectKind::limiter || ! threshold || ! ceiling || wavesPluginName (state) != "L2") // (L1 / L3 / L4: their own lists)
        return 0;
    const bool arc = wavesValue (rw, 13).value_or (1.0) >= 0.5;
    const auto releaseMs = std::clamp (wavesValue (rw, 0).value_or (10.0), 10.0, 1000.0);
    const auto ceilingDb = std::clamp (*ceiling, -12.0, 0.0);
    const auto gainDb = ceilingDb - std::clamp (*threshold, -30.0, 0.0);
    slot.params[0] = linNorm (ceilingDb, -12.0, 0.0);
    slot.params[1] = linNorm (std::max (gainDb, 0.0), 0.0, 24.0);
    slot.params[2] = logNorm (releaseMs, 10.0, 1000.0);
    slot.params[(std::size_t) ll::lawParam] = ll::l2LawValue;
    slot.params[(std::size_t) ll::arcParam] = arc ? 1.0f : 0.0f;
    slot.params[(std::size_t) ll::trimParam] = (float) (std::max (-gainDb, 0.0) / 24.0);
    // round 190mg (REA_rodada190mg_dyn §2, measured): IDR — 8 Quantize 0 … 4 = 24 / 22 / 20 / 18 / 16 bits, 11 the dither
    // setting (1 = Type 1, TPDF), 12 the shaping (0 none, 1 moderate, 2 normal, 3 ultra)
    const int quantize = std::clamp ((int) std::lround (wavesValue (rw, 8).value_or (0.0)), 0, 4);
    slot.params[(std::size_t) ll::ditherBitsParam] = (float) (24 - 2 * quantize) / 32.0f;
    slot.params[(std::size_t) ll::ditherTypeParam] = (float) (std::clamp ((int) std::lround (wavesValue (rw, 11).value_or (1.0)), 0, 2) + 1) / 4.0f;
    slot.params[(std::size_t) ll::ditherShapeParam] = (float) (std::clamp ((int) std::lround (wavesValue (rw, 12).value_or (2.0)), 0, 3) + 1) / 8.0f;
    return 6;
}

// PuigTec EQP1A (round 190lf [EXEC]: crafted states through loadcomponent on refcap030's host, impulse responses;
// docs/analise/REA_rodada190lf.md §3): RealWorld 0 output gain dB, 6 high boost, 7 bandwidth, 8 high frequency (1 … 7 =
// 3, 4, 5, 8, 10, 12, 16 kHz), 9 low atten, 10 atten select (1 … 3 = 5, 10, 20 kHz), 11 low frequency (1 … 4 = 20, 30,
// 60, 100 Hz), 12 low boost, 14 high atten (the knobs 0 … 11), 15 in (1) / out, 16 mains (round 190mg: modelled). The
// reference is an oversampled passive-EQ model (REA of the DSP: a block convolution / resampling frame, a hum generator,
// per-instance component tolerances): its curves are minimum phase plus a linear-phase resampler (149 samples at 48 kHz,
// a −0.46 dB roll-off at 20 kHz, a +1.1 dB insertion gain). LYNX plays them on Facet EQ's zero-latency bands: the low
// section on two shelves whose values are smooth (cubic) polynomials of both knobs (they interact: the boost / dip of
// the "Pultec trick"), the high boost on a bell and the atten on a 12 dB/oct high shelf interpolated over our probe
// grid (knob steps of 1.375, five bandwidths).
namespace puigtec
{
// puigtec:begin (lab/gen_pt.py writes this block from the fit JSONs)
// flat: output dB, high shelf ln Hz / dB / ln Q, low shelf ln Hz / dB / ln Q (0.012 dB rms)
constexpr double flat[7] = { 1.10397, 9.30656, -0.51596, -0.31540, 3.13946, -0.23818, -0.27100 };
// the Low Frequency switch lift over the 100 Hz position: high shelf, low shelf (20 / 30 / 60 / 100 Hz)
constexpr double lift[4][6] = { { 7.85053, 0.73400, -0.76778, 3.52671, 0.10431, 0.50302 }, { 8.28169, 0.61603, -0.44810, 10.20499, 0.02723, -13.32627 }, { 9.26000, 0.40095, -0.77403, 5.56076, 0.00027, -5.54782 }, { 8.51719, 0.00000, -0.35667, 3.21888, 0.00000, -0.35667 } };
// the low shelves' slopes (dB/oct) and value polynomials per Low Frequency (rms over the 80 probes: 0.21 / 0.32 / 0.30 / 0.33 dB;
// the fit weighs probes with both knobs <= 6.875 double: lab/fitlow5.py)
constexpr int lowSlopes[4][2] = { { 12, 12 }, { 12, 12 }, { 12, 12 }, { 12, 12 } };
constexpr double low[4][60] = {
    { 8.00014, -6.79766, -1.68429, 3.22669, -1.17728, 1.93506, 3.69993, -1.93228, -1.94843, 1.11373, 0.07587, -0.31187, 0.04808, 0.90069, 3.40043, 0.84825, -2.61003, -0.00335, -0.80528, -0.46962, -2.25990, 9.25760, -1.86685, -0.89039, -15.97424, 5.08672, 8.20249, -3.52966, 0.46862, 0.89031, 6.63507, -1.02770, 0.02860, 5.86445, -1.16706, -6.43491, 1.12766, 3.72973, -3.15208, -0.13788, -0.11490, 1.01439, 0.10458, -0.77594, -1.54098, 2.30340, 0.67453, -1.54184, 0.91955, -0.48763, -2.15807, -1.58051, 6.53026, 2.58602, 3.71307, -8.80646, -3.46532, 3.23086, 0.80332, -0.81963 },
    { 7.50045, -3.74792, -2.52959, 6.55883, -2.60723, -0.46308, 3.35033, 0.86082, -2.37890, -1.84215, -0.15601, 1.39068, -2.63432, 8.12599, 0.10679, -0.02426, -0.82502, 0.45279, -4.15615, -1.38468, -1.50776, 5.39858, -1.19706, -4.43841, -9.93930, 8.65393, 5.30401, -6.51492, 1.51081, 2.93285, 7.65344, -6.65834, -0.36750, 6.88772, 1.74867, -8.20625, 0.54550, 5.47329, -1.84909, -1.97455, -0.03043, -2.41729, -0.48343, -0.62187, 7.71715, 3.47287, -3.25981, -2.25741, -1.00963, 0.00690, -2.04788, -0.08078, 6.73657, 3.30623, -23.41788, -9.59765, 16.70714, 3.81673, -1.44752, 0.49166 },
    { 9.17641, -6.73890, -1.75223, 5.16060, -0.42820, 0.56235, 3.07458, -0.94781, -3.39643, 0.96371, -0.07720, 0.77401, -0.28254, 0.88993, 1.39332, 2.02069, -1.53365, -0.62357, -0.77718, -0.72097, -1.78194, 6.63896, -1.78358, 0.62736, -11.73817, 4.00053, 6.06802, -2.60672, -0.80517, 0.68247, 7.69696, -0.56693, 0.72012, 4.30197, -1.07532, -6.80105, 1.25639, 3.74140, -2.96514, 0.55951, -0.17023, 0.89294, 0.31490, -0.44700, -1.19819, 2.12810, 0.49175, -1.54871, 0.36789, -0.32257, -2.98668, 0.03450, 8.31758, -0.19396, 1.72816, -9.72527, -1.50371, 3.11790, -0.41651, 1.64128 },
    { 9.01700, -3.99792, -1.28852, 6.96979, -4.22994, -1.39493, 4.75079, 0.37028, -4.08262, 0.01854, -0.09072, 0.81618, 0.90335, -0.03691, 1.38312, 0.78798, -1.53800, -0.25202, -0.46223, -0.44441, -2.27627, 8.55301, -1.45041, -0.88424, -14.42499, 4.81284, 7.31602, -3.20624, -0.30844, 1.13190, 7.52895, -2.86206, 4.61618, 7.64424, 0.66247, -12.60871, 0.39597, 6.44227, -3.13439, -1.22996, -0.19227, 2.10877, 0.43704, -1.73351, -2.14155, 1.92361, 0.87348, -1.45608, 0.48420, 0.16835, -2.74427, 0.88416, 5.12566, -1.33730, 0.36350, -3.32851, -0.40060, -0.30538, -0.83364, 2.51813 },
};
// high boost bell [frequency 3 … 16 kHz][boost 0, 1.375 … 11][bandwidth 0, 2.75 … 11]: ln Hz, dB, ln Q (fine grid, ≤ 0.25 dB rms)
constexpr double bell[7][9][5][3] = {
    { { { 8.06284, 0.00000, 1.76712 }, { 8.06375, 0.00000, 1.10582 }, { 8.06601, 0.00000, 0.66404 }, { 8.06869, 0.00000, 0.37752 }, { 8.07195, 0.00000, 0.15848 } }, { { 8.06284, 0.51018, 1.76712 }, { 8.06375, 0.26378, 1.10582 }, { 8.06601, 0.17016, 0.66404 }, { 8.06869, 0.12819, 0.37752 }, { 8.07195, 0.10329, 0.15848 } }, { { 8.06548, 1.74992, 0.69456 }, { 8.06795, 1.32418, 0.41174 }, { 8.07148, 1.03683, 0.16285 }, { 8.07524, 0.86384, -0.02418 }, { 8.07966, 0.74272, -0.17970 } }, { { 8.06922, 3.14198, 0.26468 }, { 8.07255, 2.59377, 0.06753 }, { 8.07695, 2.16457, -0.11907 }, { 8.08161, 1.87833, -0.26595 }, { 8.08621, 1.66383, -0.39187 } }, { { 8.07231, 4.60344, 0.04230 }, { 8.07612, 3.93200, -0.12236 }, { 8.08095, 3.37427, -0.28228 }, { 8.08569, 2.98506, -0.41054 }, { 8.09061, 2.68389, -0.52208 } }, { { 8.07463, 6.56358, -0.10022 }, { 8.07870, 5.70733, -0.24976 }, { 8.08350, 4.97577, -0.39564 }, { 8.08835, 4.45380, -0.51346 }, { 8.09322, 4.04155, -0.61659 } }, { { 8.07524, 9.19309, -0.15665 }, { 8.07930, 8.03254, -0.30621 }, { 8.08421, 7.04022, -0.45062 }, { 8.08895, 6.32932, -0.56630 }, { 8.09369, 5.76605, -0.66705 } }, { { 8.07378, 12.94488, -0.10916 }, { 8.07770, 11.19422, -0.28064 }, { 8.08255, 9.75011, -0.43925 }, { 8.08714, 8.73883, -0.56262 }, { 8.09177, 7.94751, -0.66853 } }, { { 8.07024, 19.45147, 0.12762 }, { 8.07408, 16.05663, -0.13013 }, { 8.07871, 13.60615, -0.33810 }, { 8.08322, 12.01448, -0.48788 }, { 8.08768, 10.82218, -0.61057 } } },
    { { { 8.35793, 0.00000, 1.47755 }, { 8.36068, 0.00000, 0.81310 }, { 8.36704, 0.00000, 0.36699 }, { 8.37471, 0.00000, 0.07548 }, { 8.38343, 0.00000, -0.14806 } }, { { 8.35793, 0.50937, 1.47755 }, { 8.36068, 0.26355, 0.81310 }, { 8.36704, 0.16987, 0.36699 }, { 8.37471, 0.12795, 0.07548 }, { 8.38343, 0.10307, -0.14806 } }, { { 8.36573, 1.74723, 0.39791 }, { 8.37266, 1.32137, 0.11038 }, { 8.38196, 1.03451, -0.14310 }, { 8.39138, 0.86249, -0.33296 }, { 8.40056, 0.74212, -0.49016 } }, { { 8.37605, 3.13511, -0.03822 }, { 8.38443, 2.58850, -0.23875 }, { 8.39427, 2.16187, -0.42764 }, { 8.40326, 1.87769, -0.57573 }, { 8.41144, 1.66474, -0.70208 } }, { { 8.38321, 4.59509, -0.26285 }, { 8.39164, 3.92701, -0.42954 }, { 8.40113, 3.37301, -0.59045 }, { 8.40935, 2.98711, -0.71887 }, { 8.41664, 2.68826, -0.82997 } }, { { 8.38735, 6.55499, -0.40524 }, { 8.39544, 5.70423, -0.55572 }, { 8.40431, 4.97799, -0.70217 }, { 8.41190, 4.45956, -0.81978 }, { 8.41846, 4.05045, -0.92203 } }, { { 8.38726, 9.18283, -0.46020 }, { 8.39506, 8.03051, -0.61029 }, { 8.40352, 7.04481, -0.75498 }, { 8.41073, 6.33929, -0.87020 }, { 8.41714, 5.77972, -0.97040 } }, { { 8.38276, 12.92891, -0.41057 }, { 8.39047, 11.18951, -0.58262 }, { 8.39892, 9.75569, -0.74122 }, { 8.40612, 8.75089, -0.86453 }, { 8.41270, 7.96448, -0.96995 } }, { { 8.37396, 19.41900, -0.17147 }, { 8.38184, 16.04043, -0.42990 }, { 8.39061, 13.60547, -0.63801 }, { 8.39818, 12.02421, -0.78776 }, { 8.40515, 10.83911, -0.91030 } } },
    { { { 8.56937, 0.00000, 1.87985 }, { 8.57102, 0.00000, 1.22058 }, { 8.57541, 0.00000, 0.77765 }, { 8.58074, 0.00000, 0.48917 }, { 8.58718, 0.00000, 0.26828 } }, { { 8.56937, 0.50443, 1.87985 }, { 8.57102, 0.26143, 1.22058 }, { 8.57541, 0.16890, 0.77765 }, { 8.58074, 0.12742, 0.48917 }, { 8.58718, 0.10274, 0.26828 } }, { { 8.57452, 1.73728, 0.80835 }, { 8.57937, 1.31592, 0.52445 }, { 8.58605, 1.03117, 0.27346 }, { 8.59322, 0.85962, 0.08485 }, { 8.60063, 0.73984, -0.07083 } }, { { 8.58177, 3.12369, 0.37653 }, { 8.58792, 2.58021, 0.17794 }, { 8.59583, 2.15500, -0.00965 }, { 8.60328, 1.87189, -0.15663 }, { 8.61067, 1.65954, -0.28240 } }, { { 8.58722, 4.58063, 0.15342 }, { 8.59390, 3.91498, -0.01203 }, { 8.60193, 3.36256, -0.17232 }, { 8.60935, 2.97778, -0.29993 }, { 8.61615, 2.67970, -0.41070 } }, { { 8.59075, 6.53462, 0.01101 }, { 8.59738, 5.68675, -0.13858 }, { 8.60518, 4.96271, -0.28424 }, { 8.61230, 4.44575, -0.40128 }, { 8.61883, 4.03747, -0.50339 } }, { { 8.59096, 9.15404, -0.04462 }, { 8.59750, 8.00632, -0.19387 }, { 8.60501, 7.02393, -0.33757 }, { 8.61182, 6.32031, -0.45235 }, { 8.61808, 5.76266, -0.55197 } }, { { 8.58778, 12.88730, 0.00374 }, { 8.59411, 11.15473, -0.16727 }, { 8.60143, 9.72647, -0.32504 }, { 8.60804, 8.72519, -0.44761 }, { 8.61431, 7.94175, -0.55236 } }, { { 8.58103, 19.34654, 0.24061 }, { 8.58741, 15.98725, -0.01641 }, { 8.59461, 13.56290, -0.22335 }, { 8.60124, 11.98827, -0.37224 }, { 8.60760, 10.80834, -0.49387 } } },
    { { { 9.01792, 0.00000, 1.63586 }, { 9.02441, 0.00000, 0.99390 }, { 9.03829, 0.00000, 0.55144 }, { 9.05328, 0.00000, 0.26400 }, { 9.06771, 0.00000, 0.04566 } }, { { 9.01792, 0.48873, 1.63586 }, { 9.02441, 0.25885, 0.99390 }, { 9.03829, 0.16786, 0.55144 }, { 9.05328, 0.12690, 0.26400 }, { 9.06771, 0.10251, 0.04566 } }, { { 9.03515, 1.72718, 0.58348 }, { 9.04911, 1.31062, 0.29929 }, { 9.06508, 1.02944, 0.05154 }, { 9.07858, 0.85952, -0.13285 }, { 9.08990, 0.74026, -0.28522 } }, { { 9.05398, 3.11664, 0.15515 }, { 9.06734, 2.57951, -0.03997 }, { 9.08096, 2.15760, -0.22369 }, { 9.09174, 1.87476, -0.36770 }, { 9.10038, 1.66147, -0.49205 } }, { { 9.06396, 4.58121, -0.06230 }, { 9.07587, 3.92132, -0.22420 }, { 9.08771, 3.37016, -0.38124 }, { 9.09695, 2.98364, -0.50773 }, { 9.10453, 2.68256, -0.61844 } }, { { 9.06795, 6.54615, -0.19922 }, { 9.07885, 5.70224, -0.34596 }, { 9.08966, 4.97669, -0.48985 }, { 9.09818, 4.45573, -0.60673 }, { 9.10532, 4.04233, -0.70985 } }, { { 9.06547, 9.17556, -0.25119 }, { 9.07613, 8.03176, -0.39812 }, { 9.08691, 7.04702, -0.54084 }, { 9.09548, 6.33741, -0.65594 }, { 9.10273, 5.77232, -0.75701 } }, { { 9.05692, 12.90965, -0.20107 }, { 9.06803, 11.18963, -0.36962 }, { 9.07942, 9.76117, -0.52619 }, { 9.08873, 8.75414, -0.64904 }, { 9.09667, 7.96222, -0.75496 } }, { { 9.04189, 19.33350, 0.03459 }, { 9.05393, 16.01324, -0.21872 }, { 9.06662, 13.60681, -0.42326 }, { 9.07712, 12.03187, -0.57150 }, { 9.08629, 10.84543, -0.69371 } } },
    { { { 9.24312, 0.00000, 1.44305 }, { 9.26039, 0.00000, 0.77578 }, { 9.28640, 0.00000, 0.33865 }, { 9.30789, 0.00000, 0.05833 }, { 9.32526, 0.00000, -0.15883 } }, { { 9.24312, 0.49871, 1.44305 }, { 9.26039, 0.25908, 0.77578 }, { 9.28640, 0.16803, 0.33865 }, { 9.30789, 0.12695, 0.05833 }, { 9.32526, 0.10227, -0.15883 } }, { { 9.28032, 1.72913, 0.37069 }, { 9.30118, 1.31276, 0.09408 }, { 9.32084, 1.02862, -0.15151 }, { 9.33506, 0.85619, -0.33897 }, { 9.34612, 0.73512, -0.49850 } }, { { 9.30559, 3.12377, -0.04639 }, { 9.32140, 2.57909, -0.24159 }, { 9.33613, 2.14925, -0.43048 }, { 9.34698, 1.86219, -0.58263 }, { 9.35592, 1.64723, -0.71574 } }, { { 9.31572, 4.58790, -0.26161 }, { 9.32926, 3.91437, -0.42803 }, { 9.34202, 3.35288, -0.59328 }, { 9.35198, 2.96159, -0.72873 }, { 9.36011, 2.65962, -0.84820 } }, { { 9.31842, 6.55304, -0.39857 }, { 9.33112, 5.69081, -0.55140 }, { 9.34354, 4.95121, -0.70462 }, { 9.35318, 4.42427, -0.83067 }, { 9.36147, 4.01014, -0.94252 } }, { { 9.31393, 9.19417, -0.44871 }, { 9.32694, 8.02529, -0.60173 }, { 9.33981, 7.02009, -0.75359 }, { 9.35025, 6.29996, -0.87816 }, { 9.35903, 5.73124, -0.98840 } }, { { 9.30164, 12.96033, -0.39466 }, { 9.31603, 11.20800, -0.56785 }, { 9.33035, 9.74836, -0.73254 }, { 9.34187, 8.72106, -0.86427 }, { 9.35189, 7.91817, -0.97969 } }, { { 9.27956, 19.42857, -0.15606 }, { 9.29679, 16.08149, -0.41096 }, { 9.31361, 13.63552, -0.62050 }, { 9.32719, 12.02694, -0.77608 }, { 9.33900, 10.81497, -0.90712 } } },
    { { { 9.43517, 0.00000, 1.65153 }, { 9.45255, 0.00000, 0.98831 }, { 9.47886, 0.00000, 0.56195 }, { 9.50147, 0.00000, 0.28821 }, { 9.52022, 0.00000, 0.07443 } }, { { 9.43517, 0.49716, 1.65153 }, { 9.45255, 0.25669, 0.98831 }, { 9.47886, 0.16677, 0.56195 }, { 9.50147, 0.12579, 0.28821 }, { 9.52022, 0.10127, 0.07443 } }, { { 9.47268, 1.71765, 0.59243 }, { 9.49400, 1.30393, 0.32444 }, { 9.51499, 1.01981, 0.08311 }, { 9.53161, 0.84727, -0.10399 }, { 9.54608, 0.72643, -0.26583 } }, { { 9.49811, 3.10721, 0.18781 }, { 9.51559, 2.56046, -0.00533 }, { 9.53295, 2.12936, -0.19445 }, { 9.54746, 1.84207, -0.34975 }, { 9.56032, 1.62784, -0.48803 } }, { { 9.50882, 4.56456, -0.02339 }, { 9.52452, 3.88735, -0.18949 }, { 9.54089, 3.32296, -0.35764 }, { 9.55480, 2.93144, -0.49794 }, { 9.56704, 2.63078, -0.62373 } }, { { 9.51153, 6.52614, -0.15764 }, { 9.52670, 5.65671, -0.31150 }, { 9.54293, 4.91236, -0.46848 }, { 9.55677, 4.38428, -0.60015 }, { 9.56922, 3.97141, -0.71859 } }, { { 9.50587, 9.17161, -0.20534 }, { 9.52118, 7.99142, -0.35922 }, { 9.53764, 6.97669, -0.51456 }, { 9.55208, 6.25252, -0.64434 }, { 9.56532, 5.68350, -0.76129 } }, { { 9.49211, 12.95565, -0.14997 }, { 9.50776, 11.18692, -0.32235 }, { 9.52488, 9.71030, -0.48896 }, { 9.54009, 8.67281, -0.62487 }, { 9.55436, 7.86452, -0.74614 } }, { { 9.46954, 19.44994, 0.08682 }, { 9.48672, 16.09205, -0.16445 }, { 9.50454, 13.62240, -0.37305 }, { 9.52036, 11.99456, -0.53013 }, { 9.53546, 10.76801, -0.66485 } } },
    { { { 9.69549, 0.00000, 2.13702 }, { 9.70038, 0.00000, 1.65360 }, { 9.71217, 0.00000, 1.30341 }, { 9.72591, 0.00000, 1.07466 }, { 9.73788, 0.00000, 0.89795 } }, { { 9.69549, 0.35172, 2.13702 }, { 9.70038, 0.22663, 1.65360 }, { 9.71217, 0.15674, 1.30341 }, { 9.72591, 0.12138, 1.07466 }, { 9.73788, 0.09909, 0.89795 } }, { { 9.70883, 1.60929, 1.32712 }, { 9.72086, 1.25449, 1.10269 }, { 9.73463, 0.99713, 0.90174 }, { 9.74673, 0.83485, 0.74575 }, { 9.75803, 0.71708, 0.61065 } }, { { 9.72376, 3.02082, 0.98590 }, { 9.73492, 2.51824, 0.82596 }, { 9.74750, 2.10601, 0.66726 }, { 9.75910, 1.82358, 0.53701 }, { 9.77111, 1.60854, 0.42025 } }, { { 9.73043, 4.49511, 0.80680 }, { 9.74081, 3.85125, 0.66785 }, { 9.75292, 3.29979, 0.52709 }, { 9.76453, 2.90773, 0.40894 }, { 9.77679, 2.60081, 0.30158 } }, { { 9.73164, 6.46450, 0.68991 }, { 9.74138, 5.62940, 0.56163 }, { 9.75315, 4.89294, 0.42983 }, { 9.76473, 4.35993, 0.31885 }, { 9.77682, 3.93538, 0.21730 } }, { { 9.72748, 9.10039, 0.64507 }, { 9.73666, 7.96686, 0.51604 }, { 9.74766, 6.96707, 0.38558 }, { 9.75864, 6.23816, 0.27650 }, { 9.77038, 5.65404, 0.17712 } }, { { 9.71915, 12.78996, 0.68537 }, { 9.72769, 11.13760, 0.54175 }, { 9.73761, 9.70857, 0.40183 }, { 9.74759, 8.67775, 0.28734 }, { 9.75830, 7.85813, 0.18500 } }, { { 9.70657, 18.73596, 0.87020 }, { 9.71566, 15.84539, 0.66755 }, { 9.72509, 13.56982, 0.49439 }, { 9.73376, 12.00094, 0.36170 }, { 9.74282, 10.78840, 0.24809 } } },
};
// high atten shelf (12 dB/oct) [select 5 / 10 / 20 kHz][atten 0, 1.375 … 11]: ln Hz, dB, ln Q
constexpr double atten[3][9][3] = {
    { { 8.16761, 0.00000, -0.73632 }, { 8.16761, -0.40523, -0.73632 }, { 7.61471, -1.62499, -0.73330 }, { 7.44466, -2.45418, -0.73647 }, { 7.26957, -4.13150, -0.74720 }, { 7.19536, -6.23020, -0.76751 }, { 7.21352, -9.38653, -0.81143 }, { 7.37035, -14.28376, -0.91119 }, { 7.70641, -21.45157, -1.12154 } },
    { { 8.86751, 0.00000, -0.76864 }, { 8.86751, -0.40198, -0.76864 }, { 8.32292, -1.61790, -0.74155 }, { 8.15485, -2.44532, -0.74118 }, { 7.98134, -4.11776, -0.74923 }, { 7.90758, -6.20713, -0.76838 }, { 7.92500, -9.33972, -0.81175 }, { 8.07773, -14.15854, -0.91044 }, { 8.39396, -20.97743, -1.11005 } },
    { { 9.55020, 0.00000, -0.98818 }, { 9.55020, -0.40983, -0.98818 }, { 8.99679, -1.59913, -0.77849 }, { 8.83158, -2.41794, -0.76403 }, { 8.66112, -4.07439, -0.76218 }, { 8.58775, -6.13658, -0.77697 }, { 8.60251, -9.20324, -0.81714 }, { 8.74249, -13.80509, -0.90838 }, { 8.99632, -19.62936, -1.06475 } },
};
// puigtec:end

double knobAxis (double v, int& i) noexcept // the knob grid 0, 1.375 … 11 → cell index and fraction
{
    const double x = std::clamp (v, 0.0, 11.0) / 1.375;
    i = std::min ((int) x, 7);
    return x - i;
}
} // namespace puigtec

// round 190mg: Waves C4 (70 values; names from its ParamXML, every law measured on our demo-mode probes,
// docs/analise/REA_rodada190mg.md §2): 0 / 1 / 2 crossovers Hz, 3 crossover Q, 7 output dB; band b (0 … 3) at 14 + 14 b:
// +2 solo, +3 bypass, +4 gain dB, +5 threshold dB, +6 range dB (negative = downward), +7 knee (0 … 0.75), +8 attack ms,
// +9 release ms, +11 behaviour (0 electro, 1 opto). Prism's four bands take them as they are and its hidden C4 law
// (dyn::prism::c4Law) runs the C4's structure on them.
int translateWavesC4 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace pr = dyn::prism;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::prismMultiband || rw.size() < 70 || ! wavesValue (rw, 0) || ! wavesValue (rw, 19))
        return 0;
    auto& p = slot.params;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto set = [&p] (int k, float x) { p[(std::size_t) k] = std::clamp (x, 0.0f, 1.0f); };
    set (pr::bandLayout, 0.0f);
    set (pr::bandCountParam, pr::bandCountValue (4));
    static constexpr double defaultsHz[3] { 92.0, 4000.0, 11071.0 };
    for (int k = 0; k < 3; ++k)
        set (pr::crossoverParam (k), pr::crossoverValue (v ((std::size_t) k, defaultsHz[k])));
    bool opto = false;
    for (int b = 0; b < 4; ++b)
    {
        const auto at = [&] (int f, double fallback) { return v ((std::size_t) (14 + 14 * b + f), fallback); };
        set (pr::param (b, pr::fThreshold), pr::thresholdValue (std::clamp (at (5, -30.0), -90.0, 0.0)));
        set (pr::param (b, pr::fRange), (float) ((std::clamp (at (6, -8.0), -30.0, 30.0) + 30.0) / 60.0));
        set (pr::param (b, pr::fKnee), (float) (std::clamp (at (7, 0.5), 0.0, 0.75) * 64.0 / 48.0));
        set (pr::param (b, pr::fAttack), pr::c4AttackValue (at (8, 10.0)));
        set (pr::param (b, pr::fRelease), pr::c4ReleaseValue (at (9, 50.0)));
        set (pr::param (b, pr::fLevel), (float) ((std::clamp (at (4, 0.0), -30.0, 30.0) + 30.0) / 60.0));
        set (pr::param (b, pr::fMode), 0.0f);
        set (pr::param (b, pr::fEnabled), at (3, 0.0) >= 0.5 ? 0.0f : 1.0f);
        set (pr::param (b, pr::fSolo), at (2, 0.0) >= 0.5 ? 1.0f : 0.0f);
        opto = opto || at (11, 0.0) >= 0.5;
    }
    set (pr::mix, 1.0f);
    set (pr::input, 0.5f);
    set (pr::output, (float) ((std::clamp (v (7, 0.0), -36.0, 36.0) + 36.0) / 72.0));
    p[(std::size_t) pr::c4Law] = pr::c4LawValue (opto);
    p[(std::size_t) pr::c4Q] = pr::c4QValue (v (3, 0.6));
    return 40;
}

// round 190mg: Abbey Road Reverb Plates (409 values; the indices that move with the controls, read from refcap182's states):
// 324 pre-delay ms, 326 the plate's decay s (the plate A … D and Damper 0 … 10 already combined by the reference), 334 plate,
// 335 damper, 336 bass cut (0 off, 1 … 3), 378 treble dB, 382 drive. Foil Plate's measured plate voicing takes the decay
// and pre-delay as they are, 100 % wet like the reference (no dry path: its states mix nothing dry).
int translateWavesArPlates (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::foilPlate || rw.size() < 383 || ! wavesValue (rw, 326))
        return 0;
    auto& p = slot.params;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const double rt = std::clamp (v (326, 2.6), 0.3, 20.0);
    double lo = 0.0, hi = 1.0;
    for (int k = 0; k < 40; ++k)
        (verbs::foil::decaySec ((float) (0.5 * (lo + hi))) < rt ? lo : hi) = 0.5 * (lo + hi);
    p[0] = 1.0f;                                                                   // mix
    p[1] = (float) verbs::skewInverse (std::clamp (v (324, 0.0), 0.0, 500.0), 0.0, 500.0, 2.3219); // pre-delay
    p[2] = (float) (0.5 * (lo + hi));                                              // decay
    // the reference plates are brighter than this tank above 5 kHz (impulse probes of refcap182's five states: 8 / 16 kHz
    // octaves -1 … -5 / -2 … -7 dB, -4 / -5 on average): Foil's high shelf +5 dB at 5 kHz, then the Treble on top
    const double treble = 5.0 + std::clamp (v (378, 0.0), -12.0, 12.0);
    p[10] = (float) std::clamp (treble >= 0.0 ? 0.5 + treble / 24.0 : 0.5 + treble / 9.0, 0.0, 1.0);
    static constexpr double bassCutHz[4] { 0.0, 80.0, 160.0, 320.0 };
    const auto bc = std::clamp ((int) std::lround (v (336, 0.0)), 0, 3);
    p[13] = bc == 0 ? 0.0f : (float) (std::log (bassCutHz[bc] / 20.0) / std::log (50.0));
    p[14] = 1.0f; // the measured plate voicing
    p[(std::size_t) verbs::foil::revision] = 1.0f;
    slot.mix = 1.0f;
    return 4;
}

// round 190mg: Atlas Reverb (488 values; indices read from refcap182's states): 2 decay s, 17 pre-delay ms, 7 size. Atrium
// (the room / hall role) takes the decay and pre-delay as they are, 100 % wet like the reference.
int translateWavesAtlas (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::atrium || rw.size() < 18 || ! wavesValue (rw, 2))
        return 0;
    auto& p = slot.params;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    p[0] = 1.0f;
    p[1] = (float) std::clamp (v (17, 10.0) / 500.0, 0.0, 1.0);
    p[2] = (float) verbs::skewInverse (std::clamp (v (2, 2.5), 0.1, 100.0), 0.1, 100.0, 5.717);
    p[(std::size_t) verbs::atrium::revision] = 1.0f;
    slot.mix = 1.0f;
    return 3;
}

int translateWavesPuigTec (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace pt = puigtec;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::facetEq || rw.size() < 16 || ! wavesValue (rw, 12) || ! wavesValue (rw, 6))
        return 0;
    auto& p = slot.params;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto setBand = [&p] (int b, facet::Shape shape, double hz, double db, double q, int slope)
    {
        p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shape);
        p[(std::size_t) facet::param (b, 1)] = facet::hzValue (hz);
        p[(std::size_t) facet::param (b, 2)] = facet::gainValue (db);
        p[(std::size_t) facet::param (b, 3)] = facet::qValue (q);
        p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (slope);
    };
    // the flat response (insertion gain, the resampler's roll-off) and the Low Frequency switch's own lift
    const int lf = std::clamp ((int) std::lround (v (11, 4)), 1, 4) - 1;
    p[(std::size_t) facet::outputParam] = (float) std::clamp ((pt::flat[0] + std::clamp (v (0, 0.0), -18.0, 18.0)) / 48.0 + 0.5, 0.0, 1.0);
    setBand (4, facet::Shape::highShelf, std::exp (pt::flat[1]), pt::flat[2], std::exp (pt::flat[3]), 12);
    setBand (5, facet::Shape::lowShelf, std::exp (pt::flat[4]), pt::flat[5], std::exp (pt::flat[6]), 12);
    setBand (6, facet::Shape::highShelf, std::exp (pt::lift[lf][0]), pt::lift[lf][1], std::exp (pt::lift[lf][2]), 12);
    setBand (7, facet::Shape::lowShelf, std::exp (pt::lift[lf][3]), pt::lift[lf][4], std::exp (pt::lift[lf][5]), 12);
    // round 190mg (REA_rodada190mg_dyn §1): the output stage's tube harmonics, the resampler's fractional delay and, with
    // the Mains switch (16: 0 off, 1 = 60 Hz, 2 = 50 Hz — measured), the hum and noise floor: Facet's PuigTec law
    const int mains = std::clamp ((int) std::lround (v (16, 0.0)), 0, 2);
    p[(std::size_t) facet::puigLawParam] = facet::puigLawValue (mains == 1 ? 2 : mains == 2 ? 3 : 1);
    if (v (15, 1.0) < 0.5) // the In switch off: the curves out, the reference's flat response stays
        return 8;

    // low section: two shelves, each value a cubic polynomial of u = boost / 11, w = atten / 11
    const double boost = std::clamp (v (12, 0.0), 0.0, 11.0), atten = std::clamp (v (9, 0.0), 0.0, 11.0);
    const double u = boost / 11.0, w = atten / 11.0;
    const double t[10] = { 1.0, u, w, u * w, u * u, w * w, u * u * u, w * w * w, u * u * w, u * w * w };
    const auto poly = [&t, lf] (int k) { double s = 0.0; for (int j = 0; j < 10; ++j) s += pt::low[lf][k * 10 + j] * t[j]; return s; };
    if (boost > 0.0)
        setBand (0, facet::Shape::lowShelf, std::exp (poly (0)), 30.0 * u * poly (1), std::exp (poly (2)), pt::lowSlopes[lf][0]);
    if (atten > 0.0)
        setBand (1, facet::Shape::lowShelf, std::exp (poly (3)), -30.0 * w * poly (4), std::exp (poly (5)), pt::lowSlopes[lf][1]);

    // high boost: a bell over the boost x bandwidth grid of its frequency (bilinear)
    const double hb = std::clamp (v (6, 0.0), 0.0, 11.0);
    if (hb > 0.0)
    {
        const int hf = std::clamp ((int) std::lround (v (8, 1)), 1, 7) - 1;
        int i = 0;
        const double fi = pt::knobAxis (hb, i);
        const double bwx = std::clamp (v (7, 0.0), 0.0, 11.0) / 2.75;
        const int j = std::min ((int) bwx, 3);
        const double fj = bwx - j;
        double z[3];
        for (int k = 0; k < 3; ++k)
            z[k] = (pt::bell[hf][i][j][k] * (1 - fj) + pt::bell[hf][i][j + 1][k] * fj) * (1 - fi)
                 + (pt::bell[hf][i + 1][j][k] * (1 - fj) + pt::bell[hf][i + 1][j + 1][k] * fj) * fi;
        setBand (2, facet::Shape::bell, std::exp (z[0]), z[1], std::exp (z[2]), 12);
    }
    // high atten: a 12 dB/oct high shelf over the atten grid of its selected frequency
    const double ha = std::clamp (v (14, 0.0), 0.0, 11.0);
    if (ha > 0.0)
    {
        const int as = std::clamp ((int) std::lround (v (10, 3)), 1, 3) - 1;
        int i = 0;
        const double fi = pt::knobAxis (ha, i);
        double z[3];
        for (int k = 0; k < 3; ++k)
            z[k] = pt::atten[as][i][k] * (1 - fi) + pt::atten[as][i + 1][k] * fi;
        setBand (3, facet::Shape::highShelf, std::exp (z[0]), z[1], std::exp (z[2]), 12);
    }
    return 8;
}

// ---- round 190mm: Waves EQs on Facet EQ's Waves design law (docs/analise/REA_rodada190mm.md §2) ----------------------
namespace wavesEq
{
void band (EffectSlot& slot, int b, facet::Shape shape, facet::WaveCode code, double hzv, double db, double qv, int slope = 12)
{
    auto& p = slot.params;
    p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shape);
    p[(std::size_t) facet::param (b, 1)] = facet::hzValue (hzv);
    p[(std::size_t) facet::param (b, 2)] = facet::gainValue (db);
    p[(std::size_t) facet::param (b, 3)] = facet::qValue (qv);
    p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (slope);
    p[(std::size_t) (facet::wavesBandParam0 + b)] = facet::waveCodeValue (code);
}
void off (EffectSlot& slot, int b)
{
    slot.params[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    slot.params[(std::size_t) (facet::wavesBandParam0 + b)] = 0.0f;
}
} // namespace wavesEq

// Renaissance EQ (REQ 2 / 4 / 6: one layout, 140 values): band k = 0 … 5 at 5 k: on, type, gain dB, frequency Hz, Q (the
// left channel; the right copy at 30 + 5 k), 60 / 61 the output gain left / right. Types by band: 1st: bell, low shelf,
// high pass, resonant low shelf; 2nd / 3rd: bell, low shelf; 4th / 5th: bell, high shelf; 6th: bell, high shelf, low pass,
// resonant high shelf. REQ 2 uses the 1st and 6th, REQ 4 the 1st, 2nd, 5th and 6th. Facet's bands 0 … 5 take them.
int translateWavesReq (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace F = facet;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::facetEq || rw.size() < 62 || ! wavesValue (rw, 3) || ! wavesValue (rw, 28))
        return 0;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    int n = 0;
    for (int k = 0; k < 6; ++k)
    {
        const auto i = (std::size_t) (5 * k);
        const bool on = v (i, 0.0) >= 0.5;
        const int type = (int) std::lround (v (i + 1, 0.0));
        const double db = std::clamp (v (i + 2, 0.0), -18.0, 18.0), f = std::clamp (v (i + 3, 1000.0), 10.0, 22000.0);
        const double q = std::clamp (v (i + 4, 1.0), 0.1, 10.0);
        if (! on) { wavesEq::off (slot, k); continue; }
        ++n;
        using S = F::Shape; using C = F::WaveCode;
        if (type == 0) wavesEq::band (slot, k, S::bell, C::reqBell, f, db, q);
        else if (k == 0 && type == 2) wavesEq::band (slot, k, S::lowCut, C::reqHighPass, f, 0.0, q, 18);
        else if (k == 5 && type == 2) wavesEq::band (slot, k, S::highCut, C::reqLowPass, f, 0.0, q, 18);
        else if (k == 0 && type == 3) wavesEq::band (slot, k, S::lowShelf, C::rbjLowShelf, f, db, q);
        else if (k == 5 && type == 3) wavesEq::band (slot, k, S::highShelf, C::rbjHighShelf, f, db, q);
        else if (k <= 2) wavesEq::band (slot, k, S::lowShelf, C::reqLowShelf, f, db, q);
        else wavesEq::band (slot, k, S::highShelf, C::reqHighShelf, f, db, q);
    }
    wavesEq::off (slot, 6);
    wavesEq::off (slot, 7);
    slot.params[(std::size_t) F::outputParam] = (float) std::clamp (v (60, 0.0) / 48.0 + 0.5, 0.0, 1.0);
    slot.params[(std::size_t) F::wavesLawParam] = 1.0f;
    return 2 + 5 * n;
}

// Q10 (122 values): band k = 0 … 9 at 5 k: in, type (0 bell, 1 low shelf, 2 high shelf, 3 low pass, 4 high pass, 5 PQ
// bell), gain dB, frequency Hz, Q (the left channel; the right copy at 50 + 5 k); 100 / 102 the input / output gain left.
// Facet has eight bands: when more than eight Q10 bands are in, the eight with the largest effect (cuts first, then by
// |gain|) are kept.
int translateWavesQ10 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace F = facet;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::facetEq || rw.size() < 103 || ! wavesValue (rw, 3) || ! wavesValue (rw, 48))
        return 0;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    struct B { int type; double db, f, q, weight; };
    std::vector<B> in;
    for (int k = 0; k < 10; ++k)
    {
        const auto i = (std::size_t) (5 * k);
        if (v (i, 0.0) < 0.5) continue;
        B b { (int) std::lround (v (i + 1, 0.0)), std::clamp (v (i + 2, 0.0), -18.0, 18.0), std::clamp (v (i + 3, 1000.0), 10.0, 22000.0),
              std::clamp (v (i + 4, 7.0), 0.5, 100.0), 0.0 };
        b.weight = b.type == 3 || b.type == 4 ? 1e9 : std::abs (b.db);
        in.push_back (b);
    }
    std::stable_sort (in.begin(), in.end(), [] (const B& x, const B& y) { return x.weight > y.weight; });
    if (in.size() > (std::size_t) F::bands) in.resize ((std::size_t) F::bands);
    for (int k = 0; k < F::bands; ++k)
    {
        if (k >= (int) in.size()) { wavesEq::off (slot, k); continue; }
        const auto& b = in[(std::size_t) k];
        using S = F::Shape; using C = F::WaveCode;
        switch (b.type)
        {
            case 1: wavesEq::band (slot, k, S::lowShelf, C::lowShelf1, b.f, b.db, 0.7071, 6); break;
            case 2: wavesEq::band (slot, k, S::highShelf, C::highShelf1, b.f, b.db, 0.7071, 6); break;
            case 3: wavesEq::band (slot, k, S::highCut, C::butterLowPass, b.f, 0.0, 1.0, 12); break;
            case 4: wavesEq::band (slot, k, S::lowCut, C::butterHighPass, b.f, 0.0, 1.0, 12); break;
            case 5: wavesEq::band (slot, k, S::bell, C::q10PqBellX, b.f, b.db, b.q / 4.0); break; // round 190mt: its law above 3 kHz
            default: wavesEq::band (slot, k, S::bell, C::q10BellTan, b.f, b.db, b.q / 4.0); break; // round 190mt: exact law
        }
    }
    slot.params[(std::size_t) F::outputParam] = (float) std::clamp ((v (100, 0.0) + v (102, 0.0)) / 48.0 + 0.5, 0.0, 1.0);
    slot.params[(std::size_t) F::wavesLawParam] = 1.0f;
    return 3 + 5 * (int) in.size();
}

// Renaissance Channel (137 values: 0 … 19 the EQ's four bands like the Renaissance EQ's, 22 … 28 routing / side chain,
// 42 attack ms, 43 release ms, 45 threshold dB, 46 ratio, 47 gain dB, 52 ARC (0) / manual, 58 … 62 the gate): its
// compressor block mirrors the Renaissance Compressor's list (0 / 1 / 2 / 8 / 9 / 17 there), so it takes the same
// translation onto Compressor's Renaissance law; its EQ runs on the law's hidden channel EQ (Facet's Waves design law),
// before or after the dynamics as routed. The gate is not carried.
int translateWavesRChannel (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto threshold = wavesValue (rw, 45), ratio = wavesValue (rw, 46);
    if (slot.kind != EffectKind::compressor || rw.size() < 63 || ! threshold || ! ratio)
        return 0;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const double attack = v (42, 16.0), release = v (43, 160.0), gain = v (47, 0.0);
    slot.params[0] = linNorm (*threshold, -60.0, 0.0);
    slot.params[1] = linNorm (*ratio, 1.0, 20.0);
    slot.params[2] = logNorm (attack, 0.1, 100.0);
    slot.params[3] = logNorm (release, 10.0, 1000.0);
    slot.params[4] = linNorm (gain, 0.0, 24.0);
    slot.params[5] = 0.0f;
    slot.params[6] = 1.0f;
    slot.params[7] = linNorm (0.0, -18.0, 18.0);
    slot.params[8] = 1.0f;
    slot.params[9] = v (52, 0.0) >= 0.5 ? 1.0f : 0.0f;
    slot.params[10] = slot.params[11] = 0.0f;
    slot.params[12] = 1.0f;
    const auto r = std::clamp (*ratio, 0.5, 50.0);
    slot.params[13] = 1.0f;
    slot.params[14] = (float) (r >= 1.0 ? 0.625 * (1.0 - std::log (r) / std::log (50.0)) : 0.625 + 0.375 * std::log (r) / std::log (0.5));
    slot.params[15] = linNorm (gain, -30.0, 30.0);
    slot.params[16] = logNorm (attack, 0.5, 500.0);
    slot.params[17] = logNorm (release, 5.0, 5000.0);
    if (slot.params.size() > (std::size_t) compressorRenLaw)
        slot.params[(std::size_t) compressorRenLaw] = 1.0f;
    // round 190mt: the "RVox" compressor type (44 = 1): the vocal compressor's curve on the same law, the ratio ignored
    // ([EXEC] ratios 0.5 / 1 / 2 / 50 and ARC give the same output; the attack / release still act)
    if (v (44, 0.0) >= 0.5 && slot.params.size() > (std::size_t) compressorRVoxType)
        slot.params[(std::size_t) compressorRVoxType] = 1.0f;
    // round 190mt: the gate (57 on, 58 1 = gate / 0 = expander, 59 threshold dB (−80 = −inf: off), 60 floor dB, 62 release ms)
    if (v (57, 0.0) >= 0.5 && v (59, -80.0) > -79.95 && slot.params.size() > (std::size_t) compressorRchGateMode)
    {
        slot.params[(std::size_t) compressorRchGate] = 1.0f;
        slot.params[(std::size_t) compressorRchGateThreshold] = (float) std::clamp ((v (59, -80.0) + 90.0) / 108.0, 0.0, 1.0);
        slot.params[(std::size_t) compressorRchGateFloor] = (float) std::clamp ((v (60, -80.0) + 90.0) / 90.0, 0.0, 1.0);
        slot.params[(std::size_t) compressorRchGateRelease] = (float) std::clamp (std::log (std::clamp (v (62, 250.0), 10.0, 5000.0) / 10.0) / std::log (500.0), 0.0, 1.0);
        slot.params[(std::size_t) compressorRchGateMode] = v (58, 0.0) >= 0.5 ? 1.0f : 0.0f;
    }
    // the EQ (20 on; band k at 5 k: on, type, gain, Hz, Q; 26 routing 1 = dynamics first) on the Renaissance Channel law:
    // the Renaissance EQ's own band structures through Facet's Waves design law
    int bands = 0;
    if (v (20, 1.0) >= 0.5 && slot.params.size() > (std::size_t) (compressorChannelBand0 + 15))
    {
        using C = facet::WaveCode;
        for (int k = 0; k < 4; ++k)
        {
            const auto i = (std::size_t) (5 * k);
            if (v (i, 0.0) < 0.5) continue;
            const int type = (int) std::lround (v (i + 1, 0.0));
            C code = C::reqBell;
            if (type == 1) code = k <= 1 ? C::reqLowShelf : C::reqHighShelf;
            else if (type == 2) code = k <= 1 ? C::rbjLowShelf : C::rbjHighShelf;
            else if (type == 3) code = k == 0 ? C::reqHighPass : C::reqLowPass;
            const auto b = (std::size_t) (compressorChannelBand0 + 4 * k);
            slot.params[b] = facet::waveCodeValue (code);
            slot.params[b + 1] = facet::hzValue (std::clamp (v (i + 3, 1000.0), 10.0, 22000.0));
            slot.params[b + 2] = facet::gainValue (std::clamp (v (i + 2, 0.0), -18.0, 18.0));
            slot.params[b + 3] = facet::qValue (std::clamp (v (i + 4, 1.0), 0.1, 10.0));
            ++bands;
        }
        slot.params[(std::size_t) compressorChannelLaw] = 1.0f;
        slot.params[(std::size_t) compressorChannelRouting] = v (26, 0.0) >= 0.5 ? 1.0f : 0.0f;
    }
    return 7 + 4 * bands;
}

namespace wavesApi
{
void put (EffectSlot& slot, int b, const wavesApiEq::Band& e, double dbScale = 1.0)
{
    using S = facet::Shape;
    static constexpr S shapes[6] = { S::off, S::bell, S::lowShelf, S::highShelf, S::lowShelf, S::highShelf };
    const int k = std::clamp (e.shape, 0, 5);
    auto& p = slot.params;
    p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shapes[k]);
    p[(std::size_t) facet::param (b, 1)] = facet::hzValue (e.hz);
    p[(std::size_t) facet::param (b, 2)] = facet::gainValue (e.db * dbScale);
    p[(std::size_t) facet::param (b, 3)] = facet::qValue (e.q);
    p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (k >= 4 ? 6 : 12);
}
int nearest (const float* xs, int n, double x, bool logScale)
{
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (logScale ? std::abs (std::log (xs[i] / x)) < std::abs (std::log (xs[best] / x)) : std::abs (xs[i] - x) < std::abs (xs[best] - x))
            best = i;
    return best;
}
} // namespace wavesApi

// API-550A (26 values: 0 Filter, 1 / 2 low gain dB / Hz, 3 / 4 mid gain / kHz, 5 / 6 high gain / kHz, 14 / 15 low / high
// bell) and API-550B (the same name "API-550" in its state: 1 / 17 low gain / Hz, 21 / 18 low-mid gain / Hz, 3 / 19 high-mid
// gain / kHz, 5 / 20 high gain / kHz, 14 / 15 low / high bell; told apart by the state's sub-component). Each band is the
// fitted Facet band of its nearest control position (the reference's gains and frequencies are stepped), on top of the 550s'
// flat response (two / three bands) and the 550A's Filter (a low and a high cut).
int translateWavesApi550 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace A = wavesApiEq;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::facetEq || rw.size() < 22 || ! wavesValue (rw, 1) || ! wavesValue (rw, 6))
        return 0;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    for (int b = 0; b < facet::bands; ++b)
        slot.params[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    // the two share the name "API-550" and one RealWorld layout; the state's sub-component says which ("A5BS" / "A5BM")
    const bool typeB = findBytes (state, "<PluginSubComp>A5B") != std::string::npos;
    int b = 0, n = 0;
    const auto add = [&] (const A::Band& e) { if (b < facet::bands && e.shape > 0) wavesApi::put (slot, b++, e); };
    const auto gainPos = [] (double db, int& g) { if (std::abs (db) < 1.0) return false; g = wavesApi::nearest (A::gains550, 10, db, false); return true; };
    if (! typeB)
    {
        const double gain[3] = { v (1, 0.0), v (3, 0.0), v (5, 0.0) }, hzv[3] = { v (2, 50.0), v (4, 0.4) * 1000.0, v (6, 5.0) * 1000.0 };
        const int bell[3] = { v (14, 0.0) > 0.5 ? 1 : 0, 0, v (15, 0.0) > 0.5 ? 1 : 0 };
        for (int k = 0; k < 3; ++k)
        {
            int g = 0;
            if (! gainPos (gain[k], g)) continue;
            add (A::api550a[k][bell[k]][wavesApi::nearest (A::freqs550a[k], 5, hzv[k], true)][g]);
            ++n;
        }
        add (A::flat550a[0]);
        add (A::flat550a[1]);
        if (v (0, 0.0) > 0.5 && b + 2 <= facet::bands)
        {
            using S = facet::Shape;
            for (int k = 0; k < 2; ++k, ++b)
            {
                auto& p = slot.params;
                p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (k == 0 ? S::lowCut : S::highCut);
                p[(std::size_t) facet::param (b, 1)] = facet::hzValue (A::filterBands[k][0]);
                p[(std::size_t) facet::param (b, 3)] = facet::qValue (A::filterBands[k][1]);
                p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (A::filterSlopes[k]);
            }
        }
    }
    else
    {
        const double gain[4] = { v (1, 0.0), v (21, 0.0), v (3, 0.0), v (5, 0.0) };
        const double hzv[4] = { v (17, 30.0), v (18, 75.0), v (19, 1.5) * 1000.0, v (20, 2.5) * 1000.0 };
        const int bell[4] = { v (14, 0.0) > 0.5 ? 1 : 0, 0, 0, v (15, 0.0) > 0.5 ? 1 : 0 };
        for (int k = 0; k < 4; ++k)
        {
            int g = 0;
            if (! gainPos (gain[k], g)) continue;
            add (A::api550b[k][bell[k]][wavesApi::nearest (A::freqs550b[k], 7, hzv[k], true)][g]);
            ++n;
        }
        add (A::flat550b[0]);
        add (A::flat550b[1]);
        add (A::flat550b[2]);
        slot.params[(std::size_t) facet::outputParam] = (float) std::clamp (A::flatOutput550b / 48.0 + 0.5, 0.0, 1.0);
    }
    return 2 + 2 * n;
}

// API-560 (23 values: the ten bands' gains dB at 1 … 10 in the order 63, 250, 1k, 4k, 16k, 31, 125, 500, 2k, 8k Hz). Its
// gain is continuous: each band's fitted Facet bell is interpolated between the probed gains (frequency and Q in log),
// scaled below ±1.5 dB. Facet has eight bands: the eight largest gains are kept.
int translateWavesApi560 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace A = wavesApiEq;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::facetEq || rw.size() < 11 || ! wavesValue (rw, 1) || ! wavesValue (rw, 10))
        return 0;
    static constexpr int order[10] = { 1, 3, 5, 7, 9, 0, 2, 4, 6, 8 }; // RealWorld 1 … 10 -> band 31 Hz … 16 kHz (0 … 9)
    std::vector<std::pair<double, int>> on;
    double gain[10] {};
    for (int i = 0; i < 10; ++i)
    {
        gain[order[i]] = std::clamp (wavesValue (rw, (std::size_t) (1 + i)).value_or (0.0), -12.0, 12.0);
        if (std::abs (gain[order[i]]) > 0.05) on.push_back ({ std::abs (gain[order[i]]), order[i] });
    }
    std::stable_sort (on.begin(), on.end(), [] (auto& x, auto& y) { return x.first > y.first; });
    if (on.size() > (std::size_t) facet::bands) on.resize ((std::size_t) facet::bands);
    for (int b = 0; b < facet::bands; ++b)
        slot.params[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    int b = 0;
    for (const auto& [mag, k] : on)
    {
        const double g = gain[k];
        A::Band e {};
        if (std::abs (g) <= 1.5)
        {
            e = A::api560[k][g < 0 ? 4 : 5];
            e.db = (float) (e.db * std::abs (g) / 1.5);
        }
        else
        {
            int i = 0;
            while (i < 8 && A::gains560[i + 1] < g) ++i;
            const auto& lo = A::api560[k][i];
            const auto& hi = A::api560[k][i + 1];
            const double t = std::clamp ((g - A::gains560[i]) / (A::gains560[i + 1] - A::gains560[i]), 0.0, 1.0);
            e.shape = 1;
            e.hz = (float) std::exp (std::log (lo.hz) * (1 - t) + std::log (hi.hz) * t);
            e.db = (float) (lo.db * (1 - t) + hi.db * t);
            e.q = (float) std::exp (std::log (lo.q) * (1 - t) + std::log (hi.q) * t);
        }
        wavesApi::put (slot, b++, e);
    }
    // round 190mt: the API-560 law (Facet's hidden graphic-EQ network, core/Api560.h): it plays the ten sliders as the
    // reference's structure does (65 samples of latency, reported to PDC); the bands above stay for the editor's curve
    slot.params[(std::size_t) facet::api560LawParam] = 1.0f;
    for (int k = 0; k < 10; ++k)
        slot.params[(std::size_t) (facet::api560GainParam0 + k)] = facet::api560GainValue (gain[k]);
    return 1 + (int) on.size();
}

// SuperTap 2-Taps (98 values; round 190jh [EXEC]: refcap182's states against the settings set through the trial's own
// parameters, and the tap times / levels read off its programme renders): 4 tempo BPM, 10 feedback on, 11 feedback %,
// 13 feedback delay; tap k (k = 0, 1) at 33 + 10 k: on, gain dB, pan (−45 … +45°), delay, 6 filter kHz, 8 filter on.
// Delays are stored in units of 4 beats at 120 BPM (the time is delay × 240 / BPM s: 0.064 at 136.4 BPM = 112.6 ms,
// echo found at 112.9 ms). The pan is constant-power around the centre (centre = the tap's gain on both sides, hard =
// +3 dB on one). Echoform plays them as its clean two-line echo — one tap: Single at its time; two: Dual, the left
// line at the first tap's time and the right at the second's, Width from the pans — with the dry path at unity (Mix
// low, the echoes raised by Output, which acts on the echoes only), the feedback on the lines (SuperTap's own
// feedback delay is not modelled) and the tap filter as the high cut.
int translateWavesSuperTap (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace e = echosuite::echoform;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::echoform || rw.size() < 52 || ! wavesValue (rw, 33) || ! wavesValue (rw, 43))
        return 0;
    struct Tap { double gain, pan, sec, filterKHz; bool filter; };
    const double bpm = std::clamp (wavesValue (rw, 4).value_or (120.0), 20.0, 400.0);
    std::vector<Tap> taps;
    for (const std::size_t base : { (std::size_t) 33, (std::size_t) 43 })
        if (wavesValue (rw, base).value_or (0.0) >= 0.5)
            taps.push_back ({ wavesValue (rw, base + 1).value_or (0.0), wavesValue (rw, base + 2).value_or (0.0),
                              wavesValue (rw, base + 3).value_or (0.0) * 240.0 / bpm, wavesValue (rw, base + 6).value_or (20.0),
                              wavesValue (rw, base + 8).value_or (0.0) >= 0.5 });
    auto& p = slot.params;
    p[e::mode] = e::modeValue (e::Mode::clean);
    p[e::syncL] = p[e::syncR] = 0.0f;
    p[e::age] = p[e::modDepth] = p[e::wow] = p[e::flutter] = 0.0f;
    p[e::voicingStereo] = 0.0f;
    p[e::lrOffset] = 0.0f;
    if (taps.empty())
    {
        p[e::mix] = 0.0f;
        return 1;
    }
    double gain = -120.0, spread = 0.0;
    for (const auto& t : taps)
    {
        gain = std::max (gain, t.gain);
        spread = std::max (spread, std::min (1.0, std::abs (t.pan) / 45.0));
    }
    // the echoes' level on a side: the tap's gain, +3 dB at a hard pan; Echoform's clean line loses 0.3 dB [EXEC probe]
    const double echoDb = gain + superTapLossDb + superTapHardDb * spread;
    double mix = 0.05;
    while (20.0 * std::log10 (std::sin (mix * superTapHalfPi)) + 18.0 < echoDb && mix < 1.0)
        mix += 0.01;
    p[e::mix] = (float) std::min (1.0, mix);
    p[e::output] = linNorm (echoDb - 20.0 * std::log10 (std::sin (std::min (1.0, mix) * superTapHalfPi)), -18.0, 18.0);
    p[e::timeL] = e::timeValue (taps[0].sec * 1000.0);
    if (taps.size() == 1)
    {
        p[e::style] = e::styleValue (e::Style::single);
        p[e::timeR] = p[e::timeL];
    }
    else
    {
        p[e::style] = e::styleValue (e::Style::dual);
        p[e::timeR] = e::timeValue (taps[1].sec * 1000.0);
        p[e::width] = (float) (0.5 + 0.5 * spread);
    }
    p[e::feedback] = wavesValue (rw, 10).value_or (0.0) >= 0.5
                         ? (float) std::clamp (superTapFeedback * wavesValue (rw, 11).value_or (0.0) / 200.0, 0.0, 1.0) : 0.0f;
    double lp = 1e9;
    for (const auto& t : taps)
        if (t.filter)
            lp = std::min (lp, t.filterKHz * 1000.0);
    if (lp < 20000.0)
        p[e::highCut] = linNorm (lp, 200.0, 20000.0);
    return (int) taps.size() * 4 + 2;
}

// ======================================================================= round 190mm (sub-round dyn): Renaissance Vox,
// RDeEsser, RBass / MaxxBass, API-2500, C6, Vitamin (docs/analise/REA_rodada190mm_dyn.md). Every law below is LYNX's own
// model of the structure REA read in the reference's DSP, with every constant fitted on our demo-mode probes.

// Renaissance Vox (RealWorld [EXEC] the bank's states vs the settings read back: 0 compression dB (−36 … 0), 1 gate dB
// (−80 = off … 0), 2 gate release ms (10 … 250), 7 output gain dB (−36 … 0)) → Compressor's hidden Vox law
int translateWavesRVox (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto comp = wavesValue (rw, 0);
    if (slot.kind != EffectKind::compressor || ! comp || slot.params.size() <= (std::size_t) compressorVoxGain)
        return 0;
    slot.params[0] = linNorm (std::clamp (*comp, -36.0, 0.0), -60.0, 0.0);
    slot.params[8] = 1.0f; // the vintage voicing (its laws)
    const auto gate = std::clamp (wavesValue (rw, 1).value_or (-80.0), -80.0, 0.0);
    slot.params[(std::size_t) compressorVoxGate] = gate > -79.95 ? (float) std::max (1e-4, (gate + 80.0) / 80.0) : 0.0f;
    slot.params[(std::size_t) compressorVoxGateRelease] = linNorm (wavesValue (rw, 2).value_or (130.0), 10.0, 250.0);
    slot.params[(std::size_t) compressorVoxGain] = (float) std::clamp (-wavesValue (rw, 7).value_or (0.0) / 36.0, 0.0, 1.0);
    slot.params[(std::size_t) compressorVoxLaw] = 1.0f;
    return 4;
}
// RDeEsser (RealWorld [EXEC] the bank's states vs the settings read back: 0 threshold dB, 1 range dB, 11 frequency Hz,
// 13 filter type (0 high-pass, 1 band-pass), 14 mode (0 split, 1 wide-band), 15 monitor (not translated); 2 … 7 and 12
// hold the reference's hidden detector values, the same in every state we have: the law uses them as constants) →
// Hush De-esser's hidden Renaissance de-esser law
int translateWavesRDeEsser (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace h = mixsuite::hush;
    const auto rw = wavesRealWorldParams (state);
    const auto thr = wavesValue (rw, 0), hz = wavesValue (rw, 11);
    if (slot.kind != EffectKind::hushDeEsser || ! thr || ! hz || slot.params.size() <= (std::size_t) h::essWideBand)
        return 0;
    slot.params[h::essLaw] = 1.0f;
    slot.params[h::essThreshold] = (float) std::clamp (-*thr / 80.0, 0.0, 1.0);
    slot.params[h::essRange] = (float) std::clamp (-wavesValue (rw, 1).value_or (-16.0) / 48.0, 0.0, 1.0);
    slot.params[h::essFreq] = linNorm (*hz, 2000.0, 16000.0);
    slot.params[h::essBandPass] = wavesValue (rw, 13).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    slot.params[h::essWideBand] = wavesValue (rw, 14).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    return 5;
}
// C6 (123 values; [EXEC] the bank's states vs the settings read back): the C4's layout — 0 … 2 crossovers Hz, 3 crossover
// Q, 7 output dB, the bands at 14 + 14 b (+2 solo, +3 bypass, +4 gain, +5 threshold, +6 range, +7 knee, +8 attack ms,
// +9 release ms, +10 / +11 release mode and behaviour (Opto = 1)) — for b = 0 … 3 the crossover bands (the C6's bands 2 … 5)
// and b = 4, 5 its floating bands 1 and 6, whose frequency and Q are 98 / 99 and 102 / 103 → Prism's C4 law plus its
// hidden floating bands
int translateWavesC6 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace pr = dyn::prism;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::prismMultiband || rw.size() < 104 || ! wavesValue (rw, 0) || ! wavesValue (rw, 19)
        || slot.params.size() <= (std::size_t) pr::c6Param (1, pr::c6Rel))
        return 0;
    if (translateWavesC4 (state, slot) == 0)
        return 0;
    auto& p = slot.params;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto set = [&p] (int k, float x) { p[(std::size_t) k] = std::clamp (x, 0.0f, 1.0f); };
    static constexpr std::size_t freqAt[2] { 98, 102 };
    for (int f = 0; f < 2; ++f)
    {
        const auto at = [&] (int fld, double fallback) { return v ((std::size_t) (14 + 14 * (4 + f) + fld), fallback); };
        const double range = std::clamp (at (6, 0.0), -30.0, 30.0), gain = std::clamp (at (4, 0.0), -30.0, 30.0);
        const bool on = at (3, 0.0) < 0.5 && (range != 0.0 || gain != 0.0);
        set (pr::c6Param (f, pr::c6On), on ? 1.0f : 0.0f);
        if (! on)
            continue;
        set (pr::c6Param (f, pr::c6Freq), pr::c6FreqValue (v (freqAt[f], f == 0 ? 1000.0 : 5003.0)));
        set (pr::c6Param (f, pr::c6Width), pr::c6WidthValue (v (freqAt[f] + 1, 1.65)));
        set (pr::c6Param (f, pr::c6Thr), pr::thresholdValue (std::clamp (at (5, 0.0), -90.0, 0.0)));
        set (pr::c6Param (f, pr::c6Range), (float) ((range + 30.0) / 60.0));
        set (pr::c6Param (f, pr::c6Gain), (float) ((gain + 30.0) / 60.0));
        set (pr::c6Param (f, pr::c6Att), pr::c4AttackValue (at (8, 16.03)));
        set (pr::c6Param (f, pr::c6Rel), pr::c4ReleaseValue (at (9, 160.31)));
    }
    return 50;
}
// API-2500 (RealWorld [EXEC] the bank's states vs the settings read back: 0 threshold (+10 … −20), 1 ratio step (1.5, 2, 3,
// 4, 6, 10, ∞), 2 attack step (0.03 … 30 ms), 3 release step (0.05 … 2 s, 6 = Var), 5 In, 7 thrust (Norm, Med, Loud),
// 8 knee (Soft, Med, Hard), 11 variable release s, 12 type (1 = Old: feedback), 13 make-up (0 = Auto), 14 make-up dB,
// 37 mix %, 39 trim dB) → Compressor's hidden bus compressor law
int translateWavesApi2500 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto thr = wavesValue (rw, 0), ratio = wavesValue (rw, 1);
    if (slot.kind != EffectKind::compressor || ! thr || ! ratio || slot.params.size() <= (std::size_t) compressorApiTrim
        || wavesPluginName (state) != "API-2500")
        return 0;
    auto& p = slot.params;
    const auto v = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto put = [&p] (int k, double x) { p[(std::size_t) k] = (float) std::clamp (x, 0.0, 1.0); };
    p[8] = 0.0f; // no vintage voicing (no latency)
    put (compressorApiLaw, 1.0);
    put (compressorApiThreshold, (10.0 - std::clamp (*thr, -20.0, 10.0)) / 30.0);
    put (compressorApiRatio, std::round (std::clamp (*ratio, 0.0, 6.0)) / 6.0);
    put (compressorApiAttack, std::round (std::clamp (v (2, 3.0), 0.0, 6.0)) / 6.0);
    put (compressorApiRelease, std::round (std::clamp (v (3, 6.0), 0.0, 6.0)) / 6.0);
    put (compressorApiVar, (std::clamp (v (11, 0.5), 0.05, 3.0) - 0.05) / 2.95);
    put (compressorApiKnee, (2.0 - std::round (std::clamp (v (8, 2.0), 0.0, 2.0))) / 2.0);
    put (compressorApiThrust, std::round (std::clamp (v (7, 0.0), 0.0, 2.0)) / 2.0);
    put (compressorApiFeedback, v (12, 0.0) >= 0.5 ? 1.0 : 0.0);
    put (compressorApiManual, v (13, 0.0) >= 0.5 ? 1.0 : 0.0);
    const double mk = std::clamp (v (14, 0.0), -24.0, 24.0), trim = std::clamp (v (39, 0.0), -18.0, 18.0);
    put (compressorApiMakeup, mk == 0.0 ? 0.0 : std::max (1.0e-4, (mk + 24.0) / 48.0));
    put (compressorApiTrim, trim == 0.0 ? 0.0 : std::max (1.0e-4, (trim + 18.0) / 36.0));
    const bool in = v (5, 1.0) >= 0.5;
    put (compressorApiDry, in ? 1.0 - std::clamp (v (37, 100.0), 0.0, 100.0) / 100.0 : 1.0);
    return 12;
}

// MaxxBass (RealWorld [EXEC] the bank's states vs the settings read back: 0 input gain dB, 1 frequency Hz, 12 original bass
// gain dB, 13 harmonics gain dB (−144 = off); 2 decay, 3 response, 4 ratio and 10 the high-pass, which move only the
// harmonics, are not translated) → Low Lifter's hidden bass law (the MaxxBass role)
int translateWavesMaxxBass (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace ll = lowlifter;
    const auto rw = wavesRealWorldParams (state);
    const auto hz = wavesValue (rw, 1);
    if (slot.kind != EffectKind::flLowLifter || ! hz || slot.params.size() <= (std::size_t) ll::bassOut || wavesPluginName (state) != "MaxxBass")
        return 0;
    auto& p = slot.params;
    p[4] = 0.0f; // Low Lifter's own look-ahead off (the law has no latency)
    p[(std::size_t) ll::bassLaw] = 1.0f;
    p[(std::size_t) ll::bassMaxx] = 1.0f;
    p[(std::size_t) ll::bassFreq] = ll::bassFreqValue (*hz);
    p[(std::size_t) ll::bassInput] = ll::bassLevelValue (wavesValue (rw, 0).value_or (0.0));
    const double orig = wavesValue (rw, 12).value_or (0.0);
    p[(std::size_t) ll::bassOrig] = orig <= -143.0 ? 0.0f : (float) std::min (4.0, std::pow (10.0, orig / 20.0));
    p[(std::size_t) ll::bassHarm] = ll::bassLevelValue (wavesValue (rw, 13).value_or (-144.0));
    p[(std::size_t) ll::bassOut] = ll::bassLevelValue (0.0);
    return 4;
}

// Renaissance Bass (RealWorld [EXEC] the bank's states vs the settings read back: 1 frequency Hz, 5 intensity dB, 6 original
// in (0) / out (1), 7 output gain dB) → Low Lifter's hidden bass law (the Renaissance Bass role)
int translateWavesRBass (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace ll = lowlifter;
    const auto rw = wavesRealWorldParams (state);
    const auto hz = wavesValue (rw, 1), intensity = wavesValue (rw, 5);
    if (slot.kind != EffectKind::flLowLifter || ! hz || ! intensity || slot.params.size() <= (std::size_t) ll::bassOut)
        return 0;
    auto& p = slot.params;
    p[4] = 0.0f;
    p[(std::size_t) ll::bassLaw] = 1.0f;
    p[(std::size_t) ll::bassMaxx] = 0.0f;
    p[(std::size_t) ll::bassFreq] = ll::bassFreqValue (*hz);
    p[(std::size_t) ll::bassInput] = ll::bassLevelValue (0.0);
    p[(std::size_t) ll::bassOrig] = wavesValue (rw, 6).value_or (0.0) >= 0.5 ? 0.0f : 1.0f;
    p[(std::size_t) ll::bassHarm] = ll::bassLevelValue (std::clamp (*intensity, -24.0, 24.0));
    p[(std::size_t) ll::bassOut] = ll::bassLevelValue (std::clamp (wavesValue (rw, 7).value_or (0.0), -30.0, 0.0));
    return 4;
}

// Vitamin (145 values; [EXEC] the bank's states vs the settings read back: 0 … 3 crossovers Hz, 91 … 95 the bands' gains
// A … E (0 = Off), 100 direct gain dB; 103 punch and 125 … 129 the bands' widths are not translated) → Aether's hidden
// five-band harmonic enhancer law
int translateWavesVitamin (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace a = mixsuite::aether;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::aether || rw.size() < 101 || ! wavesValue (rw, 0) || slot.params.size() <= (std::size_t) a::vitEnd)
        return 0;
    auto& p = slot.params;
    p[(std::size_t) a::vitLaw] = 1.0f;
    static constexpr double defaults[4] { 90.0, 500.0, 2594.0, 9310.0 };
    for (int k = 0; k < 4; ++k)
    {
        const double hz = wavesValue (rw, (std::size_t) k).value_or (defaults[k]);
        p[(std::size_t) (a::vitCross0 + k)] = std::abs (hz - defaults[k]) < 0.5 ? 0.0f : a::vitCrossValue (hz);
    }
    for (int b = 0; b < 5; ++b)
        p[(std::size_t) (a::vitGain0 + b)] = (float) std::clamp (wavesValue (rw, (std::size_t) (91 + b)).value_or (0.0) / 48.0, 0.0, 1.0);
    p[(std::size_t) a::vitDirect] = a::vitDirectValue (wavesValue (rw, 100).value_or (0.0));
    return 10;
}
// ======================================================================= end of round 190mm (dyn) translators

// round 190js: the Waves reverbs on Reeverb 2 (the stand-in since round 129, now with their states translated). Black-box on
// refcap182's programme (D, X1 … X4 x drums, bass, guitar, vocal, mix; the RealWorld lists read against the settings set
// through the trials' own parameters): each reverb's decay, pre-delay and wet level are read from its state; its tone (the
// band gain of the reverb, out / in per third octave) was measured by deconvolving the programme renders and matched by
// Reeverb 2's own controls, fitted on a model of the parity score (the input's spectrum times Reeverb's impulse
// response against the reference render, every setting at once). The wet level follows the decay (energy kept about
// level as the decay grows: tc dB per decade of the decay), as the references do. Raw values are Reeverb 2's (FL's units).
// RVerb: 2 decay s, 17 pre-delay ms. H-Reverb: 5 reverb time (the tail's, x 1.45 = the measured RT), 16 pre-delay ms,
// 59 dynamics range dB, 249 dynamics on (ducking: [model] a fixed share of the range off the wet level, fitted on X4).
// TrueVerb: 0 decay s, 5 distance, 24 pre-delay ms, 29 direct off, 42 reverb level dB, 43 early reflections level dB;
// the direct path: round 190lf measured its exact filter (below, Reeverb 2's hidden direct law; it was a flat −0.7 − 0.135 x distance dB).
struct WavesVerbFit
{
    const char* name;
    int lowCut, highCut, size, diffusion, highDamping, bassMult, crossover, early; // Reeverb 2's raw controls
    double wet0, tcDb, decayScale;
};
constexpr WavesVerbFit wavesVerbFits[] {
    { "RVerb", 19, 96, 13, 100, 29, 233, 2000, 160, 0.54, -8.25, 1.0 },
    { "H-Reverb", 19, 44, 1, 44, 61, 42, 104, 160, 0.60, -7.5, 1.45 },
    { "TrueVerb", 19, 221, 9, 100, 126, 266, 2000, 160, 0.54, -5.25, 1.0 },
};
constexpr double hReverbDuckShare = 0.25, trueVerbEarly = 0.36;
int translateWavesReverb (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    if (slot.kind != EffectKind::reeverb2)
        return 0;
    const auto name = wavesPluginName (state);
    const WavesVerbFit* fit = nullptr;
    for (const auto& f : wavesVerbFits)
        if (name == f.name)
            fit = &f;
    const auto rw = wavesRealWorldParams (state);
    const bool rverb = fit != nullptr && name == "RVerb", hReverb = fit != nullptr && name == "H-Reverb";
    const std::size_t decayAt = rverb ? 2 : hReverb ? 5 : 0, predelayAt = rverb ? 17 : hReverb ? 16 : 24;
    const auto decay = wavesValue (rw, decayAt);
    if (fit == nullptr || ! decay || *decay <= 0.0)
        return 0;
    auto s = reeverb2::fromParams (slot.params);
    s.p[0] = fit->lowCut;
    s.p[1] = fit->highCut;
    s.p[3] = fit->size;
    s.p[4] = fit->diffusion;
    s.p[6] = fit->highDamping;
    s.p[7] = fit->bassMult;
    s.p[8] = fit->crossover;
    s.p[5] = std::clamp ((int) std::lround (*decay * fit->decayScale * 10.0), 1, 200);
    s.p[2] = std::clamp ((int) std::lround (wavesValue (rw, predelayAt).value_or (0.0) * 0.384), 0, 384);
    double levelDb = 0.0, early = 1.0, dry = 0.0;
    bool trueVerbTail = false; // round 190lf
    if (hReverb && wavesValue (rw, 249).value_or (0.0) >= 0.5)
        levelDb += hReverbDuckShare * wavesValue (rw, 59).value_or (0.0);
    if (! rverb && ! hReverb) // TrueVerb
    {
        levelDb += wavesValue (rw, 42).value_or (0.0);
        early = trueVerbEarly * std::pow (10.0, wavesValue (rw, 43).value_or (0.0) / 20.0);
        if (wavesValue (rw, 29).value_or (0.0) < 0.5)
        {
            // round 190lf [EXEC] (impulse responses of refcap182's four TrueVerb states with the direct sound on, REA_rodada190lf_verb
            // §2): the direct sound is a·x − b·Σ ρ^(k−1)·x[n−k], exactly (a first-order low shelf: −4.3 … −5.6 dB at DC, −0.6 dB at
            // the top), its depth growing with Distance d (m): a = 0.9002 − 0.00335 d, b = 0.0630 + 0.00267 d, ρ = 0.6948 +
            // 0.00260 d (fitted over 2.6 … 13 m; ±0.009 on a). Reeverb 2's own (flat) dry stays at 0
            const auto d = std::clamp (wavesValue (rw, 5).value_or (0.0), 0.0, 40.0);
            slot.params[(std::size_t) reeverb2DirectLaw] = 1.0f;
            slot.params[(std::size_t) reeverb2DirectLaw + 1] = (float) std::max (0.0, 0.90024 - 0.003351 * d);
            slot.params[(std::size_t) reeverb2DirectLaw + 2] = (float) std::max (0.0, 0.06297 + 0.002666 * d);
            slot.params[(std::size_t) reeverb2DirectLaw + 3] = (float) std::clamp (0.69484 + 0.002601 * d, 0.0, 0.95);
            // and its tail's decay [EXEC] (the same impulse responses after the direct sound: RT60 at 1 kHz 0.84 … 1.03 × the
            // Decay knob, ×1.35 … 1.6 at 125 Hz, ×0.65 … 0.75 at 8 kHz; Reeverb 2 rings 1.68 s per 1 s of its decay control at a
            // neutral bass multiplier): decay control 0.54 × the knob, bass ×1.30, high damping 120, the wet 3 dB up to keep its
            // energy (chosen on the prog182 table, D / X1 / X2 / X3). With the direct sound off the round-190js tail stays (its
            // tone, not refitted to this decay, sets that table's spectrum: X4 would drop 3.8 → 3.2)
            trueVerbTail = true;
            s.p[5] = std::clamp ((int) std::lround (*decay * 5.4), 1, 200);
            s.p[6] = 120;
            s.p[7] = 130;
            s.p[8] = 2000;
        }
    }
    else
        early = (double) fit->early / 160.0;
    const double wet = std::clamp (fit->wet0 * std::pow (10.0, (fit->tcDb * std::log10 (*decay / 1.5) + levelDb) / 20.0), 0.0, 1.0);
    s.p[12] = (int) std::lround (wet * 160.0);
    if (trueVerbTail) // +3 dB on Reeverb 2's wet level law, gain = (e^(2.3979 · raw / 256) − 1) / 10
    {
        const auto g = (std::exp (2.39789534 * s.p[12] / 256.0) - 1.0) * 0.1 * std::pow (10.0, 3.0 / 20.0);
        s.p[12] = std::clamp ((int) std::lround (256.0 * std::log (g / 0.1 + 1.0) / 2.39789534), 0, 160);
    }
    s.p[11] = (int) std::lround (std::min (1.0, early) * 160.0);
    s.p[10] = (int) std::lround (dry * 160.0);
    reeverb2::toParams (s, slot.params);
    slot.mix = 1.0f; // the dry path is Reeverb's own (TrueVerb's direct sound); the slot plays the whole output
    return 10;
}

// DeEsser (15 values): 0 frequency Hz, 2 threshold dB, 10 monitor (1 = side chain), 13 side chain (1 = high pass,
// 2 = band pass), 14 audio (0 = split, 1 = wideband). Round 123: Hush's "classic split" voicing is the de-esser measured
// black-box on the official trial (core/VoiceMatch.h; REA_rodada123 §2), so the controls map one to one.
int translateWavesDeEsser (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace h = mixsuite::hush;
    const auto rw = wavesRealWorldParams (state);
    const auto hz = wavesValue (rw, 0);
    if (slot.kind != EffectKind::hushDeEsser || ! hz)
        return 0;
    int n = 1;
    slot.params[h::voicing] = 1.0f;
    slot.params[h::frequency] = logNorm (*hz, 2000.0, 16000.0);
    slot.params[h::output] = 0.5f;
    if (const auto t = wavesValue (rw, 2))
        slot.params[h::threshold] = linNorm (*t, -80.0, 0.0), ++n;
    if (const auto m = wavesValue (rw, 14))
        slot.params[h::mode] = *m >= 0.5 ? 1.0f : 0.0f, ++n;
    if (const auto d = wavesValue (rw, 13))
        // round 165 [EXEC]: the 17.1 trial stores HighPass 1 / BandPass 0 (band-pass states were read as high-pass); 2 =
        // band-pass as before
        slot.params[h::detector] = (*d < 0.5 || *d >= 1.5) ? 1.0f : 0.0f, ++n;
    if (const auto l = wavesValue (rw, 10))
        slot.params[h::listen] = *l >= 0.5 ? 1.0f : 0.0f, ++n;
    return n;
}

// CLA-2A (208 values): 38 gain, 74 peak reduction, 112 comp (0) / limit, 153 Hi Freq, 206 trim dB. Round 123: Glow
// Leveler's "opto classic" voicing is the leveler measured black-box on the official trial (core/VoiceMatch.h;
// REA_rodada123 §4), where the dials read 0 .. 100 like the original's, so they map one to one.
int translateWavesCla2a (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace g = mixsuite::glow;
    const auto rw = wavesRealWorldParams (state);
    const auto gain = wavesValue (rw, 38), reduction = wavesValue (rw, 74);
    if (slot.kind != EffectKind::glowLeveler || ! gain || ! reduction)
        return 0;
    slot.params[g::voicing] = 1.0f;
    slot.params[g::gain] = (float) std::clamp (*gain / 100.0, 0.0, 1.0);
    slot.params[g::peakReduction] = (float) std::clamp (*reduction / 100.0, 0.0, 1.0);
    slot.params[g::mode] = wavesValue (rw, 112).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    slot.params[g::emphasis] = (float) std::clamp (wavesValue (rw, 153).value_or (50.0) / 100.0, 0.0, 1.0);
    slot.params[g::trim] = linNorm (wavesValue (rw, 206).value_or (0.0), -18.0, 18.0);
    // round 152 [EXEC] (REA_rodada152a §2, read back through the trial's own parameters): 204 Mix %, 207 Auto Makeup,
    // 140 Analog (0 = 50 Hz, 1 = 60 Hz, 2 = off)
    slot.params[g::mix] = (float) std::clamp (wavesValue (rw, 204).value_or (100.0) / 100.0, 0.0, 1.0);
    slot.params[g::autoMakeup] = wavesValue (rw, 207).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    const auto analog = std::lround (wavesValue (rw, 140).value_or (2.0));
    slot.params[g::analog] = analog == 0 ? 0.5f : analog == 1 ? 1.0f : 0.0f;
    slot.params[g::optoLaw] = 1.0f;
    slot.params[g::releaseLaw] = 1.0f; // round 165w: the measured release
    // round 165x [EXEC] (REA_rodada165x §1): the trial's AUTO MAKEUP turns its own Gain knob (the saved Gain, index 38,
    // already holds the make-up: 40 → 53.79 after Auto Makeup on at 46 and the dial to 80; the host's Gain parameter
    // still reads 40), so the loaded dial is the make-up's reference (round 152 added the make-up again from 46)
    slot.params[g::makeupRef] = slot.params[g::peakReduction];
    // round 165x [EXEC]: 17 = VU DISPLAY (0 In, 1 GR, 2 Out; read back through the trial's own parameter)
    const auto vu = std::lround (wavesValue (rw, 17).value_or (1.0));
    slot.params[g::vuDisplay] = vu == 0 ? 0.5f : vu == 2 ? 1.0f : 0.0f;
    // round 190lf [EXEC] (REA_rodada190lf_dyn §1): the cell law — LYNX's compact opto behaviour model, fitted on our
    // demo-mode probes of the reference (stairs at eight peak reduction dials, Compress and Limit; bursts of 50 ms … 8 s;
    // 60 Hz / 200 Hz / 1 kHz / 5 kHz stairs at Hi Freq 0 / 20 / 50 / 100; a multitone under reduction for the tilt)
    slot.params[g::cellLaw] = 1.0f;
    return 8;
}

// CLA-76 (106 values): 0 / 1 Input / Output as positive dB magnitudes (−0 … −∞ dB), 2 / 3 Attack / Release knobs
// 1 … 7, 4 Comp off, 56 Revision (0 Bluey, 1 Blacky), 59 Ratio (0 = 20, 1 = 12, 2 = 8, 3 = 4, 4 = all buttons),
// 103 Mix %, 104 Trim dB. [EXEC] Read back through the trial's own parameter API (REA_rodada127 §3); 4 / 56 / 104 by
// the bundle's parameter list (REA_rodada190ky §2).
// Rounds 127 / 148 / 190jh fitted Crest's Bus style around the reference's one factory state; round 190ky replaces that
// with Crest's FET law (DynamicsSuite.h fetLaw: LYNX's own model of the structure REA showed in the reference's DSP,
// every constant fitted on our demo-mode probes, waves190ky/lab: fit76s.py on 1 kHz stairs at 5 ratio settings × 3 input
// levels and the Input sweep, fit76t.py on level steps and release probes per knob, fit76io.py on the Input / Output
// sweeps). The voicing carries the second term and the followers; the knobs here carry the rest:
//   - Input and Output are attenuators whose real gain departs from their dB scale (the Output knob far from it: −30 →
//     −24 dB is +9.7 dB, −6 → 0 dB +0.9 dB): our measured gains, interpolated; Input lowers the detector's threshold,
//     and both plus the voicing's small-signal gain are the compressed path's make-up;
//   - attack / release (1 … 7, 7 = fastest): the slow follower's attack and half the fast follower's release, geometric
//     over the knob; two followers fitted per knob on the level steps and release probes at 4:1 (fit76r.py: 20 / 12 /
//     8), jointly on the probes and refined on refcap182's 4:1 programme (fit76j.py, fit76p.py: 4) and jointly on the
//     all-buttons probes (fit76jA.py); the voicing holds the other follower's multiples and the weights;
//   - the mix (linear, as measured in round 127) and the trim: Crest's mix and output (consoleMix), the slot at 100 %.
struct Cla76Fit { double t1, w1, s1, plateauDb; };
struct Cla76Times { double attack1, attack7, release1, release7; }; // ms at knob 1 and knob 7
// fit76:begin (CLA76FITS: lab/gen76.py)
constexpr Cla76Fit cla76Fits[10] {
    { -20.0555, 0.1526, 11.9590, -4.94 }, // Blacky 20 (law at -40 dBFS: -0.00 dB)
    { -21.6982, 0.1045, 8.5697, -4.94 }, // Blacky 12 (law at -40 dBFS: -0.00 dB)
    { -22.7854, 0.1099, 5.5674, -4.94 }, // Blacky 8 (law at -40 dBFS: -0.00 dB)
    { -24.7504, 0.1128, 3.6843, -4.94 }, // Blacky 4 (law at -40 dBFS: 0.00 dB)
    { -23.2310, 0.1762, 10.9191, -3.53 }, // Blacky A (law at -40 dBFS: -0.00 dB)
    { -24.9258, 0.2195, 18.2726, -1.71 }, // Bluey 20 (law at -40 dBFS: -0.00 dB)
    { -26.5097, 0.2127, 11.3739, -1.71 }, // Bluey 12 (law at -40 dBFS: -0.00 dB)
    { -28.3762, 0.1074, 6.5792, -1.71 }, // Bluey 8 (law at -40 dBFS: -0.00 dB)
    { -31.3386, 0.1073, 3.5943, -1.71 }, // Bluey 4 (law at -40 dBFS: 0.00 dB)
    { -27.7634, 0.1384, 11.2319, -1.06 }, // Bluey A (law at -40 dBFS: -0.00 dB)
};
constexpr Cla76Times cla76Times[4] { // the slow follower's attack and half the fast follower's release (ms) at knob 1 / 7
    { 11.8931, 5.4866, 247.8996, 30.3698 }, // 20 / 12 / 8 (fit76r.py)
    { 29.6727, 0.6607, 1039.0365, 132.2632 }, // 4 (fit76j.py, fit76p.py)
    { 5.2967, 0.1560, 1847.6547, 33.0881 }, // all buttons (fit76jA.py)
    // round 190lf [EXEC] (REA_rodada190lf_dyn §2): Bluey 20 / 12 / 8 — the release at knob 7 fitted through the shipped
    // voicings on Bluey 12:1 probes (waves2_190lf/dyn/lab/fit76x.py), knob 1 refined on refcap182's Bluey 12:1 programme
    // (X4, lab/x4prog.py: the probe fit's 2.1 s at knob 1 over-reduced it), the attack as the row above
    { 11.8931, 5.4866, 795.0, 12.6423 },
};
constexpr double cla76RangeDb = 40.0;
constexpr int cla76InPoints = 14, cla76OutPoints = 12;
constexpr double cla76InLabel[cla76InPoints] { -48.0, -42.0, -36.0, -33.0, -30.0, -27.0, -24.0, -21.0, -18.0, -15.0, -12.0, -9.0, -6.0, 0.0 };
constexpr double cla76InGain[cla76InPoints] { -22.10, -17.44, -13.53, -10.06, -5.95, -2.62, 0.00, 4.29, 6.85, 9.21, 12.21, 15.81, 18.81, 24.81 }; // dB from the −24 dB mark (−6 / 0: extrapolated)
constexpr double cla76OutLabel[cla76OutPoints] { -48.0, -36.0, -33.0, -30.0, -27.0, -24.0, -21.0, -18.0, -15.0, -12.0, -6.0, 0.0 };
constexpr double cla76OutGain[cla76OutPoints] { -29.68, -14.69, -6.62, 0.00, 5.56, 9.66, 13.27, 17.07, 20.96, 24.17, 28.57, 29.47 }; // dB from the −30 dB mark
// fit76:end
double cla76Interp (double x, const double* xs, const double* ys, int n)
{
    if (x <= xs[0])
        return ys[0] + (x - xs[0]) * (ys[1] - ys[0]) / (xs[1] - xs[0]);
    for (int i = 1; i < n; ++i)
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[n - 1];
}
int translateWavesCla76 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace c = dyn::crest;
    const auto rw = wavesRealWorldParams (state);
    const auto in = wavesValue (rw, 0), out = wavesValue (rw, 1);
    if (slot.kind != EffectKind::crestCompressor || ! in || ! out)
        return 0;
    const auto at = [&] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto inDb = -std::clamp (*in, 0.0, 60.0), outDb = -std::clamp (*out, 0.0, 60.0);
    const auto attackKnob = std::clamp (at (2, 3.0), 1.0, 7.0), releaseKnob = std::clamp (at (3, 4.0), 1.0, 7.0);
    const int ratioIndex = std::clamp ((int) std::lround (at (59, 3.0)), 0, 4);
    const bool bluey = at (56, 1.0) < 0.5, off = at (4, 0.0) >= 0.5;
    const int voice = (bluey ? 5 : 0) + ratioIndex;
    const auto& f = cla76Fits[voice];
    auto& p = slot.params;
    p[c::style] = c::styleValue (c::Style::bus);
    p[c::voicing] = 0.0f;
    p[c::busLaw] = 0.0f;
    p[c::fetLaw] = c::fetLawValue (voice);
    p[c::ratio] = c::ratioValue (1.0 + f.s1);
    p[c::knee] = linNorm (f.w1, 0.0, 72.0);
    p[c::range] = off ? 0.0f : linNorm (cla76RangeDb, 0.0, 60.0);
    // the knobs' times, geometric between knob 1 and knob 7 (20 / 12 / 8, 4 and all buttons each their own)
    const auto& t = cla76Times[ratioIndex == 4 ? 2 : ratioIndex == 3 ? 1 : bluey ? 3 : 0]; // round 190lf: Bluey 20 / 12 / 8 their own
    p[c::attack] = c::attackValue (t.attack7 * std::pow (t.attack1 / t.attack7, (7.0 - attackKnob) / 6.0));
    p[c::release] = c::releaseValue (t.release7 * std::pow (t.release1 / t.release7, (7.0 - releaseKnob) / 6.0));
    p[c::autoRelease] = 0.0f;
    p[c::hold] = p[c::lookahead] = p[c::maxLookahead] = 0.0f;
    p[c::character] = 0.0f;
    p[c::oversampling] = 0.0f;
    p[c::autoGain] = 0.0f;
    p[c::autoThreshold] = 0.0f;
    p[c::stereoLink] = 1.0f; // [EXEC] linked: a loud left channel reduces the right as much (d76_L_r4)
    p[c::linkMode] = c::linkModeValue (c::LinkMode::stereo);
    for (int b = 0; b < c::scBands; ++b)
        p[c::scParam (b, 0)] = facet::shapeValue (facet::Shape::off);
    // the Input knob's gain moves the detector's threshold (the dry path stays the plain input); its gain, the Output
    // knob's and the voicing's small-signal gain act on the compressed path only, blended with the dry input at the mix
    // and trimmed (Crest's mix and output carry it, as for SSLComp)
    const auto inGain = cla76Interp (inDb, cla76InLabel, cla76InGain, cla76InPoints);
    const auto outGain = cla76Interp (outDb, cla76OutLabel, cla76OutGain, cla76OutPoints);
    p[c::threshold] = linNorm (f.t1 - inGain, -60.0, 0.0);
    consoleMix (p, at (103, 100.0) / 100.0, inGain + outGain + f.plateauDb, at (104, 0.0));
    // round 190mg (REA_rodada190mg_dyn §3): the output amplifier's harmonics were measured and modelled (Crest's hidden
    // fetAmp), but not set here: on refcap182 it cost two files a step (X1_bass 8 → 7, X4_vocal 6 → 5, ESR 0.2 … 0.8 dB
    // worse). Auto make-up (105): no steady change measured (only a start-up ramp after reset), so it is not translated.
    slot.mix = 1.0f;
    return 13;
}

// round tp172c5: Harrison 32Classic Channel Strip and Harrison MPC Channel Strip -> Ridge Strip's own engine (brown EQ
// curve), their states read black-box from the official trials. The 32C's state is the SSL_PLUGIN_STATE XML (its own
// ids, the original's units); the MPC's a flat <HarrisonMPCChannelStrip> element of PARAMs. The laws below are fits
// of Ridge's own bands / filters / dynamics to the trial's captures (tp172c data: H32_eq, H32_dyn, H32_mk, MPC_eq,
// MPC_dyn): bells keep the dial's frequency and gain with a gain-dependent Q, shelves move to the corner and plateau
// that match the original's gentler curve, the high-pass (12 dB/oct there, 18 here) sits lower.
namespace harrison
{
// piecewise-linear table lookup on x (x ascending), clamped at the ends
double interp (double x, std::initializer_list<double> xs, std::initializer_list<double> ys)
{
    const double* a = xs.begin();
    const double* b = ys.begin();
    const auto n = xs.size();
    if (x <= a[0]) return b[0];
    for (std::size_t i = 1; i < n; ++i)
        if (x <= a[i])
            return b[i - 1] + (b[i] - b[i - 1]) * (x - a[i - 1]) / (a[i] - a[i - 1]);
    return b[n - 1];
}
double lg (double hz) { return std::log (std::max (hz, 1.0)); }

// bells (the MPC's bands on Ridge's own curve): Q 0.5 (Ridge's widest) for small gains up to 0.75 at full gain
double bellQ (double gainDb) { return interp (std::abs (gainDb), { 0.0, 7.0, 13.74 }, { 0.5, 0.55, 0.75 }); }

// (round 190le: the 32C's shelves and filters play in Ridge's 32C voicing since; the corner / plateau fits of tp172c5 that
// played them on the brown curve are gone. The filter laws below still serve the MPC's high- / low-pass shapes.)
// the 32C's filters: high-pass 12 dB/oct (Ridge's 18 dB/oct one a little lower), low-pass 12 dB/oct (as measured)
double hpfHz (double hz) { return hz * std::exp (interp (lg (hz), { lg (30.0), lg (250.0) }, { std::log (0.93), std::log (0.70) })); }
double lpfHz (double hz)
{
    return std::exp (interp (lg (hz), { lg (187.6), lg (1607.0), lg (7321.0), lg (20000.0) }, { lg (188.0), lg (1659.0), lg (10063.0), lg (19268.0) }));
}
// the 32C's auto make-up: half the gain reduction a steady signal at 0 dBFS would get (measured: thr -10 .. -36, 2 .. 20 : 1)
double autoMakeupDb (double thresholdDb, double ratio) { return std::max (0.0, -thresholdDb) * (1.0 - 1.0 / std::max (1.0, ratio)) * 0.5; }
} // namespace harrison

// every <PARAM id="…" value="…"/> from `from` to `to` (std::string::npos = the end) in the original's units
std::map<std::string, double> xmlParams (const std::string& text, std::size_t from, std::size_t to)
{
    std::map<std::string, double> v;
    for (auto q = text.find ("<PARAM ", from); q != std::string::npos && q < to; q = text.find ("<PARAM ", q + 7))
    {
        const auto id = attributeIn (text, q, "id");
        float value = 0;
        if (! id.empty() && parseFloat (attributeIn (text, q, "value"), value) && std::isfinite (value))
            v[id] = value;
    }
    return v;
}

int translateHarrisonStrip (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace r = mixsuite::ridge;
    if (slot.kind != EffectKind::ridgeStrip)
        return 0;
    const auto c32 = findBytes (state, "<SSL_PLUGIN_STATE"), mpc = findBytes (state, "<HarrisonMPCChannelStrip");
    if (c32 == std::string::npos && mpc == std::string::npos)
        return 0;
    const std::string text (state.begin() + (std::ptrdiff_t) std::min (c32, mpc), state.end());
    std::map<std::string, double> v;
    if (c32 < mpc)
    {
        if (attributeIn (text, 0, "PluginName").find ("Harrison 32") == std::string::npos)
            return 0;
        const bool useB = attributeIn (text, 0, "StateASelected") == "0";
        const auto block = text.find (useB ? "<B" : "<A");
        if (block == std::string::npos)
            return 0;
        v = xmlParams (text, block, text.find (useB ? "</B>" : "</A>", block));
    }
    else
        v = xmlParams (text, 0, std::string::npos);
    if (v.size() < 10)
        return 0;
    auto at = [&v] (const char* id, double fallback) {
        const auto it = v.find (id);
        return it != v.end() ? it->second : fallback;
    };
    auto sw = [&at] (const char* id, bool fallback) { return at (id, fallback ? 1.0 : 0.0) >= 0.5; };
    auto& p = slot.params;
    // Ridge's own engine, brown curve, its own auto make-up off (the originals' make-up goes into the output trim)
    p[r::modern] = p[r::console4k] = p[r::voicing] = p[r::vocal] = 0.0f;
    p[r::curve] = 0.0f;
    p[r::analog] = 0.0f;
    p[r::autoMakeup] = 0.0f;
    p[r::eqToSidechain] = p[r::filtersToSidechain] = 0.0f;
    auto gainV = [] (double db) { return linNorm (db, -15.0, 15.0); };
    auto setBell = [&] (int gain, int freq, int q, double hz, double db, double lo, double hi) {
        p[(std::size_t) gain] = gainV (db);
        p[(std::size_t) freq] = logNorm (hz, lo, hi);
        if (q >= 0)
            p[(std::size_t) q] = logNorm (harrison::bellQ (db), 0.5, 2.5);
    };
    auto ratioV = [] (double ratio) {
        return ratio <= 1.0005 ? 0.0f : (float) std::sqrt (std::clamp ((ratio - 1.0) / 19.0, 0.0, 0.996));
    };
    double outDb = 0.0;
    if (c32 < mpc)
    {
        // ---- 32Classic: 4-band EQ (Hi / Hi-Mid / Low-Mid / Low; shelf or bell at the ends), filters, compressor
        // (Leveler / Compressor / Limiter), gate / expander, saturator (Ridge's DRIVE section since tp172c6), trims
        // round 190le: the EQ goes to Ridge's 32C voicing (the original's measured EQ law, its own dials: the saved
        // values in the original's units map to its dial positions; the end bands' frequency and gain are saved as the
        // Bell mode's, the shelf's centre and plateau follow the dial). Before, Ridge's brown curve played it (tp172c5:
        // fitted corners and plateaus, mids moved to a neighbour band outside Ridge's ranges, an 18 dB/oct high-pass
        // stopping at 350 Hz)
        p[r::h32] = 1.0f;
        const bool filtersIn = sw ("equalizer_in", true) && sw ("equalizer_filters_in", true);
        p[r::hpf] = filtersIn ? r::h32HpfValue (at ("equalizer_highpass_filter_freq", 25.0)) : 0.0f;
        p[r::lpf] = filtersIn ? r::h32LpfValue (at ("equalizer_lowpass_filter_freq", 20000.0)) : 0.0f;
        p[r::eqOn] = sw ("equalizer_in", true) && sw ("equalizer_eq_in", true) ? 1.0f : 0.0f;
        p[r::hfBell] = sw ("equalizer_hi_bell", false) ? 1.0f : 0.0f;
        p[r::hfFreq] = r::h32HfValue (at ("equalizer_band_freq_1", 3618.97));
        p[r::hfGain] = r::h32GainValue (at ("equalizer_band_gain_1", 0.0));
        p[r::hmfFreq] = r::h32HmfValue (at ("equalizer_band_freq_2", 1421.74));
        p[r::hmfGain] = r::h32GainValue (at ("equalizer_band_gain_2", 0.0));
        p[r::lmfFreq] = r::h32LmfValue (at ("equalizer_band_freq_3", 608.74));
        p[r::lmfGain] = r::h32GainValue (at ("equalizer_band_gain_3", 0.0));
        p[r::lfBell] = sw ("equalizer_lo_bell", false) ? 1.0f : 0.0f;
        p[r::lfFreq] = r::h32LfValue (at ("equalizer_band_freq_4", 111.355));
        p[r::lfGain] = r::h32GainValue (at ("equalizer_band_gain_4", 0.0));
        // compressor: threshold in dBFS, ratio 1 .. 100 : 1. On the stair (sines) its static line meets the input at
        // the dial - 2 dB RMS (the Limiter's ~1.8 dB lower); Ridge's detector reads the sine's peak (+3 dB), so its
        // threshold sits at the dial + 1. Under Ridge's -40 dBFS floor the rest moves into the input trim (taken back
        // at the output, the gate's threshold follows). The Leveler showed no static reduction (read as off)
        const auto mode = (int) std::lround (at ("compressor_mode", 1.0));
        const bool compIn = sw ("compressor_in", true) && mode != 0;
        const auto ratio = compIn ? at ("compressor_ratio", 10.0) : 1.0, thr = at ("compressor_threshold", 0.0);
        const auto ridgeThr = thr + 1.0 - (mode == 2 ? 1.8 : 0.0);
        const auto inDb = at ("input_trim", 0.0);
        const auto lift = compIn ? std::clamp (-40.0 - ridgeThr, 0.0, std::max (0.0, 20.0 - inDb)) : 0.0;
        p[r::compRatio] = ratioV (ratio);
        p[r::compThreshold] = linNorm (ridgeThr + lift, -40.0, 0.0);
        // tp172c11 (H32_dyn stairs and bursts, grid-scored): the reduction's release time constant is far longer than the
        // dial (0.25 / 0.35 / 1.6 s at 50 / 104 / 351.5 ms, log-log between them), half that in Limiter mode; the attack
        // is the dial itself, Limiter mode ~2.5 ms
        const auto relDial = std::clamp (at ("compressor_release", 50.0), 1.0, 5000.0);
        const auto relTau = relDial < 104.0 ? 0.25 * std::pow (0.35 / 0.25, std::log (relDial / 50.0) / std::log (104.0 / 50.0))
                                            : 0.35 * std::pow (1.6 / 0.35, std::log (relDial / 104.0) / std::log (351.5 / 104.0));
        p[r::compRelease] = logNorm (mode == 2 ? 0.5 * relTau : relTau, 0.1, 4.0);
        p[r::compFastAttack] = at ("compressor_attack", 20.0) < 3.0 ? 1.0f : 0.0f;
        p[r::compAttackFine] = compIn ? (float) (std::clamp (mode == 2 ? 2.5 : at ("compressor_attack", 20.0), 0.1, 250.0) / 250.0) : 0.0f;
        p[r::compMix] = (float) std::clamp (at ("CompMix", 100.0) / 100.0, 0.0, 1.0);
        // tp172c10: its reduction stops at exactly 27.5 dB (H32_dyn stairs, Compressor and Limiter at low thresholds)
        p[r::compGrCap] = compIn ? (float) (27.5 / 60.0) : 0.0f;
        if (compIn)
            outDb += at ("compressor_makeup_gain", 0.0) + (sw ("CompAutoMakeUp", true) ? harrison::autoMakeupDb (thr, ratio) : 0.0);
        // gate (gate_mode 1 = the 1:2 expander), depth = the range
        const bool gateIn = sw ("gate_in", true);
        p[r::gateRange] = gateIn ? linNorm (at ("gate_depth", 0.0), 0.0, 60.0) : 0.0f;
        p[r::gateThreshold] = linNorm (at ("gate_threshold", 0.0) + lift, -80.0, 0.0);
        p[r::gateRelease] = logNorm (at ("gate_release", 100.0) / 1000.0, 0.1, 4.0);
        p[r::gateFastAttack] = at ("gate_attack", 1.0) < 0.5 ? 1.0f : 0.0f;
        p[r::expander] = at ("gate_mode", 0.0) >= 0.5 ? 1.0f : 0.0f;
        p[r::dynOn] = sw ("DynamicsIn", true) ? 1.0f : 0.0f;
        p[r::order] = 0.0f;
        p[r::phase] = sw ("polarity", false) ? 1.0f : 0.0f;
        p[r::input] = linNorm (inDb + lift, -20.0, 20.0);
        outDb += at ("output_trim", 0.0) + std::max (at ("FaderLevel", 0.0), -20.0) - lift;
        // round tp172c6: its saturator -> Ridge's DRIVE section, Saturate model (the dial -24 .. +24 dB, as measured);
        // before the output trim / fader, as Ridge's drive sits
        p[r::driveOn] = sw ("saturator_in", false) ? 1.0f : 0.0f;
        p[r::driveModel] = r::driveModelValue (r::driveSaturate);
        p[r::driveAmount] = r::driveAmountValue (r::driveSaturate, at ("saturator_drive", 0.0));
        p[r::driveInput] = p[r::driveTrim] = 0.5f;
        p[r::driveMix] = 1.0f;
        if (sw ("bypass", false))
            slot.enabled = false;
    }
    else
    {
        // ---- MPC: 8 bands (shape 1/2 low-pass 12/24, 3/4 high-pass 12/24, 5 low shelf, 6 high shelf, 7 bell, 8 notch,
        // 9 band-pass) onto Ridge's 4: the active bells / shelves with gain, the largest four (shelves on LF / HF, the
        // bells on the free slots by frequency); the high- / low-pass shapes -> Ridge's filters. Its bell's Q reads as the cookbook's
        // Q x 6.35 / |gain| (measured at 9.6 .. 19.2 dB); shelves: corner x 1.4 (low) / x 0.71 (high), slope 1.
        // Compressor -> Ridge's (threshold dBFS, ratio, release, make-up); its de-esser and denoiser are not reproduced
        struct Band { double hz, db, q; int shape; };
        std::vector<Band> bands;
        double hpHz = 0.0, lpHz = 0.0;
        const bool eqIn = sw ("mpc_equalizerin", true);
        for (int b = 1; b <= 8 && eqIn; ++b)
        {
            const auto key = [b] (const char* n) { return std::string (n) + std::to_string (b); };
            if (at (key ("mpceq_bandIn").c_str(), 1.0) < 0.5)
                continue;
            const auto hz = at (key ("mpceq_bandFreq").c_str(), 1000.0), db = at (key ("mpceq_bandGain").c_str(), 0.0);
            const auto shape = (int) std::lround (at (key ("mpceq_bandShape").c_str(), 7.0));
            // the 12 dB/oct shapes take the 32C's filter laws (same family), the 24 dB/oct ones the dial
            if (shape == 3 || shape == 4) hpHz = std::max (hpHz, shape == 3 ? harrison::hpfHz (hz) : hz);
            else if (shape == 1 || shape == 2)
            {
                const auto lz = shape == 1 ? harrison::lpfHz (hz) : hz;
                lpHz = lpHz > 0.0 ? std::min (lpHz, lz) : lz;
            }
            else if ((shape >= 5 && shape <= 7) && std::abs (db) > 0.05)
                bands.push_back ({ hz, db, at (key ("mpceq_bandQ").c_str(), 3.0), shape });
        }
        std::sort (bands.begin(), bands.end(), [] (const Band& a, const Band& b) { return std::abs (a.db) > std::abs (b.db); });
        if (bands.size() > 4)
            bands.resize (4);
        std::sort (bands.begin(), bands.end(), [] (const Band& a, const Band& b) { return a.hz < b.hz; });
        auto rbjQ = [] (const Band& b) { return b.q * std::min (1.0, 6.35 / std::max (1e-3, std::abs (b.db))); };
        p[r::hpf] = hpHz > 0.0 ? std::max (1e-4f, logNorm (hpHz, 16.0, 350.0)) : 0.0f;
        p[r::lpf] = lpHz > 0.0 ? std::max (1e-4f, logNorm (lpHz, 22000.0, 3000.0)) : 0.0f;
        for (int g : { r::lfGain, r::lmfGain, r::hmfGain, r::hfGain })
            p[(std::size_t) g] = 0.5f;
        // slots: a low shelf takes LF, a high shelf HF (Ridge's only shelves); the bells (and a second shelf of a
        // kind, played as a bell) take the free slots, the mids first (they have a Q), in frequency order
        int slotOf[4] = { -1, -1, -1, -1 };
        bool taken[4] = { false, false, false, false };
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            const int want = bands[i].shape == 5 ? 0 : bands[i].shape == 6 ? 3 : -1;
            if (want >= 0 && ! taken[want]) { taken[want] = true; slotOf[i] = want; }
        }
        std::vector<int> freeSlots;
        for (int sl : { 1, 2, 0, 3 })
            if (! taken[sl]) freeSlots.push_back (sl);
        std::size_t rest = 0;
        for (std::size_t i = 0; i < bands.size(); ++i)
            rest += slotOf[i] < 0 ? 1 : 0;
        freeSlots.resize (std::min (freeSlots.size(), rest));
        std::sort (freeSlots.begin(), freeSlots.end());
        for (std::size_t i = 0, k = 0; i < bands.size(); ++i)   // bands are in frequency order
            if (slotOf[i] < 0 && k < freeSlots.size()) slotOf[i] = freeSlots[k++];
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            const auto& b = bands[i];
            const bool shelf = (b.shape == 5 && slotOf[i] == 0) || (b.shape == 6 && slotOf[i] == 3);
            const auto shelfHz = b.shape == 5 ? b.hz * 1.4 : b.hz * 0.71;
            switch (slotOf[i])
            {
                case 0:
                    p[r::lfBell] = shelf ? 0.0f : 1.0f;
                    setBell (r::lfGain, r::lfFreq, -1, shelf ? shelfHz : b.hz, b.db, 30.0, 450.0);
                    break;
                case 1:
                    setBell (r::lmfGain, r::lmfFreq, -1, b.hz, b.db, 200.0, 2500.0);
                    p[r::lmfQ] = logNorm (rbjQ (b), 0.5, 2.5);
                    break;
                case 2:
                    setBell (r::hmfGain, r::hmfFreq, -1, b.hz, b.db, 600.0, 7000.0);
                    p[r::hmfQ] = logNorm (rbjQ (b), 0.5, 2.5);
                    break;
                case 3:
                    p[r::hfBell] = shelf ? 0.0f : 1.0f;
                    setBell (r::hfGain, r::hfFreq, -1, shelf ? shelfHz : b.hz, b.db, 1500.0, 16000.0);
                    break;
                default:
                    break;
            }
        }
        p[r::eqOn] = eqIn ? 1.0f : 0.0f;
        // its compressor on the stair: the static line meets the input 1.5 .. 3.6 dB under the dial (more at high
        // ratios); a wide knee (dial 1 .. 30 dB, 3 by default) reads as a gentler ratio from a higher threshold
        // (15.5 dB: ~55 % of the slope, ~4 dB up). Ridge's detector reads the sine's peak (+3 dB); under its
        // -40 dBFS floor the rest moves into the input trim, taken back at the output
        const bool compIn = sw ("mpc_compressorin", true);
        const auto dialRatio = compIn ? std::max (1.0, at ("mpccomp_ratio", 1.5)) : 1.0;
        const auto knee = std::max (0.0, at ("mpccomp_knee", 3.0) - 3.0);
        const auto slope = (1.0 - 1.0 / dialRatio) * (1.0 - 0.45 * std::min (1.0, knee / 12.5));
        const auto ratio = 1.0 / std::max (1e-3, 1.0 - slope);
        const auto ridgeThr = at ("mpccomp_threshold", -10.0) + 3.0 - 1.5 - 2.2 * (1.0 - 1.0 / dialRatio) + 0.3 * knee;
        const auto trimDbIn = at ("mpc_trim", 0.0);
        const auto lift = compIn ? std::clamp (-40.0 - ridgeThr, 0.0, std::max (0.0, 20.0 - trimDbIn)) : 0.0;
        p[r::compRatio] = ratioV (ratio);
        p[r::compThreshold] = linNorm (ridgeThr + lift, -40.0, 0.0);
        p[r::compRelease] = logNorm (at ("mpccomp_release", 100.0) / 1000.0, 0.1, 4.0);
        p[r::compFastAttack] = at ("mpccomp_attack", 5.0) < 3.0 ? 1.0f : 0.0f;
        p[r::compMix] = 1.0f;
        if (compIn)
            outDb += at ("mpccomp_makeup", 0.0);
        p[r::gateRange] = 0.0f;
        p[r::dynOn] = 1.0f;
        p[r::order] = 0.0f;
        p[r::phase] = sw ("mpc_polarity", false) ? 1.0f : 0.0f;
        p[r::input] = linNorm (trimDbIn + lift, -20.0, 20.0);
        outDb -= lift;
        if (sw ("bypass", false))
            slot.enabled = false;
    }
    p[r::output] = linNorm (outDb, -20.0, 20.0);
    return (int) v.size();
}

// round tp172c6: Slate VCC Channel (its state: a <package> archive, a Common section and Slot0 / Slot1 sections of
// normalised entries; the "Current slot" plays). Round tp172c7: onto Kiln Channel, LYNX's console channel (it was
// Ridge's DRIVE section): the console selector (Brit 4k E / Brit 4k G / US A / Brit N / Y / RC-Tube -> Kiln's six
// steps, the same order), the drive 0 .. 18 dB, Input and Output (-18 .. +18 dB), Noise Reduction and Group Bypass (its
// switches store 0.25 = off, 0.75 = on), the Group in Common (None, 1 .. 8 over nine equal bins) and the main gains
// (StandaloneInputGain / StandaloneOutputGain, -24 .. +24 dB); its GROUP I/O and LINK I/O lamps (IOGroup / IOLink, Common)
int translateVccChannel (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace k = mixsuite::kiln;
    const auto root = findBytes (state, "<package");
    if (root == std::string::npos || slot.kind != EffectKind::kilnChannel || findBytes (state, "VccC-state") == std::string::npos)
        return 0;
    const std::string text (state.begin() + (std::ptrdiff_t) root, state.end());
    auto entries = [&text] (const std::string& section) {
        std::map<std::string, double> v;
        const auto at = text.find ("<section id=\"" + section + "\"");
        if (at == std::string::npos)
            return v;
        const auto end = text.find ("</section>", at);
        for (auto q = text.find ("<entry ", at); q != std::string::npos && q < end; q = text.find ("<entry ", q + 7))
        {
            float value = 0;
            const auto id = attributeIn (text, q, "id");
            if (! id.empty() && parseFloat (attributeIn (text, q, "value"), value) && std::isfinite (value))
                v[id] = value;
        }
        return v;
    };
    const auto common = entries ("Common");
    const auto slotIndex = common.count ("Current slot") ? (int) std::lround (common.at ("Current slot")) : 0;
    auto v = entries ("Slot" + std::to_string (std::clamp (slotIndex, 0, 1)));
    if (v.count ("Console") == 0 || v.count ("Drive") == 0)
        return 0;
    auto at = [&v] (const char* id, double fallback) { const auto it = v.find (id); return it != v.end() ? it->second : fallback; };
    auto& p = slot.params;
    const auto console = std::clamp ((int) std::floor (at ("Console", 0.0) * 6.0), 0, 5);
    p[k::console] = k::consoleValue (console);
    p[k::drive] = (float) std::clamp (at ("Drive", 1.0 / 3.0), 0.0, 1.0);
    p[k::input] = (float) std::clamp (at ("Input", 0.5), 0.0, 1.0);   // both -18 .. +18 dB, linear
    p[k::output] = (float) std::clamp (at ("Output", 0.5), 0.0, 1.0);
    p[k::noiseReduction] = at ("NoiseReduction", 0.75) >= 0.5 ? 1.0f : 0.0f;
    p[k::groupBypass] = at ("GroupBypass", 0.25) >= 0.5 ? 1.0f : 0.0f;
    const auto group = common.count ("Group") ? std::clamp ((int) std::floor (common.at ("Group") * 9.0), 0, 8) : 0;
    p[k::group] = (float) group / 8.0f;
    p[k::mainIn] = (float) std::clamp (at ("StandaloneInputGain", 0.5), 0.0, 1.0);   // both -24 .. +24 dB, linear
    p[k::mainOut] = (float) std::clamp (at ("StandaloneOutputGain", 0.5), 0.0, 1.0);
    p[k::ioLink] = (common.count ("IOLink") ? common.at ("IOLink") : 0.75) >= 0.5 ? 1.0f : 0.0f;   // in Common, 0.25 / 0.75
    p[k::ioGroup] = (common.count ("IOGroup") ? common.at ("IOGroup") : 0.75) >= 0.5 ? 1.0f : 0.0f;
    if (at ("Bypass", 0.0) >= 0.5)
        slot.enabled = false;
    return (int) v.size();
}

// round tp172c6: SSL Guitarstrip (its state: the SSL_PLUGIN_STATE XML, the A or B setting in its own units) -> Ridge's
// engine; round tp172c8: -> Fret Channel, whose controls have the original's own ranges and laws (core/StripSuite.inc):
// every setting reads across one to one (Align Mode 0..2, Compressor Mode 0..4, All-Pass Order / Drive Type 0..1 as
// indices; the filters' 20 = out). Bypass -> the slot off
int translateSslGuitarstrip (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace f = mixsuite::fret;
    const auto root = findBytes (state, "<SSL_PLUGIN_STATE");
    if (root == std::string::npos || slot.kind != EffectKind::fretChannel)
        return 0;
    const std::string text (state.begin() + (std::ptrdiff_t) root, state.end());
    if (attributeIn (text, 0, "PluginName").find ("Guitarstrip") == std::string::npos)
        return 0;
    const bool useB = attributeIn (text, 0, "StateASelected") == "0";
    const auto block = text.find (useB ? "<B" : "<A");
    if (block == std::string::npos)
        return 0;
    const auto v = xmlParams (text, block, text.find (useB ? "</B>" : "</A>", block));
    if (v.size() < 10)
        return 0;
    auto at = [&v] (const char* id, double fallback) { const auto it = v.find (id); return it != v.end() ? it->second : fallback; };
    auto sw = [&at] (const char* id, bool fallback) { return at (id, fallback ? 1.0 : 0.0) >= 0.5 ? 1.0f : 0.0f; };
    auto& p = slot.params;
    p[f::input] = linNorm (at ("Input Gain", 0.0), -24.0, 24.0);
    p[f::output] = linNorm (at ("Output Gain", 0.0), -24.0, 24.0);
    p[f::outputMix] = linNorm (at ("Output Mix", 100.0), 0.0, 100.0);
    p[f::phaseCorrection] = sw ("Phase Correction", true);
    p[f::alignMode] = (float) std::clamp (std::round (at ("Align Mode", 0.0)), 0.0, 2.0) / 2.0f;
    p[f::delayIn] = sw ("Delay In", true);
    p[f::delay] = linNorm (at ("Delay Amount", 0.0), -20.0, 20.0);
    p[f::phaseInvert] = sw ("Phase Invert", false);
    p[f::allPassIn] = sw ("All-Pass In", false);
    p[f::allPassFreq] = logNorm (std::clamp (at ("All-Pass Frequency", 2000.0), 20.0, 20000.0), 20.0, 20000.0);
    p[f::allPassQ] = logNorm (std::clamp (at ("All-Pass Q", 1.0), 0.1, 6.0), 0.1, 6.0);
    p[f::allPassOrder] = sw ("All-Pass Order", true);
    p[f::hpf] = f::hpfValue (at ("High-Pass Filter", 20.0));
    p[f::eqIn] = sw ("EQ", true);
    p[f::eqPrePost] = sw ("EQ Pre-Post", false);
    p[f::lpf] = f::lpfValue (at ("Low-Pass Filter", 20.0));
    p[f::lowGain] = linNorm (at ("Low Gain", 0.0), -12.0, 12.0);
    p[f::midFreq] = f::midValue (std::clamp (at ("Mid Frequency", 1.0), 0.5, 7.0));
    p[f::midGain] = linNorm (at ("Mid Gain", 0.0), -12.0, 12.0);
    p[f::highGain] = linNorm (at ("High Gain", 0.0), -12.0, 12.0);
    p[f::compIn] = sw ("Compressor", true);
    p[f::compThreshold] = linNorm (at ("Compressor Threshold", 0.0), -36.0, 0.0);
    p[f::compMix] = linNorm (at ("Compressor Mix", 100.0), 0.0, 100.0);
    p[f::compAutoGain] = sw ("Compressor Auto Gain", false);
    p[f::compMode] = f::modeValue ((int) std::lround (std::clamp (at ("Compressor Mode", 2.0), 0.0, 4.0)));
    p[f::driveIn] = sw ("Drive", false);
    p[f::driveModel] = sw ("Drive Type", false);
    p[f::preAmp] = linNorm (at ("Guitar Pre-Amp", 0.0), -6.0, 30.0);
    p[f::guitarDrive] = linNorm (at ("Guitar Drive", 0.0), 0.0, 11.0);
    p[f::guitarBoost] = linNorm (at ("Guitar Boost", 0.0), -100.0, 100.0);
    p[f::guitarPunch] = linNorm (at ("Guitar Punch", 0.0), -100.0, 100.0);
    p[f::guitarEdge] = linNorm (at ("Guitar Edge", 0.0), 0.0, 10.0);
    p[f::bassDrive] = linNorm (at ("Bass Drive", 0.0), 0.0, 11.0);
    p[f::bassPunch] = linNorm (at ("Bass Punch", 0.0), -100.0, 100.0);
    p[f::bassShape] = linNorm (at ("Bass Shape", 0.0), 0.0, 10.0);
    p[f::driveMix] = linNorm (at ("Drive Mix", 100.0), 0.0, 100.0);
    p[f::monoListen] = sw ("Mono Listen", false);
    p[f::scListen] = sw ("Side-Chain Listen", false);
    p[f::autoListen] = sw ("EQ Auto Listen", false);
    if (sw ("Bypass", false) >= 0.5f)
        slot.enabled = false;
    return (int) v.size();
}

// round tp172c6: SSL Native Drumstrip (the SSL_PLUGIN_STATE XML, A / B) -> Ridge's gate; round tp172c8: -> Shell
// Channel, whose controls have the original's own ranges and laws: every setting reads across one to one (processOrder
// is stored as its 0..1 position over the 120 orders). bypass -> the slot off
int translateSslDrumstrip (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace s = mixsuite::shell;
    const auto root = findBytes (state, "<SSL_PLUGIN_STATE");
    if (root == std::string::npos || slot.kind != EffectKind::shellChannel)
        return 0;
    const std::string text (state.begin() + (std::ptrdiff_t) root, state.end());
    if (attributeIn (text, 0, "PluginName").find ("Drumstrip") == std::string::npos)
        return 0;
    const bool useB = attributeIn (text, 0, "StateASelected") == "0";
    const auto block = text.find (useB ? "<B" : "<A");
    if (block == std::string::npos)
        return 0;
    const auto v = xmlParams (text, block, text.find (useB ? "</B>" : "</A>", block));
    if (v.size() < 10)
        return 0;
    auto at = [&v] (const char* id, double fallback) { const auto it = v.find (id); return it != v.end() ? it->second : fallback; };
    auto sw = [&at] (const char* id) { return at (id, 0.0) >= 0.5 ? 1.0f : 0.0f; };
    auto& p = slot.params;
    p[s::input] = linNorm (at ("input", 0.0), -20.0, 20.0);
    p[s::gateOn] = sw ("gateOn");
    p[s::gateAttack] = linNorm (at ("gateAttack", 0.05), 0.0, 0.1);
    p[s::gateHold] = linNorm (at ("gateHold", 0.0), 0.0, 4000.0);
    p[s::gateRelease] = s::releaseValue (std::clamp (at ("gateRelease", 0.5), 0.01, 1.0));
    p[s::gateOpen] = s::thresholdValue (at ("gateOpen", -15.0));
    p[s::gateClose] = s::thresholdValue (at ("gateClose", -24.0));
    p[s::gateRange] = linNorm (at ("gateRange", 80.0), 0.0, 80.0);
    p[s::tsGain] = linNorm (at ("transGain", 0.0), -20.0, 20.0);
    p[s::tsSpeed] = (float) std::clamp (at ("transSpeed", 0.5), 0.0, 1.0);
    p[s::tsAmount] = linNorm (at ("transAmount", 0.0), 0.0, 100.0);
    p[s::tsOn] = sw ("TransOn");
    p[s::tsInvert] = sw ("transInvert");
    p[s::tsListen] = sw ("transListen");
    p[s::hfOn] = sw ("hfOn");
    p[s::hfFreq] = s::hfValue (std::clamp (at ("hfFreq", 6225.0), 2000.0, 20000.0));
    p[s::hfDrive] = linNorm (at ("hfDrive", 5.0), 0.0, 10.0);
    p[s::hfAmount] = linNorm (at ("hfAmount", 0.0), 0.0, 100.0);
    p[s::lfOn] = sw ("lfOn");
    p[s::lfFreq] = s::lfValue (std::clamp (at ("lfFreq", 70.0), 20.0, 250.0));
    p[s::lfDrive] = linNorm (at ("lfDrive", 5.0), 0.0, 10.0);
    p[s::lfAmount] = linNorm (at ("lfAmount", 0.0), 0.0, 100.0);
    p[s::lmcOn] = sw ("lmcOn");
    p[s::lmcNarrow] = sw ("lmcNarrowband");
    p[s::lmcComp] = linNorm (at ("lmcComp", 0.0), 0.0, 100.0);
    p[s::lmcMakeup] = linNorm (at ("lmcMakeup", 0.0), -20.0, 20.0);
    p[s::lmcMix] = linNorm (at ("lmcMix", 100.0), 0.0, 100.0);
    p[s::output] = linNorm (at ("output", 0.0), -20.0, 20.0);
    p[s::outputMix] = linNorm (at ("outputMix", 100.0), 0.0, 100.0);
    p[s::processOrder] = s::orderValue (s::orderIndex ((float) std::clamp (at ("processOrder", 0.0), 0.0, 1.0)));
    if (at ("bypass", 0.0) >= 0.5)
        slot.enabled = false;
    return (int) v.size();
}

// SSL Native Vocalstrip 2 (round 145): its state is XML with named parameters in the original's own units
// (<SSL_PLUGIN_STATE … StateASelected="1"><A><PROCESSOR_STATE><PARAM id="compThreshold" value="-33.2"/> …); the A or B
// setting that plays maps one to one onto Ridge Strip's Vocal strip voicing, which reproduces the official trial
// measured black-box (docs/analise/REA_rodada145.md §2). Other SSL Native strips keep Ridge's defaults.
int translateSslVocalstrip2 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace r = mixsuite::ridge;
    namespace vs = voxstrip;
    const auto root = findBytes (state, "<SSL_PLUGIN_STATE");
    if (root == std::string::npos || slot.kind != EffectKind::ridgeStrip)
        return 0;
    const std::string text (state.begin() + (std::ptrdiff_t) root, state.end());
    const auto plugin = attributeIn (text, 0, "PluginName");
    const bool k4e = plugin.find ("4K E") != std::string::npos;
    // round tp172c: SSL Native Channel Strip 2 (the same XML form) -> the Modern voicing
    const bool cs2 = plugin.find ("Channel Strip 2") != std::string::npos;
    // round tp172c: SSL 4K G (its EQ = the 4K voicing's Black; LMF ÷3 / HMF ×3 move the band) and SSL 4K B (its EQ,
    // filters and gate; round tp172c4: its stepped compressor through the 4K voicing's)
    const bool k4g = plugin.find ("4K G") != std::string::npos, k4b = plugin.find ("4K B") != std::string::npos;
    if (plugin.find ("Vocalstrip 2") == std::string::npos && ! k4e && ! cs2 && ! k4g && ! k4b)
        return 0;
    const bool useB = attributeIn (text, 0, "StateASelected") == "0";
    const auto block = text.find (useB ? "<B" : "<A");
    if (block == std::string::npos)
        return 0;
    const auto blockEnd = text.find (useB ? "</B>" : "</A>", block);
    std::map<std::string, double> v;
    for (auto q = text.find ("<PARAM ", block); q != std::string::npos && q < blockEnd; q = text.find ("<PARAM ", q + 7))
    {
        const auto id = attributeIn (text, q, "id");
        float value = 0;
        if (! id.empty() && parseFloat (attributeIn (text, q, "value"), value) && std::isfinite (value))
            v[id] = value;
    }
    if (v.size() < 10)
        return 0;
    auto at = [&v] (const char* id, double fallback) {
        const auto it = v.find (id);
        return it != v.end() ? it->second : fallback;
    };
    auto sw = [&at] (const char* id, bool fallback) { return at (id, fallback ? 1.0 : 0.0) >= 0.5 ? 1.0f : 0.0f; };
    auto& p = slot.params;
    if (cs2)
    {
        // round tp172c: SSL Native Channel Strip 2 -> Ridge's Modern voicing (core/VoiceMatch.h strip2, measured on its
        // trial), every control in the original's units through the voicing's own laws (HighPassFreq 20 = out,
        // LowPassFreq 35 = out, EqE 1 = E / 0 = G, CompRatio 1e9+ = infinity). Not read: pan, width, the external key,
        // the side-chain routing and listen, filters to input, the legacy switches
        p[r::modern] = 1.0f;
        p[r::console4k] = 0.0f;
        p[r::voicing] = 0.0f;
        p[r::vocal] = 0.0f;
        p[r::input] = linNorm (at ("InputTrim", 0.0), -20.0, 20.0);
        const auto fader = at ("FaderLevel", 0.0);
        p[r::output] = linNorm (at ("OutputTrim", 0.0) + std::max (fader, -20.0), -20.0, 20.0);
        p[r::phase] = sw ("PhaseInvert", false);
        const auto hp = at ("HighPassFreq", 20.0), lp = at ("LowPassFreq", 35.0);
        p[r::hpf] = hp < 20.5 ? 0.0f : std::max (1e-4f, logNorm (hp, 20.9, 500.0));
        p[r::lpf] = lp > 27.6 ? 0.0f : std::max (1e-4f, logNorm (lp, 27.5, 3.0));
        if (at ("FiltersIn", 1.0) < 0.5)
            p[r::hpf] = p[r::lpf] = 0.0f;
        p[r::curve] = at ("EqE", 0.0) >= 0.5 ? 0.0f : 1.0f;
        p[r::lfGain] = linNorm (at ("LowEqGain", 0.0), -16.5, 16.5);
        p[r::lfFreq] = logNorm (at ("LowEqFreq", 182.0), 40.0, 600.0);
        p[r::lfBell] = sw ("LowEqBell", false);
        p[r::lmfGain] = linNorm (at ("LowMidEqGain", 0.0), -20.0, 20.0);
        p[r::lmfFreq] = logNorm (at ("LowMidEqFreq", 0.8), 0.2, 2.0);
        p[r::lmfQ] = linNorm (at ("LowMidEqQ", 1.5), 0.5, 2.5);
        p[r::hmfGain] = linNorm (at ("HighMidEqGain", 0.0), -20.0, 20.0);
        p[r::hmfFreq] = logNorm (at ("HighMidEqFreq", 2.45), 0.6, 7.0);
        p[r::hmfQ] = linNorm (at ("HighMidEqQ", 1.5), 0.5, 2.5);
        p[r::hfGain] = linNorm (at ("HighEqGain", 0.0), -20.0, 20.0);
        p[r::hfFreq] = logNorm (at ("HighEqFreq", 7.5), 1.5, 22.0);
        p[r::hfBell] = sw ("HighEqBell", false);
        p[r::eqOn] = sw ("EqIn", true);
        p[r::compRatio] = r::mRatioValue (at ("CompRatio", 1.0));
        p[r::compThreshold] = linNorm (at ("CompThreshold", 10.0), -20.0, 10.0);
        p[r::compRelease] = logNorm (at ("CompRelease", 0.1), 0.1, 4.0);
        p[r::compFastAttack] = sw ("CompFastAttack", false);
        p[r::compPeak] = sw ("CompPeak", false);
        p[r::compMix] = (float) std::clamp (at ("CompMix", 100.0) / 100.0, 0.0, 1.0);
        p[r::autoMakeup] = k4b ? 0.0f : sw ("CompAutoMakeUp", true);
        p[r::k4MakeupOffset] = linNorm (at ("CompAutoMakeUpCompensation", 0.0), -12.0, 12.0);
        p[r::gateRange] = linNorm (at ("GateRange", 0.0), 0.0, 40.0);
        p[r::gateThreshold] = linNorm (at ("GateThreshold", 10.0), -30.0, 10.0);
        p[r::gateRelease] = logNorm (at ("GateRelease", 0.1), 0.1, 4.0);
        p[r::gateHold] = linNorm (at ("GateHold", 0.25), 0.0, 4.0);
        p[r::gateFastAttack] = sw ("GateFastAttack", false);
        p[r::expander] = sw ("GateExpander", false);
        p[r::dynOn] = sw ("DynamicsIn", true);
        p[r::order] = sw ("DynamicsPreEq", false);
        p[r::analog] = 0.0f;
        if (at ("Bypass", 0.0) >= 0.5)
            slot.enabled = false;
        return (int) v.size();
    }
    if (k4e || k4g || k4b)
    {
        // round 148: SSL 4K E -> Ridge's 4K voicing, the controls in the original's units through Ridge's own laws
        // (EQ and filter frequencies: LF in Hz, the others in kHz; HighPassFreq 10 = out, LowPassFreq 35 = out; CompRatio
        // 40 = infinity; EqColour 0 / 1 / 2 = Brown / Black / Orange; SaturationIn / Amount = the mic pre and its gain).
        // Not read: pan, width, the external key, filters / EQ to the side chain, filters to input, high quality
        p[r::console4k] = 1.0f;
        p[r::voicing] = 0.0f;
        p[r::vocal] = 0.0f;
        p[r::input] = linNorm (at ("InputTrim", 0.0), -20.0, 20.0);
        const auto fader = at ("FaderLevel", 0.0);
        p[r::output] = linNorm (at ("OutputTrim", 0.0) + std::max (fader, -20.0), -20.0, 20.0);
        p[r::phase] = sw ("PhaseInvert", false);
        const auto hp = at ("HighPassFreq", 10.0), lp = at ("LowPassFreq", 35.0);
        p[r::hpf] = hp < 16.5 ? 0.0f : std::max (1e-4f, logNorm (hp, 16.0, 350.0));
        p[r::lpf] = lp > 15.95 ? 0.0f : std::max (1e-4f, logNorm (lp * 1000.0, 22000.0, 3000.0));
        auto gain = [] (double db) { return linNorm (db, -15.0, 15.0); };
        // round tp172c4: the 4K G's dials span ±18 (LF) / ±20 (HF) / ±22 dB (mids) for the same curves this console draws
        // at ±15 (its Black renders matched the 4K E's Black node for node, K4G_ir): its gains scale onto the ±15 dials
        const double gLf = k4g ? 15.0 / 18.0 : 1.0, gHf = k4g ? 15.0 / 20.0 : 1.0, gMid = k4g ? 15.0 / 22.0 : 1.0;
        p[r::lfGain] = gain (gLf * at ("LowEqGain", 0.0));
        p[r::lfFreq] = logNorm (at ("LowEqFreq", 200.0), 30.0, 450.0);
        p[r::lfBell] = sw ("LowEqBell", false);
        p[r::lmfGain] = gain (gMid * at ("LowMidEqGain", 0.0));
        // round 190ho: on the 4K G the ÷3 / ×3 switches act in its Pink EQ only (K4G_ir: Black with ÷3 or ×3 renders the
        // same as without); Pink takes them as its own switches (they move its centre ×0.24 / ×2.9, not ÷3 / ×3)
        const bool div3 = ! k4g && at ("LowMidEqDiv3", 0.0) >= 0.5, mult3 = ! k4g && at ("HighMidEqMult3", 0.0) >= 0.5;
        p[r::lmfFreq] = logNorm (at ("LowMidEqFreq", 1.0) * 1000.0 / (div3 ? 3.0 : 1.0), 200.0, 2500.0);
        p[r::lmfQ] = r::k4QValue (at ("LowMidEqQ", 1.5));
        p[r::hmfGain] = gain (gMid * at ("HighMidEqGain", 0.0));
        p[r::hmfFreq] = logNorm (at ("HighMidEqFreq", 3.0) * 1000.0 * (mult3 ? 3.0 : 1.0), 600.0, 7000.0);
        p[r::hmfQ] = r::k4QValue (at ("HighMidEqQ", 1.5));
        p[r::hfGain] = gain (gHf * at ("HighEqGain", 0.0));
        p[r::hfFreq] = logNorm (at ("HighEqFreq", 8.0) * 1000.0, 1500.0, 16000.0);
        p[r::hfBell] = sw ("HighEqBell", false);
        p[r::eqOn] = sw ("EqIn", true);
        // (the 4K G's Black = this console's Black; its Pink = the hidden Pink EQ above, the selector then reads Black)
        p[r::k4Colour] = r::k4ColourValue (k4g ? 1 : k4b ? 0 : (int) std::lround (at ("EqColour", 0.0)));
        if (k4e && ! k4g && ! k4b)
        {
            // round 190lb (black-box, BLACKBOX_rodada190lb §3): the 4K E's high-pass is 18 dB/oct in Black and Orange
            // (its trial's K4E_ir renders; Brown keeps the 12 dB/oct one this console was fitted with), from 14.8 Hz
            const auto colour = (int) std::lround (at ("EqColour", 0.0));
            if (colour >= 1)
            {
                p[r::k4eHpLaw] = 1.0f;
                const auto hpE = at ("HighPassFreq", 10.0);
                p[r::hpf] = hpE < 10.5 ? 0.0f : std::max (1e-4f, logNorm (hpE, 16.0, 350.0));
            }
        }
        if (k4g)
        {
            // round 190lb (black-box, BLACKBOX_rodada190lb §2): the 4K G's EqType 0 = Pink (its default), 1 = Black. Pink
            // is its own EQ (the 4K voicing's hidden Pink colour): the gains stay scaled onto the ±15 dials as above,
            // the mid frequencies are the dials before ÷3 / ×3 (the switches go with them). Either colour: its own
            // filters (an 18 dB/oct high-pass from 14.8 Hz, a low-pass up to 23.8 kHz; HighPassFreq 10 / LowPassFreq 35 =
            // out) and its 1.1108 output ceiling (k4gLaw)
            const bool pink = at ("EqType", 0.0) < 0.5;
            p[r::k4Pink] = pink ? 1.0f : 0.0f;
            p[r::k4gLaw] = 1.0f;
            const auto hp4 = at ("HighPassFreq", 10.0), lp4 = at ("LowPassFreq", 35.0);
            p[r::hpf] = hp4 < 10.5 ? 0.0f : std::max (1e-4f, logNorm (hp4, 16.0, 350.0));
            p[r::lpf] = lp4 > 34.0 ? 0.0f : std::max (1e-4f, logNorm (lp4 * 1000.0, 22000.0, 3000.0));
            if (pink)
            {
                p[r::lmfFreq] = logNorm (at ("LowMidEqFreq", 1.0) * 1000.0, 200.0, 2500.0);
                p[r::hmfFreq] = logNorm (at ("HighMidEqFreq", 3.0) * 1000.0, 600.0, 7000.0);
                p[r::k4LmfDiv3] = sw ("LowMidEqDiv3", false);
                p[r::k4HmfMult3] = sw ("HighMidEqMult3", false);
            }
        }
        // round tp172c4: the B's ratio, compressor release and gate release are switch steps (its ratio: out, 2, 4, 10 : 1,
        // "ds"; its releases: "ds", .2, .4, .8, 1.6 s, auto — the two non-numbered ones read as the times whose static
        // curve they match), measured on its trial (K4B_dyn / K4B_mk / K4B_mic): every step compresses near-limiting,
        // its knee 8 (2 and 4 : 1), 10 (10 : 1) or 18 dB ("ds") above where this console's knee sits for the same dial ->
        // the 4K voicing at infinity with the threshold moved up; the B's auto make-up depends on the threshold only
        // (12.6 / 9.1 / 3.5 / 1.5 dB at -15 / -5 / 0 / +5; none in "ds"), so it goes into the output instead
        auto step = [&at] (const char* id, double fallback, std::initializer_list<double> values) {
            const auto i = (std::size_t) std::clamp ((int) std::lround (at (id, fallback)), 0, (int) values.size() - 1);
            return *(values.begin() + i);
        };
        const auto bStep = k4b ? (int) std::clamp (std::lround (at ("CompRatio", 0.0)), 0L, 4L) : 0;
        const auto ratio = k4b ? (bStep == 0 ? 1.0 : 1e9) : at ("CompRatio", 1.0);
        p[r::compRatio] = ratio <= 1.0005 ? 0.0f : (ratio >= 30.0 ? 1.0f : (float) std::sqrt (std::clamp ((ratio - 1.0) / 19.0, 0.0, 0.996)));
        double bOffset = k4b ? step ("CompRatio", 0.0, { 0.0, 8.0, 8.0, 10.0, 18.0 }) : 0.0;
        // (the 4K G's slow-attack compression runs 1 .. 3 dB deeper than this console's for the same dial, K4G_dyn: its
        // threshold reads 1 dB lower at 0 and 3 dB lower at -15 and below; with fast attack they agree)
        if (k4g && sw ("CompFastAttack", false) < 0.5f)
            bOffset = -1.0 - 2.0 * std::clamp (-at ("CompThreshold", 10.0) / 15.0, 0.0, 1.0);
        p[r::compThreshold] = linNorm (at ("CompThreshold", 10.0) + bOffset, -20.0, 10.0);
        if (k4b)
        {
            double outDb = at ("OutputTrim", 0.0) + std::max (at ("FaderLevel", 0.0), -20.0);
            const auto t = at ("CompThreshold", 10.0);
            if (bStep >= 1 && bStep <= 3 && sw ("CompAutoMakeUp", true) >= 0.5f && sw ("DynamicsIn", true) >= 0.5f)
                outDb += t <= -15.0 ? 12.6 + 0.35 * (-15.0 - t)
                       : t <= -5.0 ? 12.6 + (t + 15.0) * (9.1 - 12.6) / 10.0
                       : t <= 0.0 ? 9.1 + (t + 5.0) * (3.5 - 9.1) / 5.0
                       : t <= 5.0 ? 3.5 + t * (1.5 - 3.5) / 5.0 : std::max (0.0, 1.5 - (t - 5.0) * 0.2);
            if (sw ("SaturationIn", false) >= 0.5f)
                outDb += 0.27; // its mic pre has no level loss (this console's Pre In alone is -0.27 dB)
            p[r::output] = linNorm (outDb, -20.0, 20.0);
        }
        p[r::compRelease] = logNorm (k4b ? step ("CompRelease", 5.0, { 1.4, 0.2, 0.4, 0.8, 1.6, 1.0 }) : at ("CompRelease", 0.4), 0.1, 4.0);
        p[r::compFastAttack] = sw ("CompFastAttack", false);
        p[r::compMix] = (float) std::clamp (at ("CompMix", 100.0) / 100.0, 0.0, 1.0);
        p[r::autoMakeup] = k4b ? 0.0f : sw ("CompAutoMakeUp", true);
        p[r::k4MakeupOffset] = linNorm (at ("CompAutoMakeUpCompensation", 0.0), -12.0, 12.0);
        p[r::gateRange] = linNorm (at ("GateRange", 0.0), 0.0, 40.0);
        p[r::gateThreshold] = linNorm (at ("GateThreshold", -30.0), -30.0, 10.0);
        p[r::gateRelease] = logNorm (k4b ? step ("GateRelease", 2.0, { 0.1, 0.2, 0.4, 0.8, 1.6 }) : at ("GateRelease", 0.4), 0.1, 4.0);
        p[r::gateFastAttack] = sw ("GateFastAttack", false);
        p[r::expander] = sw ("GateExpander", false);
        p[r::dynOn] = sw ("DynamicsIn", true);
        p[r::order] = sw ("DynamicsPreEq", false);
        p[r::k4Pre] = sw ("SaturationIn", false);
        // (the B's mic pre is far gentler: its Mic dial reads as this console's 5 + 0.45 x Mic, K4B_mic)
        p[r::k4Mic] = linNorm (k4b ? 5.0 + 0.45 * at ("SaturationAmount", 0.0) : at ("SaturationAmount", 0.0), 0.0, 50.0);
        p[r::analog] = sw ("AnalogueVca", true);
        if (at ("Bypass", 0.0) >= 0.5)
            slot.enabled = false;
        return (int) v.size();
    }
    p[r::vocal] = 1.0f;
    p[r::vInput] = vs::gainValue (at ("inputGain", 0.0));
    p[r::vOutput] = vs::gainValue (at ("outputGain", 0.0));
    p[r::vOrder] = (float) std::clamp (at ("processOrder", 0.0), 0.0, 1.0);
    p[r::vDsOn] = sw ("dsOn", false);
    p[r::vDsFreq] = vs::dsFreqValue (at ("dsFreq", 7500.0));
    p[r::vDsThreshold] = vs::thresholdValue (at ("dsThreshold", -24.0));
    p[r::vDsSplit] = sw ("dsSplit", true);
    p[r::vDsLookahead] = sw ("dsLookAhead", false);
    p[r::vDsListen] = sw ("dsAudition", false);
    p[r::vDpOn] = sw ("dpOn", false);
    p[r::vDpFreq] = vs::dpFreqValue (at ("dpFreq", 120.0));
    p[r::vDpThreshold] = vs::thresholdValue (at ("dpThreshold", -24.0));
    p[r::vDpSplit] = sw ("dpSplit", true);
    p[r::vDpLookahead] = sw ("dpLookAhead", false);
    p[r::vDpListen] = sw ("dpAudition", false);
    p[r::vEqOn] = sw ("eqOn", false);
    p[r::vHpfOn] = sw ("hpfOn", true);
    p[r::vHpfFreq] = vs::hpfValue (at ("hpfFreq", 30.0));
    p[r::vNotchOn] = sw ("notchOn", true);
    p[r::vNotchFreq] = vs::notchValue (at ("notchFreq", 200.0));
    p[r::vNotchGain] = vs::notchGainValue (at ("notchGain", 0.0));
    p[r::vHighOn] = sw ("hsOn", true);
    p[r::vHighFreq] = vs::highValue (at ("hsFreq", 1000.0));
    p[r::vHighGain] = vs::highGainValue (at ("hsGain", 0.0));
    p[r::vCompOn] = sw ("compOn", false);
    p[r::vExpThreshold] = vs::expThresholdValue (at ("expThreshold", -96.0));
    p[r::vCompThreshold] = vs::thresholdValue (at ("compThreshold", 0.0));
    p[r::vRatio] = vs::ratioValue (at ("compRatio", 4.0));
    p[r::vAttack] = vs::attackValue (at ("compAttack", 0.1));
    p[r::vRelease] = vs::releaseValue (at ("compRelease", 100.0));
    p[r::vMakeup] = vs::makeupValue (at ("compMakeup", 0.0));
    p[r::vKnee] = sw ("compKnee", false);
    p[r::vDrive] = sw ("compDrive", false);
    if (at ("bypassAll", 0.0) >= 0.5)
        slot.enabled = false;
    return (int) v.size();
}

// SSL E-Channel (57 values; RW index: 0 comp threshold dB, 1 ratio, 2 fast (1 = on), 3 release s, 5 expander threshold,
// 6 range, 7 gate (1) / expander (0), 8 expander fast attack, 9 expander release, 11 dynamics bypass, 12 dynamics to
// channel out, 13 LF bell, 14/15 LF gain / Hz, 16/17/18 LMF gain / kHz / Q, 19/20/21 HMF, 22 HF bell, 23/24 HF gain / kHz,
// 25 EQ bypass, 26 EQ to dynamics side chain, 27 HP Hz (< 17 = out), 28 LP kHz (30 = out), 29 filter split (1 = on),
// 30 output dB, 31 analog, 39 phase, 40 input dB). Round 123: Ridge Strip's "E" voicing (core/VoiceMatch.h) is the
// console measured black-box on the official trial — every EQ band and filter from its impulse responses, the
// feedback compressor and expander from sine stairs, tone bursts, noise and her takes — so the controls map one to
// one in the console's own units (docs/analise/REA_rodada123.md). Round 147b: analog translated (off in every state of
// her corpus). Round 165: EQ to the dynamics side chain (Dyn S.C., RealWorld 26) translated.
int translateWavesSslChannel (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace r = mixsuite::ridge;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::ridgeStrip || rw.size() < 31)
        return 0;
    const auto at = [&] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto gain = [] (double db) { return linNorm (db, -15.0, 15.0); };
    auto& p = slot.params;
    p[r::voicing] = 1.0f;
    p[r::curve] = 0.0f;
    p[r::input] = linNorm (at (40, 0.0), -20.0, 20.0);
    p[r::output] = linNorm (at (30, 0.0), -20.0, 20.0);
    p[r::phase] = at (39, 0.0) > 0.5 ? 1.0f : 0.0f;
    const auto hp = at (27, 0.0);
    p[r::hpf] = hp >= 16.5 ? std::max (1e-4f, logNorm (std::min (hp, 350.0), 16.0, 350.0)) : 0.0f;
    const auto lpk = at (28, 30.0);
    // Ridge's low-pass law runs 22 kHz (dial 0) .. 3 kHz (dial 1), log
    p[r::lpf] = lpk < 25.0 ? std::max (1e-4f, (float) std::clamp (std::log (22000.0 / (lpk * 1000.0)) / std::log (22000.0 / 3000.0), 0.0, 1.0)) : 0.0f;
    p[r::lfGain] = gain (at (14, 0.0));
    p[r::lfFreq] = logNorm (at (15, 60.0), 30.0, 450.0);
    p[r::lfBell] = at (13, 0.0) > 0.5 ? 1.0f : 0.0f;
    p[r::lmfGain] = gain (at (16, 0.0));
    p[r::lmfFreq] = logNorm (at (17, 0.5) * 1000.0, 200.0, 2500.0);
    p[r::lmfQ] = (float) (1.0 - vmatch::echannel::dialFromQ (at (18, 2.5)));
    p[r::hmfGain] = gain (at (19, 0.0));
    p[r::hmfFreq] = logNorm (at (20, 3.5) * 1000.0, 600.0, 7000.0);
    p[r::hmfQ] = (float) (1.0 - vmatch::echannel::dialFromQ (at (21, 1.5)));
    p[r::hfGain] = gain (at (23, 0.0));
    p[r::hfFreq] = logNorm (at (24, 8.0) * 1000.0, 1500.0, 16000.0);
    p[r::hfBell] = at (22, 0.0) > 0.5 ? 1.0f : 0.0f;
    p[r::eqOn] = at (25, 0.0) > 0.5 ? 0.0f : 1.0f;
    const auto ratio = std::max (1.0, at (1, 1.0));
    // round 165 [EXEC]: the console's ∞ : 1 is stored as 100 (it played as 20 : 1 here, 3 … 5 dB too loud)
    p[r::compRatio] = ratio >= 99.0 ? 1.0f : (float) std::sqrt (std::clamp ((ratio - 1.0) / 19.0, 0.0, 0.998));
    p[r::compThreshold] = linNorm (at (0, 0.0), -20.0, 10.0);
    p[r::compRelease] = logNorm (std::clamp (at (3, 0.4), 0.1, 4.0), 0.1, 4.0);
    p[r::compFastAttack] = at (2, 2.0) == 1.0 ? 1.0f : 0.0f;
    p[r::gateRange] = linNorm (at (6, 0.0), 0.0, 40.0);
    p[r::gateThreshold] = linNorm (at (5, -30.0), -30.0, 10.0);
    p[r::gateRelease] = logNorm (std::clamp (at (9, 0.2), 0.1, 4.0), 0.1, 4.0);
    p[r::gateFastAttack] = at (8, 0.0) > 0.5 ? 1.0f : 0.0f;
    p[r::expander] = at (7, 0.0) > 0.5 ? 0.0f : 1.0f;
    p[r::dynOn] = at (11, 0.0) > 0.5 ? 0.0f : 1.0f;
    p[r::order] = at (12, 0.0) > 0.5 ? 0.0f : 1.0f; // Ridge: on = dynamics before the EQ
    p[r::filterSplit] = at (29, 1.0) == 1.0 ? 1.0f : 0.0f;
    p[r::filtersToSidechain] = 0.0f;
    p[r::compMix] = 1.0f;
    p[r::analog] = at (31, 0.0) > 0.5 ? 1.0f : 0.0f; // round 147b: ANALOG (its noise floor, measured on the trial)
    p[r::eqToSidechain] = at (26, 0.0) > 0.5 ? 1.0f : 0.0f; // round 165: Dyn S.C. ([EXEC] RealWorld 26 follows the switch)
    return 32;
}

// round 190mm: SSL G-Channel (57 values: the E channel's layout; 55 LMF ÷3, 56 HMF ×3, no LF / HF bell switches) on
// Ridge Strip's E voicing through the E-Channel's translation, then the G's own switches
int translateWavesSslG (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace r = mixsuite::ridge;
    const int n = translateWavesSslChannel (state, slot);
    if (n == 0)
        return 0;
    const auto rw = wavesRealWorldParams (state);
    const auto at = [&rw] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    auto& p = slot.params;
    p[r::lfBell] = p[r::hfBell] = 0.0f;
    if (at (55, 0.0) > 0.5)
        p[r::lmfFreq] = logNorm (at (17, 0.5) * 1000.0 / 3.0, 200.0, 2500.0);
    if (at (56, 0.0) > 0.5)
        p[r::hmfFreq] = logNorm (at (20, 3.5) * 3000.0, 600.0, 7000.0);
    // round 190mt: the G's own EQ sections (Ridge's hidden G law: gain-dependent bells and shelves on the G's ranges,
    // LF / HF ±17 dB, LMF / HMF ±20 dB, its ÷3 / ×3 switches; REA_rodada190mt §1). The E fields above stay as they were.
    namespace g = r::gch;
    p[r::gEqLaw] = 1.0f;
    p[r::gLfGain] = g::gainValue (at (14, 0.0));
    p[r::gLfHz] = g::lfValue (at (15, 60.0));
    p[r::gLmfGain] = g::gainValue (at (16, 0.0));
    p[r::gLmfKHz] = g::lmfValue (at (17, 0.5));
    p[r::gLmfQ] = g::qValue (at (18, 2.5));
    p[r::gHmfGain] = g::gainValue (at (19, 0.0));
    p[r::gHmfKHz] = g::hmfValue (at (20, 3.5));
    p[r::gHmfQ] = g::qValue (at (21, 1.5));
    p[r::gHfGain] = g::gainValue (at (23, 0.0));
    p[r::gHfKHz] = g::hfValue (at (24, 8.0));
    p[r::gLmfDiv3] = at (55, 0.0) > 0.5 ? 1.0f : 0.0f;
    p[r::gHmfX3] = at (56, 0.0) > 0.5 ? 1.0f : 0.0f;
    return n + 2;
}

// H-Delay (RealWorld list, round 119): 1 sync (0 BPM, 1 host, 2 ms), 2 BPM, 3 delay ms, 4 note (3 per length: triplet,
// straight, dotted from 1/64; 18 = 1 bar, 19 = 2 bars), 6 mix %, 7 feedback, 10 ping-pong (1) / stereo (0), 14 output dB,
// 15 low-pass Hz (20100 = off), 17 high-pass Hz (19 = off), 19 / 20 modulation depth % / rate Hz, 23 analog (0 = off).
// [EXEC] Measured on the official trial: ping-pong sends the mono sum to the left echo, the right side repeats it one
// time later at nearly the same level (-0.3 dB) and the loop gain per left-right pair is 0.93 x (feedback / 100)^2
// (feedback 30 / 60 / 90: -20.9 / -9.5 / -2.7 dB); stereo mode feeds each side back at 0.98 x feedback / 100 per
// repeat; the echoes are flat (+-0.5 dB, 30 Hz-20 kHz); the first echo is +1.1 dB over the mono sum with the analog
// modes and +2.5 dB without (stereo mode: -1.9 / -0.4 dB over its own side); the mix is an equal-power dry / wet.
int translateWavesHDelay (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace e = echosuite::echoform;
    const auto rw = wavesRealWorldParams (state);
    const auto sync = wavesValue (rw, 1), fb = wavesValue (rw, 7);
    if (slot.kind != EffectKind::echoform || ! sync || ! fb)
        return 0;
    auto& p = slot.params;
    // round 135w: [EXEC] round 119's probe: 1 = PingPong, 2 = Stereo (2 used to read as ping-pong)
    const auto ppValue = wavesValue (rw, 10).value_or (1.0);
    const bool pingPong = ppValue >= 0.5 && ppValue < 1.5;
    const bool analog = wavesValue (rw, 23).value_or (2.0) >= 0.5;
    p[e::style] = e::styleValue (pingPong ? e::Style::pingPong : e::Style::single);
    p[e::pairs] = pingPong ? 1.0f : 0.0f;
    // round 129: phase L / R (RealWorld 8 / 9, round 119 §2 [EXEC]; 1 = inverted)
    p[e::phaseL] = wavesValue (rw, 8).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    p[e::phaseR] = wavesValue (rw, 9).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    // round 135w: the ping-pong's start side (RealWorld 45: 0 left, 1 right — round 119 probe [EXEC])
    p[e::startSide] = wavesValue (rw, 45).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
    const auto note = (int) std::lround (wavesValue (rw, 4).value_or (13.0));
    const auto ownTempo = wavesValue (rw, 2);
    // round 135w: H-Delay's own BPM (sync 0) keeps its note and tempo (was folded into milliseconds)
    const bool ownBpmSync = *sync < 0.5 && ownTempo && *ownTempo >= 30.0 && *ownTempo <= 300.0;
    p[e::tempoSource] = ownBpmSync ? 1.0f : 0.0f;
    if (ownBpmSync)
        p[e::ownBpm] = e::bpmValue (*ownTempo);
    if ((ownBpmSync || (*sync >= 0.5 && *sync < 1.5)) && note >= 0 && note <= 19)
    {
        // host tempo (or its own): the note follows that tempo
        const auto index = note >= 18 ? note - 12 : note / 3;  // 1/64 … 1/2, 1 bar, 2 bars
        const auto kind = note >= 18 ? 1 : (note % 3 == 0 ? 3 : (note % 3 == 1 ? 1 : 2)); // triplet, straight, dotted
        for (const auto side : { e::syncL, e::syncR })
            p[(std::size_t) side] = ((float) kind + 1.5f) / 4.0f;
        for (const auto side : { e::noteL, e::noteR })
            p[(std::size_t) side] = ((float) index + 0.5f) / 10.0f;
    }
    else
    {
        // its own BPM or milliseconds: the time it computed is in the list
        const auto ms = std::clamp (wavesValue (rw, 3).value_or (375.0), 1.0, 3500.0);
        for (const auto side : { e::syncL, e::syncR })
            p[(std::size_t) side] = 0.0f;
        for (const auto side : { e::timeL, e::timeR })
            p[(std::size_t) side] = e::timeValue (ms);
    }
    const auto f = std::max (0.0, *fb) / 100.0;
    // round 124: the hybrid engine reproduces the trial's loop exactly (REA_rodada124 §5) when the state uses no
    // filter, modulation or lo-fi (every state of her corpus); otherwise the round-119 approximation below
    const auto hp0 = wavesValue (rw, 17).value_or (19.0), lp0 = wavesValue (rw, 15).value_or (20100.0);
    if (hp0 <= 19.5 && lp0 >= 20000.0 && wavesValue (rw, 19).value_or (0.0) <= 0.0 && wavesValue (rw, 21).value_or (0.0) < 0.5)
    {
        p[e::hybrid] = e::hybridValue (analog ? 2 : 1);
        p[e::feedback] = (float) std::clamp (f / 2.0, 0.0, 1.0);
        p[e::mix] = (float) std::clamp (wavesValue (rw, 6).value_or (100.0) / 100.0, 0.0, 1.0);
        p[e::output] = linNorm (wavesValue (rw, 14).value_or (0.0), -18.0, 18.0);
        return 9;
    }
    // round 190lf: the filters, LoFi and the modulation measured on the same loop (REA_rodada190lf_verb §3): the hybrid
    // engine's law 1 (HiPass / LoPass / LoFi inside the loop, the smoothed-triangle delay swing of ± depth · T / 2, the
    // LFO at twice the Rate knob with the BPM / host sync and at the knob with ms [EXEC, 120 BPM])
    {
        p[e::hybrid] = e::hybridValue (analog ? 2 : 1);
        p[e::hybridLaw] = 1.0f;
        p[e::feedback] = (float) std::clamp (f / 2.0, 0.0, 1.0);
        p[e::mix] = (float) std::clamp (wavesValue (rw, 6).value_or (100.0) / 100.0, 0.0, 1.0);
        p[e::output] = linNorm (wavesValue (rw, 14).value_or (0.0), -18.0, 18.0);
        p[e::lofi] = wavesValue (rw, 21).value_or (0.0) >= 0.5 ? 1.0f : 0.0f;
        p[e::hybridHp] = hp0 > 19.5 ? e::hybridFilterValue (hp0) : 0.0f;
        p[e::hybridLp] = lp0 < 20000.0 ? e::hybridFilterValue (lp0) : 0.0f;
        const auto depth = std::clamp (wavesValue (rw, 19).value_or (0.0), 0.0, 100.0);
        p[e::hybridDepth] = (float) (depth / 100.0);
        const auto rate = std::clamp (wavesValue (rw, 20).value_or (0.1), 0.0, 10.0) * (*sync < 1.5 ? 2.0 : 1.0);
        p[e::hybridRate] = depth > 0.0 ? (float) (rate / 20.0) : 0.0f;
        return 10;
    }
}

// NS1 (RealWorld list, round 119): value 229 is the one knob, 0 … 100 %; Quell's Amount follows the same scale
// (fitted to the official trial, docs/analise/REA_rodada119.md §3).
int translateWavesNs1 (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto amount = wavesValue (rw, 229);
    if (slot.kind != EffectKind::quellDenoiser || ! amount)
        return 0;
    slot.params[voicetools::quell::amount] = (float) std::clamp (*amount / 100.0, 0.0, 1.0);
    slot.params[voicetools::quell::matched] = 1.0f; // round 124: the engine fitted to the trial (REA_rodada124 §6)
    // round 190lf: 0 % is NS1's bare all-pass pair, the engine fades in up to 20 % (REA_rodada190lf_ns1.md)
    slot.params[voicetools::quell::zeroLaw] = 1.0f;
    // round 190mg (ns1): above 0 % the engine with NS1's own structure (REA_rodada190mg_ns1.md); its layout follows the
    // Amount by the law through NS1's five saved states (voicetools::quell::ns1Layout)
    slot.params[voicetools::quell::ns1Law] = 1.0f;
    return 4;
}

// Doubler (RealWorld list, round 119): 0 output dB, 1 direct on, 2 direct gain dB, 3 direct pan deg, 4 align,
// 9 / 10 / 11 low shelf on / gain dB / Hz, 12 / 13 / 14 high shelf Hz / gain dB / on, 15 feedback high-pass Hz,
// 16 / 17 voice 1 / 2 source (0 own side, 1 sum, 2 difference), 18 range (0 = 80 Hz), then a block of 10 per voice from
// 26: on, gain dB, pan deg, octave, detune cents, delay ms, feedback %, depth cents, rate. [EXEC] Gemini's laws are the
// measured ones (equal-power voice pan, stereo rotation of the direct signal, exact detune, octave down, LFO at
// rate / 2 Hz, feedback = %/100 per repeat, the aligned direct signal 6.7 ms late), so the values map one to one.
int translateWavesDoubler (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace g = voicetools::gemini;
    const auto rw = wavesRealWorldParams (state);
    if (slot.kind != EffectKind::geminiDoubler || rw.size() < 45 || ! wavesValue (rw, 31))
        return 0;
    const auto at = [&] (std::size_t i, double fallback) { return wavesValue (rw, i).value_or (fallback); };
    const auto flag = [&] (std::size_t i, double fallback) { return at (i, fallback) >= 0.5 ? 1.0f : 0.0f; };
    auto& p = slot.params;
    p[g::output] = linNorm (at (0, 0.0), -24.0, 0.0);
    p[g::directOn] = flag (1, 1.0);
    p[g::directGain] = linNorm (at (2, 0.0), -48.0, 0.0);
    p[g::directPan] = linNorm (at (3, 0.0), -45.0, 45.0);
    p[g::align] = flag (4, 1.0);
    p[g::lowOn] = flag (9, 0.0);
    // round 165w: the measured shelves take the reference's whole range (−88 … +12 dB: the deep cuts too) one to one
    p[g::eqLaw] = 1.0f;
    p[g::lowGain] = g::shelfValue (at (10, 0.0));
    p[g::lowFreq] = g::freqValue (at (11, 100.0));
    p[g::highFreq] = g::freqValue (at (12, 5000.0));
    p[g::highGain] = g::shelfValue (at (13, 0.0));
    p[g::highOn] = flag (14, 0.0);
    p[g::feedbackCut] = g::freqValue (at (15, 16.0));
    p[g::range] = flag (18, 0.0);
    // round 190mg: the Waves DSP's splice law for the read head (REA_rodada190mg_voc.md; hidden, Align off only)
    p[g::spliceLaw] = 1.0f;
    // round 135w: a four-voice doubler's voices 3 and 4 (blocks 46 / 56, the same layout as 26 / 36 [model]: a two-voice
    // state carries those blocks too, so they are read only when the state names the four-voice plug-in; their source
    // indices are not probed yet: own side)
    const bool four = wavesPluginName (state).find ("Doubler4") != std::string::npos;
    for (int v = 0; v < (four ? 4 : 2); ++v)
    {
        const auto b = (std::size_t) (26 + 10 * v);
        const auto param = [&] (g::VoiceParam q) -> float& { return p[(std::size_t) g::voiceParam (v, q)]; };
        param (g::vOn) = flag (b, 1.0);
        param (g::vGain) = linNorm (at (b + 1, 0.0), -48.0, 0.0);
        param (g::vPan) = linNorm (at (b + 2, v % 2 == 0 ? -45.0 : 45.0), -45.0, 45.0);
        param (g::vOctave) = flag (b + 3, 0.0);
        param (g::vDetune) = linNorm (at (b + 4, 0.0), -100.0, 100.0);
        param (g::vDelay) = linNorm (at (b + 5, 10.0), 0.0, 100.0);
        param (g::vFeedback) = linNorm (at (b + 6, 0.0), 0.0, 100.0);
        param (g::vDepth) = linNorm (at (b + 7, 0.0), -200.0, 200.0);
        param (g::vRate) = logNorm (std::clamp (at (b + 8, 1.0), 0.1, 400.0), 0.1, 400.0);
        param (g::vSource) = v >= 2 ? 1.0f / 6.0f : ((float) std::clamp ((int) std::lround (at (16 + (std::size_t) v, 0.0)), 0, 2) + 0.5f) / 3.0f;
    }
    return 33;
}

// S1 Imager (RealWorld list, round 124): 0 gain dB, 1 width (0 … 3, the plug-in keeps two decimals), 4 rotation deg
// (±45), 5 asymmetry deg (±90), 13 swap, 14 / 15 invert left / right, 16 M/S input. [EXEC] Black-box on the official
// trial (REA_rodada124_extras §4): a frequency-flat 2 × 2 matrix, gain · rotation · width · input. Width w below 1
// scales the side signal (mid 1, side w); above 1 the side / mid ratio is w with mid = √(0.625 + 0.375 / w²) (exact on
// 41 measured points); rotation φ is the plain rotation [cos −sin; sin cos]. Fruity Stereo Shaper (LYNX's exact engine)
// holds any 2 × 2 matrix, so the translation nulls the reference. Asymmetry, swap / invert and M/S input are not
// measured beyond their neutral settings (all neutral in her states): a state that uses them is not translated.
int translateWavesS1Imager (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto rw = wavesRealWorldParams (state);
    const auto width = wavesValue (rw, 1);
    if (slot.kind != EffectKind::stereoShaper || ! width)
        return 0;
    const auto at = [&] (std::size_t i) { return wavesValue (rw, i).value_or (0.0); };
    if (std::abs (at (5)) > 1e-6 || at (13) >= 0.5 || at (14) >= 0.5 || at (15) >= 0.5 || at (16) >= 0.5)
        return 0;
    const auto w = std::clamp (std::round (*width * 100.0) / 100.0, 0.0, 3.0);
    const auto mid = w <= 1.0 ? 1.0 : std::sqrt (0.625 + 0.375 / (w * w));
    const auto side = w * mid;
    const auto a = 0.5 * (mid + side), b = 0.5 * (mid - side);
    const auto phi = std::clamp (at (4), -45.0, 45.0) * 3.14159265358979323846 / 180.0;
    const auto c = std::cos (phi), s = std::sin (phi);
    const auto g = std::pow (10.0, std::clamp (at (0), -24.0, 0.0) / 20.0);
    // rotation · width: [c −s; s c] · [a b; b a]
    const double m00 = g * (c * a - s * b), m01 = g * (c * b - s * a), m10 = g * (s * a + c * b), m11 = g * (s * b + c * a);
    // the Shaper's knob law: |gain| = (3^|x| − 1) / 2 with x = raw / 12800
    const auto raw = [] (double gain) {
        const auto x = std::log (2.0 * std::abs (gain) + 1.0) / std::log (3.0);
        return std::clamp ((int) std::lround ((gain < 0.0 ? -x : x) * 12800.0), -25600, 25600);
    };
    const auto unit = [] (int r) { return (float) (r + 25600) / 51200.0f; };
    auto& p = slot.params;
    p[0] = unit (raw (m01)); // R → L
    p[1] = unit (raw (m00)); // L
    p[2] = unit (raw (m11)); // R
    p[3] = unit (raw (m10)); // L → R
    p[4] = 0.5f;             // no delay
    p[5] = 0.5f;             // no phase
    p[6] = 0.0f;
    p[7] = 0.0f;
    return 4;
}

// --- Soundtoys (round 135): her states, read black-box in the official trials -------------------------------------
// A Soundtoys state is text ("WIDGET = …; PRESET =<encoded>;"); the payload is NOT decoded. Each state found in her
// projects was loaded into the official trial through the host API and every parameter read back
// (docs/analise/REA_rodada135s.md §1); the table maps the payload's FNV-1a hash to those settings in LYNX's units.
struct KnownState
{
    std::uint64_t hash;
    std::vector<std::pair<int, float>> values;
};
namespace vs = voiceshift;
namespace em = mixsuite::ember;
namespace ap = autopan;
namespace st135
{
#include "core/SoundtoysStates.inc"
}

std::optional<std::uint64_t> soundtoysPayloadHash (const std::vector<std::uint8_t>& state)
{
    static constexpr char key[] = "PRESET =";
    const auto* b = state.data();
    const auto n = state.size();
    for (std::size_t i = 0; i + sizeof (key) - 1 <= n; ++i)
        if (std::memcmp (b + i, key, sizeof (key) - 1) == 0)
        {
            auto start = i + sizeof (key) - 1, end = start;
            while (end < n && b[end] != ';') ++end;
            if (end >= n) return std::nullopt;
            while (start < end && std::isspace (b[start])) ++start;
            while (end > start && std::isspace (b[end - 1])) --end;
            std::uint64_t h = 0xcbf29ce484222325ull;
            for (auto k = start; k < end; ++k) { h ^= b[k]; h *= 0x100000001b3ull; }
            return h;
        }
    return std::nullopt;
}

template <std::size_t N>
int translateKnown (const KnownState (&table)[N], EffectKind kind, const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    if (slot.kind != kind) return 0;
    const auto h = soundtoysPayloadHash (state);
    if (! h) return 0;
    for (const auto& k : table)
        if (k.hash == *h)
        {
            for (const auto& [p, v] : k.values)
                if (p >= 0 && p < (int) slot.params.size()) slot.params[(std::size_t) p] = v;
            return (int) k.values.size();
        }
    return 0;
}

[[maybe_unused]] int translateAlterBoy (const std::vector<std::uint8_t>& s, EffectSlot& slot) { return translateKnown (st135::alterBoyStates, EffectKind::voiceShift, s, slot); }
[[maybe_unused]] int translateDecapitator (const std::vector<std::uint8_t>& s, EffectSlot& slot) { return translateKnown (st135::decapitatorStates, EffectKind::ember, s, slot); }
[[maybe_unused]] int translatePanMan (const std::vector<std::uint8_t>& s, EffectSlot& slot) { return translateKnown (st135::panManStates, EffectKind::autoPan, s, slot); }
} // namespace

int knownSoundtoysStates() noexcept { return (int) (std::size (st135::alterBoyStates) + std::size (st135::decapitatorStates) + std::size (st135::panManStates)); }
std::optional<std::uint64_t> soundtoysStateHash (const std::vector<std::uint8_t>& state) { return soundtoysPayloadHash (state); }

namespace
{
// ---- Soundtoys (round 136): the encoded preset is not decoded; her saved states are recognised by its hash
namespace st136
{
#include "core/SoundtoysStates136.inc"
}
using namespace st136;

std::uint64_t presetHash (const std::vector<std::uint8_t>& state)
{
    const auto at = findBytes (state, "PRESET =");
    if (at == std::string::npos)
        return 0;
    std::size_t end = at + 8;
    while (end < state.size() && state[end] != '\r')
        ++end;
    if (end > at + 8 && state[end - 1] == ';')
        --end;
    std::uint64_t h = 0xcbf29ce484222325ull; // FNV-1a 64
    for (auto i = at + 8; i < end; ++i)
        h = (h ^ state[i]) * 0x100000001b3ull;
    return h;
}

// ---- Soundtoys (round 190kv): the factory presets, recognised by a fingerprint of the encoded payload ---------------
// docs/analise/REA_rodada190kv.md. Every factory preset of the installed plug-ins was loaded into the official plug-in
// in its own hidden host process (public VST3 API: the preset's archive as the component state, every parameter read
// back as normalised values, the state re-saved) and its values mapped onto LYNX's own engine (round 190is's value maps).
// The payload is still not decoded. Two saves of one setting differ only in a few volatile fields (the last 24
// characters and characters 300 … 369: they change with every save and with what the instance loaded before), so the
// fingerprint is FNV-1a 64 over the payload's length and every other character. Hashes and LYNX values only.
std::uint64_t soundtoysFingerprint (const std::vector<std::uint8_t>& state)
{
    const auto at = findBytes (state, "PRESET =");
    if (at == std::string::npos)
        return 0;
    const std::size_t begin = at + 8;
    std::size_t end = begin;
    while (end < state.size() && state[end] != '\r')
        ++end;
    if (end > begin && state[end - 1] == ';')
        --end;
    constexpr std::size_t volatileFrom = 300, volatileTo = 370, volatileTail = 24;
    const std::size_t n = end - begin;
    if (n <= volatileTail)
        return 0;
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int k = 0; k < 8; ++k)
        h = (h ^ ((n >> (8 * k)) & 0xffu)) * 0x100000001b3ull;
    for (std::size_t i = 0; i < n - volatileTail; ++i)
        if (i < volatileFrom || i >= volatileTo)
            h = (h ^ state[begin + i]) * 0x100000001b3ull;
    return h;
}

namespace st190kv
{
struct FactoryParam
{
    std::uint16_t index;
    float value;
};
struct FactoryState
{
    std::uint64_t fingerprint;
    std::uint16_t first, count; // a run of the plug-in's parameter list
};
struct FactoryTable
{
    const char* widget; // "WIDGET = <name>;" in the state
    EffectKind kind;
    const FactoryState* states;
    std::size_t size;
    const FactoryParam* params;
};
#include "core/SoundtoysFactory190kv.inc"
} // namespace st190kv

// a factory preset of the state's plug-in: the LYNX effect's new-instance values (makeEffectSlot: the current revisions)
// with the preset's mapped values on top; 0 = not a factory preset (the substitute's preset stays, bit for bit as before)
int applyFactoryPreset (std::string_view widget, std::uint64_t fp, EffectSlot& slot)
{
    using namespace st190kv;
    if (fp == 0)
        return 0;
    for (const auto& t : factoryTables)
    {
        if (widget != t.widget || slot.kind != t.kind)
            continue;
        const auto* end = t.states + t.size;
        const auto* hit = std::lower_bound (t.states, end, fp, [] (const FactoryState& x, std::uint64_t v) { return x.fingerprint < v; });
        if (hit == end || hit->fingerprint != fp)
            return 0;
        const auto fresh = makeEffectSlot (slot.kind);
        for (std::size_t i = 0; i < slot.params.size() && i < fresh.params.size(); ++i)
            slot.params[i] = fresh.params[i];
        for (int i = 0; i < hit->count; ++i)
        {
            const auto& p = t.params[hit->first + i];
            if (p.index < slot.params.size())
                slot.params[p.index] = p.value;
        }
        slot.mix = 1.0f;
        return hit->count;
    }
    return 0;
}

std::string soundtoysWidget (const std::vector<std::uint8_t>& state)
{
    const auto w = findBytes (state, "WIDGET =");
    if (w == std::string::npos)
        return {};
    std::size_t a = w + 8, b = a;
    while (b < state.size() && b - a < 64 && state[b] != ';' && state[b] != '\r')
        ++b;
    return trim (std::string (state.begin() + (std::ptrdiff_t) a, state.begin() + (std::ptrdiff_t) b));
}

int translateSoundtoysFactory (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const auto widget = soundtoysWidget (state);
    return widget.empty() ? 0 : applyFactoryPreset (widget, soundtoysFingerprint (state), slot);
}

int translateSoundtoysDecapitator (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace e = mixsuite::ember;
    if (slot.kind != EffectKind::ember)
        return 0;
    const auto h = presetHash (state);
    for (const auto& st : decapitatorStates)
    {
        if (st.hash != h)
            continue;
        static constexpr int order[10] { e::classicStyle, e::classicDrive, e::classicPunish, e::classicLowCut, e::classicThump,
                                         e::classicTone, e::classicHighCut, e::classicSteep, e::classicMix, e::classicOutput };
        slot.params[(std::size_t) e::voicing] = 1.0f;
        for (int i = 0; i < 10; ++i)
            slot.params[(std::size_t) order[i]] = st.v[i];
        slot.mix = 1.0f;
        return 10;
    }
    return translateSoundtoysFactory (state, slot);
}

int translateSoundtoysPanMan (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    if (slot.kind != EffectKind::autoPan || slot.params.size() < (std::size_t) autopan::count + 12)
        return 0;
    const auto h = presetHash (state);
    for (const auto& st : panManStates)
    {
        if (st.hash != h)
            continue;
        for (int i = 0; i < 12; ++i)
            slot.params[(std::size_t) (autopan::count + i)] = st.v[i]; // round 136's Rhythm voicing params
        slot.mix = 1.0f;
        return 12;
    }
    return 0;
}

// Little AlterBoy's normalised pitch, formant, drive, mix, shift mode and formant link on LYNX Tune (round 136)
int applyAlterBoy (const float* v, EffectSlot& slot)
{
    namespace t = lynxtune;
    const double pitch = -12.0 + 24.0 * v[0], formant = -12.0 + 24.0 * v[1];
    const int mode = std::clamp ((int) std::lround (v[4] * 2.0), 0, 2); // transpose, quantize, robot
    auto& p = slot.params;
    p[(std::size_t) t::scale] = 0.0f; // chromatic
    p[(std::size_t) t::retune] = 0.0f;
    p[(std::size_t) t::humanize] = 0.0f;
    p[(std::size_t) t::flexTune] = 0.0f;
    for (int k = 0; k < 12; ++k)
    {
        p[(std::size_t) (t::noteC + k)] = 1.0f;
        p[(std::size_t) (t::bypassC + k)] = mode == 0 ? 1.0f : 0.0f; // Transpose: no correction
    }
    p[(std::size_t) t::transpose] = (float) ((std::clamp (std::lround (pitch), -12L, 12L) + 12) / 24.0);
    const bool wide = std::abs (formant) > 7.0;
    p[(std::size_t) t::wideFormant] = wide ? 1.0f : 0.0f;
    p[(std::size_t) t::formantShift] = (float) std::clamp (formant / (wide ? 24.0 : 14.0) + 0.5, 0.0, 1.0);
    p[(std::size_t) t::formantPreserve] = v[5] >= 0.5f ? 0.0f : 1.0f; // formant link: the formants follow the pitch
    p[(std::size_t) t::driveStage] = 1.0f;
    p[(std::size_t) t::drive] = v[2];
    p[(std::size_t) t::mix] = v[3];
    p[(std::size_t) t::outputTrim] = 0.5f;
    slot.mix = 1.0f;
    return 8;
}

int translateSoundtoysAlterBoy (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace t = lynxtune;
    if (slot.kind != EffectKind::lynxTune || slot.params.size() < (std::size_t) t::count)
        return 0;
    const auto h = presetHash (state);
    for (const auto& st : alterBoyStates)
        if (st.hash == h)
            return applyAlterBoy (st.v, slot);
    // round 190kv: a factory preset (its fingerprint), the same mapping
    if (soundtoysWidget (state) == "Little AlterBoy")
        if (const auto fp = soundtoysFingerprint (state); fp != 0)
            for (const auto& st : st190kv::alterBoyFactory)
                if (st.fingerprint == fp)
                    return applyAlterBoy (st.v, slot);
    return 0;
}

int translateSoundtoysEchoBoy (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace e = echosuite::echoform;
    if (slot.kind != EffectKind::echoform || slot.params.size() < (std::size_t) e::paramCount)
        return 0;
    const auto h = presetHash (state);
    for (const auto& st : echoBoyStates)
    {
        if (st.hash != h)
            continue;
        auto& p = slot.params;
        p[(std::size_t) e::tapeVoicing] = e::tapeVoicingValue (st.voicing);
        p[(std::size_t) e::syncL] = p[(std::size_t) e::syncR] = 0.625f; // a note at the project tempo
        p[(std::size_t) e::noteL] = p[(std::size_t) e::noteR] = st.note;
        p[(std::size_t) e::feedback] = st.loopGain / 2.0f;              // feedbackGain = 2 v
        p[(std::size_t) e::mix] = st.mix;
        p[(std::size_t) e::output] = 0.5f;
        slot.mix = 1.0f;
        return 6;
    }
    return translateSoundtoysFactory (state, slot); // EchoBoy's and EchoBoy Jr's factory presets
}

// round 190ar: fitted setting maps as state translators. A map (reference parameter -> LYNX parameter, a transfer fitted
// black-box on the reference's probe renders) is stored against the entry of the component state that carries that
// parameter — found on states saved through the public parameter API at known settings (the one entry that moves along a
// one-control sweep) — as knots  state value -> LYNX value  (linear between them, or the nearest one for a switch; an
// "add" link is an offset on a value another link set). `fixed` = LYNX parameters the map holds constant.
// The table is generated (core/FittedMaps.inc); nothing of the reference is read but its saved parameter values.
struct FittedLink
{
    const char* id;
    int target;
    bool add, step;
    std::vector<std::pair<float, float>> knots;
};
struct FittedMap
{
    const char* marker; // text that names the reference inside its state
    const char* effect; // the LYNX effect the map was fitted on
    std::vector<std::pair<int, float>> fixed;
    std::vector<FittedLink> links;
};
const std::vector<FittedMap>& fittedMaps()
{
    static const std::vector<FittedMap> maps {
#include "core/FittedMaps.inc"
    };
    return maps;
}

float fittedValue (const FittedLink& l, double v)
{
    const auto& k = l.knots;
    if (k.empty())
        return 0.0f;
    if (l.step)
    {
        auto best = k.front();
        for (const auto& q : k)
            if (std::abs (q.first - v) < std::abs (best.first - v))
                best = q;
        return best.second;
    }
    if (v <= k.front().first)
        return k.front().second;
    for (std::size_t i = 1; i < k.size(); ++i)
        if (v <= k[i].first)
        {
            const double span = (double) k[i].first - k[i - 1].first;
            const double f = span > 0.0 ? (v - k[i - 1].first) / span : 1.0;
            return (float) (k[i - 1].second + f * (k[i].second - k[i - 1].second));
        }
    return k.back().second;
}

// round 190le: LiquidSonics Lustrous Plates -> Foil Plate. Its component state carries a plain JUCE ValueTree
// (LP_FullState > StateVT > PARAM id="<host index>" value=<double in the parameter's own units>; read, nothing decoded:
// BLACKBOX_rodada190lb.md §5.2). The Foil settings are round 190ca's physical fit of the plate's scored impulse renders
// (nebula190ca/maps/maps2/Lustrous_Plates.json: T30 500 Hz … 2 kHz, onset, band energies, L/R correlation, time
// variance; Iron type, full band, Foil's Classic law — the law it was fitted on, kept as the role preset keeps it),
// keyed by the Damper (2.5 / 5.0) with the plate model's offset (Chrome / model 12, which plays Iridium). Dry/Wet goes to
// Foil's Mix (the same wet : dry ratio on its equal-power law), Width scales the fitted width, Master Gain moves the two
// 700 Hz shelves together (a flat wet gain), a Master EQ low cut above the fitted one replaces it.
namespace lustrous
{
// one JUCE ValueTree node (ValueTree::writeToStream): name, properties, children; false = malformed
struct Reader
{
    const std::vector<std::uint8_t>& d;
    std::size_t at;
    bool ok = true;
    long long compressed()
    {
        if (at >= d.size()) { ok = false; return 0; }
        const auto head = d[at++];
        const int n = head & 0x7f;
        if (n > 8 || at + (std::size_t) n > d.size()) { ok = false; return 0; }
        unsigned long long v = 0;
        for (int i = 0; i < n; ++i)
            v |= (unsigned long long) d[at + (std::size_t) i] << (8 * i);
        at += (std::size_t) n;
        return (head & 0x80) != 0 ? -(long long) v : (long long) v;
    }
    std::string cstring()
    {
        std::string s;
        while (at < d.size() && d[at] != 0)
            s += (char) d[at++];
        if (at >= d.size()) ok = false;
        ++at;
        return s;
    }
};
// the StateVT node's PARAM children: id -> value (doubles, ints and numeric strings)
std::map<int, double> stateParams (const std::vector<std::uint8_t>& state)
{
    std::map<int, double> out;
    const auto start = findBytes (state, std::string_view ("StateVT\0", 8));
    if (start == std::string::npos)
        return out;
    Reader r { state, start };
    std::function<bool (int, bool)> node = [&] (int depth, bool keep) -> bool {
        const auto name = r.cstring();
        const auto props = r.compressed();
        if (! r.ok || props < 0 || props > 4096 || depth > 8)
            return false;
        std::string id;
        std::optional<double> value;
        for (long long k = 0; k < props && r.ok; ++k)
        {
            const auto key = r.cstring();
            const auto size = r.compressed();
            if (! r.ok || size < 0 || r.at + (std::size_t) size > state.size())
                return false;
            const auto* p = state.data() + r.at;
            if (size > 0)
            {
                const auto type = p[0];
                double v = std::numeric_limits<double>::quiet_NaN();
                std::string text;
                if (type == 1 && size >= 5) { std::int32_t x; std::memcpy (&x, p + 1, 4); v = x; }
                else if (type == 4 && size >= 9) std::memcpy (&v, p + 1, 8);
                else if (type == 6 && size >= 9) { std::int64_t x; std::memcpy (&x, p + 1, 8); v = (double) x; }
                else if (type == 5) text.assign ((const char*) p + 1, (std::size_t) size - 1);
                if (! text.empty() && text.back() == 0) text.pop_back();
                if (key == "id") id = ! text.empty() ? text : (std::isfinite (v) ? std::to_string ((long long) v) : std::string());
                else if (key == "value")
                {
                    float f = 0;
                    if (std::isfinite (v)) value = v;
                    else if (! text.empty() && parseFloat (text, f) && std::isfinite (f)) value = f;
                }
            }
            r.at += (std::size_t) size;
        }
        if (keep && name == "PARAM" && ! id.empty() && value && id.find_first_not_of ("0123456789") == std::string::npos && id.size() < 4)
            out[std::stoi (id)] = *value;
        const auto children = r.compressed();
        if (! r.ok || children < 0 || children > 4096)
            return false;
        for (long long c = 0; c < children; ++c)
            if (! node (depth + 1, depth == 0))
                return false;
        return true;
    };
    if (! node (0, false))
        out.clear();
    return out;
}
double knots (double x, double x0, double y0, double x1, double y1) noexcept
{
    const auto t = std::clamp ((x - x0) / (x1 - x0), 0.0, 1.0);
    return y0 + t * (y1 - y0);
}
} // namespace lustrous

int translateLustrousPlates (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    if (slot.kind != EffectKind::foilPlate || findBytes (state, "LP_FullState") == std::string::npos)
        return 0;
    const auto v = lustrous::stateParams (state);
    if (v.size() < 10)
        return 0;
    auto at = [&v] (int id, double fallback) {
        const auto it = v.find (id);
        return it != v.end() && std::isfinite (it->second) ? it->second : fallback;
    };
    auto& p = slot.params;
    using lustrous::knots;
    // the host table's units: 0 Master Gain dB, 1 Dry/Wet 0 … 1, 2 Plate Model 0 … 24, 3 Damper 0 … 10, 9 Width 0 … 10,
    // 12 Master EQ, 13 Low Cut Hz
    const auto damper = std::clamp (at (3, 5.0), 0.0, 10.0) / 10.0, model = std::clamp (at (2, 0.0), 0.0, 24.0) / 24.0;
    const auto offset = [model] (double y) { return knots (model, 0.0, 0.0, 0.5, y); };
    p[1] = 0.0f;                                                   // the fit's onset: Foil's own plate delay is already later
    p[2] = (float) std::clamp (knots (damper, 0.25, 0.3876, 0.5, 0.38241) + offset (-0.10713), 0.0, 1.0);
    p[3] = 0.0f;
    p[5] = 0.0f;
    p[6] = 0.04363f;
    p[7] = 0.742f;
    p[9] = 0.0606f;
    p[11] = 0.792f;                                                // Iron (full band, the reference's 16 kHz reach)
    p[12] = 0.0f;
    p[14] = 0.0f;
    p[16] = 0.0f;                                                  // the Classic law (what the fit was made on)
    p[(std::size_t) verbs::foil::revision] = 0.0f;
    const auto width = std::clamp (at (9, 7.5), 0.0, 10.0);
    p[4] = (float) std::clamp (0.36298 * width / 7.5, 0.0, 1.0);
    // Master Gain: both 700 Hz shelves (low −12 … +12 dB over the dial; high +24 dB per dial above its middle)
    const auto gainDb = std::clamp (at (0, 0.0), -20.0, 20.0);
    const auto low = knots (damper, 0.25, 0.65531, 0.5, 0.64922) + offset (0.05273);
    const auto high = knots (damper, 0.25, 0.56156, 0.5, 0.56589) + offset (0.09439);
    p[8] = (float) std::clamp (low + gainDb / 24.0, 0.0, 1.0);
    p[10] = (float) std::clamp (high + gainDb / 24.0, 0.5, 1.0);
    // low cut: the fit's (Foil's 20 Hz … 1 kHz law), or the reference's Master EQ low cut when that is higher
    auto lowCut = (float) std::clamp (knots (damper, 0.25, 0.25244, 0.5, 0.17804) + offset (-0.0248), 0.0, 1.0);
    if (at (12, 0.0) >= 0.5)
    {
        const auto hz = std::clamp (at (13, 20.0), 20.0, 1000.0);
        const auto value = (float) (std::log (hz / 20.0) / std::log (1000.0 / 20.0));
        if (verbs::foil::lowCutHz (value) > verbs::foil::lowCutHz (lowCut))
            lowCut = value;
    }
    p[13] = lowCut;
    // Dry/Wet: dry 0 dB and wet -40 dB per unit below the middle, wet 0 dB and dry falling above it; the ends are one path
    const auto mixV = std::clamp (at (1, 1.0), 0.0, 1.0);
    if (mixV >= 0.9995)
        p[0] = 1.0f;
    else if (mixV <= 0.0005)
        p[0] = 0.0f;
    else
    {
        const auto ratio = std::pow (10.0, (mixV <= 0.5 ? -40.0 * (0.5 - mixV) : 40.0 * (mixV - 0.5)) / 20.0); // wet : dry
        p[0] = (float) (std::atan (ratio) / (3.14159265358979 * 0.5));
    }
    slot.mix = 1.0f;
    return (int) v.size();
}

int translateFittedMap (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    const std::string text (state.begin(), state.end());
    for (const auto& m : fittedMaps())
    {
        if (text.find (m.marker) == std::string::npos)
            continue;
        const auto kind = effectKindNamed (m.effect);
        if (! kind || slot.kind != *kind)
            return 0;
        // the block to read: the selected A / B state of a two-state plugin, else the whole text; the first entry of an id wins
        std::size_t from = 0, to = text.size();
        if (const auto root = text.find ("<SSL_PLUGIN_STATE"); root != std::string::npos)
        {
            const bool useB = attributeIn (text, root, "StateASelected") == "0";
            const auto block = text.find (useB ? "<B " : "<A ", root);
            if (block == std::string::npos)
                return 0;
            from = block;
            const auto close = text.find (useB ? "</B>" : "</A>", block);
            to = close == std::string::npos ? text.size() : close;
        }
        std::map<std::string, double> values;
        for (const char* tag : { "<PARAM ", "<entry " })
            for (auto q = text.find (tag, from); q != std::string::npos && q < to; q = text.find (tag, q + 7))
            {
                const auto id = attributeIn (text, q, "id");
                float value = 0;
                if (! id.empty() && values.find (id) == values.end() && parseFloat (attributeIn (text, q, "value"), value) && std::isfinite (value))
                    values[id] = value;
            }
        int count = 0;
        for (const auto& l : m.links)
            if (values.find (l.id) != values.end())
                ++count;
        if (count == 0)
            return 0; // not a state this map knows: the entry's defaults stay
        for (const auto& [index, value] : m.fixed)
            if (index >= 0 && index < maxEffectParams)
                slot.params[(std::size_t) index] = value;
        for (const auto& l : m.links)
        {
            const auto it = values.find (l.id);
            if (it == values.end() || l.target < 0 || l.target >= maxEffectParams)
                continue;
            auto& p = slot.params[(std::size_t) l.target];
            const auto y = fittedValue (l, it->second);
            p = std::clamp (l.add ? p + y : y, 0.0f, 1.0f);
        }
        return count;
    }
    return 0;
}

// round 190kv (tests, tools): the factory-preset tables
std::uint64_t factoryFingerprintOf (const std::vector<std::uint8_t>& state) { return soundtoysFingerprint (state); }
std::vector<std::uint64_t> factoryFingerprintsOf (std::string_view widget)
{
    std::vector<std::uint64_t> out;
    for (const auto& t : st190kv::factoryTables)
        if (widget == t.widget)
            for (std::size_t i = 0; i < t.size; ++i)
                out.push_back (t.states[i].fingerprint);
    if (widget == "Little AlterBoy")
        for (const auto& st : st190kv::alterBoyFactory)
            out.push_back (st.fingerprint);
    return out;
}
int applyFactoryPresetOf (std::string_view widget, std::uint64_t fp, EffectSlot& slot)
{
    if (widget == "Little AlterBoy")
    {
        if (slot.kind != EffectKind::lynxTune || slot.params.size() < (std::size_t) lynxtune::count)
            return 0;
        for (const auto& st : st190kv::alterBoyFactory)
            if (st.fingerprint == fp)
                return applyAlterBoy (st.v, slot);
        return 0;
    }
    return applyFactoryPreset (widget, fp, slot);
}

// round 190ln: Eiosis AirEQ (and Slate's VM AirEQ Air / Earth, the same filters) -> Facet EQ, in physical units.
// The state is the plug-in's own XML archive of normalised (0..1) parameter values (the current slot first, then the A / B
// copies: the first entry of an id wins). Value laws: the reference's tapers are tables interpolated linearly in the unit
// (seen statically, REA_rodada190ln §3); their points are not copied — the laws below run through the plug-in's own value
// texts at k/8 read through its public parameter API (refcap0124/par). Filter shapes fitted black-box on the references'
// renders (refcap0124 one-control sweeps, refcap182 / trial190k settings) with Facet's own prototypes; every number here
// is LYNX's fit (docs/analise/REA_rodada190ln.md §3).
namespace aireq
{
double knots9 (const double* y, double v) // piecewise linear through the 9 public samples at v = k/8
{
    v = std::clamp (v, 0.0, 1.0) * 8.0;
    const int i = std::min (7, (int) v);
    return y[i] + (v - i) * (y[i + 1] - y[i]);
}
constexpr double bandHz[9] { 10, 85, 240, 530, 1000, 2750, 7500, 16000, 30000 };
constexpr double lowCutHz[9] { 5, 30, 105, 248, 500, 1250, 2750, 5375, 10000 };
constexpr double highCutHz[9] { 20, 85, 240, 530, 1000, 2750, 7500, 16000, 30000 };
constexpr double bandQ[9] { 0.1, 0.24, 0.43, 0.65, 1.0, 1.55, 2.5, 4.5, 7.0 };
constexpr double cutQ[9] { 0.35, 0.4, 0.46, 0.55, 0.7, 1.05, 1.68, 2.68, 4.0 };
constexpr double strengthPct[9] { 0, 26.3, 53, 79.8, 100, 111, 123, 135, 150 };
constexpr int slopeSteps[5] { 6, 12, 18, 24, 36 };

// The bell: a second-order bell (Facet's prototype) whose peak gain and Q follow the dial gain g (dB) and Q q. Fitted on
// the reference's renders (each within 0.03 … 0.07 dB rms): the peak is larger than the dial at small gains and grows with
// the dial's Q as q^0.35; the band widens as |g| grows, its Q following q^0.876 (REA_rodada190ln §3 table).
void bell (double g, double q, double& peakDb, double& facetQ)
{
    const double a = std::abs (g);
    static constexpr double gx[6] { 0.0, 3.0, 4.0, 6.0, 9.0, 18.0 };
    static constexpr double pk[6] { 0.0, 5.6, 6.82, 9.5, 11.6, 18.1 };   // peak dB at q = 1
    static constexpr double qr[6] { 1.05, 1.0, 0.93, 0.80, 0.71, 0.504 }; // Facet Q at q = 1
    int i = 0;
    while (i < 4 && a > gx[i + 1])
        ++i;
    const double t = a > gx[5] ? 1.0 + (a - gx[5]) / (gx[5] - gx[4]) : std::clamp ((a - gx[i]) / (gx[i + 1] - gx[i]), 0.0, 1.0);
    const double peak = pk[i] + t * (pk[i + 1] - pk[i]);
    const double qs = std::max (0.3, qr[i] + t * (qr[i + 1] - qr[i]));
    peakDb = (g < 0 ? -1.0 : 1.0) * peak * std::pow (q, 0.35);
    facetQ = qs * std::pow (q, 0.876);
}
} // namespace aireq

int translateAirEq (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace a = aireq;
    if (slot.kind != EffectKind::facetEq)
        return 0;
    const std::string text (state.begin(), state.end());
    if (text.find ("AEq") == std::string::npos)
        return 0;
    std::map<std::string, double> v;
    for (auto q = text.find ("<entry "); q != std::string::npos; q = text.find ("<entry ", q + 7))
    {
        const auto id = attributeIn (text, q, "id");
        float x = 0;
        if (! id.empty() && v.find (id) == v.end() && parseFloat (attributeIn (text, q, "value"), x))
            v[id] = x;
    }
    const auto get = [&] (const std::string& id, double fallback) { const auto it = v.find (id); return it == v.end() ? fallback : it->second; };
    const auto on = [&] (const std::string& id) { return get (id, 0.25) >= 0.5; };
    if (v.find ("InputGain") == v.end() && v.find ("AirGain") == v.end() && v.find ("EarthGain") == v.end() && v.find ("Band1Gain") == v.end())
        return 0;
    auto& p = slot.params;
    int band = 0, n = 0;
    const bool ms = on ("MidSideModeOn");
    const double strength = a::knots9 (a::strengthPct, get ("Strength", 0.5)) / 100.0;
    const auto place = [&] (const std::string& prefix) {
        const int c = std::clamp ((int) std::lround (get (prefix + "Channels", 1.0 / 6.0) * 3.0 - 0.5), 0, 2);
        return c == 0 ? facet::Placement::stereo : c == 1 ? (ms ? facet::Placement::mid : facet::Placement::left)
                                                          : (ms ? facet::Placement::side : facet::Placement::right);
    };
    const auto gainOf = [&] (const std::string& prefix, const std::string& id) {
        const double g = -18.0 + 36.0 * get (id, 0.5);
        return on (prefix + "ExcludeFromStrength") ? g : g * strength;
    };
    const auto add = [&] (facet::Shape s, double hz, double db, double fq, int slope, facet::Placement pl) {
        if (band >= facet::bands)
            return;
        p[(std::size_t) facet::param (band, 0)] = facet::shapeValue (s);
        p[(std::size_t) facet::param (band, 1)] = facet::hzValue (std::clamp (hz, 10.0, 30000.0));
        p[(std::size_t) facet::param (band, 2)] = facet::gainValue (std::clamp (db, -30.0, 30.0));
        p[(std::size_t) facet::param (band, 3)] = facet::qValue (std::clamp (fq, 0.025, 40.0));
        p[(std::size_t) facet::param (band, 4)] = facet::slopeValue (slope);
        p[(std::size_t) (facet::placementParam0 + band)] = facet::placementValue (pl);
        ++band;
        ++n;
    };
    const auto cut = [&] (const std::string& id, bool high) {
        if (! on (id + "On"))
            return;
        // Eiosis: <id>Type (shallow / steep) + a slope per type; the VM modules: one <id>Slope, same five steps
        const bool vm = v.find (id + "Slope") != v.end();
        const bool steep = ! vm && get (id + "Type", 0.25) >= 0.5;
        const auto slopeId = vm ? id + "Slope" : id + (steep ? "SteepSlope" : "ShallowSlope");
        int slope = a::slopeSteps[std::clamp ((int) std::lround (get (slopeId, steep ? 0.3 : 0.1) * 4.0), 0, 4)];
        const double hz = a::knots9 (high ? a::highCutHz : a::lowCutHz, get (id + (vm ? "Freq" : "Frequency"), 0.5));
        const double q = a::knots9 (a::cutQ, get (id + "Q", 0.5028));
        // its 6 dB/oct high cut takes the Q too (a resonant section with a 6 dB/oct skirt): away from 0.7 it plays best as
        // Facet's 12 dB/oct cut at that Q (refcap0124's Q sweep: 1 … 2 -> 5); the low cut's did not gain from it
        if (high && slope == 6 && std::abs (std::log (q / 0.7)) > 0.1)
            slope = 12;
        // the reference's cuts are Butterworth at Q 0.71 (Facet's Q 1: its highest-Q section × Q)
        add (high ? facet::Shape::highCut : facet::Shape::lowCut, hz, 0.0, q * std::sqrt (2.0), slope, place (id));
    };
    cut (v.find ("LoCutOn") != v.end() ? "LoCut" : "HighPass", false); // the low cut ("LoCut" on the panel)
    // Earth / Air: the reference's own shelves, fitted per dial gain (dB) as Facet shelves: Hz, gain, Q
    const auto shelfFit = [] (const double (*t)[4], int count, double g, double& hz, double& db, double& fq) {
        int i = 0;
        while (i < count - 2 && g > t[i + 1][0])
            ++i;
        const double u = std::clamp ((g - t[i][0]) / (t[i + 1][0] - t[i][0]), 0.0, 1.0);
        hz = std::exp (std::log (t[i][1]) + u * (std::log (t[i + 1][1]) - std::log (t[i][1])));
        db = t[i][2] + u * (t[i + 1][2] - t[i][2]);
        fq = std::exp (std::log (t[i][3]) + u * (std::log (t[i + 1][3]) - std::log (t[i][3])));
    };
    // {dial dB, Hz, gain dB, Q}
    static constexpr double earth[][4] { { -18, 43.3, -22.2, 0.87 }, { -9, 40.3, -10.8, 1.26 }, { 0, 30.0, 0.0, 1.0 },
                                         { 4.5, 15.6, 30.0, 0.52 }, { 9, 20.8, 27.2, 0.53 }, { 18, 35.1, 30.0, 1.04 } };
    static constexpr double air[][4] { { -18, 4601, -30.0, 0.238 }, { -9, 3869, -16.2, 0.383 }, { -4.5, 3515, -9.11, 0.446 },
                                       { 0, 8000, 0.0, 0.45 }, { 2.5, 11079, 9.07, 0.45 }, { 4.5, 9554, 15.2, 0.396 },
                                       { 9, 8167, 23.6, 0.306 }, { 18, 4491, 30.0, 0.282 } };
    if (on ("EarthOn"))
    {
        double hz, db, fq;
        const double g = gainOf ("Earth", "EarthGain");
        shelfFit (earth, 6, g, hz, db, fq);
        if (std::abs (g) > 1e-3)
            add (facet::Shape::lowShelf, hz, db, fq, 12, place ("Earth"));
    }
    for (int b = 1; b <= 5; ++b)
    {
        const auto id = "Band" + std::to_string (b);
        if (! on (id + "On"))
            continue;
        const double hz = a::knots9 (a::bandHz, get (id + "Frequency", 0.5));
        const double q = a::knots9 (a::bandQ, get (id + "Q", 0.5));
        const double g = gainOf (id, id + "Gain");
        const bool shelfType = (b == 1 || b == 5) && get (id + "Type", 0.25) >= 0.5;
        if (shelfType)
        {
            // the reference's shelf at Q q is the cookbook shelf of slope parameter q: Facet's Q law Qs = 0.7071·Q^0.4697
            add (b == 1 ? facet::Shape::lowShelf : facet::Shape::highShelf, hz, g, std::pow (q / 0.7071, 1.0 / 0.4697), 12, place (id));
        }
        // (the "steep" switch, stored as <id>NotchOn, leaves a 0 dB band flat on the references' renders: not modelled)
        else if (std::abs (g) > 1e-3)
        {
            double peak, fq;
            a::bell (g, q, peak, fq);
            add (facet::Shape::bell, hz, peak, fq, 12, place (id));
        }
    }
    if (on ("AirOn"))
    {
        double hz, db, fq;
        const double g = gainOf ("Air", "AirGain");
        shelfFit (air, 8, g, hz, db, fq);
        if (std::abs (g) > 1e-3)
            add (facet::Shape::highShelf, hz, db, fq, 12, place ("Air"));
    }
    cut (v.find ("HiCutOn") != v.end() ? "HiCut" : "LowPass", true); // the high cut ("HiCut")
    for (int b = band; b < facet::bands; ++b)
        p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    // input + output trims (±18 dB each, linear) on Facet's output (±24 dB); the polarity switch on all channels
    const double trim = v.find ("StandaloneInputGain") != v.end()
                            ? -48.0 + 48.0 * get ("StandaloneInputGain", 0.5) + 48.0 * get ("StandaloneOutputGain", 0.5) // VM: ±24 dB each
                            : -36.0 + 36.0 * get ("InputGain", 0.5) + 36.0 * get ("OutputGain", 0.5);
    p[(std::size_t) facet::outputParam] = (float) std::clamp ((trim + 24.0) / 48.0, 0.0, 1.0);
    if (on ("PhaseReverse") && get ("PhaseReverseChannels", 1.0 / 6.0) < 1.0 / 3.0)
        p[(std::size_t) facet::invertParam] = 1.0f;
    if (on ("Bypass"))
        for (int b = 0; b < facet::bands; ++b)
            p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    return std::max (n, 1);
}

// round 190ln: Slate Digital Infinity EQ -> Facet EQ, in physical units. The same XML archive of normalised values as
// AirEQ. Value laws through the plug-in's own value texts (refcap0124/par): frequency 10 Hz × 3000^v (log, exact), gains
// ±36 dB linear, Q 0.35 · 10^(v^1.13), bell width in semitones (the cookbook bandwidth), slope through its texts. Shapes
// fitted black-box on refcap182's five settings (REA_rodada190ln §4): gains as dialled, the bells' width = the cookbook
// bandwidth (Facet Q √2 / (2 sinh(ln 2 / 2 · octaves))), shelves the cookbook shelf of slope Q1. 24 bands: the first eight
// enabled ones play.
namespace infeq
{
constexpr double slopeText[9] { 12, 12, 12, 12, 17.9, 29.6, 48.9, 78.2, 120 };
constexpr double widthSt[9] { 0.15, 4.13, 9.45, 16.5, 26.0, 38.6, 55.5, 78.0, 108 };
constexpr double outText[9] { -36, -21.6, -12, -5.14, 0, 5.14, 12, 21.6, 36 };
constexpr double panText[9] { -200, -100, -66.7, -33.3, 0, 33.3, 66.7, 100, 200 };
constexpr double cutQScale = 1.4142135623730951; // Facet Q = Q1 · √2 · this: its cuts at Q1 0.71 peak +3 dB at the corner (fitted)
constexpr double slopeScale = 0.75;           // the Facet slope nearest 0.75 × the dial's (its 24.7 dB/oct cut plays as 18; fitted)
constexpr double tiltScale = 0.85;            // its tilt: Facet's first-order tilt shelf at 0.85 × the gain (fitted)
} // namespace infeq

int translateInfEq (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace ie = infeq;
    if (slot.kind != EffectKind::facetEq)
        return 0;
    const std::string text (state.begin(), state.end());
    if (text.find ("InfE") == std::string::npos)
        return 0;
    std::map<std::string, double> v;
    for (auto q = text.find ("<entry "); q != std::string::npos; q = text.find ("<entry ", q + 7))
    {
        const auto id = attributeIn (text, q, "id");
        float x = 0;
        if (! id.empty() && v.find (id) == v.end() && parseFloat (attributeIn (text, q, "value"), x))
            v[id] = x;
    }
    const auto get = [&] (const std::string& id, double fallback) { const auto it = v.find (id); return it == v.end() ? fallback : it->second; };
    if (v.find ("Band1State") == v.end())
        return 0;
    auto& p = slot.params;
    int band = 0, n = 0;
    for (int b = 1; b <= 24 && band < facet::bands; ++b)
    {
        const auto id = "Band" + std::to_string (b);
        const double stateV = get (id + "State", 1.0 / 6.0);
        if (stateV < 1.0 / 3.0 || stateV >= 2.0 / 3.0) // disabled / bypassed
            continue;
        const int type = std::clamp ((int) std::floor (get (id + "Type", 2.5 / 12.0) * 12.0), 0, 11);
        const double hz = 10.0 * std::pow (3000.0, std::clamp (get (id + "Frequency", 0.575188), 0.0, 1.0));
        const double g = -36.0 + 72.0 * get (id + "Gain", 0.5);
        const double q1 = 0.35 * std::pow (10.0, std::pow (std::clamp (get (id + "Q1", 0.348672), 0.0, 1.0), 1.13));
        const double oct = aireq::knots9 (ie::widthSt, get (id + "Width", 0.5)) / 12.0;
        const double slopeDb = aireq::knots9 (ie::slopeText, get (id + "Slope", 0.386969));
        int slope = facet::slopes[0];
        for (const int s : facet::slopes)
            if (std::abs (std::log ((double) s / (ie::slopeScale * slopeDb))) < std::abs (std::log ((double) slope / (ie::slopeScale * slopeDb))))
                slope = s;
        facet::Shape shape = facet::Shape::off;
        double gain = g, fq = 1.0;
        switch (type)
        {
            case 0: shape = facet::Shape::lowCut; gain = 0; fq = q1 * std::sqrt (2.0) * ie::cutQScale; break;
            case 1: shape = facet::Shape::lowShelf; fq = std::pow (q1 / 0.7071, 1.0 / 0.4697); slope = 12; break;
            case 2: shape = facet::Shape::bell; fq = std::sqrt (2.0) / (2.0 * std::sinh (std::log (2.0) / 2.0 * std::max (oct, 0.01))); slope = 12; break;
            case 3: shape = facet::Shape::highShelf; fq = std::pow (q1 / 0.7071, 1.0 / 0.4697); slope = 12; break;
            case 4: shape = facet::Shape::highCut; gain = 0; fq = q1 * std::sqrt (2.0) * ie::cutQScale; break;
            case 6: shape = facet::Shape::notch; gain = 0; fq = std::sqrt (2.0) / (2.0 * std::sinh (std::log (2.0) / 2.0 * std::max (oct, 0.01))); slope = 12; break;
            case 7: shape = facet::Shape::tiltShelf; slope = 6; gain = g * ie::tiltScale; break; // a first-order tilt
            default: break;
        }
        if (shape == facet::Shape::off || (facet::hasGain (shape) && std::abs (g) < 1e-3 && get (id + "DynamicState", 0.25) < 0.5))
            continue;
        p[(std::size_t) facet::param (band, 0)] = facet::shapeValue (shape);
        p[(std::size_t) facet::param (band, 1)] = facet::hzValue (std::clamp (hz, 10.0, 30000.0));
        p[(std::size_t) facet::param (band, 2)] = facet::gainValue (std::clamp (gain, -30.0, 30.0));
        p[(std::size_t) facet::param (band, 3)] = facet::qValue (std::clamp (fq, 0.025, 40.0));
        p[(std::size_t) facet::param (band, 4)] = facet::slopeValue (slope);
        // channel: Mono = both; Mid/Side or Left/Right with the band's panning (−100 = mid / left only, +100 = side / right)
        const int mode = std::clamp ((int) std::lround (get (id + "ChannelMode", 1.0 / 6.0) * 3.0 - 0.5), 0, 2);
        const double pan = aireq::knots9 (ie::panText, get (id + "ChannelPanning", 0.5));
        auto pl = facet::Placement::stereo;
        if (mode > 0 && std::abs (pan) >= 50.0)
            pl = mode == 1 ? (pan < 0 ? facet::Placement::mid : facet::Placement::side) : (pan < 0 ? facet::Placement::left : facet::Placement::right);
        p[(std::size_t) (facet::placementParam0 + band)] = facet::placementValue (pl);
        // dynamics: the band moves to its target (dB) above the threshold (−90 … 0 dB linear) or the automatic threshold
        if (get (id + "DynamicState", 0.25) >= 0.5 && facet::hasGain (shape))
        {
            const double range = -36.0 + 72.0 * get (id + "DynamicTarget", 0.5) - g;
            p[(std::size_t) (facet::rangeParam0 + band)] = facet::rangeValue (std::clamp (range, -30.0, 30.0));
            if (get (id + "DynamicAutoThresh", 0.75) >= 0.5)
                p[(std::size_t) (facet::autoThresholdParam0 + band)] = 1.0f;
            else
                p[(std::size_t) (facet::thresholdParam0 + band)] = facet::thresholdValue (std::clamp (-90.0 + 90.0 * get (id + "DynamicThreshold", 0.777778), -80.0, 0.0));
        }
        ++band;
        ++n;
    }
    for (int b = band; b < facet::bands; ++b)
        p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    const double out = aireq::knots9 (ie::outText, get ("Gain", 0.5));
    p[(std::size_t) facet::outputParam] = (float) std::clamp ((out + 24.0) / 48.0, 0.0, 1.0);
    if (get ("PhaseReverse", 0.25) >= 0.5)
        p[(std::size_t) facet::invertParam] = 1.0f;
    if (get ("Bypass", 0.25) >= 0.5)
        for (int b = 0; b < facet::bands; ++b)
            p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    return std::max (n, 1);
}
#include "core/KiloheartsStates.inc" // round 190ln

// ---- round 190mm (scheps): Scheps Omni Channel -> Ridge Strip's hidden omni law (MixSuite.h OmniParam) ----
// The state holds the strip's own RealWorld list (input / output, phase, limiter, PARAM_SLOT_SERILIZATION_PLUGIN = the
// module order as slot numbers 1 … 6, first to last) and one <slot index="k"> per module with the module's own preset
// (its SETUP_A RealWorld list) and <plugin_bypass>. Indices below: REA_rodada190mm_scheps §1 (states of the bank and
// the module presets compared value by value); the left / mid values are taken (both sides linked).
namespace
{
std::vector<double> omniNumbers (std::string_view text)
{
    std::vector<double> out;
    std::size_t p = 0;
    while (p < text.size())
    {
        while (p < text.size() && std::isspace ((unsigned char) text[p])) ++p;
        const auto s = p;
        while (p < text.size() && ! std::isspace ((unsigned char) text[p])) ++p;
        if (p == s) break;
        const std::string tok (text.substr (s, p - s));
        char* end = nullptr;
        const auto v = std::strtod (tok.c_str(), &end);
        out.push_back (tok != "*" && end != nullptr && *end == 0 ? v : std::numeric_limits<double>::quiet_NaN());
    }
    return out;
}
std::string_view omniBetween (std::string_view t, std::string_view a, std::string_view b, std::size_t from = 0)
{
    const auto i = t.find (a, from);
    if (i == std::string_view::npos) return {};
    const auto j = t.find (b, i + a.size());
    return j == std::string_view::npos ? std::string_view {} : t.substr (i + a.size(), j - i - a.size());
}
struct OmniModule { int kind = 0; bool bypass = true; std::vector<double> rw; };
} // namespace

int translateWavesSchepsOmni (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    namespace o = mixsuite::ridge::omni;
    namespace rg = mixsuite::ridge;
    if (slot.kind != EffectKind::ridgeStrip)
        return 0;
    const auto start = findBytes (state, "<PresetChunkXMLTree");
    if (start == std::string::npos)
        return 0;
    const std::string xml (state.begin() + (std::ptrdiff_t) start, state.end());
    const std::string_view x (xml);
    if (omniBetween (x, "<PluginName>", "</PluginName>").find ("Scheps Omni Channel") == std::string_view::npos)
        return 0;
    const auto outer = wavesRealWorldParams (state);
    const auto a = x.find ("<PresetData Setup=\"SETUP_A\"");
    const auto slotsEnd = a == std::string_view::npos ? a : x.find ("</PluginSpecificXMLData>", a);
    if (outer.size() < 58 || slotsEnd == std::string_view::npos)
        return 0;
    OmniModule slots[6];
    for (int k = 0; k < 6; ++k)
    {
        const auto tag = "<slot index=\"" + std::to_string (k) + "\">";
        const auto i = x.find (tag, a);
        if (i == std::string_view::npos || i > slotsEnd) continue;
        const auto j = x.find ("</slot>", i);
        const auto t = x.substr (i, (j == std::string_view::npos ? slotsEnd : j) - i);
        const auto name = omniBetween (t, "<plugin_name>", "</plugin_name>");
        auto& m = slots[k];
        m.kind = name == "Internal_SOC_Pre" ? o::pre : name == "Internal_SOC_Gate" ? o::gate : name == "Internal_SOC_DeEsser" ? o::ds2
               : name == "Internal_SOC_EQ" ? o::eq : name == "Internal_SOC_Comp" ? o::comp : 0;
        m.bypass = omniBetween (t, "<plugin_bypass>", "</plugin_bypass>") != "false";
        const auto pd = t.find ("<PresetData Setup=\"SETUP_A\"");
        if (pd != std::string_view::npos)
            m.rw = omniNumbers (omniBetween (t, "<Parameters Type=\"RealWorld\">", "</Parameters>", pd));
    }
    auto& p = slot.params;
    const auto set = [&p] (int k, float v) { p[(std::size_t) k] = std::clamp (v, 0.0f, 1.0f); };
    const auto val = [] (const std::vector<double>& rw, std::size_t i, double fallback) {
        return i < rw.size() && std::isfinite (rw[i]) ? rw[i] : fallback;
    };
    for (int k = rg::omniLaw; k < rg::omniEnd; ++k) p[(std::size_t) k] = 0.0f;
    set (rg::omniLaw, 1.0f);
    // the order: the slot numbers, first to last (the insert slot, 6, holds no LYNX module)
    int order[5] = {}, n = 0;
    const auto code = (long) std::llround (val (outer, 57, 153426.0));
    const auto digits = std::to_string (code);
    for (const auto c : digits)
    {
        const auto s = c - '1';
        if (s >= 0 && s < 6 && slots[s].kind != 0 && n < 5) order[n++] = slots[s].kind;
    }
    set (rg::omniOrder, n == 5 ? o::orderValue (order) : 0.0f);
    set (rg::omniIn, o::dbValue (std::max (-72.0, val (outer, 0, 0.0))));
    set (rg::omniOut, o::dbValue (std::max (-72.0, val (outer, 4, 0.0))));
    set (rg::omniInvL, val (outer, 2, 0.0) >= 0.5 ? 1.0f : 0.0f);
    set (rg::omniInvR, val (outer, 3, 0.0) >= 0.5 ? 1.0f : 0.0f);
    set (rg::omniLimOn, val (outer, 35, 0.0) >= 0.5 ? 1.0f : 0.0f);
    set (rg::omniLimThr, o::dbValue (val (outer, 36, 0.0)));
    int modules = 0;
    for (const auto& m : slots)
    {
        if (m.kind == 0 || m.rw.empty()) continue;
        const auto& rw = m.rw;
        const auto on = ! m.bypass;
        modules += on ? 1 : 0;
        switch (m.kind)
        {
            case o::pre:
                set (rg::oPreOn, on ? 1.0f : 0.0f);
                set (rg::oSatAmount, (float) (val (rw, 9, 0.0) / 100.0));
                set (rg::oSatOn, val (rw, 10, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oSatType, o::indexValue ((int) std::lround (val (rw, 11, 0.0))));
                set (rg::oThump, val (rw, 12, 0.0) >= 0.5 ? (val (rw, 13, 0.0) >= 0.5 ? 1.0f : 0.5f) : 0.0f);
                set (rg::oHpOn, val (rw, 14, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oHpSlope, o::indexValue ((int) std::lround (val (rw, 15, 1.0))));
                set (rg::oHpHz, o::hzValue (val (rw, 16, 20.0)));
                set (rg::oHpQ, o::qValue (val (rw, 17, 1.0)));
                set (rg::oLpOn, val (rw, 18, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oLpSlope, o::indexValue ((int) std::lround (val (rw, 19, 1.0))));
                set (rg::oLpHz, o::hzValue (val (rw, 20, 20000.0)));
                set (rg::oLpQ, o::qValue (val (rw, 21, 1.0)));
                break;
            case o::gate:
                set (rg::oGateOn, on ? 1.0f : 0.0f);
                set (rg::oGateThr, val (rw, 18, -144.0) <= -72.0 ? 0.0f : o::dbValue (val (rw, 18, -144.0)));
                set (rg::oGateFloor, o::dbValue (val (rw, 23, -60.0)));
                set (rg::oGateClose, o::dbValue (val (rw, 28, 0.0)));
                set (rg::oGateAtt, o::msValue (val (rw, 24, 1.0)));
                set (rg::oGateRel, o::msValue (val (rw, 25, 100.0)));
                set (rg::oGateExp, val (rw, 26, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oGateHpOn, val (rw, 13, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oGateHpHz, o::hzValue (val (rw, 14, 60.0)));
                set (rg::oGateLpOn, val (rw, 15, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oGateLpHz, o::hzValue (val (rw, 16, 15000.0)));
                break;
            case o::ds2:
                set (rg::oDsOn, on ? 1.0f : 0.0f);
                for (int b = 0; b < 2; ++b)
                {
                    const auto i0 = (std::size_t) (b == 0 ? 20 : 28); const int k0 = b == 0 ? rg::oDs1On : rg::oDs2On;
                    set (k0, val (rw, i0, 0.0) >= 0.5 ? 1.0f : 0.0f);
                    set (k0 + 1, o::dbValue (val (rw, i0 + 1, 0.0)));
                    set (k0 + 4, o::dbValue (val (rw, i0 + 2, -12.0)));
                    set (k0 + 3, o::hzValue (val (rw, i0 + 3, b == 0 ? 4490.0 : 300.0)));
                    set (k0 + 2, o::indexValue ((int) std::lround (val (rw, i0 + 4, b == 0 ? 3.0 : 1.0))));
                }
                set (rg::oDsOut, o::dbValue (val (rw, 17, 0.0)));
                set (rg::oDsAtt, o::msValue (val (rw, 12, 42.0) * 0.1));
                set (rg::oDsRel, o::msValue (val (rw, 15, 50.0)));
                break;
            case o::eq:
                set (rg::oEqOn, on ? 1.0f : 0.0f);
                for (int b = 0; b < 4; ++b)
                {
                    const auto i0 = (std::size_t) (6 * b); const int k0 = rg::oEqBand0 + 5 * b;
                    set (k0, val (rw, i0 + 1, 1.0) >= 0.5 ? 1.0f : 0.0f);
                    set (k0 + 1, o::indexValue ((int) std::lround (val (rw, i0 + 2, 1.0))));
                    set (k0 + 2, o::hzValue (val (rw, i0 + 4, 1000.0)));
                    set (k0 + 3, o::dbValue (val (rw, i0 + 3, 0.0)));
                    set (k0 + 4, o::qValue (val (rw, i0 + 5, 1.0)));
                }
                break;
            case o::comp:
                set (rg::oCompOn, on ? 1.0f : 0.0f);
                set (rg::oCompType, o::indexValue ((int) std::lround (val (rw, 29, 0.0))));
                set (rg::oCompRatio, o::ratioValue (val (rw, 38, 3.5)));
                set (rg::oCompAtt, o::msValue (val (rw, 44, 3.0)));
                set (rg::oCompRel, o::msValue (val (rw, 42, 200.0)));
                set (rg::oCompThr, o::dbValue (val (rw, 40, 0.0)));
                set (rg::oCompMix, (float) (val (rw, 78, 100.0) / 100.0));
                set (rg::oCompOut, o::dbValue (val (rw, 34, 0.0)));
                set (rg::oScHpOn, val (rw, 47, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oScHpHz, o::hzValue (val (rw, 49, 80.0)));
                set (rg::oScBellOn, val (rw, 51, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oScBellDb, o::dbValue (val (rw, 53, 0.0)));
                set (rg::oScBellHz, o::hzValue (val (rw, 55, 500.0)));
                set (rg::oScShelfOn, val (rw, 59, 0.0) >= 0.5 ? 1.0f : 0.0f);
                set (rg::oScShelfDb, o::dbValue (val (rw, 61, 0.0)));
                set (rg::oScShelfHz, o::hzValue (val (rw, 63, 8000.0)));
                break;
            default: break;
        }
    }
    return 8 + modules;
}

RawTranslator rawTranslatorNamed (std::string_view name)
{
    if (name == "slate-infinity-eq")
        return translateInfEq;
    if (name == "eiosis-aireq")
        return translateAirEq;
    if (name == "fitted-map")
        return translateFittedMap;
    if (name == "soundtoys-echoboy")
        return translateSoundtoysEchoBoy;
    if (name == "soundtoys-littlealterboy")
        return translateSoundtoysAlterBoy;
    if (name == "soundtoys-decapitator")
        return translateSoundtoysDecapitator;
    if (name == "soundtoys-panman")
        return translateSoundtoysPanMan;
    if (name == "soundtoys-factory")
        return translateSoundtoysFactory;
    if (name == "waves-s1-imager")
        return translateWavesS1Imager;
    if (name == "waves-h-delay")
        return translateWavesHDelay;
    if (name == "waves-ns1")
        return translateWavesNs1;
    if (name == "waves-doubler")
        return translateWavesDoubler;
    if (name == "waves-ssl-channel")
        return translateWavesSslChannel;
    if (name == "ssl-vocalstrip-2")
        return translateSslVocalstrip2;
    if (name == "harrison-strip")
        return translateHarrisonStrip;
    if (name == "lustrous-plates") // round 190le
        return translateLustrousPlates;
    if (name == "vcc-channel")
        return translateVccChannel;
    if (name == "ssl-guitarstrip")
        return translateSslGuitarstrip;
    if (name == "ssl-drumstrip")
        return translateSslDrumstrip;
    if (name == "auto-tune-pro")
        return translateAutoTunePro;
    if (name == "waves-tune")
        return translateWavesTune;
    if (name == "fresh-air")
        return translateFreshAir;
    if (name == "waves-rcomp")
        return translateWavesRComp;
    if (name == "waves-deesser")
        return translateWavesDeEsser;
    if (name == "waves-cla-2a")
        return translateWavesCla2a;
    if (name == "waves-cla-76")
        return translateWavesCla76;
    if (name == "waves-l2")
        return translateWavesL2;
    if (name == "waves-ssl-g-channel")
        return translateWavesSslG;
    if (name == "waves-rchannel")
        return translateWavesRChannel;
    if (name == "waves-api-550")
        return translateWavesApi550;
    if (name == "waves-api-560")
        return translateWavesApi560;
    if (name == "waves-req")
        return translateWavesReq;
    if (name == "waves-q10")
        return translateWavesQ10;
    if (name == "waves-puigtec")
        return translateWavesPuigTec;
    if (name == "waves-c4")
        return translateWavesC4;
    if (name == "waves-rvox") // round 190mm (dyn)
        return translateWavesRVox;
    if (name == "waves-rdeesser")
        return translateWavesRDeEsser;
    if (name == "waves-c6")
        return translateWavesC6;
    if (name == "waves-api-2500")
        return translateWavesApi2500;
    if (name == "waves-maxxbass")
        return translateWavesMaxxBass;
    if (name == "waves-rbass")
        return translateWavesRBass;
    if (name == "waves-vitamin")
        return translateWavesVitamin;
    if (name == "waves-ar-plates")
        return translateWavesArPlates;
    if (name == "waves-atlas")
        return translateWavesAtlas;
    if (name == "waves-supertap")
        return translateWavesSuperTap;
    if (name == "waves-sslcomp")
        return translateWavesSslComp;
    if (name == "waves-sslcomp-bus")
        return translateWavesSslCompBus;
    if (name == "waves-reverb")
        return translateWavesReverb;
    if (name == "kilohearts") // round 190ln
        return translateKilohearts;
    if (name == "waves-scheps-omni") // round 190mm (scheps)
        return translateWavesSchepsOmni;
    return nullptr;
}

// the value of attribute `name` in the element starting at `from` ("" when absent)
std::string attributeIn (const std::string& text, std::size_t from, const std::string& name)
{
    const auto end = text.find ('>', from);
    const auto key = " " + name + "=\"";
    const auto at = text.find (key, from);
    if (at == std::string::npos || (end != std::string::npos && at > end))
        return {};
    const auto start = at + key.size();
    const auto close = text.find ('"', start);
    return close == std::string::npos ? std::string() : text.substr (start, close - start);
}
} // namespace

std::uint64_t soundtoysFactoryFingerprint (const std::vector<std::uint8_t>& state) { return factoryFingerprintOf (state); }
std::vector<std::uint64_t> soundtoysFactoryFingerprints (std::string_view widget) { return factoryFingerprintsOf (widget); }
int applySoundtoysFactoryPreset (std::string_view widget, std::uint64_t fingerprint, EffectSlot& slot) { return applyFactoryPresetOf (widget, fingerprint, slot); }

std::optional<std::pair<int, int>> autoTuneProKey (const std::vector<std::uint8_t>& state)
{
    const auto at = findBytes (state, "<Custom ");
    if (at == std::string::npos || findBytes (state, "<SessionChunkData") == std::string::npos)
        return std::nullopt;
    const std::string text (state.begin() + (std::ptrdiff_t) at, state.begin() + (std::ptrdiff_t) std::min (state.size(), at + 4096));
    float key = -1, scale = -1;
    if (! parseFloat (attributeIn (text, 0, "ScaleGivenKey"), key) || ! parseFloat (attributeIn (text, 0, "ScaleGivenScale"), scale))
        return std::nullopt;
    if (key < 0 || key > 11 || key != std::floor (key))
        return std::nullopt;
    // [EXEC] the scale list's first entries (the official trial's own texts, REA round 120 §5): 0 = Major, 1 = Minor,
    // 2 = Chromatic — as read from her takes in round 113 §2. The other scales (tunings) are not translated.
    // Round 138 (REA_rodada138 §3.1): a state that carries <ModernScales> (saved by a newer Auto-Tune Pro) stores the
    // index of the MODERN scale list instead (0 Chromatic, 1 Major, 2 Minor, 3 Harmonic Minor, 4 Jazz Melodic Minor,
    // 5 Dorian, 6 Phrygian, 7 Lydian, 8 Mixolydian, 9 Locrian, 10 Major Pentatonic, 11 Minor Pentatonic, 12 Blues,
    // 13 Whole Tone, 14 Diminished): [EXEC] her 42 states with "2" load into the official trial as Minor, the one with
    // "1" as Major (the old reading took them for Chromatic / Minor)
    vocal::Scale lynxScale;
    if (findBytes (state, "<ModernScales") != std::string::npos)
    {
        using S = vocal::Scale;
        static constexpr S modern[15] { S::chromatic, S::major, S::minor, S::harmonicMinor, S::melodicMinor, S::dorian, S::phrygian,
                                        S::lydian, S::mixolydian, S::locrian, S::majorPentatonic, S::minorPentatonic, S::blues,
                                        S::wholeTone, S::diminished };
        if (scale < 0.0f || scale > 14.0f || scale != std::floor (scale))
            return std::nullopt;
        lynxScale = modern[(int) scale];
    }
    else if (scale == 0.0f)
        lynxScale = vocal::Scale::major;
    else if (scale == 1.0f)
        lynxScale = vocal::Scale::minor;
    else if (scale == 2.0f)
        lynxScale = vocal::Scale::chromatic;
    else
        return std::nullopt; // the other scales: not confirmed
    return std::pair<int, int> { (int) key, (int) lynxScale };
}

std::optional<std::pair<std::uint16_t, std::uint16_t>> autoTuneProNoteEdits (const std::vector<std::uint8_t>& state, int key, int lynxScale)
{
    // [DATA] <Scales><Scale ScaleIndex="n" Length="7|12"><Bypass Values="0100000"/><Remove Values="…"/>: one digit per
    // scale degree (major / minor: 7 degrees above the root; chromatic: 12 semitones above the root)
    // round 138: a <ModernScales> state keeps one 12-digit row per modern scale (semitones above the root; Remove = the
    // notes that are not targets, the scale's own non-members included)
    if (const auto ms = findBytes (state, "<ModernScales"); ms != std::string::npos)
    {
        using S = vocal::Scale;
        static constexpr S modern[15] { S::chromatic, S::major, S::minor, S::harmonicMinor, S::melodicMinor, S::dorian, S::phrygian,
                                        S::lydian, S::mixolydian, S::locrian, S::majorPentatonic, S::minorPentatonic, S::blues,
                                        S::wholeTone, S::diminished };
        int mi = -1;
        for (int k = 0; k < 15; ++k)
            if ((int) modern[k] == lynxScale)
                mi = k;
        const auto at = mi < 0 ? std::string::npos : findBytes (state, "<Scale ScaleIndex=\"" + std::to_string (mi) + "\"", ms);
        if (at == std::string::npos)
            return std::nullopt;
        const std::string text (state.begin() + (std::ptrdiff_t) at, state.begin() + (std::ptrdiff_t) std::min (state.size(), at + 600));
        auto mask = [&] (const char* element) -> std::optional<std::uint16_t> {
            const auto e = text.find (element);
            if (e == std::string::npos)
                return std::nullopt;
            const auto v = attributeIn (text, e, "Values");
            if (v.size() != 12u)
                return std::nullopt;
            std::uint16_t m = 0;
            for (int d = 0; d < 12; ++d)
                if (v[(std::size_t) d] == '1')
                    m = (std::uint16_t) (m | (1u << ((key + d) % 12)));
            return m;
        };
        const auto bypass = mask ("<Bypass "), remove = mask ("<Remove ");
        if (! bypass || ! remove)
            return std::nullopt;
        return std::pair<std::uint16_t, std::uint16_t> { *bypass, *remove };
    }
    const int index = lynxScale == (int) vocal::Scale::major ? 0 : lynxScale == (int) vocal::Scale::minor ? 1 : lynxScale == (int) vocal::Scale::chromatic ? 2 : -1;
    if (index < 0)
        return std::nullopt;
    const auto at = findBytes (state, "<Scale ScaleIndex=\"" + std::to_string (index) + "\"");
    if (at == std::string::npos)
        return std::nullopt;
    const std::string text (state.begin() + (std::ptrdiff_t) at, state.begin() + (std::ptrdiff_t) std::min (state.size(), at + 600));
    static constexpr int major[7] { 0, 2, 4, 5, 7, 9, 11 }, minor[7] { 0, 2, 3, 5, 7, 8, 10 };
    auto mask = [&] (const char* element) -> std::optional<std::uint16_t> {
        const auto e = text.find (element);
        if (e == std::string::npos)
            return std::nullopt;
        const auto v = attributeIn (text, e, "Values");
        const auto n = index == 2 ? 12u : 7u;
        if (v.size() != n)
            return std::nullopt;
        std::uint16_t m = 0;
        for (std::size_t d = 0; d < n; ++d)
            if (v[d] == '1')
                m = (std::uint16_t) (m | (1u << ((key + (index == 2 ? (int) d : index == 0 ? major[d] : minor[d])) % 12)));
        return m;
    };
    const auto bypass = mask ("<Bypass "), remove = mask ("<Remove ");
    if (! bypass || ! remove)
        return std::nullopt;
    return std::pair<std::uint16_t, std::uint16_t> { *bypass, *remove };
}

std::map<int, double> autoTuneProParams (const std::vector<std::uint8_t>& state)
{
    std::map<int, double> out;
    const auto at = findBytes (state, "ParamsData=\"");
    if (at == std::string::npos || findBytes (state, "<SessionChunkData") == std::string::npos)
        return out;
    const auto start = at + 12;
    const auto end = findBytes (state, "\"", start);
    if (end == std::string::npos)
        return out;
    // JUCE MemoryBlock::toBase64Encoding: "<size>.<chars>", 6 bits per char, least significant bit first
    const std::string text (state.begin() + (std::ptrdiff_t) start, state.begin() + (std::ptrdiff_t) end);
    const auto dot = text.find ('.');
    if (dot == std::string::npos || dot == 0 || dot > 9)
        return out;
    std::size_t size = 0;
    for (std::size_t i = 0; i < dot; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return out;
        size = size * 10 + (std::size_t) (text[i] - '0');
    }
    if (size > (1u << 20))
        return out;
    static constexpr const char* alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    std::string raw (size, '\0');
    std::size_t bit = 0;
    for (std::size_t i = dot + 1; i < text.size() && bit < size * 8; ++i)
    {
        const auto* c = std::strchr (alphabet, text[i]);
        if (c == nullptr || text[i] == '\0')
            return {};
        const auto v = (unsigned) (c - alphabet);
        for (int k = 0; k < 6 && bit < size * 8; ++k, ++bit)
            if ((v >> k) & 1u)
                raw[bit >> 3] = (char) (raw[bit >> 3] | (1 << (bit & 7)));
    }
    // <Parameter X<n>="<value>"/> entries
    for (auto p = raw.find (" X"); p != std::string::npos; p = raw.find (" X", p + 2))
    {
        std::size_t q = p + 2;
        int n = 0, digits = 0;
        while (q < raw.size() && raw[q] >= '0' && raw[q] <= '9' && digits < 4)
            n = n * 10 + (raw[q++] - '0'), ++digits;
        if (digits == 0 || q + 1 >= raw.size() || raw[q] != '=' || raw[q + 1] != '"')
            continue;
        const auto close = raw.find ('"', q + 2);
        if (close == std::string::npos)
            break;
        float v = 0;
        if (parseFloat (raw.substr (q + 2, close - q - 2), v))
            out[n] = (double) v;
    }
    return out;
}

std::optional<PitchCorrectorScale> wavesTuneScale (const std::vector<std::uint8_t>& state)
{
    const auto start = findBytes (state, "<PresetChunkXMLTree");
    if (start == std::string::npos)
        return std::nullopt;
    const std::string xml (state.begin() + (std::ptrdiff_t) start, state.end());
    std::string setup = "SETUP_A";
    if (const auto a = xml.find ("<ActiveSetup>"); a != std::string::npos)
        if (const auto b = xml.find ("</ActiveSetup>", a); b != std::string::npos)
            setup = trim (std::string_view (xml).substr (a + 13, b - a - 13));
    const auto data = xml.find ("<PresetData Setup=\"" + setup + "\"");
    if (data == std::string::npos)
        return std::nullopt;
    const auto end = std::min (xml.find ("</PresetData>", data), xml.size());
    const auto scaleAt = xml.find ("<Scale ", data);
    const auto segAt = xml.find ("<NoteSegmentation>", data);
    const auto segEnd = xml.find ("</NoteSegmentation>", data);
    const auto rootAt = xml.find ("<Root>", data);
    if (scaleAt >= end || segAt >= end || segEnd >= end || rootAt >= end)
        return std::nullopt;
    int root = -1;
    try
    {
        root = std::stoi (xml.substr (rootAt + 6, 4));
    }
    catch (...)
    {
        return std::nullopt;
    }
    if (root < 0 || root > 11)
        return std::nullopt;
    // the twelve notes above the root, in order (NoteCents 0, 100 … 1100): "x" = excluded, anything else = a target
    std::uint16_t rel = 0;
    int count = 0;
    for (auto p = xml.find ("<NoteStatus>", segAt); p < segEnd && count < 12; p = xml.find ("<NoteStatus>", p + 1), ++count)
    {
        const auto q = xml.find ("</NoteStatus>", p);
        if (q == std::string::npos || q > segEnd)
            return std::nullopt;
        if (trim (std::string_view (xml).substr (p + 12, q - p - 12)) != "x")
            rel = (std::uint16_t) (rel | (1u << count));
    }
    if (count != 12 || rel == 0)
        return std::nullopt;
    PitchCorrectorScale out;
    out.key = root;
    out.mask = (std::uint16_t) (((rel << root) | (rel >> (12 - root))) & 0xfff);
    out.scale = (int) vocal::Scale::custom;
    for (int s = 0; s < vocal::scaleCount; ++s)
        if (s != (int) vocal::Scale::custom && vocal::scaleMask ((vocal::Scale) s, root) == out.mask)
        {
            out.scale = s;
            break;
        }
    return out;
}

std::optional<PitchCorrectorScale> pitchCorrectorScale (const std::vector<std::uint8_t>& state)
{
    if (const auto k = autoTuneProKey (state))
        return PitchCorrectorScale { k->first, k->second, vocal::scaleMask ((vocal::Scale) k->second, k->first) };
    if (findBytes (state, "<AUTOTUNEEFXPLUS") != std::string::npos) // Auto-Tune EFX+ (round 127)
    {
        EffectSlot tune = makeEffectSlot (EffectKind::lynxTune);
        if (translateAutoTuneEfx (state, tune) == 0)
            return std::nullopt;
        std::uint16_t mask = 0;
        for (int k = 0; k < 12; ++k)
            if (tune.params[(std::size_t) (lynxtune::noteC + k)] >= 0.5f)
                mask = (std::uint16_t) (mask | (1u << k));
        return PitchCorrectorScale { lynxtune::keyIndex (tune.params[lynxtune::key]), lynxtune::scaleIndex (tune.params[lynxtune::scale]), mask };
    }
    return wavesTuneScale (state);
}

std::optional<std::pair<float, float>> freshAirAmounts (const std::vector<std::uint8_t>& state)
{
    // [DATA] the component state of every Fresh Air slot surveyed (round 113): 2530 bytes, u32 1, u32 size − 8, then
    // two floats 0..1 at 1290 / 1294; [EXEC] the official trial saves its "Mid Air" / "High Air" parameters there
    if (state.size() != 2530)
        return std::nullopt;
    auto u32 = [&state] (std::size_t at) {
        std::uint32_t v = 0;
        std::memcpy (&v, state.data() + at, 4);
        return v;
    };
    auto f32 = [&state] (std::size_t at) {
        float v = 0;
        std::memcpy (&v, state.data() + at, 4);
        return v;
    };
    if (u32 (0) != 1 || u32 (4) != (std::uint32_t) state.size() - 8)
        return std::nullopt;
    const auto mid = f32 (1290), high = f32 (1294);
    if (! std::isfinite (mid) || ! std::isfinite (high) || mid < 0.0f || mid > 1.0f || high < 0.0f || high > 1.0f)
        return std::nullopt;
    return std::pair<float, float> { mid, high };
}


PluginIdentity pluginIdentity (const ForeignPlugin& plugin)
{
    PluginIdentity id;
    id.format = plugin.format;
    id.name = plugin.name;
    id.path = plugin.path;
    id.vst2Id = plugin.vst2Id;
    id.classId = plugin.classId;
    id.hasClassId = plugin.hasClassId;
    id.clapId = plugin.clapId;
    if (plugin.format == ForeignPlugin::Format::vst3)
    {
        std::vector<std::uint8_t> component, controller;
        if (splitFlVst3State (plugin.state, component, controller))
            id.state = std::move (component);
    }
    else if (plugin.format == ForeignPlugin::Format::clap)
    {
        if (plugin.state.size() > 4) // u32 0 + the plugin's own state (REA_rodada22 §2.5)
            id.state.assign (plugin.state.begin() + 4, plugin.state.end());
    }
    else
        id.state = plugin.state;
    return id;
}

// round 190ca: a role entry stands in for several devices whose own default settings differ. `default = <plugin name> |
// <preset items>` gives one of them its own starting preset (the values a setting map fitted on the device's probe
// renders gives at the device's default setting); it is applied over `preset` when that plugin is replaced, with the slot
// mix at 100 % (the effect's own Mix carries the device's dry/wet). Kept beside the table (entry key -> defaults).
// Round 190cm: an item "mix=<0…1>" (lower case, like the `mix` key and a map's target) is the slot's dry/wet instead of
// 100 %: for a device whose own default is all wet (a send reverb) while nothing in its saved state tells the mix it
// was used at — the stand-in then opens at the role's wet amount, as it did before the device had a preset.
struct DeviceDefault
{
    std::string nameKey;
    std::vector<std::pair<int, float>> preset;
    float slotMix = 1.0f;
};
std::mutex& deviceDefaultsLock()
{
    static std::mutex m;
    return m;
}
std::map<std::string, std::vector<DeviceDefault>>& deviceDefaults()
{
    static std::map<std::string, std::vector<DeviceDefault>> d;
    return d;
}

std::vector<SubstituteEntry> parseSubstituteTable (std::string_view text, int* errors)
{
    std::vector<SubstituteEntry> entries;
    int bad = 0;
    std::optional<SubstituteEntry> current;
    std::string lynxName;
    std::vector<std::string> presetItems, replacedItems, mapItems, defaultItems;
    bool entryBad = false;

    auto finish = [&] {
        if (! current)
            return;
        const auto kind = effectKindNamed (lynxName);
        if (! kind || entryBad || current->key.empty())
        {
            ++bad;
            current.reset();
            return;
        }
        current->effect = *kind;
        for (int list = 0; list < 2; ++list)
        for (const auto& item : list == 0 ? presetItems : replacedItems)
        {
            const auto eq = item.rfind ('=');
            float v = 0;
            auto index = eq == std::string::npos ? -1 : paramNamed (*kind, item.substr (0, eq));
            // round 190is: "#<index>" names a hidden parameter (past the effect's info, e.g. Echoform's revision)
            if (index < 0 && eq != std::string::npos && eq > 1 && item[0] == '#')
            {
                float at = -1.0f;
                if (parseFloat (trim (item.substr (1, eq - 1)), at) && at == std::floor (at)
                    && at >= (float) effectInfo (*kind).params.size() && at < (float) maxEffectParams)
                    index = (int) at;
            }
            if (index < 0 || ! parseFloat (trim (item.substr (eq + 1)), v))
            {
                ++bad;
                continue;
            }
            (list == 0 ? current->preset : current->replacedPreset).push_back ({ index, std::clamp (v, 0.0f, 1.0f) });
        }
        for (const auto& item : mapItems)
        {
            SubstituteParamMap m;
            if (parseMap (item, *kind, m))
                current->params.push_back (std::move (m));
            else
                ++bad;
        }
        std::vector<DeviceDefault> defaults;
        for (const auto& line : defaultItems)
        {
            const auto bar = line.find ('|');
            DeviceDefault d;
            if (bar != std::string::npos)
                d.nameKey = matchKey (trim (line.substr (0, bar)));
            if (d.nameKey.empty())
            {
                ++bad;
                continue;
            }
            for (const auto& item : splitList (line.substr (bar + 1), ';'))
            {
                const auto eq = item.rfind ('=');
                float v = 0;
                if (eq != std::string::npos && trim (item.substr (0, eq)) == "mix" && parseFloat (trim (item.substr (eq + 1)), v))
                {
                    d.slotMix = std::clamp (v, 0.0f, 1.0f);
                    continue;
                }
                const auto index = eq == std::string::npos ? -1 : paramNamed (*kind, item.substr (0, eq));
                if (index < 0 || ! parseFloat (trim (item.substr (eq + 1)), v))
                {
                    ++bad;
                    continue;
                }
                d.preset.push_back ({ index, std::clamp (v, 0.0f, 1.0f) });
            }
            defaults.push_back (std::move (d));
        }
        {
            const std::lock_guard<std::mutex> guard (deviceDefaultsLock());
            if (defaults.empty())
                deviceDefaults().erase (current->key);
            else
                deviceDefaults()[current->key] = std::move (defaults);
        }
        if (current->product.empty())
            current->product = current->key;
        entries.push_back (std::move (*current));
        current.reset();
    };

    std::size_t start = 0;
    while (start <= text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const auto line = trim (text.substr (start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            finish();
            current = SubstituteEntry {};
            current->key = trim (std::string_view (line).substr (1, line.size() - 2));
            lynxName.clear();
            presetItems.clear();
            replacedItems.clear();
            mapItems.clear();
            defaultItems.clear();
            entryBad = false;
            continue;
        }
        const auto eq = line.find ('=');
        if (! current || eq == std::string::npos)
        {
            ++bad;
            continue;
        }
        const auto key = lowerAscii (trim (std::string_view (line).substr (0, eq)));
        const auto value = trim (std::string_view (line).substr (eq + 1));
        auto& e = *current;
        if (key == "product")
            e.product = value;
        else if (key == "vendor")
            e.vendor = value;
        else if (key == "role")
        {
            if (lowerAscii (value) == "instrument")
                e.instrument = true;
            else if (lowerAscii (value) != "effect")
                entryBad = true;
        }
        else if (key == "lynx")
            lynxName = value;
        else if (key == "vst2")
        {
            for (const auto& item : splitList (value, ';'))
            {
                std::uint32_t id = 0;
                if (parseVst2Id (item, id))
                    e.vst2Ids.push_back (id);
                else
                    ++bad;
            }
        }
        else if (key == "vst3")
        {
            for (const auto& item : splitList (value, ';'))
            {
                std::array<std::uint8_t, 16> id {};
                if (parseHex16 (item, id))
                    e.vst3ClassIds.push_back (id);
                else
                    ++bad;
            }
        }
        else if (key == "clap")
            for (const auto& item : splitList (value, ';'))
                e.clapIds.push_back (item);
        else if (key == "names")
            for (const auto& item : splitList (value, ';'))
            {
                if (auto k = matchKey (item); ! k.empty())
                    e.names.push_back (std::move (k));
            }
        else if (key == "waves")
            for (const auto& item : splitList (value, ';'))
            {
                if (auto k = matchKey (item); ! k.empty())
                    e.wavesNames.push_back (std::move (k));
            }
        else if (key == "preset")
            for (auto& item : splitList (value, ';'))
                presetItems.push_back (std::move (item));
        else if (key == "replaced") // round 190ly
            for (auto& item : splitList (value, ';'))
                replacedItems.push_back (std::move (item));
        else if (key == "default")
            defaultItems.push_back (value);
        else if (key == "mix")
        {
            float v = 1;
            if (parseFloat (value, v))
                e.mix = std::clamp (v, 0.0f, 1.0f);
            else
                ++bad;
        }
        else if (key == "map")
            mapItems.push_back (value);
        else if (key == "translate")
        {
            if (fabfilter::translatorNamed (value) != nullptr || rawTranslatorNamed (value) != nullptr)
                e.translator = value;
            else
                ++bad;
        }
        else if (key == "vst2params")
        {
            float v = 0;
            if (parseFloat (value, v) && v >= 1 && v <= 100000 && v == std::floor (v))
                e.vst2Params = (int) v;
            else
                ++bad;
        }
        else
            ++bad;
    }
    finish();
    if (errors != nullptr)
        *errors = bad;
    return entries;
}

const std::vector<SubstituteEntry>& pluginSubstitutes()
{
    return registry();
}

int registerPluginSubstitutes (std::string_view tableText)
{
    auto added = parseSubstituteTable (tableText);
    auto& entries = registry();
    for (auto& entry : added)
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const SubstituteEntry& e) { return e.key == entry.key; }), entries.end());
        // later registrations win: they go first, so findPluginSubstitute meets them before the built-in ones
        entries.insert (entries.begin(), std::move (entry));
    }
    return (int) added.size();
}

const SubstituteEntry* findPluginSubstitute (const PluginIdentity& plugin, bool instrument)
{
    const auto& entries = registry();
    auto candidates = [&] (auto&& matches) -> const SubstituteEntry* {
        for (const auto& e : entries)
            if (e.instrument == instrument && matches (e))
                return &e;
        return nullptr;
    };
    // 1) format identifiers
    if (plugin.vst2Id != 0 && (plugin.format == ForeignPlugin::Format::vst2 || plugin.format == ForeignPlugin::Format::none))
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.vst2Ids.begin(), x.vst2Ids.end(), plugin.vst2Id) != x.vst2Ids.end(); }))
            return e;
    if (plugin.hasClassId)
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.vst3ClassIds.begin(), x.vst3ClassIds.end(), plugin.classId) != x.vst3ClassIds.end(); }))
            return e;
    if (! plugin.clapId.empty())
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.clapIds.begin(), x.clapIds.end(), plugin.clapId) != x.clapIds.end(); }))
            return e;
    // 2) a WaveShell sub-plugin, by the name its own state carries
    const auto waves = matchKey (wavesPluginName (plugin.state));
    if (! waves.empty())
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.wavesNames.begin(), x.wavesNames.end(), waves) != x.wavesNames.end(); }))
            return e;
    // 3) names (never the shell's file name: it holds many plugins)
    std::vector<std::string> keys;
    const auto nameKey = matchKey (plugin.name);
    if (nameKey.rfind ("waveshell", 0) == 0)
        return nullptr;
    if (! nameKey.empty())
        keys.push_back (nameKey);
    {
        auto file = plugin.path;
        std::replace (file.begin(), file.end(), '\\', '/');
        if (const auto slash = file.rfind ('/'); slash != std::string::npos)
            file = file.substr (slash + 1);
        if (const auto dot = file.rfind ('.'); dot != std::string::npos)
            file = file.substr (0, dot);
        if (auto k = matchKey (file); ! k.empty() && k.rfind ("waveshell", 0) != 0)
            keys.push_back (std::move (k));
    }
    for (const auto& key : keys)
        if (auto* e = candidates ([&] (const SubstituteEntry& x) {
                return std::find (x.names.begin(), x.names.end(), key) != x.names.end()
                       || (! waves.empty() && std::find (x.wavesNames.begin(), x.wavesNames.end(), key) != x.wavesNames.end());
            }))
            return e;
    return nullptr;
}

EffectSlot makeSubstituteSlot (const SubstituteEntry& entry, const PluginIdentity* original, int* translated)
{
    auto slot = makeEffectSlot (entry.effect);
    // round 190kw: the substitutes' Crest settings were measured on the fitted laws: they keep revision 0 (the Pro-C 3
    // translator, applied below, sets 1)
    if (slot.kind == EffectKind::crestCompressor)
        slot.params[(std::size_t) dyn::crest::revision] = 0.0f;
    // round 190lp: … and their Apex settings the laws before the styles' refit (the Pro-L 2 translator sets 1)
    if (slot.kind == EffectKind::apexLimiter)
        slot.params[(std::size_t) apex::pRevision] = 0.0f;
    if (slot.kind == EffectKind::sentryGate)
        slot.params[(std::size_t) dyn::sentry::revision] = 0.0f;
    for (const auto& [index, value] : entry.preset)
        if (index >= 0 && index < maxEffectParams)
            slot.params[(std::size_t) index] = value;
    slot.mix = entry.mix;
    if (original != nullptr) // round 190ly: the entry's items for a translated device only
        for (const auto& [index, value] : entry.replacedPreset)
            if (index >= 0 && index < maxEffectParams)
                slot.params[(std::size_t) index] = value;
    if (original != nullptr) // round 190ca: the replaced device's own default preset, when the entry carries one
    {
        std::vector<std::string> keys { matchKey (original->name) };
        auto file = original->path;
        std::replace (file.begin(), file.end(), '\\', '/');
        if (const auto slash = file.rfind ('/'); slash != std::string::npos)
            file = file.substr (slash + 1);
        if (const auto dot = file.rfind ('.'); dot != std::string::npos)
            file = file.substr (0, dot);
        keys.push_back (matchKey (file));
        const std::lock_guard<std::mutex> guard (deviceDefaultsLock());
        if (const auto it = deviceDefaults().find (entry.key); it != deviceDefaults().end())
            for (const auto& key : keys)
            {
                const auto d = std::find_if (it->second.begin(), it->second.end(), [&key] (const DeviceDefault& x) { return ! key.empty() && x.nameKey == key; });
                if (d == it->second.end())
                    continue;
                for (const auto& [index, value] : d->preset)
                    if (index >= 0 && index < maxEffectParams)
                        slot.params[(std::size_t) index] = value;
                slot.mix = d->slotMix;
                break;
            }
    }

    int count = 0;
    if (original != nullptr && ! original->state.empty() && ! entry.params.empty())
    {
        std::optional<FlVst2State> vst2;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<double> waves;
        bool attributesRead = false, wavesRead = false;
        for (const auto& m : entry.params)
        {
            std::optional<double> value;
            switch (m.source)
            {
                case SubstituteParamMap::Source::vst2:
                {
                    if (! vst2)
                    {
                        FlVst2State s;
                        vst2 = parseFlVst2State (original->state, s) ? s : FlVst2State {}; // a chunk / unreadable: no params
                    }
                    if (vst2->params.empty())
                        break;
                    // all programs saved: the current one's block (needs the plugin's parameter count)
                    std::size_t base = 0, perProgram = vst2->params.size();
                    if (vst2->allPrograms())
                    {
                        const auto n = (std::size_t) entry.vst2Params;
                        if (n > 0 && vst2->params.size() % n == 0 && (std::size_t) vst2->currentProgram < vst2->params.size() / n)
                        {
                            perProgram = n;
                            base = (std::size_t) std::max (0, vst2->currentProgram) * n;
                        }
                        else if (n > 0 && vst2->params.size() >= n && vst2->currentProgram == 0)
                            perProgram = n;
                        else
                            break; // which block is current is not known
                    }
                    if ((std::size_t) m.index < perProgram)
                        value = (double) vst2->params[base + (std::size_t) m.index];
                    break;
                }
                case SubstituteParamMap::Source::xml:
                {
                    if (! attributesRead)
                    {
                        attributes = juceXmlStateAttributes (original->state);
                        attributesRead = true;
                    }
                    for (const auto& [name, text] : attributes)
                        if (name == m.attribute)
                        {
                            float f = 0;
                            if (parseFloat (text, f))
                                value = (double) f;
                            break;
                        }
                    break;
                }
                case SubstituteParamMap::Source::waves:
                {
                    if (! wavesRead)
                    {
                        waves = wavesRealWorldParams (original->state);
                        wavesRead = true;
                    }
                    if ((std::size_t) m.index < waves.size() && std::isfinite (waves[(std::size_t) m.index]))
                        value = waves[(std::size_t) m.index];
                    break;
                }
            }
            if (! value)
                continue;
            const auto v = mapValue (m, *value);
            if (m.toSlotMix)
                slot.mix = v;
            else if (m.target >= 0 && m.target < maxEffectParams)
                slot.params[(std::size_t) m.target] = v;
            ++count;
        }
    }
    if (original != nullptr && ! original->state.empty() && ! entry.translator.empty())
        if (const auto translate = rawTranslatorNamed (entry.translator))
            count += translate (original->state, slot);
    if (original != nullptr && ! original->state.empty() && ! entry.translator.empty())
        if (const auto translate = fabfilter::translatorNamed (entry.translator))
            if (const auto state = fabfilter::parseState (original->state))
                count += translate (*state, slot);
    // round 135: the header shows the original's preset name (LYNX engines only: an FL effect's preset box lists FL's
    // own presets)
    if (original != nullptr && ! original->state.empty() && flPluginsForEffect (slot.kind).empty())
        slot.presetName = originalPresetName (original->state);
    if (translated != nullptr)
        *translated = count;
    return slot;
}

std::string originalPresetName (const std::vector<std::uint8_t>& state)
{
    if (const auto chunk = findBytes (state, "<PresetChunkXMLTree"); chunk != std::string::npos)
    {
        const auto a = findBytes (state, "<Preset Name=\"", chunk);
        if (a == std::string::npos)
            return {};
        const auto start = a + 14;
        const auto b = findBytes (state, "\"", start);
        if (b == std::string::npos || b - start > 200)
            return {};
        return xmlUnescape (trim (std::string (state.begin() + (std::ptrdiff_t) start, state.begin() + (std::ptrdiff_t) b)));
    }
    for (const auto& [name, value] : juceXmlStateAttributes (state))
        if (name == "presetName")
            return trim (value);
    return {};
}

bool substituteEffectSlot (EffectSlot& slot, const PluginIdentity& identity)
{
    return substituteEffectSlot (slot, identity, SubstituteContext {});
}

bool substituteEffectSlot (EffectSlot& slot, const PluginIdentity& identity, const SubstituteContext& context)
{
    if (slot.kind != EffectKind::plugin)
        return false;
    const auto* entry = findPluginSubstitute (identity, false);
    if (entry == nullptr)
        return false;
    int translated = 0;
    auto replacement = makeSubstituteSlot (*entry, &identity, &translated);
    // a pitch corrector whose own key is unknown: the project's key (its other pitch correctors), with the entry's
    // gentle retune; no key anywhere: the entry's chromatic preset
    if (replacement.kind == EffectKind::lynxTune && context.knowsKey() && ! pitchCorrectorScale (identity.state))
    {
        replacement.params[lynxtune::key] = (float) context.key / 11.0f;
        replacement.params[lynxtune::scale] = (float) context.scale / (float) (vocal::scaleCount - 1);
    }
    replacement.enabled = slot.enabled;
    replacement.mix = std::clamp (slot.mix * replacement.mix, 0.0f, 1.0f); // nested dry/wet crossfades multiply
    replacement.sidechain = slot.sidechain;
    replacement.sidechain2 = slot.sidechain2;
    replacement.substituteFor = slot.plugin;
    if (replacement.substituteFor.name.empty())
        replacement.substituteFor.name = identity.name;
    slot = std::move (replacement);
    return true;
}

SubstituteContext substituteContextOf (const Song& song)
{
    std::vector<std::pair<std::pair<int, int>, int>> votes; // (key, scale) -> slots
    auto look = [&votes] (const ForeignPlugin& plugin) {
        if (plugin.format == ForeignPlugin::Format::none)
            return;
        const auto sc = pitchCorrectorScale (pluginIdentity (plugin).state);
        if (sc && sc->scale != (int) vocal::Scale::custom)
        {
            const std::pair<int, int> k { sc->key, sc->scale };
            auto it = std::find_if (votes.begin(), votes.end(), [&] (const auto& v) { return v.first == k; });
            if (it == votes.end())
                votes.push_back ({ k, 1 });
            else
                ++it->second;
        }
    };
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
        {
            look (fx.plugin.foreign);
            look (fx.substituteFor.foreign);
        }
    SubstituteContext context;
    int best = 0;
    for (const auto& [k, n] : votes)
        if (n > best)
        {
            best = n;
            context.key = k.first;
            context.scale = k.second;
        }
    return context;
}

bool restoreOriginalPlugin (EffectSlot& slot)
{
    if (! slot.isSubstitute())
        return false;
    EffectSlot original;
    original.kind = EffectKind::plugin;
    original.enabled = slot.enabled;
    original.mix = 1.0f; // the substitute's own dry/wet doesn't belong to the plugin; FL's slot mix is not kept
    original.sidechain = slot.sidechain;
    original.sidechain2 = slot.sidechain2;
    original.plugin = std::move (slot.substituteFor);
    slot = std::move (original);
    return true;
}

std::string substituteOriginalName (const EffectSlot& slot)
{
    if (! slot.isSubstitute())
        return {};
    if (! slot.substituteFor.name.empty())
        return slot.substituteFor.name;
    if (! slot.substituteFor.foreign.name.empty())
        return slot.substituteFor.foreign.name;
    return slot.substituteFor.id;
}

std::vector<std::pair<std::string, std::string>> juceXmlStateAttributes (const std::vector<std::uint8_t>& state)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    auto start = findBytes (state, "VC2!");
    start = findBytes (state, "<?xml", start == std::string::npos ? 0 : start);
    if (start == std::string::npos)
        return attributes;
    // the first element after the declaration
    std::size_t i = start + 5;
    while (i < state.size() && state[i] != '>')
        ++i;
    while (i < state.size() && state[i] != '<')
        ++i;
    if (i >= state.size())
        return attributes;
    std::size_t end = i;
    char quote = 0;
    for (; end < state.size(); ++end)
    {
        const auto c = (char) state[end];
        if (quote != 0)
        {
            if (c == quote)
                quote = 0;
        }
        else if (c == '"' || c == '\'')
            quote = c;
        else if (c == '>')
            break;
        else if (c == 0)
            return attributes;
    }
    const std::string tag (state.begin() + (std::ptrdiff_t) i + 1, state.begin() + (std::ptrdiff_t) end);
    // skip the element name, then name="value" pairs
    std::size_t p = 0;
    while (p < tag.size() && ! std::isspace ((unsigned char) tag[p]) && tag[p] != '/')
        ++p;
    while (p < tag.size())
    {
        while (p < tag.size() && (std::isspace ((unsigned char) tag[p]) || tag[p] == '/'))
            ++p;
        const auto nameStart = p;
        while (p < tag.size() && tag[p] != '=' && ! std::isspace ((unsigned char) tag[p]))
            ++p;
        const auto name = tag.substr (nameStart, p - nameStart);
        while (p < tag.size() && std::isspace ((unsigned char) tag[p]))
            ++p;
        if (p >= tag.size() || tag[p] != '=')
            break;
        ++p;
        while (p < tag.size() && std::isspace ((unsigned char) tag[p]))
            ++p;
        if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\''))
            break;
        const auto q = tag[p++];
        const auto close = tag.find (q, p);
        if (close == std::string::npos)
            break;
        attributes.push_back ({ name, xmlUnescape (tag.substr (p, close - p)) });
        p = close + 1;
    }
    return attributes;
}

std::string wavesPluginName (const std::vector<std::uint8_t>& state)
{
    const auto chunk = findBytes (state, "<PresetChunkXMLTree");
    if (chunk == std::string::npos)
        return {};
    const auto a = findBytes (state, "<PluginName>", chunk);
    if (a == std::string::npos)
        return {};
    const auto b = findBytes (state, "</PluginName>", a);
    if (b == std::string::npos)
        return {};
    return xmlUnescape (trim (std::string (state.begin() + (std::ptrdiff_t) a + 12, state.begin() + (std::ptrdiff_t) b)));
}

std::vector<double> wavesRealWorldParams (const std::vector<std::uint8_t>& state)
{
    std::vector<double> values;
    const auto text = wavesParametersText (state);
    std::size_t p = 0;
    while (p < text.size())
    {
        while (p < text.size() && std::isspace ((unsigned char) text[p]))
            ++p;
        const auto start = p;
        while (p < text.size() && ! std::isspace ((unsigned char) text[p]))
            ++p;
        if (p == start)
            break;
        const auto token = text.substr (start, p - start);
        if (token == "*")
        {
            values.push_back (std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        try
        {
            std::size_t used = 0;
            const auto v = std::stod (token, &used);
            values.push_back (used == token.size() ? v : std::numeric_limits<double>::quiet_NaN());
        }
        catch (...)
        {
            values.push_back (std::numeric_limits<double>::quiet_NaN());
        }
    }
    return values;
}

std::string foreignToText (const ForeignPlugin& plugin)
{
    std::vector<std::uint8_t> out;
    putU32 (out, foreignTextMagic);
    putU32 (out, (std::uint32_t) plugin.format);
    putU32 (out, plugin.vst2Id);
    out.insert (out.end(), plugin.classId.begin(), plugin.classId.end());
    putU32 (out, plugin.hasClassId ? 1u : 0u);
    putString (out, plugin.clapId);
    putString (out, plugin.name);
    putString (out, plugin.path);
    putBytes (out, plugin.state.data(), plugin.state.size());
    putU32 (out, (std::uint32_t) plugin.midiInPort);
    putU32 (out, (std::uint32_t) plugin.midiOutPort);
    return base64Encode (out.data(), out.size());
}

bool foreignFromText (const std::string& text, ForeignPlugin& out)
{
    std::vector<std::uint8_t> data;
    if (! base64Decode (text, data))
        return false;
    BinReader r { data };
    if (r.u32() != foreignTextMagic)
        return false;
    ForeignPlugin p;
    const auto format = r.u32();
    if (format > (std::uint32_t) ForeignPlugin::Format::other)
        return false;
    p.format = (ForeignPlugin::Format) format;
    p.vst2Id = r.u32();
    if (r.pos + 16 > data.size())
        return false;
    std::copy (data.begin() + (std::ptrdiff_t) r.pos, data.begin() + (std::ptrdiff_t) r.pos + 16, p.classId.begin());
    r.pos += 16;
    p.hasClassId = r.u32() != 0;
    p.clapId = r.str();
    p.name = r.str();
    p.path = r.str();
    p.state = r.bytes();
    p.midiInPort = (int) r.u32();
    p.midiOutPort = (int) r.u32();
    if (! r.ok)
        return false;
    out = std::move (p);
    return true;
}

std::string base64Encode (const std::uint8_t* data, std::size_t size)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve ((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3)
    {
        const std::uint32_t b0 = data[i], b1 = i + 1 < size ? data[i + 1] : 0, b2 = i + 2 < size ? data[i + 2] : 0;
        const auto v = (b0 << 16) | (b1 << 8) | b2;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < size ? table[(v >> 6) & 63] : '=';
        out += i + 2 < size ? table[v & 63] : '=';
    }
    return out;
}

bool base64Decode (const std::string& text, std::vector<std::uint8_t>& out)
{
    out.clear();
    std::uint32_t buffer = 0;
    int bits = 0, padding = 0;
    for (const auto c : text)
    {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++padding; continue; }
        else if (std::isspace ((unsigned char) c)) continue;
        else return false;
        if (padding > 0)
            return false; // data after padding
        buffer = (buffer << 6) | (std::uint32_t) v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back ((std::uint8_t) (buffer >> bits));
        }
    }
    return padding <= 2;
}
std::uint64_t soundtoysPresetHash (const std::vector<std::uint8_t>& state) { return presetHash (state); }

} // namespace lynx
