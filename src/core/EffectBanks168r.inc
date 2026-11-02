// LYNX's own preset banks for the FL effects that run without FL's files and for LYNX's simple built-ins (round 168r) —
// included by core/EffectPresets.cpp after EffectPresetTable.inc and parsed with it (same format: "[engine]",
// "folder <name>", "<preset> | <parameter> = <value>; …", values as the editor shows them or "#0.25" normalised).
// Clean-room: settings designed for LYNX (own names and values), not taken or converted from FL's preset files; FL's
// factory presets stay listed beside them when FL Studio is installed. Every preset is rendered by the core tests
// (tests/tests.cpp, design168r_*): no silence, no clipping, no NaN.
namespace lynx::presets
{
namespace
{
const char* const effectBanks168r = R"PRESETS(

[Fruity Fast LP]
Open                 | Cutoff = 100 %; Resonance = 0 %
Warm roll-off        | Cutoff = 75 %; Resonance = 10 %
Muffled              | Cutoff = 50 %; Resonance = 5 %
Behind the wall      | Cutoff = 35 %; Resonance = 15 %
Sub only             | Cutoff = 18 %; Resonance = 0 %
Resonant edge        | Cutoff = 60 %; Resonance = 55 %
Acid peak            | Cutoff = 48 %; Resonance = 78 %

[Fruity Bass Boost]
Subtle sub           | Frequency = 50 Hz; Amount = 25 %
Kick weight          | Frequency = 60 Hz; Amount = 45 %
Deep 808             | Frequency = 40 Hz; Amount = 55 %
Bass warmth          | Frequency = 90 Hz; Amount = 35 %
Heavy low end        | Frequency = 70 Hz; Amount = 65 %

[Fruity Delay]
Eighth echo          | Input = 33 %; Feedback = 40 %; Cutoff = 100 %; Time = 2/16; Mode = Normal
Dotted eighth        | Input = 33 %; Feedback = 45 %; Cutoff = 70 %; Time = 3/16; Mode = Normal
Quarter ping-pong    | Input = 30 %; Feedback = 50 %; Cutoff = 80 %; Time = 4/16; Mode = Ping-pong
Wide inverted        | Input = 33 %; Feedback = 35 %; Cutoff = 90 %; Time = 3/16; Mode = Inverted stereo
Slapback             | Input = 40 %; Feedback = 10 %; Cutoff = 85 %; Time = 1/16; Mode = Normal
Dub tail             | Input = 30 %; Feedback = 68 %; Cutoff = 45 %; Time = 6/16; Mode = Ping-pong
Half-bar space       | Input = 25 %; Feedback = 55 %; Cutoff = 60 %; Time = 8/16; Mode = Normal

[Fruity 7 Band EQ]
Flat                 | 63 Hz = 0 dB; 250 Hz = 0 dB; 500 Hz = 0 dB; 1.5 kHz = 0 dB; 3 kHz = 0 dB; 5 kHz = 0 dB; 8 kHz = 0 dB
Smile                | 63 Hz = 4 dB; 250 Hz = 1 dB; 500 Hz = -1 dB; 1.5 kHz = -2 dB; 3 kHz = 0 dB; 5 kHz = 2 dB; 8 kHz = 4 dB
Vocal presence       | 63 Hz = -4 dB; 250 Hz = -2 dB; 500 Hz = -1 dB; 1.5 kHz = 1 dB; 3 kHz = 3 dB; 5 kHz = 2 dB; 8 kHz = 1 dB
Warm                 | 63 Hz = 2 dB; 250 Hz = 2 dB; 500 Hz = 1 dB; 3 kHz = -1 dB; 5 kHz = -2 dB; 8 kHz = -3 dB
Mud cut              | 250 Hz = -4 dB; 500 Hz = -3 dB
Air                  | 5 kHz = 2 dB; 8 kHz = 5 dB
Tight bass           | 63 Hz = 3 dB; 250 Hz = -3 dB; 500 Hz = -1 dB
Telephone            | 63 Hz = -18 dB; 250 Hz = -10 dB; 500 Hz = 2 dB; 1.5 kHz = 6 dB; 3 kHz = 3 dB; 5 kHz = -10 dB; 8 kHz = -18 dB

[Fruity Center]
On (remove DC)       | On = On
Off                  | On = Off

[Fruity Phase Inverter]
Invert left          | Invert = Left
Invert right         | Invert = Right
No inversion         | Invert = None

[Spreader]
Subtle width         | Width = 60 %; Separation = 0 %; Mono bass = off
Wide pad             | Width = 82 %; Separation = 25 %; Mono bass = off
Super wide           | Width = 100 %; Separation = 55 %; Mono bass = off
Wide, mono low end   | Width = 80 %; Separation = 20 %; Low bypass = 150 Hz; Mono bass = on
Narrower             | Width = 30 %; Separation = 0 %
Separation only      | Width = 50 %; Separation = 70 %

[Frequency Shifter]
Gentle detune        | Type = Frequency shifter; Range = 200 Hz; Frequency = #0.53; Mix = 40 %; Feedback = 0 %
Barber-pole rise     | Type = Frequency shifter; Range = 200 Hz; Frequency = #0.56; Feedback = 55 %; Mix = 60 %
Falling spiral       | Type = Frequency shifter; Range = 200 Hz; Frequency = #0.44; Feedback = 50 %; Mix = 60 %
Stereo swirl         | Type = Frequency shifter; Range = 200 Hz; Frequency = #0.52; L/R phase offset = 90°; Mix = 50 %
Metallic ring        | Type = Ring modulator; Range = 20 kHz; Frequency = #0.72; Mix = 45 %
Robot voice          | Type = Ring modulator; Range = 20 kHz; Frequency = #0.66; Mix = 70 %
Bright shift up      | Type = Frequency shifter HQ; Range = 20 kHz; Frequency = #0.70; Mix = 35 %

[Low Lifter]
Gentle lift          | On = On; Dry/Wet = 50 %; Low band gain = #0.5; Slope = #0.7; Pre gain = #0.5
Fuller bass          | On = On; Dry/Wet = 75 %; Low band gain = #0.6; Slope = #0.6; Pre gain = #0.55
Small speakers       | On = On; Dry/Wet = 100 %; Low band gain = #0.45; Slope = #0.5; Pre gain = #0.6
Multiband, tight     | On = On; Multiband = On; Dry/Wet = 80 %; Low band gain = #0.55; High band gain = #0.9; Crossover = #0.85; Slope = #0.7
Diffused warmth      | On = On; Dry/Wet = 70 %; Diffuser mix = 40 %; Diffuser depth = #0.6; Slope = #0.65

[Peak Controller]
Follow the peaks     | Base = 0 %; Amount = #0.85; Tension = #0.5; Decay = #0.5; LFO amount = #0.5
Duck (inverted peak) | Base = 100 %; Amount = #0.15; Tension = #0.5; Decay = #0.45; LFO amount = #0.5
Slow release         | Base = 0 %; Amount = #0.85; Decay = #0.3
Slow sine LFO        | LFO base = #0.5; LFO amount = #0.75; LFO speed = #0.62; LFO shape = Sine; Amount = #0.5
Fast LFO             | LFO base = #0.5; LFO amount = #0.75; LFO speed = #0.2; LFO shape = Triangle; Amount = #0.5
Random steps         | LFO amount = #1; LFO speed = #0.2; LFO shape = Random; Ramp = on; Amount = #0.5

[Pitch correction]
folder Correction
Natural              | Speed = #0.55; Fine = #0.5; Formant = off
Fast                 | Speed = #0.85; Fine = #0.5
Hard tune            | Speed = instant; Fine = #0.5
Formant-safe         | Speed = #0.7; Formant = on
folder Major keys
C major              | C = yes; C# = no; D = yes; D# = no; E = yes; F = yes; F# = no; G = yes; G# = no; A = yes; A# = no; B = yes; Speed = #0.8
G major              | C = yes; C# = no; D = yes; D# = no; E = yes; F = no; F# = yes; G = yes; G# = no; A = yes; A# = no; B = yes; Speed = #0.8
D major              | C = no; C# = yes; D = yes; D# = no; E = yes; F = no; F# = yes; G = yes; G# = no; A = yes; A# = no; B = yes; Speed = #0.8
A major              | C = no; C# = yes; D = yes; D# = no; E = yes; F = no; F# = yes; G = no; G# = yes; A = yes; A# = no; B = yes; Speed = #0.8
E major              | C = no; C# = yes; D = no; D# = yes; E = yes; F = no; F# = yes; G = no; G# = yes; A = yes; A# = no; B = yes; Speed = #0.8
F major              | C = yes; C# = no; D = yes; D# = no; E = yes; F = yes; F# = no; G = yes; G# = no; A = yes; A# = yes; B = no; Speed = #0.8
Bb major             | C = yes; C# = no; D = yes; D# = yes; E = no; F = yes; F# = no; G = yes; G# = no; A = yes; A# = yes; B = no; Speed = #0.8
Eb major             | C = yes; C# = no; D = yes; D# = yes; E = no; F = yes; F# = no; G = yes; G# = yes; A = no; A# = yes; B = no; Speed = #0.8
folder Minor keys
A minor              | C = yes; C# = no; D = yes; D# = no; E = yes; F = yes; F# = no; G = yes; G# = no; A = yes; A# = no; B = yes; Speed = #0.8
E minor              | C = yes; C# = no; D = yes; D# = no; E = yes; F = no; F# = yes; G = yes; G# = no; A = yes; A# = no; B = yes; Speed = #0.8
D minor              | C = yes; C# = no; D = yes; D# = no; E = yes; F = yes; F# = no; G = yes; G# = no; A = yes; A# = yes; B = no; Speed = #0.8
C minor              | C = yes; C# = no; D = yes; D# = yes; E = no; F = yes; F# = no; G = yes; G# = yes; A = no; A# = yes; B = no; Speed = #0.8
F# minor             | C = no; C# = yes; D = yes; D# = no; E = yes; F = no; F# = yes; G = no; G# = yes; A = yes; A# = no; B = yes; Speed = #0.8
G minor              | C = yes; C# = no; D = yes; D# = no; E = no; F = yes; F# = no; G = yes; G# = no; A = yes; A# = yes; B = no; Speed = #0.8
folder Effects
Lower voice          | Gender = #0.3; Formant = on; Speed = #0.6
Higher voice         | Gender = #0.7; Formant = on; Speed = #0.6

[EQ]
Flat                 | Low = 0 dB; Mid = 0 dB; High = 0 dB; Output = 0 dB
Bass boost           | Low = 4 dB; Output = -2 dB
Bright               | High = 4 dB; Output = -1.5 dB
Mid scoop            | Low = 2 dB; Mid = -4 dB; Mid freq. = 800 Hz; High = 2 dB
Vocal presence       | Low = -3 dB; Mid = 3 dB; Mid freq. = 3000 Hz; High = 1.5 dB; Output = -1 dB
Warm                 | Low = 2 dB; Mid = 1 dB; Mid freq. = 300 Hz; High = -2.5 dB
Kick punch           | Low = 3 dB; Mid = -4 dB; Mid freq. = 400 Hz; High = 2 dB; Output = -1.5 dB
Telephone            | Low = -18 dB; Mid = 6 dB; Mid freq. = 1500 Hz; High = -15 dB
De-box               | Mid = -3 dB; Mid freq. = 500 Hz
Hi-hat tame          | High = -4 dB

[Delay]
Eighth echo          | Time = #0.19; Feedback = 35 %; Mix = 25 %; Ping-pong = off; Damping = 30 %
Dotted eighth        | Time = #0.31; Feedback = 40 %; Mix = 25 %; Ping-pong = off; Damping = 40 %
Quarter ping-pong    | Time = #0.44; Feedback = 45 %; Mix = 25 %; Ping-pong = on; Damping = 35 %
Slapback             | Time = #0.06; Feedback = 8 %; Mix = 30 %; Ping-pong = off; Damping = 20 %
Dub tail             | Time = #0.56; Feedback = 70 %; Mix = 30 %; Ping-pong = on; Damping = 70 %
Half-note wash       | Time = #0.69; Feedback = 60 %; Mix = 22 %; Ping-pong = on; Damping = 60 %
Vocal throw          | Time = #0.44; Feedback = 30 %; Mix = 20 %; Ping-pong = off; Damping = 55 %
One-bar repeat       | Time = #0.94; Feedback = 40 %; Mix = 25 %; Ping-pong = off; Damping = 30 %

[Reverb]
Small room           | Room = 30 %; Damping = 55 %; Width = 80 %; Mix = 18 %; Dry = 100 %; Predelay = 5 ms
Vocal plate          | Room = 55 %; Damping = 35 %; Width = 100 %; Mix = 22 %; Dry = 100 %; Low cut = 200 Hz; Predelay = 30 ms
Large hall           | Room = 85 %; Damping = 45 %; Width = 100 %; Mix = 25 %; Dry = 100 %; Low cut = 120 Hz; Predelay = 25 ms
Dark chamber         | Room = 65 %; Damping = 80 %; Width = 90 %; Mix = 22 %; Dry = 100 %; High cut = 4.0 kHz
Bright ambience      | Room = 40 %; Damping = 15 %; Width = 100 %; Mix = 15 %; Dry = 100 %; Low cut = 300 Hz
Drum room            | Room = 35 %; Damping = 50 %; Width = 70 %; Mix = 20 %; Dry = 100 %; Low cut = 150 Hz; High cut = 8.0 kHz
Huge space           | Room = 98 %; Damping = 30 %; Width = 100 %; Mix = 35 %; Dry = 90 %; Low cut = 150 Hz; Predelay = 60 ms
Wet only (send)      | Room = 70 %; Damping = 45 %; Width = 100 %; Mix = 100 %; Dry = 0 %; Low cut = 150 Hz

[Limiter]
Safety ceiling       | Ceiling = -0.3 dB; Gain = 0 dB; Release = 80 ms
Loud master          | Ceiling = -0.5 dB; Gain = 6 dB; Release = 120 ms
Gentle glue          | Ceiling = -1.0 dB; Gain = 2 dB; Release = 250 ms
Drum smash           | Ceiling = -1.0 dB; Gain = 10 dB; Release = 40 ms
Broadcast -1 dB      | Ceiling = -1.0 dB; Gain = 3 dB; Release = 150 ms

[Filter]
Low-pass, warm       | Type = Low-pass; Cutoff = 4000 Hz; Reso. = Q 0.71; Mix = 100 %
Low-pass, dark       | Type = Low-pass; Cutoff = 900 Hz; Reso. = Q 0.9; Mix = 100 %
Resonant low-pass    | Type = Low-pass; Cutoff = 1200 Hz; Reso. = Q 5; Mix = 100 %
High-pass, thin      | Type = High-pass; Cutoff = 400 Hz; Reso. = Q 0.71; Mix = 100 %
Rumble cut           | Type = High-pass; Cutoff = 40 Hz; Reso. = Q 0.71; Mix = 100 %
Radio band           | Type = Band-pass; Cutoff = 1500 Hz; Reso. = Q 1.2; Mix = 100 %
Wah peak             | Type = Band-pass; Cutoff = 800 Hz; Reso. = Q 6; Mix = 70 %

[Distortion]
Warm saturation      | Drive = 6 dB; Tone = 9000 Hz; Mix = 100 %; Output = -3 dB
Crunch               | Drive = 15 dB; Tone = 6000 Hz; Mix = 100 %; Output = -6 dB
Fuzz                 | Drive = 30 dB; Tone = 3500 Hz; Mix = 100 %; Output = -9 dB
Parallel grit        | Drive = 20 dB; Tone = 5000 Hz; Mix = 35 %; Output = -3 dB
Bass drive           | Drive = 12 dB; Tone = 2500 Hz; Mix = 60 %; Output = -4 dB
Lo-fi radio          | Drive = 18 dB; Tone = 1800 Hz; Mix = 100 %; Output = -6 dB

[Chorus]
Subtle widen         | Rate = 0.3 Hz; Depth = 2.0 ms; Mix = 30 %; Feedback = 0 %
Classic chorus       | Rate = 0.8 Hz; Depth = 3.5 ms; Mix = 50 %; Feedback = 10 %
Lush pad             | Rate = 0.25 Hz; Depth = 6.0 ms; Mix = 55 %; Feedback = 20 %
Vibrato-ish          | Rate = 4.5 Hz; Depth = 2.5 ms; Mix = 100 %; Feedback = 0 %
Metallic flange      | Rate = 0.15 Hz; Depth = 1.0 ms; Mix = 50 %; Feedback = 60 %

[Utility]
Unity                | Gain = 0 dB; Balance = center; L -> L = 1.00; R -> L = 0.00; L -> R = 0.00; R -> R = 1.00
Mono                 | L -> L = 0.50; R -> L = 0.50; L -> R = 0.50; R -> R = 0.50
Swap left / right    | L -> L = 0.00; R -> L = 1.00; L -> R = 1.00; R -> R = 0.00
Left only            | L -> L = 1.00; R -> L = 0.00; L -> R = 1.00; R -> R = 0.00
Right only           | L -> L = 0.00; R -> L = 1.00; L -> R = 0.00; R -> R = 1.00
Phase flip           | L -> L = -1.00; R -> L = 0.00; L -> R = 0.00; R -> R = -1.00
Side only            | L -> L = 0.50; R -> L = -0.50; L -> R = -0.50; R -> R = 0.50
-6 dB pad            | Gain = -6 dB
+6 dB boost          | Gain = 6 dB

[Lo-fi]
Light grit           | Rate = 22050 Hz; Bits = 12.0 bits; Mix = 60 %
Retro sampler        | Rate = 16000 Hz; Bits = 12.0 bits; Mix = 100 %
8-bit console        | Rate = 11025 Hz; Bits = 8.0 bits; Mix = 100 %
Crushed              | Rate = 6000 Hz; Bits = 6.0 bits; Mix = 80 %
Phone line           | Rate = 8000 Hz; Bits = 10.0 bits; Mix = 100 %

[Crossover]
Low band (< 200 Hz)  | High-pass = off; Low-pass = on; LP cutoff = 200 Hz; LP slope = 24 dB/oct
Mid band             | High-pass = on; HP cutoff = 200 Hz; HP slope = 24 dB/oct; Low-pass = on; LP cutoff = 2500 Hz; LP slope = 24 dB/oct
High band (> 2.5 kHz)| High-pass = on; HP cutoff = 2500 Hz; HP slope = 24 dB/oct; Low-pass = off
Rumble cut           | High-pass = on; HP cutoff = 35 Hz; HP slope = 24 dB/oct; Low-pass = off
Band-limit (radio)   | High-pass = on; HP cutoff = 300 Hz; HP slope = 12 dB/oct; Low-pass = on; LP cutoff = 3500 Hz; LP slope = 12 dB/oct

[Pitch shift]
Octave down          | Semitones = -12 st; Cents = 0 ct; Mix = 100 %
Octave up            | Semitones = 12 st; Cents = 0 ct; Mix = 100 %
Fifth up harmony     | Semitones = 7 st; Mix = 40 %
Fourth down harmony  | Semitones = -5 st; Mix = 40 %
Thicken (detune)     | Semitones = 0 st; Cents = 12 ct; Mix = 40 %
Sub octave blend     | Semitones = -12 st; Mix = 35 %

[Frequency shifter]
Subtle shimmer       | Shift = 3 Hz; Mix = 30 %; Stereo = 50 %
Inharmonic up        | Shift = 120 Hz; Mix = 50 %; Stereo = 0 %
Inharmonic down      | Shift = -90 Hz; Mix = 50 %; Stereo = 0 %
Wide phasing         | Shift = 0.8 Hz; Mix = 50 %; Stereo = 100 %

[Rhythmic envelope]
Sidechain pump       | Period = 1 beats; Points = 3; P1 pos = 0 %; P1 level = 0 %; P1 tension = 50 %; P2 pos = 40 %; P2 level = 100 %; P2 tension = 30 %; P3 pos = 100 %; P3 level = 100 %; P3 tension = 50 %
Soft pump            | Period = 1 beats; Points = 3; P1 pos = 0 %; P1 level = 35 %; P1 tension = 50 %; P2 pos = 55 %; P2 level = 100 %; P2 tension = 35 %; P3 pos = 100 %; P3 level = 100 %; P3 tension = 50 %
Half-time pump       | Period = 2 beats; Points = 3; P1 pos = 0 %; P1 level = 0 %; P1 tension = 50 %; P2 pos = 35 %; P2 level = 100 %; P2 tension = 30 %; P3 pos = 100 %; P3 level = 100 %; P3 tension = 50 %
Swell in             | Period = 4 beats; Points = 2; P1 pos = 0 %; P1 level = 10 %; P1 tension = 50 %; P2 pos = 100 %; P2 level = 100 %; P2 tension = 70 %
Eighth chop          | Period = 0.5 beats; Points = 4; P1 pos = 0 %; P1 level = 100 %; P1 tension = 50 %; P2 pos = 45 %; P2 level = 100 %; P2 tension = 50 %; P3 pos = 55 %; P3 level = 0 %; P3 tension = 50 %; P4 pos = 100 %; P4 level = 0 %; P4 tension = 50 %

[Volume gate]
Sixteenth trance     | Step 1 = 100 %; Step 2 = 0 %; Step 3 = 100 %; Step 4 = 0 %; Step 5 = 100 %; Step 6 = 0 %; Step 7 = 100 %; Step 8 = 0 %; Step 9 = 100 %; Step 10 = 0 %; Step 11 = 100 %; Step 12 = 0 %; Step 13 = 100 %; Step 14 = 0 %; Step 15 = 100 %; Step 16 = 0 %; Step 17 = 100 %; Step 18 = 0 %; Step 19 = 100 %; Step 20 = 0 %; Step 21 = 100 %; Step 22 = 0 %; Step 23 = 100 %; Step 24 = 0 %; Step 25 = 100 %; Step 26 = 0 %; Step 27 = 100 %; Step 28 = 0 %; Step 29 = 100 %; Step 30 = 0 %; Step 31 = 100 %; Step 32 = 0 %; Mix = 100 %
Offbeat              | Step 1 = 0 %; Step 2 = 0 %; Step 3 = 100 %; Step 4 = 100 %; Step 5 = 0 %; Step 6 = 0 %; Step 7 = 100 %; Step 8 = 100 %; Step 9 = 0 %; Step 10 = 0 %; Step 11 = 100 %; Step 12 = 100 %; Step 13 = 0 %; Step 14 = 0 %; Step 15 = 100 %; Step 16 = 100 %; Step 17 = 0 %; Step 18 = 0 %; Step 19 = 100 %; Step 20 = 100 %; Step 21 = 0 %; Step 22 = 0 %; Step 23 = 100 %; Step 24 = 100 %; Step 25 = 0 %; Step 26 = 0 %; Step 27 = 100 %; Step 28 = 100 %; Step 29 = 0 %; Step 30 = 0 %; Step 31 = 100 %; Step 32 = 100 %; Mix = 100 %
Stutter build        | Step 1 = 100 %; Step 2 = 100 %; Step 3 = 100 %; Step 4 = 0 %; Step 5 = 100 %; Step 6 = 0 %; Step 7 = 100 %; Step 8 = 100 %; Step 9 = 0 %; Step 10 = 100 %; Step 11 = 100 %; Step 12 = 0 %; Step 13 = 100 %; Step 14 = 0 %; Step 15 = 100 %; Step 16 = 0 %; Step 17 = 100 %; Step 18 = 100 %; Step 19 = 100 %; Step 20 = 0 %; Step 21 = 100 %; Step 22 = 0 %; Step 23 = 100 %; Step 24 = 100 %; Step 25 = 100 %; Step 26 = 0 %; Step 27 = 100 %; Step 28 = 0 %; Step 29 = 100 %; Step 30 = 0 %; Step 31 = 100 %; Step 32 = 0 %; Mix = 100 %
Soft trance (50 %)   | Step 1 = 100 %; Step 2 = 30 %; Step 3 = 100 %; Step 4 = 30 %; Step 5 = 100 %; Step 6 = 30 %; Step 7 = 100 %; Step 8 = 30 %; Step 9 = 100 %; Step 10 = 30 %; Step 11 = 100 %; Step 12 = 30 %; Step 13 = 100 %; Step 14 = 30 %; Step 15 = 100 %; Step 16 = 30 %; Step 17 = 100 %; Step 18 = 30 %; Step 19 = 100 %; Step 20 = 30 %; Step 21 = 100 %; Step 22 = 30 %; Step 23 = 100 %; Step 24 = 30 %; Step 25 = 100 %; Step 26 = 30 %; Step 27 = 100 %; Step 28 = 30 %; Step 29 = 100 %; Step 30 = 30 %; Step 31 = 100 %; Step 32 = 30 %; Mix = 100 %
Half-bar mute        | Step 1 = 100 %; Step 2 = 100 %; Step 3 = 100 %; Step 4 = 100 %; Step 5 = 100 %; Step 6 = 100 %; Step 7 = 100 %; Step 8 = 100 %; Step 9 = 0 %; Step 10 = 0 %; Step 11 = 0 %; Step 12 = 0 %; Step 13 = 0 %; Step 14 = 0 %; Step 15 = 0 %; Step 16 = 0 %; Step 17 = 100 %; Step 18 = 100 %; Step 19 = 100 %; Step 20 = 100 %; Step 21 = 100 %; Step 22 = 100 %; Step 23 = 100 %; Step 24 = 100 %; Step 25 = 0 %; Step 26 = 0 %; Step 27 = 0 %; Step 28 = 0 %; Step 29 = 0 %; Step 30 = 0 %; Step 31 = 0 %; Step 32 = 0 %; Mix = 100 %
)PRESETS";
}
}
