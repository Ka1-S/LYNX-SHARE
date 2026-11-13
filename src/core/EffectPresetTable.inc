// LYNX's factory presets (REA round 129) — included by core/EffectPresets.cpp only.
//
// Format: "[<engine name>]" starts an engine, "folder <name>" starts a sub-menu (until the next folder / engine),
// "<preset name> | <parameter> = <value>; …" one preset. Values are written as the editor shows them ("2.5 s",
// "40 %", "Concert Hall"; "#0.25" = a normalised value); parameters not named keep the engine's default, except
// the ones an engine lists in "keep = a; b; …" (they keep the slot's current value: a tuner's key and scale).
// These are LYNX's own starting points (own names and settings), not copies of any third-party preset library.
namespace lynx::presets
{
namespace
{
const char* const builtInTable = R"PRESETS(
[Glow Leveler]
Gentle vocal leveling   | Peak reduction = 35; Gain = 3 dB; Emphasis = 0 %; Voicing = 0.00
Vocal, steady           | Peak reduction = 55; Gain = 6 dB; Emphasis = 0 %; Voicing = 0.00
Bass, even              | Peak reduction = 50; Gain = 5 dB; Emphasis = 0 %; Voicing = 0.00
Bright source, de-harsh | Peak reduction = 45; Gain = 4 dB; Emphasis = 60 %; Voicing = 0.00
Limit, peaks only       | Peak reduction = 40; Gain = 3 dB; Limit = limit; Emphasis = 0 %; Voicing = 0.00
Parallel squash         | Peak reduction = 85; Gain = 12 dB; Mix = 40 %; Emphasis = 0 %; Voicing = 0.00

[Compressor]
RComp-style start      | Voicing = vintage program; Ranges = RComp; Threshold = 0 dB; RC ratio = 1.00:1; RC gain = 0 dB; RC attack = 16 ms; RC release = 160 ms; Limiter = on
Vocal, program         | Voicing = vintage program; Ranges = RComp; Threshold = -18 dB; RC ratio = 3.00:1; RC attack = 3 ms; RC release = 160 ms; Limiter = on
Bus, glue              | Voicing = vintage program; Ranges = RComp; Threshold = -12 dB; RC ratio = 2.00:1; RC attack = 30 ms; RC release = 150 ms; Release curve = opto
Upward lift            | Voicing = vintage program; Ranges = RComp; Threshold = -30 dB; RC ratio = 0.79:1; Limiter = on

[Ridge Strip]
folder Vocals
Vocal, clean lift      | HPF = 75 Hz; LMF gain = -2.5 dB; LMF freq. = 400 Hz; HMF gain = 2 dB; HMF freq. = 3.8 kHz; HF gain = 3 dB; HF freq. = 11.9 kHz; Ratio = 3.7 : 1; Comp. threshold = -18 dB; Comp. release = 0.25 s
Vocal, warm            | HPF = 75 Hz; LF gain = 1.5 dB; LF freq. = 163 Hz; HMF gain = -1.5 dB; HMF freq. = 2.79 kHz; Ratio = 2.2 : 1; Comp. threshold = -15 dB
Rap vocal, upfront     | HPF = 110 Hz; LMF gain = -3 dB; LMF freq. = 516 Hz; HMF gain = 3 dB; HMF freq. = 3.79 kHz; HF gain = 2 dB; Ratio = 5.8 : 1; Comp. threshold = -22 dB; Fast attack = on
folder Instruments
Acoustic guitar        | HPF = 75 Hz; LMF gain = -3 dB; LMF freq. = 274 Hz; HF gain = 3 dB; HF freq. = 8.85 kHz; Ratio = 2.2 : 1; Comp. threshold = -12 dB
Kick, punch            | LF gain = 4 dB; LF freq. = 59 Hz; LMF gain = -4 dB; LMF freq. = 376 Hz; HMF gain = 3 dB; HMF freq. = 3.79 kHz; Ratio = 3.7 : 1; Comp. threshold = -15 dB
Snare, crack           | HPF = 75 Hz; LMF gain = 2 dB; LMF freq. = 200 Hz; HMF gain = 3 dB; HMF freq. = 5.15 kHz; Ratio = 3.7 : 1; Comp. threshold = -18 dB; Fast attack = on
Bass, tight            | HPF = 35 Hz; LF gain = 2 dB; LF freq. = 83 Hz; LMF gain = -2 dB; LMF freq. = 274 Hz; Ratio = 3.7 : 1; Comp. threshold = -18 dB
Drum gate              | Gate range = 30 dB; Gate threshold = -40 dB; Gate release = 0.16 s; Gate fast = on
folder Utility
Filters only           | HPF = 75 Hz; LPF = 17.15 kHz; Dynamics on = off
Phase flip             | Phase = inverted; EQ on = off; Dynamics on = off

[Hush De-esser]
Female vocal       | Frequency = 7.3 kHz; Threshold = -32 dB; Range = 8 dB; Release = 50 ms
Male vocal         | Frequency = 5.7 kHz; Threshold = -30 dB; Range = 8 dB; Release = 50 ms
Gentle, wide band  | Frequency = 6 kHz; Threshold = -28 dB; Range = 5 dB; Wideband = wideband
Harsh esses        | Frequency = 6.5 kHz; Threshold = -38 dB; Range = 14 dB; Band detector = band
Cymbal tamer       | Frequency = 9.5 kHz; Threshold = -30 dB; Range = 6 dB; Release = 112 ms
folder Classic split (band detector)
Classic default    | Voicing = classic split; Frequency = 5.5 kHz; Band detector = band; Wideband = split; Threshold =#0.75
Male S             | Voicing = classic split; Frequency = 4.5 kHz; Band detector = band; Wideband = split; Threshold =#0.75
Male SH            | Voicing = classic split; Frequency = 3.4 kHz; Band detector = band; Wideband = split; Threshold =#0.75
Female S           | Voicing = classic split; Frequency = 6.8 kHz; Band detector = band; Wideband = split; Threshold =#0.75
Female SH          | Voicing = classic split; Frequency = 5.1 kHz; Band detector = band; Wideband = split; Threshold =#0.75

[Quell Denoiser]
Light hiss    | Amount = 15 %
Room noise    | Amount = 30 %
Noisy take    | Amount = 50 %
Heavy cleanup | Amount = 75 %

[Gemini Doubler]
Classic double     | V1 delay = 9.4 ms; V2 delay = 23.7 ms; V1 detune = +6 c; V2 detune = -6 c; V1 gain = -6 dB; V2 gain = -6 dB
Tight double       | V1 delay = 12 ms; V2 delay = 18 ms; V1 detune = +4 c; V2 detune = -4 c; V1 gain = -9 dB; V2 gain = -9 dB; V1 pan = -30°; V2 pan = +30°
Wide chorus        | V1 delay = 20 ms; V2 delay = 27 ms; V1 detune = +10 c; V2 detune = -10 c; V1 depth = +10 c; V2 depth = +10 c; V1 rate = 2.2; V2 rate = 2.2
Stereo slap        | V1 delay = 78 ms; V2 delay = 100 ms; V1 detune = 0 c; V2 detune = 0 c; V1 feedback = 39 %; V2 feedback = 39 %
Octave under       | V1 octave = on; V2 on = off; V1 pan = 0°; V1 gain = -12 dB; V1 delay = 10 ms; V1 detune = 0 c

[LYNX Tune]
keep = Key; Scale; Reference; Range; MIDI; C; C#; D; D#; E; F; F#; G; G#; A; A#; B
Natural            | Retune = 50 ms; Humanize = 50 %; Flex-Tune = 20 %
Pop vocal          | Retune = 20 ms; Humanize = 25 %
Tight              | Retune = 6 ms; Humanize = 10 %
Hard tune (robot)  | Retune = 0 ms; Humanize = 0 %; Flex-Tune = 0 %
Gentle touch       | Retune = 100 ms; Humanize = 75 %; Flex-Tune = 40 %

[Echoform]
folder Tape
Tape slap         | Mode = Tape; Time L = 110 ms; Feedback = 15 %; Mix = 25 %
Tape 1/4          | Mode = Tape; Sync L = note; Note L = 1/4; Feedback = 35 %; Mix = 25 %
Worn cassette     | Mode = Lo-Fi; Sync L = dotted; Note L = 1/8; Feedback = 40 %; Age = 80 %; Mix = 30 %
folder Vocal
Vocal throw 1/4   | Mode = Analog; Style = Ping-pong; Sync L = note; Note L = 1/4; Feedback = 30 %; High cut = 5 kHz; Low cut = 250 Hz; Ducking = 50 %; Mix = 20 %
Vocal 1/8 dotted  | Mode = Digital; Sync L = dotted; Note L = 1/8; Feedback = 25 %; High cut = 6 kHz; Low cut = 300 Hz; Mix = 18 %
Ping-pong 1/8     | Mode = Clean; Style = Ping-pong; Sync L = note; Note L = 1/8; Feedback = 35 %; Mix = 25 %
folder Space
Diffused echo     | Mode = Hi-Fi tape; Time L = 380 ms; Feedback = 55 %; Diffusion = 60 %; Mix = 30 %
Shimmer echo      | Mode = Pitch; Pitch = +12 st; Time L = 420 ms; Feedback = 50 %; Diffusion = 40 %; Mix = 25 %
Reverse wash      | Mode = Reverse; Time L = 500 ms; Feedback = 40 %; Mix = 35 %

[Spiral Echo]
Barber pole up   | Shift = +2.0 Hz; Delay = 120 ms; Feedback = 70 %
Barber pole down | Shift = -2.0 Hz; Delay = 120 ms; Feedback = 70 %
Metallic         | Shift = +40 Hz; Delay = 20 ms; Feedback = 60 %
Synced spiral    | Sync = 1/8; Shift = +5.0 Hz; Feedback = 60 %

[Orbit]
Slow flange      | Mode = Rise; Rate = 0.1 Hz; Depth = 5 ms; Feedback = 50 %
Jet flange       | Mode = Through-zero +; Rate = 0.2 Hz; Depth = 10 ms; Feedback = 60 %
Lush chorus      | Mode = Tri chorus; Rate = 0.5 Hz; Depth = 10 ms; Feedback = 0 %
Doubler          | Mode = Doubler; Rate = 0.3 Hz; Depth = 5 ms; Mix = 40 %
Ensemble strings | Mode = Ensemble; Rate = 0.6 Hz; Depth = 16.8 ms

[Lattice Mod]
Subtle chorus   | Taps = 2; Depth = 25 %; Delay = 10 ms; Mix = 35 %
Wide ensemble   | Taps = 4; Depth = 50 %; Delay = 15 ms; Width = 150 %
Shimmer taps    | Taps = 8; Delay = 250 ms; Feedback = 40 %; Spread = 75 %; Diffusion on = on; Mix = 30 %
Rhythmic taps   | Taps = 4; Sync = 1/8; Feedback = 25 %; Spread = 100 %; Mix = 30 %

[Epoch Verb]
folder Vocal
Vocal plate      | Mode = Plate; Decay = 1.6 s; Predelay = 30 ms; High cut = 6 kHz; Low cut = 196 Hz; Mix = 25 %; Color = Now
Vocal hall       | Mode = Concert Hall; Decay = 2.8 s; Predelay = 40 ms; High cut = 6 kHz; Low cut = 196 Hz; Mix = 22 %; Color = 1980s
Small vocal room | Mode = Room; Decay = 0.86 s; Predelay = 10 ms; Mix = 18 %; Color = Now
folder Spaces
Big hall         | Mode = Bright Hall; Decay = 4 s; Predelay = 20 ms; Mix = 30 %; Color = 1980s
Cathedral        | Mode = Cathedral; Decay = 7 s; Predelay = 51 ms; Mix = 35 %
Chamber          | Mode = Chamber; Decay = 1.8 s; Predelay = 20 ms; Mix = 25 %; Color = 1970s
Gated drums      | Mode = Gated; Decay = 0.86 s; Mix = 30 %; Color = 1980s
folder Effects
Huge ambience    | Mode = Random Space; Decay = 14.6 s; Mod depth = 60 %; Mix = 40 %
Dark chorus wash | Mode = Chorus Space; Decay = 7 s; High cut = 3.66 kHz; Mix = 35 %; Color = 1970s

[Atrium]
Vocal room     | Type = Medium Room; Decay = 0.8 s; Predelay = 10 ms; Mix = 20 %
Large room     | Type = Large Room; Decay = 1.5 s; Mix = 25 %
Bright room    | Type = Bright Room; Decay = 1.2 s; Mix = 22 %
Drum chamber   | Type = Large Chamber; Decay = 1.8 s; Early/late = 40 %; Mix = 25 %
Dark space     | Type = Dark Space; Decay = 6.9 s; Mix = 30 %
Distant space  | Type = Distant Space; Decay = 12 s; Predelay = 62.5 ms; Mix = 30 %

[Foil Plate]
Vocal plate    | Type = Silver; Decay = 1.75 s; Predelay = 20 ms; Low EQ gain = -3 dB; Mix = 25 %
Bright plate   | Type = Platinum; Decay = 2.38 s; High EQ gain = +3 dB; Mix = 25 %
Dark plate     | Type = Lead; Decay = 3 s; High EQ gain = -6 dB; Mix = 25 %
Snare plate    | Type = Nickel; Decay = 1.12 s; Mix = 30 %
Long plate     | Type = Gold; Decay = 5.9 s; Mod depth = 25 %; Mix = 30 %

[Nebula]
Dreamy delay   | Mode = Ripple; Sync = Note; Note = 1/4; Feedback = 60 %; Mix = 30 %
Vast space     | Mode = Abyss; Delay = 588.5 ms; Feedback = 75 %; Density = 50 %; Mix = 35 %
Swell pad      | Mode = Swell; Delay = 300 ms; Feedback = 70 %; Warp = 40 %; Mix = 40 %
Endless        | Mode = Eternity; Delay = 1.17 s; Feedback = 88 %; Mix = 40 %
Short bloom    | Mode = Glint; Delay = 75 ms; Feedback = 50 %; Mix = 25 %

[Halo]
Octave shimmer   | Shift = +12 st; Feedback = 50 %; Size = 62 %; Mix = 30 %
Fifth shimmer    | Shift = +7 st; Feedback = 40 %; Size = 50 %; Mix = 25 %
Octave down dark | Shift = -12 st; Color = Dark; Feedback = 40 %; Mix = 25 %
Plain wash       | Shift mode = No shift; Size = 75 %; Feedback = 25 %; Mix = 30 %

[Horizon]
Tape into hall    | Echo mode = Tape; Reverb mode = Hall; Echo sync = Note; Echo note = 1/8; Echo level = 50 %; Echo feedback = 30 %; Decay = 3.8 s; Mix = 30 %
Vocal echo verb   | Echo mode = Analog; Reverb mode = Plate; Echo sync = Dotted; Echo note = 1/8; Echo level = 38 %; Echo feedback = 25 %; Decay = 2.6 s; Mix = 25 %
Sparkle space     | Echo mode = Sparkle; Reverb mode = Space; Echo level = 50 %; Decay = 7.41 s; Mix = 35 %
Reverse room      | Echo mode = Reverse; Reverb mode = Room; Echo level = 62 %; Decay = 1.4 s; Mix = 30 %
[Facet EQ]
folder Vocals
Vocal, low cut 90 Hz   | 1 Shape = Low Cut; 1 Freq = 90 Hz; 1 Slope = 24 dB/oct
Vocal presence         | 1 Shape = Low Cut; 1 Freq = 80 Hz; 1 Slope = 18 dB/oct; 2 Shape = Bell; 2 Freq = 300 Hz; 2 Gain = -2.5 dB; 2 Q = 1.2; 3 Shape = Bell; 3 Freq = 3.5 kHz; 3 Gain = 2 dB; 3 Q = 0.8; 4 Shape = High Shelf; 4 Freq = 10 kHz; 4 Gain = 2.5 dB
Vocal, tame harshness  | 1 Shape = Low Cut; 1 Freq = 80 Hz; 2 Shape = Bell; 2 Freq = 2.8 kHz; 2 Gain = 0 dB; 2 Q = 2; 2 Range = -4 dB; 2 Threshold = -30 dB
folder Mix
Low end cleanup        | 1 Shape = Low Cut; 1 Freq = 30 Hz; 1 Slope = 48 dB/oct; 2 Shape = Bell; 2 Freq = 250 Hz; 2 Gain = -2 dB; 2 Q = 1
Air                    | 1 Shape = High Shelf; 1 Freq = 12 kHz; 1 Gain = 3 dB
Telephone              | 1 Shape = Low Cut; 1 Freq = 400 Hz; 1 Slope = 24 dB/oct; 2 Shape = High Cut; 2 Freq = 3.4 kHz; 2 Slope = 24 dB/oct; 3 Shape = Bell; 3 Freq = 1.5 kHz; 3 Gain = 4 dB

[Apex Limiter]
Master, transparent    | Gain = 3 dB; Ceiling = -1 dB; Style = Clean; True Peak = On
Master, loud           | Gain = 8 dB; Ceiling = -0.3 dB; Style = Loud; True Peak = On
Bus, glue              | Gain = 2 dB; Ceiling = -0.5 dB; Style = Bus
Safety limiter         | Gain = 0 dB; Ceiling = -0.3 dB; Style = Safe; True Peak = On

[Crest Compressor]
Vocal, even            | Style = Vocal; Threshold = -24 dB; Ratio = 3 : 1; Attack = 5 ms; Release = 120 ms
Drum bus glue          | Style = Bus; Threshold = -18 dB; Ratio = 2 : 1; Attack = 10 ms; Release = 100 ms
Punchy drums           | Style = Punch; Threshold = -20 dB; Ratio = 4 : 1; Attack = 20 ms; Release = 80 ms
Bass, steady           | Style = Opto; Threshold = -22 dB; Ratio = 4 : 1
Pumping                | Style = Pumping; Threshold = -26 dB; Ratio = 8 : 1

[Sentry Gate]
Drum gate              | Style = Classic; Threshold = -35 dB; Range = 40 dB; Attack = 0.1 ms; Hold = 20 ms; Release = 100 ms
Vocal cleanup          | Style = Vocal; Threshold = -45 dB; Range = 15 dB; Release = 200 ms
Gentle expander        | Style = Clean; Threshold = -40 dB; Range = 10 dB

[Ember Saturator]
Warm tape              | Style = tape; Drive = 9 dB
Tube vocal             | Style = tube; Drive = 6 dB; Mix = 60 %
Gritty transistor      | Style = transistor; Drive = 18 dB
Fuzz, parallel         | Style = fuzz; Drive = 24 dB; Mix = 30 %
folder Voicings
Vocal colour           | Voice = Tape pre; Drive = 7 dB; Tone = 15 %; Wide cuts = on; High cut (wide) = 8 kHz; Mix = 88 %; Auto gain = off
Console warmth         | Voice = Console; Drive = 10 dB; Wide cuts = on
Punished drums         | Voice = Grit; Drive = 12 dB; Punish = on; Mix = 50 %
Thick triode bass      | Voice = Triode; Drive = 14 dB; Wide cuts = on; Low cut (wide) = 60 Hz; Thump = on
Pentode edge           | Voice = Pentode; Drive = 16 dB; Wide cuts = on; High cut (wide) = 6 kHz; Steep = 30 dB/oct

[Anvil Bus]
Glue, gentle           | Drive = 10 %; Comp = On; Dry/Wet = 60 %
Warm room              | Drive = 25 %; Drive type = Soft; Damp = 9 kHz; Transients = +20 %; Boom = 15 %; Boom freq. = 50 Hz
Punchy kit             | Drive = 30 %; Drive type = Medium; Crunch = 20 %; Transients = +40 %; Comp = On; Output = -3 dB
Hard hitter            | Drive = 60 %; Drive type = Hard; Crunch = 30 %; Transients = +30 %; Boom = 25 %; Boom freq. = 55 Hz; Comp = On; Output = -6 dB
Tight and dry          | Drive = 15 %; Transients = -40 %; Damp = 12 kHz
Sub thump              | Drive = 20 %; Boom = 50 %; Boom freq. = 45 Hz; Boom decay = 80 %; Damp = 10 kHz
Crushed tape           | Drive = 80 %; Drive type = Soft; Crunch = 50 %; Damp = 4 kHz; Output = -6 dB
Parallel smash         | Drive = 70 %; Drive type = Hard; Comp = On; Dry/Wet = 40 %

[Voice Shift]
Hard tune              | Mode = Quantize; Formant = +1.2 st; Drive = 4.6
Octave down            | Pitch = -12.0 st; Formant = -12.0 st; Drive = 5.5
Deeper voice           | Formant = -3.0 st
Brighter voice         | Formant = +2.5 st
Chipmunk               | Pitch = +7.0 st; Link = On
Robot                  | Mode = Robot; Drive = 3.0
Harmony a fifth up     | Pitch = +7.0 st; Mix = 50 %

[Aether]
Vocal air              | Mid air = 25 %; High air = 45 %
Gentle sheen           | Mid air = 10 %; High air = 25 %
Bright                 | Mid air = 40 %; High air = 75 %
)PRESETS";
}
}
