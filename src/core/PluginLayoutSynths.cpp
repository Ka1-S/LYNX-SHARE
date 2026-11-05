#include "core/PluginLayoutSynths.h"

#include <utility>

// (the i18n test scans the sources for TR_NOOP: captions below are translated by the editor)
#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::plugui
{
namespace
{
// --- builders: rectangles as the plugins' editor forms give them (Left, Top, Width, Height) ----------------------------

struct Page
{
    Layout l;
    std::vector<LayoutItem>& v = l.items;

    explicit Page (const char* fl, int width, int height)
    {
        l.fl = fl;
        l.width = width;
        l.height = height;
    }

    void section (float x, float y, float w, float h, const char* title, std::uint16_t style = 0)
    {
        LayoutItem i;
        i.type = Item::section;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.label = title;
        i.style = style;
        v.push_back (i);
    }

    void label (float x, float y, float w, float h, const char* text, std::uint16_t style = 0)
    {
        LayoutItem i;
        i.type = Item::label;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.label = text;
        i.style = style;
        v.push_back (i);
    }

    // a knob in the square (x, y, d, d), its caption under it
    void knob (float x, float y, float d, const char* param, const char* caption, std::uint16_t style = 0, int steps = 0)
    {
        LayoutItem i;
        i.steps = steps;
        i.type = Item::knob;
        i.x = x, i.y = y, i.w = d;
        i.h = caption != nullptr && (style & noCaption) == 0 ? d + 14.0f : d;
        i.param = param;
        i.label = caption;
        i.style = caption == nullptr ? (std::uint16_t) (style | noCaption) : style;
        v.push_back (i);
    }

    // a vertical fader in (x, y, w, h), its caption under it
    void fader (float x, float y, float w, float h, const char* param, const char* caption, std::uint16_t style = 0)
    {
        LayoutItem i;
        i.type = Item::vslider;
        i.x = x, i.y = y, i.w = w;
        i.h = caption != nullptr ? h + 14.0f : h;
        i.param = param;
        i.label = caption;
        i.style = caption == nullptr ? (std::uint16_t) (style | noCaption) : style;
        v.push_back (i);
    }

    // an on / off switch: `boxed` = a push button with the caption inside (FL's VEL buttons), else an LED and the caption
    void toggle (float x, float y, float w, float h, const char* param, const char* caption, std::uint16_t style = 0)
    {
        LayoutItem i;
        i.type = Item::toggle;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.param = param;
        i.label = caption;
        i.style = style;
        v.push_back (i);
    }

    void vel (float x, float y, const char* param) { toggle (x, y, 30, 17, param, TR_NOOP ("Vel"), boxed); }

    // a choice list; with a caption, the caption sits above the box
    void menu (float x, float y, float w, float h, const char* param, const char* caption, const char* options, std::vector<float> values = {})
    {
        LayoutItem i;
        i.type = Item::menu;
        i.x = x, i.y = caption != nullptr ? y - 13.0f : y, i.w = w, i.h = caption != nullptr ? h + 13.0f : h;
        i.param = param;
        i.label = caption;
        i.options = options;
        i.values = std::move (values);
        v.push_back (i);
    }

    // segmented buttons
    void tabs (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {})
    {
        LayoutItem i;
        i.type = Item::tabs;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.param = param;
        i.options = options;
        i.values = std::move (values);
        v.push_back (i);
    }

    void stepper (float x, float y, float w, float h, const char* param, const char* caption, int steps)
    {
        LayoutItem i;
        i.type = Item::stepper;
        i.x = x, i.y = caption != nullptr ? y - 12.0f : y, i.w = w, i.h = caption != nullptr ? h + 12.0f : h;
        i.param = param;
        i.label = caption;
        i.steps = steps;
        v.push_back (i);
    }

    void display (float x, float y, float w, float h, const char* id, const char* param = nullptr, const char* param2 = nullptr)
    {
        LayoutItem i;
        i.type = Item::display;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.display = id;
        i.param = param;
        i.param2 = param2;
        v.push_back (i);
    }

    // the parameter's value as text
    void value (float x, float y, float w, float h, const char* param)
    {
        LayoutItem i;
        i.type = Item::value;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.param = param;
        v.push_back (i);
    }

    // the plugin's name, big and quiet
    void name (float x, float y, float w, float h, const char* text) { label (x, y, w, h, text, dim); }
};

// --- Harmless (Harmless_x64.dll TSynthEditorForm, 848 × 569; the keyboard strip under y = 500 left out) -------------

Layout harmlessLayout()
{
    Page p ("Harmless", 848, 492);

    // TIMBRE: low harmonics protection, the three subs, the six-band timbre EQ, colour and phase randomness
    p.section (24, 0, 222, 162, TR_NOOP ("Timbre"));
    p.fader (34, 22, 17, 83, "Low harmonics protection", TR_NOOP ("Prot"));
    p.fader (64, 22, 17, 83, "Sub harmonic 1", "1");
    p.fader (82, 22, 17, 83, "Sub harmonic 3", "3");
    p.fader (100, 22, 17, 83, "Sub harmonic 4", "4");
    p.label (60, 120, 62, 12, TR_NOOP ("Sub"), dim);
    static const char* const eq[6] = { "Timbre EQ band 1", "Timbre EQ band 2", "Timbre EQ band 3", "Timbre EQ band 4", "Timbre EQ band 5", "Timbre EQ band 6" };
    for (int i = 0; i < 6; ++i)
        p.fader (130.0f + 18.0f * (float) i, 22, 17, 83, eq[i], nullptr, bipolar);
    p.label (130, 106, 108, 12, TR_NOOP ("Timbre EQ"), dim);
    p.menu (34, 136, 150, 20, "Timbre color", TR_NOOP ("Color"),
            "Saw|Bright saw|Brighter saw|Dephased saw|Dephased saw 2|Squaretooth|Squaretooth 2|Mole|Double saw|Triple saw|Triple saw 2|Custom");
    p.knob (198, 118, 30, "Phase randomness", TR_NOOP ("Phase"), bipolar);

    // MASK: twelve harmonic mask faders, the mask mix and its LFO amount
    p.section (250, 0, 324, 74, TR_NOOP ("Harmonic mask"));
    static const char* const mask[12] = { "Harmonic mask 1", "Harmonic mask 2", "Harmonic mask 3", "Harmonic mask 4", "Harmonic mask 5", "Harmonic mask 6",
                                          "Harmonic mask 7", "Harmonic mask 8", "Harmonic mask 9", "Harmonic mask 10", "Harmonic mask 11", "Harmonic mask 12" };
    for (int i = 0; i < 12; ++i)
        p.fader (254.0f + 17.5f * (float) i, 20, 15, 50, mask[i], nullptr);
    p.knob (476, 18, 34, "Harmonic mask mix", TR_NOOP ("Mix"));
    p.knob (526, 22, 28, "Mask mix LFO amount", TR_NOOP ("LFO"), bipolar);

    // ENVELOPE: stereo tremolo, attack / decay / release with their velocity switches
    p.section (250, 78, 222, 84, TR_NOOP ("Envelope"));
    p.knob (262, 98, 30, "Stereo tremolo depth", TR_NOOP ("Trem"));
    p.knob (306, 94, 36, "Volume attack", TR_NOOP ("Att"));
    p.knob (354, 94, 36, "Volume decay", TR_NOOP ("Dec"));
    p.knob (402, 94, 36, "Volume release", TR_NOOP ("Rel"));
    p.vel (309, 142, "Velocity to attack");
    p.vel (405, 142, "Release velocity to release");

    // PLUCK: decay, its velocity switch and the three modes
    p.section (476, 78, 98, 84, TR_NOOP ("Pluck"));
    p.knob (482, 94, 34, "Pluck decay", TR_NOOP ("Decay"));
    p.vel (484, 142, "Velocity to pluck decay");
    p.toggle (520, 96, 52, 14, "Alternate pluck mode", TR_NOOP ("Alt"));
    p.toggle (520, 112, 52, 14, "Pluck uses harmonic mask", TR_NOOP ("Mask"));
    p.toggle (520, 128, 52, 14, "Pluck only on release", TR_NOOP ("Rel"));

    // VOLUME and PITCH
    p.section (578, 0, 46, 162, TR_NOOP ("Vol"));
    p.fader (591, 22, 19, 104, "Master volume", nullptr);
    p.vel (586, 142, "Velocity to volume");
    p.section (628, 0, 196, 162, TR_NOOP ("Pitch"));
    p.fader (636, 22, 19, 104, "Master pitch", TR_NOOP ("Pitch"), bipolar);
    p.fader (664, 22, 19, 104, "Detuning", TR_NOOP ("Det"), bipolar);
    p.knob (700, 20, 28, "Grittiness", TR_NOOP ("Grit"), bipolar);
    p.knob (700, 66, 28, "Vibrato depth", TR_NOOP ("Vib"));
    p.knob (700, 110, 28, "Pitch LFO amount", TR_NOOP ("LFO"), bipolar);
    p.toggle (746, 22, 72, 14, "Portamento", TR_NOOP ("Porta"));
    p.toggle (746, 40, 72, 14, "Monophonic", TR_NOOP ("Mono"));
    p.knob (752, 64, 30, "Portamento time", TR_NOOP ("Time"), bipolar);
    p.vel (787, 72, "Velocity to portamento time");

    // FILTER: the filter envelope (adaptive, attack / decay with their slopes, amount), LFO and key amounts, width,
    // frequency, resonance (amount, offset, self-oscillation), types and scales
    p.section (24, 166, 550, 128, TR_NOOP ("Filter"));
    p.toggle (32, 188, 60, 14, "Adaptive filter envelope", TR_NOOP ("Adapt"));
    p.knob (96, 182, 34, "Filter env attack", TR_NOOP ("Att"));
    p.knob (138, 182, 34, "Filter env decay", TR_NOOP ("Dec"));
    p.knob (100, 238, 28, "Filter env attack slope", TR_NOOP ("Slope"), bipolar);
    p.knob (142, 238, 28, "Filter env decay slope", TR_NOOP ("Slope"), bipolar);
    p.knob (184, 178, 44, "Filter env amount", TR_NOOP ("Env"), big | bipolar);
    p.vel (191, 246, "Velocity to filter env amount");
    p.knob (238, 186, 28, "Filter LFO amount", TR_NOOP ("LFO"), bipolar);
    p.knob (274, 186, 28, "Key to filter frequency", TR_NOOP ("Key"), bipolar);
    p.knob (312, 184, 32, "Filter width", TR_NOOP ("Width"));
    p.knob (360, 176, 48, "Filter frequency", TR_NOOP ("Freq"), big);
    p.knob (420, 180, 38, "Resonance amount", TR_NOOP ("Res"));
    p.knob (470, 186, 30, "Resonance offset", TR_NOOP ("Ofs"), bipolar);
    p.knob (510, 186, 30, "Resonance self-oscillation", TR_NOOP ("Osc"));
    p.menu (238, 260, 120, 20, "Filter type", TR_NOOP ("Type"),
            "Classic low pass|Classic band pass|Classic band stop|Classic high pass|Crude low pass|Crude band pass|Crude band stop|"
            "Crude high pass|Hill low pass|Leg low pass|Hybrid low pass|Snake low pass|Fin low pass|Leaky low pass|Phaser|Band phaser|Flat band pass");
    p.tabs (364, 262, 50, 16, "Filter scale", "Oct|Hz");
    p.menu (420, 260, 86, 20, "Resonance type", TR_NOOP ("Res type"), "Classic|Spring|Pedestal|Sedge hat|Wide bump|Double cone|Well|Wormhole|Noise");
    p.tabs (420, 282, 50, 10, "Resonance scale", "Oct|Hz");
    p.toggle (510, 236, 62, 14, "Track filter frequency motion", TR_NOOP ("Motion"));
    p.toggle (510, 252, 62, 14, "Adaptive resonance width", TR_NOOP ("Adapt"));

    // UNISON
    p.section (578, 166, 246, 128, TR_NOOP ("Unison"));
    p.stepper (590, 194, 34, 24, "Unison order", TR_NOOP ("Order"), 8);
    p.menu (590, 248, 70, 20, "Unison type", TR_NOOP ("Type"), "Classic|Uniform|Blurred");
    p.vel (630, 196, "Velocity to unison pitch thickness");
    p.fader (690, 186, 17, 88, "Unison panning", TR_NOOP ("Pan"));
    p.fader (722, 186, 17, 88, "Unison pitch thickness", TR_NOOP ("Pitch"));
    p.fader (754, 186, 17, 88, "Unison phase", TR_NOOP ("Phase"));
    p.fader (786, 186, 17, 88, "Unison variation", TR_NOOP ("Var"));

    // PHASER
    p.section (24, 298, 296, 120, TR_NOOP ("Phaser"));
    p.menu (32, 330, 64, 20, "Phaser type", TR_NOOP ("Type"), "Classic|Triangle|Eggs|Deep|Deeper|Condom|Twins|Cascade|Box|Freq");
    p.tabs (32, 370, 64, 18, "Phaser scale", "Oct|Hz");
    p.knob (104, 318, 44, "Phaser mix", TR_NOOP ("Mix"), big);
    p.knob (156, 322, 34, "Phaser width", TR_NOOP ("Width"));
    p.knob (198, 322, 34, "Phaser offset", TR_NOOP ("Offset"));
    p.knob (159, 376, 26, "Phaser width LFO amount", TR_NOOP ("LFO"), bipolar);
    p.knob (202, 376, 26, "Phaser offset LFO amount", TR_NOOP ("LFO"), bipolar);
    p.knob (240, 322, 30, "Phaser motion speed", TR_NOOP ("Speed"), bipolar);
    p.knob (280, 322, 30, "Key to phaser offset", TR_NOOP ("Key"), bipolar);

    // HARMONIZER
    p.section (324, 298, 286, 120, TR_NOOP ("Harmonizer"));
    p.knob (334, 318, 44, "Harmonizer mix", TR_NOOP ("Mix"), big);
    p.knob (386, 322, 34, "Harmonizer width", TR_NOOP ("Width"));
    p.knob (389, 376, 26, "Harmonizer width LFO amount", TR_NOOP ("LFO"), bipolar);
    p.vel (424, 380, "Velocity to harmonizer width");
    p.knob (430, 322, 30, "Harmonizer strength", TR_NOOP ("Str"));
    p.menu (472, 332, 132, 20, "Harmonizer type", TR_NOOP ("Type"), "Oct up|Oct up +1|Oct up +2|Harm up|Harm up +1");
    p.menu (472, 376, 132, 20, "Harmonizer position", TR_NOOP ("Position"), "Pre-filter|Pre-phaser|Pre-pluck|Final");

    // LFO
    p.section (614, 298, 210, 120, TR_NOOP ("LFO"));
    p.menu (622, 330, 104, 20, "LFO shape", TR_NOOP ("Shape"),
            "Sine|Smooth saw|Smooth pulse|Saw|Pulse|Stairs|Triangle|Sickle|Multisine|Filter envelope|Filter frequency|Voice random|Velocity|Pitch");
    p.toggle (622, 370, 100, 14, "Global LFO", TR_NOOP ("Global"));
    p.knob (734, 324, 30, "LFO attack", TR_NOOP ("Att"));
    p.knob (772, 318, 44, "LFO speed", TR_NOOP ("Speed"), big | bipolar);

    // EFFECTS: chorus, delay, reverb, compressor; threaded and quality
    p.section (24, 422, 800, 66, TR_NOOP ("Effects"));
    p.menu (32, 452, 92, 20, "Chorus type", TR_NOOP ("Chorus"),
            "Off|Classic|Thick|Deep|Flanger|Alt flanger|Stereoized|Modulation|Surround|Vibration");
    p.knob (130, 442, 30, "Chorus mix", TR_NOOP ("Mix"));
    p.menu (174, 452, 86, 20, "Delay type", TR_NOOP ("Delay"), "Off|Classic|Ping-pong|Stereoized");
    p.knob (266, 442, 30, "Delay input level", TR_NOOP ("Vol"));
    p.knob (302, 442, 30, "Delay feedback", TR_NOOP ("Fb"));
    p.knob (338, 442, 30, "Delay time", TR_NOOP ("Time"));
    p.menu (382, 452, 100, 20, "Reverb type", TR_NOOP ("Reverb"), "Off|Classic|Large hall|Cathedral|The venue|Drum room|Small studio");
    p.knob (488, 442, 30, "Reverb wet level", TR_NOOP ("Wet"));
    p.menu (534, 452, 92, 20, "Compression type", TR_NOOP ("Comp"), "Off|Limiting|Warming|Heating|Distortion");
    p.knob (632, 442, 30, "Compression level", TR_NOOP ("Level"));
    p.toggle (680, 444, 70, 14, "High quality", TR_NOOP ("HQ"));
    p.toggle (680, 464, 70, 14, "Threaded", TR_NOOP ("Threaded"));
    return std::move (p.l);
}

// --- Fruity DX10 (Fruity DX10_x64.dll TSynthEditorForm, 536 × 340; keyboard under y = 238 left out) ----------------

Layout dx10Layout()
{
    Page p ("Fruity DX10", 536, 236);
    // MISC: waveform, coarse (octave), LFO rate, vibrato
    p.section (2, 2, 176, 89, TR_NOOP ("Misc"));
    p.knob (6, 30, 36, "Waveform", TR_NOOP ("Wave"));
    p.knob (50, 30, 36, "Coarse", TR_NOOP ("Coarse"), bipolar);
    p.knob (95, 30, 36, "LFO rate", TR_NOOP ("LFO rate"));
    p.knob (140, 30, 36, "Vibrato", TR_NOOP ("Vib"));
    // AMPLITUDE: the envelope display, attack / decay / release
    p.section (2, 93, 176, 143, TR_NOOP ("Amplitude"));
    p.display (6, 114, 170, 50, "dx10amp");
    p.knob (6, 172, 36, "Attack", TR_NOOP ("Att"));
    p.knob (50, 172, 36, "Decay", TR_NOOP ("Dec"));
    p.knob (95, 172, 36, "Release", TR_NOOP ("Rel"));
    // MODULATION 1 and 2: coarse, fine, velocity sensitivity (Mod thru on the first), the envelope and its knobs
    p.section (180, 2, 176, 201, TR_NOOP ("Modulation 1"));
    p.knob (185, 30, 36, "Mod 1 coarse", TR_NOOP ("Coarse"));
    p.knob (228, 30, 36, "Mod 1 fine", TR_NOOP ("Fine"));
    p.knob (273, 30, 36, "Mod 1 velocity", TR_NOOP ("Vel sens"));
    p.knob (317, 30, 36, "Mod thru", TR_NOOP ("Thru"));
    p.display (184, 86, 170, 50, "dx10mod1");
    p.knob (185, 142, 36, "Mod 1 init", TR_NOOP ("Init"));
    p.knob (228, 142, 36, "Mod 1 time", TR_NOOP ("Time"));
    p.knob (273, 142, 36, "Mod 1 sustain", TR_NOOP ("Sus"));
    p.knob (317, 142, 36, "Mod 1 release", TR_NOOP ("Rel"));
    p.section (358, 2, 176, 201, TR_NOOP ("Modulation 2"));
    p.knob (363, 30, 36, "Mod 2 coarse", TR_NOOP ("Coarse"));
    p.knob (407, 30, 36, "Mod 2 fine", TR_NOOP ("Fine"));
    p.knob (452, 30, 36, "Mod 2 velocity", TR_NOOP ("Vel sens"));
    p.display (362, 86, 170, 50, "dx10mod2");
    p.knob (362, 142, 36, "Mod 2 init", TR_NOOP ("Init"));
    p.knob (406, 142, 36, "Mod 2 time", TR_NOOP ("Time"));
    p.knob (450, 142, 36, "Mod 2 sustain", TR_NOOP ("Sus"));
    p.knob (495, 142, 36, "Mod 2 release", TR_NOOP ("Rel"));
    p.section (180, 205, 354, 31, nullptr, boxed);
    p.name (186, 207, 340, 27, "Fruity DX10  FM synth");
    return std::move (p.l);
}

// --- SimSynth (SimSynth_x64.dll TSynthEditorForm, 562 × 376; the preview keyboard at the bottom left left out) -------

Layout simSynthLayout()
{
    Page p ("SimSynth", 562, 376);
    // the three oscillators: on, wave, shape (pulse width), coarse, fine, level, LFO and envelope to pitch, +1 / +2
    // octave layers and WARM (unison); ring modulation on the first
    static const char* const on[3] = { "Osc 1 on", "Osc 2 on", "Osc 3 on" };
    static const char* const wave[3] = { "Osc 1 wave", "Osc 2 wave", "Osc 3 wave" };
    static const char* const shape[3] = { "Osc 1 shape", "Osc 2 shape", "Osc 3 shape" };
    static const char* const coarse[3] = { "Osc 1 coarse", "Osc 2 coarse", "Osc 3 coarse" };
    static const char* const fine[3] = { "Osc 1 fine", "Osc 2 fine", "Osc 3 fine" };
    static const char* const level[3] = { "Osc 1 level", "Osc 2 level", "Osc 3 level" };
    static const char* const lfo[3] = { "Osc 1 LFO", "Osc 2 LFO", "Osc 3 LFO" };
    static const char* const env[3] = { "Osc 1 envelope", "Osc 2 envelope", "Osc 3 envelope" };
    static const char* const oct1[3] = { "Osc 1 +1 octave", "Osc 2 +1 octave", "Osc 3 +1 octave" };
    static const char* const oct2[3] = { "Osc 1 +2 octaves", "Osc 2 +2 octaves", "Osc 3 +2 octaves" };
    static const char* const warm[3] = { "Osc 1 warm", "Osc 2 warm", "Osc 3 warm" };
    static const char* const titles[3] = { TR_NOOP ("Osc 1"), TR_NOOP ("Osc 2"), TR_NOOP ("Osc 3") };
    for (int o = 0; o < 3; ++o)
    {
        const auto y = 97.0f * (float) o;
        p.section (2, y + 2, 326, 92, titles[o]);
        p.toggle (60, y + 4, 28, 14, on[o], nullptr);
        p.tabs (94, y + 20, 161, 18, wave[o], "Pulse|Cos|Tri|Noise|Sine", { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f });
        p.knob (8, y + 44, 30, shape[o], TR_NOOP ("Shape"));
        p.knob (51, y + 44, 30, coarse[o], TR_NOOP ("Coarse"), bipolar);
        p.knob (94, y + 44, 30, fine[o], TR_NOOP ("Fine"), bipolar);
        p.knob (137, y + 44, 30, level[o], TR_NOOP ("Level"));
        p.knob (180, y + 44, 30, lfo[o], TR_NOOP ("LFO"), bipolar);
        p.knob (223, y + 44, 30, env[o], TR_NOOP ("Env"), bipolar);
        p.toggle (264, y + 44, 30, 16, oct1[o], "+1", boxed);
        p.toggle (294, y + 44, 30, 16, oct2[o], "+2", boxed);
        p.toggle (264, y + 64, 60, 16, warm[o], TR_NOOP ("Warm"), boxed);
    }
    p.toggle (262, 20, 64, 16, "Ring mod 1 x 2", TR_NOOP ("Ring 1x2"), boxed);

    // AMP: the envelope display, attack / decay / sustain / release, velocity to volume
    p.section (332, 2, 228, 92, TR_NOOP ("Amp"));
    p.display (342, 19, 208, 20, "simamp");
    p.knob (347, 42, 30, "Attack", TR_NOOP ("Att"));
    p.knob (390, 42, 30, "Decay", TR_NOOP ("Dec"));
    p.knob (433, 42, 30, "Sustain", TR_NOOP ("Sus"));
    p.knob (476, 42, 30, "Release", TR_NOOP ("Rel"));
    p.knob (519, 42, 30, "Velocity to volume", TR_NOOP ("Vel"));

    // SVF: on, the filter envelope (display and knobs), its amount, LFO, key tracking, track amp, cutoff, emphasis,
    // high and band pass mix
    p.section (332, 98, 228, 190, TR_NOOP ("Filter"));
    p.toggle (380, 101, 28, 14, "Filter on", nullptr);
    p.display (342, 116, 208, 20, "simfilt");
    p.knob (347, 140, 30, "Filter attack", TR_NOOP ("Att"));
    p.knob (390, 140, 30, "Filter decay", TR_NOOP ("Dec"));
    p.knob (433, 140, 30, "Filter sustain", TR_NOOP ("Sus"));
    p.knob (476, 140, 30, "Filter release", TR_NOOP ("Rel"));
    p.knob (519, 140, 30, "Filter envelope", TR_NOOP ("Env"), bipolar);
    p.knob (347, 190, 30, "Filter LFO", TR_NOOP ("LFO"), bipolar);
    p.knob (390, 190, 30, "Key tracking", TR_NOOP ("Kb"));
    p.toggle (436, 196, 118, 16, "Filter tracks amp envelope", TR_NOOP ("Track amp"), boxed);
    p.knob (347, 240, 30, "Cutoff", TR_NOOP ("Cut"));
    p.knob (390, 240, 30, "Emphasis", TR_NOOP ("Emph"));
    p.knob (433, 240, 30, "High pass", TR_NOOP ("High"));
    p.knob (476, 240, 30, "Band pass", TR_NOOP ("Band"));

    // LFO: on, shape, rate, delay, retrigger; chorus
    p.section (332, 292, 186, 82, TR_NOOP ("LFO"));
    p.toggle (380, 295, 28, 14, "LFO on", nullptr);
    p.tabs (426, 318, 88, 16, "LFO shape", "Sq|Saw|Tri|S&H", { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f });
    p.knob (347, 320, 30, "LFO rate", TR_NOOP ("Rate"));
    p.knob (390, 320, 30, "LFO delay", TR_NOOP ("Delay"));
    p.toggle (426, 342, 88, 16, "LFO retrigger", TR_NOOP ("Retrigger"), boxed);
    p.section (522, 292, 38, 82, nullptr, boxed);
    p.toggle (525, 300, 32, 66, "Chorus", TR_NOOP ("Ch"), boxed);
    p.name (6, 296, 320, 74, "SimSynth");
    return std::move (p.l);
}
// --- Ogun / Autogun (Ogun_x64.dll TSynthEditorForm, 560 × 640): master, modulation X/Y, timbre, unison, filter, the
// output EQ, the articulator editor (the saved curves, not edited in LYNX) and the effects row ------------------------

Layout ogunLayout (bool autogun)
{
    Page p (autogun ? "Autogun" : "Ogun", 560, 548);
    p.section (4, 0, 92, 96, TR_NOOP ("Master"));
    p.fader (11, 18, 20, 58, "Master volume", TR_NOOP ("Vol"));
    p.fader (40, 18, 20, 58, "Coarse pitch", TR_NOOP ("Crs"), bipolar);
    p.fader (69, 18, 20, 58, "Fine pitch", TR_NOOP ("Fine"), bipolar);
    p.section (98, 0, 216, 96, TR_NOOP ("Modulation"));
    p.display (104, 18, 74, 74, "xy", "Modulation X", "Modulation Y");
    p.knob (188, 18, 22, "Modulation X", "X");
    p.knob (188, 56, 22, "Modulation Y", "Y");
    p.stepper (240, 36, 66, 22, "Quality", TR_NOOP ("Quality"), 4);
    p.section (318, 0, 238, 96, nullptr, boxed);
    p.name (326, 8, 222, 50, autogun ? "Autogun" : "Ogun");
    if (autogun)
        p.knob (500, 54, 34, "Magic dust", TR_NOOP ("Dust"));

    // TIMBRE: richness, pre-decay, decay, release, fullness, morph, the two seeds
    p.section (4, 100, 230, 116, TR_NOOP ("Timbre"));
    p.fader (12, 122, 20, 70, "Richness", TR_NOOP ("Rich"));
    p.fader (39, 122, 20, 70, "Pre-decay", TR_NOOP ("Pre"));
    p.fader (66, 122, 20, 70, "Decay time", TR_NOOP ("Dec"));
    p.fader (93, 122, 20, 70, "Release time", TR_NOOP ("Rel"));
    p.fader (120, 122, 20, 70, "Fullness", TR_NOOP ("Full"), bipolar);
    p.fader (150, 122, 20, 70, "Morph", TR_NOOP ("Morph"));
    p.stepper (184, 136, 44, 22, "Seed 2", TR_NOOP ("Seed 2"), 9999);
    p.stepper (184, 182, 44, 22, "Seed 1", TR_NOOP ("Seed 1"), 9999);
    // UNISON
    p.section (238, 100, 156, 116, TR_NOOP ("Unison"));
    p.stepper (246, 136, 30, 22, "Unison order", TR_NOOP ("Ord"), 8);
    p.fader (284, 122, 20, 70, "Unison panning", TR_NOOP ("Pan"));
    p.fader (310, 122, 20, 70, "Unison volume", TR_NOOP ("Vol"));
    p.fader (336, 122, 20, 70, "Unison pitch", TR_NOOP ("Pitch"));
    p.fader (362, 122, 20, 70, "Unison phase", TR_NOOP ("Phase"));
    // FILTER
    p.section (398, 100, 158, 116, TR_NOOP ("Filter"));
    p.knob (414, 130, 46, "Filter frequency", TR_NOOP ("Freq"), big);
    p.knob (488, 136, 36, "Filter resonance", TR_NOOP ("Res"));
    // EQ: three bands (level faders, frequency and width knobs)
    p.section (446, 220, 110, 210, TR_NOOP ("EQ"));
    static const char* const lv[3] = { "EQ band 1 level", "EQ band 2 level", "EQ band 3 level" };
    static const char* const fq[3] = { "EQ band 1 freq", "EQ band 2 freq", "EQ band 3 freq" };
    static const char* const bw[3] = { "EQ band 1 width", "EQ band 2 width", "EQ band 3 width" };
    for (int k = 0; k < 3; ++k)
    {
        const auto x = 458.0f + 32.0f * (float) k;
        p.fader (x + 2, 240, 20, 80, lv[k], nullptr, bipolar);
        p.knob (x, 330, 26, fq[k], TR_NOOP ("Freq"));
        p.knob (x, 376, 26, bw[k], TR_NOOP ("Width"));
    }
    // the articulators (13 point-curve envelopes / LFOs / maps): FL's curve editor, kept as saved
    p.section (4, 220, 438, 210, TR_NOOP ("Articulators"));
    p.label (16, 300, 414, 16, TR_NOOP ("Envelopes, LFOs and maps keep the saved patch"), dim);
    p.label (16, 324, 414, 16, TR_NOOP ("Plays FL Studio's own presets and data files: no LYNX presets for this engine"), dim);
    // EFFECTS: chorus, delay, reverb
    p.section (4, 434, 168, 114, TR_NOOP ("Chorus"));
    p.stepper (8, 466, 26, 22, "Chorus order", TR_NOOP ("Ord"), 9);
    p.fader (38, 452, 18, 74, "Chorus depth", TR_NOOP ("Dep"));
    p.fader (60, 452, 18, 74, "Chorus speed", TR_NOOP ("Spd"));
    p.fader (82, 452, 18, 74, "Chorus delay", TR_NOOP ("Del"));
    p.fader (104, 452, 18, 74, "Chorus spread", TR_NOOP ("Spr"));
    p.fader (126, 452, 18, 74, "Chorus cross", TR_NOOP ("Crs"), bipolar);
    p.fader (148, 452, 18, 74, "Chorus mix", TR_NOOP ("Mix"), bipolar);
    p.section (176, 434, 160, 114, TR_NOOP ("Delay"));
    p.stepper (184, 466, 40, 22, "Delay feedback mode", TR_NOOP ("Mode"), 3);
    // (round 170i3: the delay and reverb run only with these switches on; FL's Plugin database Ogun has both off)
    p.toggle (184, 496, 48, 16, "Delay on", TR_NOOP ("On"), boxed);
    p.toggle (184, 518, 48, 16, "Delay tempo sync", TR_NOOP ("Tempo"), boxed);
    p.fader (240, 452, 18, 74, "Delay feedback", TR_NOOP ("Fb"));
    p.fader (262, 452, 18, 74, "Delay time", TR_NOOP ("Time"));
    p.fader (284, 452, 18, 74, "Delay stereo offset", TR_NOOP ("Ofs"), bipolar);
    p.fader (306, 452, 18, 74, "Delay volume", TR_NOOP ("Vol"), bipolar);
    p.section (340, 434, 216, 114, TR_NOOP ("Reverb"));
    p.stepper (346, 466, 26, 22, "Reverb color", TR_NOOP ("Col"), 4);
    p.toggle (344, 496, 30, 16, "Reverb on", TR_NOOP ("On"), boxed);
    static const std::pair<const char*, const char*> rev[8] = {
        { "Reverb low cut", TR_NOOP ("LC") }, { "Reverb high cut", TR_NOOP ("HC") }, { "Reverb predelay", TR_NOOP ("Pre") },
        { "Reverb room size", TR_NOOP ("Size") }, { "Reverb diffusion", TR_NOOP ("Diff") }, { "Reverb decay", TR_NOOP ("Dec") },
        { "Reverb high damping", TR_NOOP ("HD") }, { "Reverb wet", TR_NOOP ("Wet") } };
    for (int i = 0; i < 8; ++i)
        p.fader (378.0f + 22.0f * (float) i, 452, 18, 74, rev[i].first, rev[i].second, i == 7 ? bipolar : 0);
    return std::move (p.l);
}

// --- MiniSynth (its own skin, 717 × 473 with the six page tabs: Oscillator, Amplifier, Filter, Filter Env, LFO, Effects;
// the Effects page's four units shown side by side) --------------------------------------------------------------------

Layout miniSynthLayout()
{
    Page p ("MiniSynth", 717, 400);
    p.l.pages = { TR_NOOP ("Oscillator"), TR_NOOP ("Amplifier"), TR_NOOP ("Filter"), TR_NOOP ("Filter env"), TR_NOOP ("LFO"), TR_NOOP ("Effects") };
    {
        LayoutItem t;
        t.type = Item::tabs;
        t.x = 8, t.y = 4, t.w = 701, t.h = 30;
        t.options = "Oscillator|Amplifier|Filter|Filter env|LFO|Effects";
        t.style = pageTabs;
        p.v.push_back (t);
    }
    const auto on = [&p] (int page, std::size_t from) {
        for (auto i = from; i < p.v.size(); ++i)
            p.v[i].page = page;
    };
    // OSCILLATOR
    auto from = p.v.size();
    p.section (18, 50, 300, 300, TR_NOOP ("Waveform"));
    p.menu (34, 110, 140, 26, "Osc kind", nullptr, "Saw|Saw + saw|Pulse|Square + saw|Square + square|Supersaw|Bell|Deaf saw|Saw spread|Saw spread 2",
            { 0.0f, 1.0f / 9.0f, 2.0f / 9.0f, 3.0f / 9.0f, 4.0f / 9.0f, 5.0f / 9.0f, 6.0f / 9.0f, 7.0f / 9.0f, 8.0f / 9.0f, 1.0f });
    p.knob (206, 80, 74, "Transpose", TR_NOOP ("Transpose"), bipolar | big);
    p.knob (44, 216, 74, "Osc mod", TR_NOOP ("Modifier"), big);
    p.knob (206, 216, 74, "Noise", TR_NOOP ("Noise"), big);
    p.section (362, 50, 144, 300, TR_NOOP ("Voice mode"));
    p.v.push_back ([] {
        LayoutItem r;
        r.type = Item::radio;
        r.x = 380, r.y = 80, r.w = 110, r.h = 120;
        r.param = "Voices mode";
        r.options = "Poly|Mono|Lead";
        r.values = { 0.0f, 0.5f, 1.0f };
        return r;
    }());
    p.knob (398, 230, 74, "Slide time", TR_NOOP ("Slide time"), big);
    p.section (548, 50, 130, 300, TR_NOOP ("Mod wheel"));
    p.v.push_back ([] {
        LayoutItem r;
        r.type = Item::radio;
        r.x = 562, r.y = 100, r.w = 100, r.h = 90;
        r.param = "Mod wheel destination";
        r.options = "Filter|LFO";
        r.values = { 0.0f, 1.0f };
        return r;
    }());
    p.knob (576, 230, 74, "Mod wheel amount", TR_NOOP ("Amount"), big);
    on (0, from);
    // AMPLIFIER
    from = p.v.size();
    p.section (30, 50, 330, 300, TR_NOOP ("Amplifier"));
    static const std::pair<const char*, const char*> amp[4] = { { "Attack", TR_NOOP ("Attack") }, { "Decay", TR_NOOP ("Decay") },
                                                                { "Sustain", TR_NOOP ("Sustain") }, { "Release", TR_NOOP ("Release") } };
    for (int i = 0; i < 4; ++i)
        p.fader (60.0f + 72.0f * (float) i, 78, 26, 230, amp[i].first, amp[i].second);
    p.display (390, 70, 300, 200, "ministenv", "Attack");
    on (1, from);
    // FILTER
    from = p.v.size();
    p.section (24, 50, 330, 300, TR_NOOP ("Filter"));
    p.display (40, 74, 296, 260, "xy", "Cutoff", "Resonance");
    p.section (380, 50, 300, 190, TR_NOOP ("Filter tracking"));
    p.knob (420, 90, 74, "Filter key tracking", TR_NOOP ("Keyboard"), big);
    p.knob (560, 90, 74, "Filter velocity", TR_NOOP ("Velocity"), big);
    p.knob (420, 260, 50, "Cutoff", TR_NOOP ("Cutoff"));
    p.knob (520, 260, 50, "Resonance", TR_NOOP ("Resonance"));
    on (2, from);
    // FILTER ENV
    from = p.v.size();
    p.section (24, 50, 380, 300, TR_NOOP ("Filter envelope"));
    static const std::pair<const char*, const char*> fenv[4] = { { "Filter attack", TR_NOOP ("Attack") }, { "Filter decay", TR_NOOP ("Decay") },
                                                                 { "Filter sustain", TR_NOOP ("Sustain") }, { "Filter release", TR_NOOP ("Release") } };
    for (int i = 0; i < 4; ++i)
        p.fader (56.0f + 88.0f * (float) i, 78, 26, 230, fenv[i].first, fenv[i].second);
    p.section (430, 120, 140, 150, TR_NOOP ("Amount"));
    p.knob (464, 150, 70, "Filter env amount", nullptr, bipolar | big);
    on (3, from);
    // LFO
    from = p.v.size();
    p.section (18, 50, 136, 300, TR_NOOP ("Shape"));
    p.v.push_back ([] {
        LayoutItem r;
        r.type = Item::radio;
        r.x = 30, r.y = 80, r.w = 110, r.h = 140;
        r.param = "LFO shape";
        r.options = "Sine|Saw|Square";
        r.values = { 0.0f, 0.5f, 1.0f };
        return r;
    }());
    p.section (168, 50, 136, 300, TR_NOOP ("Rate"));
    p.knob (200, 110, 72, "LFO rate", nullptr, big, 11);
    p.value (190, 200, 92, 20, "LFO rate");
    p.section (320, 50, 190, 300, TR_NOOP ("Destination"));
    p.v.push_back ([] {
        LayoutItem r;
        r.type = Item::radio;
        r.x = 340, r.y = 80, r.w = 150, r.h = 140;
        r.param = "LFO destination";
        r.options = "Filter|Frequency|Osc. modifier";
        r.values = { 0.0f, 0.5f, 1.0f };
        return r;
    }());
    p.section (526, 50, 136, 300, TR_NOOP ("Amount"));
    p.knob (558, 110, 72, "LFO amount", nullptr, bipolar | big);
    on (4, from);
    // EFFECTS: distortion, delay, chorus, phaser
    from = p.v.size();
    p.section (8, 50, 160, 300, TR_NOOP ("Distortion"));
    p.toggle (20, 74, 120, 18, "Distortion on", TR_NOOP ("On"));
    p.knob (50, 110, 56, "Overdrive", TR_NOOP ("Overdrive"));
    p.knob (50, 220, 56, "Decimator", TR_NOOP ("Decimator"));
    p.section (176, 50, 176, 300, TR_NOOP ("Delay"));
    p.toggle (188, 74, 120, 18, "Delay on", TR_NOOP ("On"));
    p.knob (190, 110, 44, "Delay time", TR_NOOP ("Time"));
    p.knob (290, 110, 44, "Delay feedback", TR_NOOP ("Feedback"));
    p.knob (190, 220, 44, "Delay filter", TR_NOOP ("Filter"));
    p.knob (290, 220, 44, "Delay mix", TR_NOOP ("Mix"));
    p.section (360, 50, 160, 300, TR_NOOP ("Chorus"));
    p.toggle (372, 74, 120, 18, "Chorus on", TR_NOOP ("On"));
    p.knob (376, 110, 44, "Chorus rate", TR_NOOP ("Rate"));
    p.knob (456, 110, 44, "Chorus depth", TR_NOOP ("Mod"));
    p.knob (416, 220, 44, "Chorus mix", TR_NOOP ("Mix"));
    p.section (528, 50, 181, 300, TR_NOOP ("Phaser"));
    p.toggle (540, 74, 120, 18, "Phaser on", TR_NOOP ("On"));
    p.knob (544, 110, 40, "Phaser speed", TR_NOOP ("Speed"), 0, 11);
    p.knob (634, 110, 40, "Phaser width", TR_NOOP ("Width"));
    p.knob (544, 210, 40, "Phaser feedback", TR_NOOP ("Feedback"));
    p.knob (634, 210, 40, "Phaser mix", TR_NOOP ("Mix"));
    p.knob (590, 280, 40, "Phaser frequency", TR_NOOP ("Freq"));
    on (5, from);
    p.name (8, 360, 200, 36, "MiniSynth");
    p.label (220, 370, 489, 16, TR_NOOP ("Plays FL Studio's own presets and data files: no LYNX presets for this engine"), dim);
    return std::move (p.l);
}

// --- GMS (its skin, 1065 × 414): the oscillator screen (waves read from FL, so their names only), mix / phase / unison /
// octave, modulation, filter, the two envelopes and LFOs side by side, the level envelope, channel, and the ten effects
// on pages ----------------------------------------------------------------------------------------------------------

Layout gmsLayout()
{
    Page p ("GMS", 1000, 440);
    p.label (4, 420, 992, 16, TR_NOOP ("Plays FL Studio's own presets and data files: no LYNX presets for this engine"), dim);
    static const char* const fx[10] = { TR_NOOP ("Distortion"), TR_NOOP ("Crusher"), TR_NOOP ("Flanger"), TR_NOOP ("Phaser"), TR_NOOP ("Low pass"),
                                        TR_NOOP ("High pass"), TR_NOOP ("Delay"), TR_NOOP ("Reverb"), TR_NOOP ("Panner"), TR_NOOP ("Trancer") };
    p.l.pages.assign (fx, fx + 10);

    // the oscillator screen
    p.section (4, 4, 432, 196, TR_NOOP ("Oscillators"));
    p.display (12, 24, 150, 84, "gmswaves");
    p.label (166, 24, 44, 12, TR_NOOP ("Pitch"), dim);
    p.label (212, 24, 44, 12, TR_NOOP ("Fine"), dim);
    static const char* const pitch[3] = { "Osc 1 pitch", "Osc 2 pitch", "Osc 3 pitch" };
    static const char* const fine[3] = { "Osc 1 detune", "Osc 2 detune", "Osc 3 detune" };
    for (int o = 0; o < 3; ++o)
    {
        p.knob (174, 38.0f + 24.0f * (float) o, 22, pitch[o], nullptr, bipolar);
        p.knob (220, 38.0f + 24.0f * (float) o, 22, fine[o], nullptr, bipolar);
    }
    p.knob (266, 30, 28, "Osc 2 mix", TR_NOOP ("Osc 2"));
    p.knob (266, 78, 28, "Osc 3 mix", TR_NOOP ("Osc 3"));
    p.knob (266, 126, 28, "Noise mix", TR_NOOP ("Noise"));
    p.knob (310, 30, 28, "Osc 2 phase offset", TR_NOOP ("Phase"));
    p.toggle (302, 78, 50, 16, "Osc 2 phase retrigger", TR_NOOP ("Retrig"), boxed);
    p.toggle (302, 98, 50, 16, "Osc 2 invert", TR_NOOP ("Inv"), boxed);
    p.knob (310, 126, 28, "Unison phase", TR_NOOP ("Unison"));
    p.toggle (302, 170, 50, 16, "Unison phase retrigger", TR_NOOP ("Retrig"), boxed);
    p.knob (356, 30, 28, "Unison voices", TR_NOOP ("Voices"), 0, 15);
    p.knob (356, 78, 28, "Unison stereo", TR_NOOP ("Stereo"));
    p.knob (356, 126, 28, "Unison detune", TR_NOOP ("Detune"));
    p.knob (398, 30, 28, "Octave", TR_NOOP ("Octave"), bipolar, 4);
    p.toggle (392, 78, 40, 16, "Mono", TR_NOOP ("Mono"), boxed);
    p.menu (12, 124, 150, 20, "Modulation kind", TR_NOOP ("Modulation"), "Off|FM|Sync", { 0.0f, 0.5f, 1.0f });
    p.fader (12, 160, 150, 16, "Modulation", nullptr);
    p.v.back().type = Item::hslider;
    p.label (12, 182, 150, 12, TR_NOOP ("Amount"), dim);

    // FILTER, ENVELOPES, LFOS
    p.section (4, 204, 120, 212, TR_NOOP ("Filter"));
    p.fader (12, 226, 18, 120, "Cutoff", TR_NOOP ("Cut"));
    p.fader (38, 226, 18, 120, "Resonance", TR_NOOP ("Res"));
    p.fader (64, 226, 18, 120, "Key tracking", TR_NOOP ("Kbd"));
    p.v.push_back ([] {
        LayoutItem r;
        r.type = Item::radio;
        r.x = 88, r.y = 290, r.w = 32, r.h = 66;
        r.param = "Filter type";
        r.options = "LP|BP|HP";
        r.values = { 0.0f, 0.5f, 1.0f };
        return r;
    }());
    static const char* const envTitles[2] = { TR_NOOP ("Envelope 1"), TR_NOOP ("Envelope 2") };
    for (int e = 0; e < 2; ++e)
    {
        static const char* const att[2] = { "Envelope 1 attack", "Envelope 2 attack" };
        static const char* const dec[2] = { "Envelope 1 decay", "Envelope 2 decay" };
        static const char* const amt[2] = { "Envelope 1 amount", "Envelope 2 amount" };
        static const char* const inv[2] = { "Envelope 1 invert", "Envelope 2 invert" };
        static const char* const dst[2] = { "Envelope 1 destination", "Envelope 2 destination" };
        const auto x = 128.0f + 124.0f * (float) e;
        p.section (x, 204, 120, 212, envTitles[e]);
        p.fader (x + 6, 226, 18, 120, att[e], TR_NOOP ("Atk"));
        p.fader (x + 30, 226, 18, 120, dec[e], TR_NOOP ("Dec"));
        p.fader (x + 54, 226, 18, 120, amt[e], TR_NOOP ("Amt"));
        p.toggle (x + 78, 226, 38, 16, inv[e], TR_NOOP ("Inv"), boxed);
        p.v.push_back ([&] {
            LayoutItem r;
            r.type = Item::radio;
            r.x = x + 78, r.y = 268, r.w = 40, r.h = 90;
            r.param = dst[e];
            r.options = "Flt|Mod|Freq|LFO";
            r.values = { 0.0f, 0.34f, 0.67f, 1.0f };
            return r;
        }());
    }
    static const char* const lfoTitles[2] = { TR_NOOP ("LFO 1"), TR_NOOP ("LFO 2") };
    for (int l = 0; l < 2; ++l)
    {
        static const char* const rate[2] = { "LFO 1 rate", "LFO 2 rate" };
        static const char* const amt[2] = { "LFO 1 amount", "LFO 2 amount" };
        static const char* const sync[2] = { "LFO 1 sync", "LFO 2 sync" };
        static const char* const retrig[2] = { "LFO 1 retrigger", "LFO 2 retrigger" };
        static const char* const inv[2] = { "LFO 1 invert", "LFO 2 invert" };
        static const char* const dst[2] = { "LFO 1 destination", "LFO 2 destination" };
        static const char* const shape[2] = { "LFO 1 shape", "LFO 2 shape" };
        const auto x = 376.0f + 150.0f * (float) l;
        p.section (x, 204, 146, 212, lfoTitles[l]);
        p.fader (x + 6, 226, 18, 120, rate[l], TR_NOOP ("Rate"));
        p.fader (x + 30, 226, 18, 120, amt[l], TR_NOOP ("Amt"));
        p.menu (x + 56, 238, 84, 18, sync[l], TR_NOOP ("Sync"), "None|Beat|Bar", { 0.0f, 0.5f, 1.0f });
        p.toggle (x + 56, 262, 40, 16, retrig[l], TR_NOOP ("Retrig"), boxed);
        p.toggle (x + 100, 262, 40, 16, inv[l], TR_NOOP ("Inv"), boxed);
        p.menu (x + 56, 298, 84, 18, dst[l], TR_NOOP ("Dest"), "Flt|Mod|Freq|Level", { 0.0f, 0.34f, 0.67f, 1.0f });
        p.menu (x + 56, 336, 84, 18, shape[l], TR_NOOP ("Shape"), "Sine|Triangle|Saw|Square", { 0.0f, 0.34f, 0.67f, 1.0f });
    }
    // LEVEL ENVELOPE and CHANNEL
    p.section (440, 4, 100, 196, TR_NOOP ("Level EG"));
    p.knob (448, 26, 30, "Amp level", TR_NOOP ("Amp"));
    p.knob (500, 26, 30, "Attack", TR_NOOP ("Att"));
    p.knob (448, 84, 30, "Decay", TR_NOOP ("Dec"));
    p.knob (500, 84, 30, "Sustain", TR_NOOP ("Sus"));
    p.knob (474, 140, 30, "Release", TR_NOOP ("Rel"));
    p.section (544, 4, 128, 196, TR_NOOP ("Channel"));
    p.fader (552, 24, 22, 130, "Master level", TR_NOOP ("Out"));
    p.knob (590, 26, 30, "Panning", TR_NOOP ("Pan"), bipolar);
    p.knob (632, 26, 30, "Pitch", TR_NOOP ("Pitch"), bipolar);
    p.knob (590, 90, 30, "Slide time", TR_NOOP ("Slide"));
    p.toggle (628, 96, 40, 16, "Slide", TR_NOOP ("On"), boxed);
    // EFFECTS: one page per unit
    p.section (676, 4, 320, 196, TR_NOOP ("Effects"));
    {
        LayoutItem t;
        t.type = Item::tabs;
        t.x = 682, t.y = 24, t.w = 308, t.h = 36;
        t.options = "Dist|Crush|Flang|Phas|LP|HP|Delay|Verb|Pan|Tranc";
        t.style = pageTabs;
        p.v.push_back (t);
    }
    static const char* const fxNames[10] = { "Distortion", "Crusher", "Flanger", "Phaser", "Low pass", "High pass", "Delay", "Reverb", "Panner", "Trancer" };
    static const char* const xNames[10] = { TR_NOOP ("Distortion"), TR_NOOP ("Crush"), TR_NOOP ("Time"), TR_NOOP ("Depth"), TR_NOOP ("Frequency"),
                                            TR_NOOP ("Frequency"), TR_NOOP ("Feedback"), TR_NOOP ("Decay"), TR_NOOP ("Panning"), TR_NOOP ("Release") };
    static const char* const yNames[10] = { TR_NOOP ("Filter"), TR_NOOP ("Filter"), TR_NOOP ("Feedback"), TR_NOOP ("Feedback"), TR_NOOP ("Bandwidth"),
                                            TR_NOOP ("Bandwidth"), TR_NOOP ("Filter"), TR_NOOP ("Damping"), TR_NOOP ("Binaural"), TR_NOOP ("Depth") };
    static std::vector<std::string> keep; // the items' parameter names ("<unit> X"...): stable storage
    if (keep.empty())
        for (int k = 0; k < 10; ++k)
            for (const char* f : { " on", " X", " Y", " LFO rate", " LFO amount", " LFO shape", " LFO sync" })
                keep.push_back (std::string (fxNames[k]) + f);
    for (int k = 0; k < 10; ++k)
    {
        const auto at = [&] (int f) { return keep[(std::size_t) (7 * k + f)].c_str(); };
        const auto from = p.v.size();
        p.toggle (690, 70, 60, 18, at (0), TR_NOOP ("On"), boxed);
        p.knob (692, 108, 40, at (1), xNames[k]);
        p.knob (752, 108, 40, at (2), yNames[k]);
        p.knob (820, 70, 30, at (3), TR_NOOP ("LFO rate"));
        p.knob (870, 70, 30, at (4), TR_NOOP ("LFO amt"), bipolar);
        p.menu (820, 136, 80, 18, at (5), TR_NOOP ("Shape"), "Sine|Triangle|Saw|Square", { 0.0f, 0.34f, 0.67f, 1.0f });
        p.menu (910, 136, 80, 18, at (6), TR_NOOP ("Sync"), "None|Beat|Bar", { 0.0f, 0.5f, 1.0f });
        for (auto i = from; i < p.v.size(); ++i)
            p.v[i].page = k;
    }
    // output EQ
    p.section (676, 204, 320, 212, TR_NOOP ("Output EQ"));
    p.toggle (686, 226, 60, 18, "Output EQ", TR_NOOP ("On"), boxed);
    static const char* const eq[5] = { "EQ band 1", "EQ band 2", "EQ band 3", "EQ band 4", "EQ band 5" };
    for (int i = 0; i < 5; ++i)
        p.fader (760.0f + 44.0f * (float) i, 230, 20, 140, eq[i], nullptr, bipolar);
    p.knob (690, 290, 34, "Pitch bend range", TR_NOOP ("Bend"), 0, 12);
    return std::move (p.l);
}

// --- Morphine (its skin, 886 × 552): the top bar (polyphony / transpose / fine, glide, velocity curve, the master
// envelope, drive and level), the noise generator and the effects row (chorus, delay, reverb, the 8-band EQ); the
// generators, morph line and modulation pages keep the saved patch ------------------------------------------------

Layout morphineLayout()
{
    Page p ("Morphine", 886, 420);
    p.section (4, 4, 270, 96, nullptr, boxed);
    p.name (12, 8, 254, 40, "Morphine");
    p.knob (20, 56, 28, "Transpose", TR_NOOP ("Transp"), bipolar);
    p.knob (90, 56, 28, "Fine", TR_NOOP ("Fine"), bipolar);
    p.knob (160, 56, 28, "Bend range", TR_NOOP ("Bend"));
    p.section (278, 4, 130, 96, nullptr, boxed);
    p.knob (290, 30, 34, "Glide", TR_NOOP ("Glide"));
    p.knob (350, 30, 34, "Velocity curve", TR_NOOP ("Vel crv"));
    p.section (412, 4, 320, 96, TR_NOOP ("Master envelope"));
    p.knob (430, 30, 34, "Attack", TR_NOOP ("Attack"));
    p.knob (505, 30, 34, "Decay", TR_NOOP ("Decay"));
    p.knob (580, 30, 34, "Sustain", TR_NOOP ("Sustain"));
    p.knob (655, 30, 34, "Release", TR_NOOP ("Release"));
    p.section (736, 4, 146, 96, TR_NOOP ("Master"));
    p.knob (750, 30, 34, "Overdrive", TR_NOOP ("Drive"));
    p.knob (820, 30, 34, "Master level", TR_NOOP ("Level"));

    p.section (4, 104, 600, 180, TR_NOOP ("Generators"));
    p.label (20, 180, 568, 16, TR_NOOP ("Generators, morph line and modulation keep the saved patch"), dim);
    p.label (20, 204, 568, 16, TR_NOOP ("Plays FL Studio's own presets and data files: no LYNX presets for this engine"), dim);
    p.section (608, 104, 274, 180, TR_NOOP ("Noise"));
    p.knob (624, 130, 40, "Noise mix", TR_NOOP ("Mix"));
    p.knob (690, 130, 40, "Noise pan", TR_NOOP ("Pan"), bipolar);
    p.knob (756, 130, 40, "Noise velocity", TR_NOOP ("Vel"));
    p.toggle (624, 214, 90, 18, "Noise mute", TR_NOOP ("Mute"), boxed);

    p.section (4, 288, 170, 128, TR_NOOP ("Chorus"));
    p.toggle (130, 290, 40, 14, "Chorus on", TR_NOOP ("On"), boxed);
    p.knob (14, 322, 34, "Chorus depth", TR_NOOP ("Depth"));
    p.knob (70, 322, 34, "Chorus rate", TR_NOOP ("Rate"));
    p.knob (126, 322, 34, "Chorus mix", TR_NOOP ("Mix"));
    p.section (178, 288, 284, 128, TR_NOOP ("Delay"));
    p.toggle (418, 290, 40, 14, "Delay on", TR_NOOP ("On"), boxed);
    p.knob (188, 322, 34, "Delay time", TR_NOOP ("Time"));
    p.knob (244, 322, 34, "Delay stereo", TR_NOOP ("Stereo"), bipolar);
    p.knob (300, 322, 34, "Delay feedback", TR_NOOP ("Fdbk"));
    p.knob (356, 322, 34, "Delay modulation", TR_NOOP ("Mod depth"));
    p.knob (412, 322, 34, "Delay mix", TR_NOOP ("Mix"));
    p.section (466, 288, 168, 128, TR_NOOP ("Reverb"));
    p.toggle (590, 290, 40, 14, "Reverb on", TR_NOOP ("On"), boxed);
    p.knob (476, 322, 34, "Reverb decay", TR_NOOP ("Decay"));
    p.knob (532, 322, 34, "Reverb damping", TR_NOOP ("Hi-cut"));
    p.knob (588, 322, 34, "Reverb mix", TR_NOOP ("Mix"));
    p.section (638, 288, 244, 128, TR_NOOP ("Equalizer"));
    p.toggle (838, 290, 40, 14, "EQ on", TR_NOOP ("On"), boxed);
    static const std::pair<const char*, const char*> eq[8] = { { "EQ 62 Hz", "62" }, { "EQ 125 Hz", "125" }, { "EQ 250 Hz", "250" },
                                                               { "EQ 500 Hz", "500" }, { "EQ 1 kHz", "1K" }, { "EQ 2 kHz", "2K" },
                                                               { "EQ 4 kHz", "4K" }, { "EQ 8 kHz", "8K" } };
    for (int i = 0; i < 8; ++i)
        p.fader (646.0f + 29.0f * (float) i, 310, 18, 84, eq[i].first, eq[i].second, bipolar);
    return std::move (p.l);
}
} // namespace

const Layout* synthGroupLayout (InstrumentKind kind)
{
    static const Layout harmless = harmlessLayout(), dx10 = dx10Layout(), sim = simSynthLayout(), ogun = ogunLayout (false),
                        autogun = ogunLayout (true), mini = miniSynthLayout(), gms = gmsLayout(), morphine = morphineLayout();
    switch (kind)
    {
        case InstrumentKind::harmless: return &harmless;
        case InstrumentKind::dx10: return &dx10;
        case InstrumentKind::simsynth: return &sim;
        case InstrumentKind::ogun: return &ogun;
        case InstrumentKind::autogun: return &autogun;
        case InstrumentKind::minisynth: return &mini;
        case InstrumentKind::gms: return &gms;
        case InstrumentKind::morphine: return &morphine;
        default: return nullptr;
    }
}

const std::vector<InstrumentKind>& synthGroupLayoutKinds()
{
    static const std::vector<InstrumentKind> kinds { InstrumentKind::harmless, InstrumentKind::dx10, InstrumentKind::simsynth, InstrumentKind::ogun,
                                                     InstrumentKind::autogun, InstrumentKind::minisynth, InstrumentKind::gms, InstrumentKind::morphine };
    return kinds;
}
} // namespace lynx::plugui
