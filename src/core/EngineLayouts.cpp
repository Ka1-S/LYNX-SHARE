#include "core/EngineLayouts.h"

#include <string>

// Positions: FL's editor forms at 100 % (the SynthPanel of each plugin's TSynthEditorForm, docs/analise/REA_rodada168q.md
// §1), one design unit = one pixel. Section frames and captions are LYNX's (FL paints its own in the background art).

// (the i18n test scans the sources for TR_NOOP: captions below are translated by the editor)
#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::plugui
{
namespace
{
LayoutItem item (Item type, float x, float y, float w, float h, const char* param, const char* label, std::uint16_t style = 0)
{
    LayoutItem i;
    i.type = type;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = label;
    i.style = style;
    return i;
}

LayoutItem section (float x, float y, float w, float h, const char* title, std::uint16_t style = 0)
{
    return item (Item::section, x, y, w, h, nullptr, title, style);
}

LayoutItem label (float x, float y, float w, float h, const char* text, std::uint16_t style = 0)
{
    return item (Item::label, x, y, w, h, nullptr, text, style);
}

// a vertical fader at FL's place (x, y, w, h = the fader itself) with its caption under it
LayoutItem fader (float x, float y, float w, float h, const char* param, const char* caption, std::uint16_t style = 0)
{
    return item (Item::vslider, x, y, w, h + (caption != nullptr ? 14.0f : 0.0f), param, caption, (std::uint16_t) (style | (caption == nullptr ? noCaption : 0)));
}

// a horizontal slider
LayoutItem hfader (float x, float y, float w, float h, const char* param, const char* caption, std::uint16_t style = 0)
{
    return item (Item::hslider, x, y, w, h + (caption != nullptr ? 14.0f : 0.0f), param, caption, (std::uint16_t) (style | (caption == nullptr ? noCaption : 0)));
}

// a knob centred at (cx, cy), `d` wide, the caption under it
LayoutItem dial (float cx, float cy, float d, const char* param, const char* caption, std::uint16_t style = 0)
{
    if (caption == nullptr)
        style = (std::uint16_t) (style | noCaption);
    return item (Item::knob, cx - d * 0.5f, cy - d * 0.5f, d, d + (caption != nullptr ? 16.0f : 0.0f), param, caption, style);
}

LayoutItem toggle (float x, float y, float w, float h, const char* param, const char* caption, std::uint16_t style = 0)
{
    return item (Item::toggle, x, y, w, h, param, caption, style);
}

LayoutItem radio (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {})
{
    auto i = item (Item::radio, x, y, w, h, param, nullptr);
    i.options = options;
    i.values = std::move (values);
    return i;
}

LayoutItem tabs (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {}, std::uint16_t style = 0)
{
    auto i = item (Item::tabs, x, y, w, h, param, nullptr, style);
    i.options = options;
    i.values = std::move (values);
    return i;
}

LayoutItem pageBar (float x, float y, float w, float h, const char* options)
{
    return tabs (x, y, w, h, nullptr, options, {}, plugui::pageTabs);
}

LayoutItem menu (float x, float y, float w, float h, const char* param, const char* caption, const char* options, std::vector<float> values = {})
{
    auto i = item (Item::menu, x, y, w, h, param, caption);
    i.options = options;
    i.values = std::move (values);
    return i;
}

LayoutItem stepper (float x, float y, float w, float h, const char* param, const char* caption, int steps = 0)
{
    auto i = item (Item::stepper, x, y, w, h, param, caption);
    i.steps = steps;
    return i;
}

LayoutItem display (float x, float y, float w, float h, const char* id, const char* param = nullptr)
{
    auto i = item (Item::display, x, y, w, h, param, nullptr);
    i.display = id;
    return i;
}

LayoutItem value (float x, float y, float w, float h, const char* param, std::uint16_t style = 0)
{
    return item (Item::value, x, y, w, h, param, nullptr, style);
}

LayoutItem onPage (LayoutItem i, int page)
{
    i.page = page;
    return i;
}

const std::vector<float> two { 0.0f, 1.0f }, three { 0.0f, 0.5f, 1.0f };
const std::vector<float> four { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
const std::vector<float> seven { 0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 1.0f };
const std::vector<float> six { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
const std::vector<float> nine { 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f };
} // namespace

// --- Kepler ---------------------------------------------------------------------------------------------------
// FL's panel (1080 × 217): the Juno-style strip ARP, LFO, DCO, HPF, VCF, VCA, ENV, CHORUS with Hold, Octave and
// Volume on top. Every value is the panel's own 0…1 float ("Kepler-1" order, REA_rodada46 §1.1); switches store
// 0 / 0.5 / 1 (three positions) or 0 / 1, the chorus 0, 1/3, 2/3, 1.
Layout keplerLayout()
{
    Layout l;
    l.fl = "Kepler";
    l.width = 1080;
    l.height = 217;
    auto& v = l.items;
    v.push_back (label (880, 6, 190, 36, "Kepler", dim));
    v.push_back (section (6, 48, 160, 166, TR_NOOP ("Arpeggio")));
    v.push_back (section (168, 48, 140, 166, TR_NOOP ("LFO")));
    v.push_back (section (310, 48, 278, 166, "DCO"));
    v.push_back (section (590, 48, 40, 166, "HPF"));
    v.push_back (section (632, 48, 178, 166, "VCF"));
    v.push_back (section (812, 48, 50, 166, "VCA"));
    v.push_back (section (864, 48, 122, 166, TR_NOOP ("Env")));
    v.push_back (section (988, 48, 86, 166, TR_NOOP ("Chorus")));

    v.push_back (toggle (11, 18, 37, 19, "Hold", TR_NOOP ("Hold"), boxed));
    v.push_back (label (52, 18, 32, 18, TR_NOOP ("Octave"), alignLeft));
    v.push_back (tabs (88, 18, 66, 18, "Octave", "-1|0|+1", three));
    v.push_back (label (836, 20, 40, 16, TR_NOOP ("Volume")));
    v.push_back (dial (896, 28, 24, "Volume", nullptr));

    v.push_back (toggle (17, 128, 33, 33, "Arp on", TR_NOOP ("On"), boxed));
    v.push_back (label (50, 68, 44, 14, TR_NOOP ("Mode")));
    v.push_back (radio (52, 88, 42, 76, "Arp mode", TR_NOOP ("Up|Up/dn|Down"), three));
    v.push_back (label (92, 68, 34, 14, TR_NOOP ("Range")));
    v.push_back (radio (94, 88, 32, 76, "Arp range", "1|2|3", three));
    v.push_back (fader (139, 93, 23, 104, "Arp rate", TR_NOOP ("Rate")));
    v.push_back (toggle (127, 68, 38, 16, "Arp sync", TR_NOOP ("Sync"))); // (inside the section, round 168q2)

    v.push_back (fader (178, 93, 23, 104, "LFO rate", TR_NOOP ("Rate")));
    v.push_back (toggle (172, 68, 30, 16, "LFO sync", TR_NOOP ("Sync")));
    v.push_back (fader (204, 93, 25, 104, "LFO delay", TR_NOOP ("Delay")));
    v.push_back (radio (234, 96, 42, 52, "LFO trig mode", TR_NOOP ("Auto|Man"), two));
    v.push_back (toggle (236, 168, 38, 22, "LFO trigger", TR_NOOP ("Trig"), boxed));
    v.push_back (toggle (262, 68, 44, 16, "LFO retrig", TR_NOOP ("Phase")));
    v.push_back (fader (279, 93, 23, 104, "LFO phase", nullptr));

    v.push_back (fader (318, 93, 25, 104, "DCO LFO", TR_NOOP ("LFO")));
    v.push_back (fader (345, 93, 25, 104, "PW", "PWM"));
    v.push_back (radio (373, 88, 40, 76, "PW source", TR_NOOP ("LFO|Man|Env"), three));
    v.push_back (toggle (416, 128, 33, 33, "Pulse", TR_NOOP ("Pulse"), boxed));
    v.push_back (toggle (452, 128, 33, 33, "Saw", TR_NOOP ("Saw"), boxed));
    v.push_back (toggle (488, 128, 33, 33, "Sub", TR_NOOP ("Sub"), boxed));
    v.push_back (fader (527, 92, 28, 104, "Sub level", TR_NOOP ("Sub")));
    v.push_back (fader (557, 92, 25, 104, "Noise", TR_NOOP ("Noise")));

    v.push_back (fader (598, 92, 25, 104, "High-pass", TR_NOOP ("Freq")));

    v.push_back (fader (639, 93, 25, 104, "Cutoff", TR_NOOP ("Freq")));
    v.push_back (fader (666, 93, 25, 104, "Resonance", TR_NOOP ("Res")));
    v.push_back (radio (695, 110, 24, 50, "Env polarity", "+|-", two));
    v.push_back (fader (723, 93, 25, 104, "Env amount", TR_NOOP ("Env")));
    v.push_back (fader (750, 93, 25, 104, "VCF LFO", TR_NOOP ("LFO")));
    v.push_back (fader (778, 93, 25, 104, "Key follow", TR_NOOP ("Kybd")));

    v.push_back (radio (815, 100, 44, 56, "VCA source", TR_NOOP ("Env|Gate"), two));

    v.push_back (fader (869, 93, 25, 104, "Attack", "A"));
    v.push_back (fader (896, 93, 25, 104, "Decay", "D"));
    v.push_back (fader (924, 93, 25, 104, "Sustain", "S"));
    v.push_back (fader (952, 93, 25, 104, "Release", "R"));

    v.push_back (radio (993, 74, 76, 104, "Chorus", TR_NOOP ("Off|I|II|I+II"), four));
    return l;
}

// --- Kepler Exo -----------------------------------------------------------------------------------------------
// FL's panel (1080 × 574): DCO 1, cross modulation, DCO 2 and the mixer on top; VCF, HPF, VCA, the two envelopes and
// the two LFOs in the middle; voice modes, arpeggiator, modulation-matrix amounts, the Juno chorus and the effect pages
// (saturation, hyper chorus, EQ, delay, reverb) at the bottom. Values are the "Kepler-2" list's 0…1 floats by internal
// index (REA_rodada167d §1); switch positions as the engine reads them (Kepler.cpp: 6-way shapes x·5, arp mode x·8,
// VCA source Env1 / gate / Env2).
Layout keplerExoLayout()
{
    Layout l;
    l.fl = "Kepler Exo";
    l.width = 1080;
    l.height = 574;
    l.pages = { TR_NOOP ("Saturation"), TR_NOOP ("Chorus"), TR_NOOP ("EQ"), TR_NOOP ("Delay"), TR_NOOP ("Reverb") };
    auto& v = l.items;
    v.push_back (label (910, 6, 160, 34, "Kepler Exo", dim));
    v.push_back (section (18, 46, 330, 186, "DCO 1"));
    v.push_back (section (352, 46, 180, 186, TR_NOOP ("Cross mod")));
    v.push_back (section (536, 46, 406, 186, "DCO 2"));
    v.push_back (section (944, 46, 118, 186, TR_NOOP ("Mixer")));
    v.push_back (section (18, 238, 334, 164, "VCF"));
    v.push_back (section (356, 238, 46, 164, "HPF"));
    v.push_back (section (404, 238, 62, 164, "VCA"));
    v.push_back (section (468, 238, 114, 164, "Env 1"));
    v.push_back (section (588, 238, 112, 164, "Env 2"));
    v.push_back (section (704, 238, 148, 164, TR_NOOP ("LFO 1")));
    v.push_back (section (856, 238, 206, 164, TR_NOOP ("LFO 2")));
    v.push_back (section (18, 406, 124, 164, TR_NOOP ("Voice")));
    v.push_back (section (144, 406, 180, 164, TR_NOOP ("Arpeggio")));
    v.push_back (section (326, 406, 224, 164, TR_NOOP ("Matrix")));
    v.push_back (section (552, 406, 86, 164, TR_NOOP ("Chorus")));
    v.push_back (section (640, 406, 422, 164, nullptr)); // (the page tabs are its header, round 168q2)
    v.push_back (pageBar (724, 407, 334, 16, TR_NOOP ("Saturation|Chorus|EQ|Delay|Reverb")));
    v.push_back (onPage (label (666, 438, 36, 14, TR_NOOP ("Low")), 2));
    v.push_back (onPage (label (697, 438, 36, 14, TR_NOOP ("Mid")), 2));
    v.push_back (onPage (label (728, 438, 36, 14, TR_NOOP ("High")), 2));
    v.push_back (onPage (label (644, 461, 24, 14, TR_NOOP ("Gain"), alignLeft), 2));
    v.push_back (onPage (label (644, 497, 24, 14, TR_NOOP ("Freq"), alignLeft), 2));
    v.push_back (onPage (label (644, 533, 24, 14, "Q", alignLeft), 2));
    // top bar: the X / Y offsets of the modulation routes, master detune, volume and pan
    v.push_back (label (590, 16, 18, 20, "X"));
    v.push_back (dial (620, 26, 22, "X offset", nullptr, bipolar));
    v.push_back (label (638, 16, 18, 20, "Y"));
    v.push_back (dial (668, 26, 22, "Y offset", nullptr, bipolar));
    v.push_back (label (686, 16, 44, 20, TR_NOOP ("Detune")));
    v.push_back (dial (742, 26, 22, "Master detune", nullptr, bipolar));
    v.push_back (label (760, 16, 34, 20, TR_NOOP ("Vol")));
    v.push_back (dial (806, 26, 22, "Volume", nullptr));
    v.push_back (label (824, 16, 30, 20, TR_NOOP ("Pan")));
    v.push_back (dial (866, 26, 22, "Pan", nullptr, bipolar));
    v.push_back (toggle (27, 17, 33, 19, "Hold", TR_NOOP ("Hold"), boxed));
    v.push_back (tabs (94, 17, 66, 18, "Octave", "-1|0|+1", three));
    v.push_back (fader (29, 108, 23, 104, "DCO1 LFO1", "LFO1"));
    v.push_back (toggle (32, 80, 14, 13, "DCO1 LFO1 wide", nullptr));
    v.push_back (fader (55, 108, 23, 104, "DCO1 LFO2", "LFO2"));
    v.push_back (toggle (58, 80, 14, 13, "DCO1 LFO2 wide", nullptr));
    v.push_back (fader (81, 108, 23, 104, "DCO1 env2", "Env2"));
    v.push_back (fader (107, 108, 23, 104, "DCO1 PW", "PWM"));
    v.push_back (radio (134, 112, 42, 70, "DCO1 PW source", TR_NOOP ("LFO|Man|Env"), three));
    v.push_back (toggle (179, 144, 33, 33, "DCO1 pulse", TR_NOOP ("Pulse"), boxed));
    v.push_back (toggle (215, 144, 33, 33, "DCO1 saw", TR_NOOP ("Saw"), boxed));
    v.push_back (toggle (251, 144, 33, 33, "DCO1 sub", TR_NOOP ("Sub"), boxed));
    v.push_back (fader (292, 108, 23, 104, "DCO1 sub level", TR_NOOP ("Sub")));
    v.push_back (fader (321, 108, 23, 104, "DCO1 sub phase", TR_NOOP ("Phase")));
    v.push_back (fader (364, 108, 23, 104, "Cross mod 1>2", "1>2"));
    v.push_back (radio (395, 108, 49, 104, "Cross mod 1>2 shape", TR_NOOP ("Sine|Tri|Pulse|Saw|Noise|DCO1"), six));
    v.push_back (fader (446, 108, 23, 104, "Cross mod 2>1", "2>1"));
    v.push_back (radio (478, 108, 49, 104, "Cross mod 2>1 shape", TR_NOOP ("Sine|Tri|Pulse|Saw|Noise|DCO2"), six));
    v.push_back (toggle (896, 66, 40, 14, "DCO2 on", TR_NOOP ("On")));
    v.push_back (fader (543, 108, 23, 104, "DCO2 LFO1", "LFO1"));
    v.push_back (toggle (546, 80, 14, 13, "DCO2 LFO1 wide", nullptr));
    v.push_back (fader (569, 108, 23, 104, "DCO2 LFO2", "LFO2"));
    v.push_back (toggle (572, 80, 14, 13, "DCO2 LFO2 wide", nullptr));
    v.push_back (fader (595, 108, 23, 104, "DCO2 env2", "Env2"));
    v.push_back (fader (621, 108, 23, 104, "DCO2 PW", "PWM"));
    v.push_back (radio (646, 112, 42, 70, "DCO2 PW source", TR_NOOP ("LFO|Man|Env"), three));
    v.push_back (toggle (691, 144, 33, 33, "DCO2 pulse", TR_NOOP ("Pulse"), boxed));
    v.push_back (toggle (727, 144, 33, 33, "DCO2 saw", TR_NOOP ("Saw"), boxed));
    v.push_back (toggle (763, 144, 33, 33, "DCO2 sub", TR_NOOP ("Sub"), boxed));
    v.push_back (fader (803, 108, 23, 104, "DCO2 sub level", TR_NOOP ("Sub")));
    v.push_back (fader (829, 108, 23, 104, "DCO2 sub phase", TR_NOOP ("Phase")));
    v.push_back (fader (855, 108, 23, 104, "DCO2 semitones", TR_NOOP ("Semi")));
    v.push_back (fader (881, 108, 23, 104, "DCO2 detune", TR_NOOP ("Fine")));
    v.push_back (toggle (908, 80, 34, 14, "DCO2 sync", TR_NOOP ("Sync")));
    v.push_back (fader (950, 108, 23, 104, "DCO1 level", "DCO1"));
    v.push_back (fader (976, 108, 23, 104, "DCO2 level", "DCO2"));
    v.push_back (fader (1002, 108, 23, 104, "Noise", TR_NOOP ("Noise")));
    v.push_back (fader (1027, 108, 23, 104, "AM", "AM"));
    v.push_back (radio (22, 284, 48, 84, "Filter slope", "6 dB|12 dB|18 dB|24 dB", { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (fader (74, 279, 23, 104, "Cutoff", TR_NOOP ("Freq")));
    v.push_back (fader (99, 279, 23, 104, "Resonance", TR_NOOP ("Res")));
    v.push_back (radio (126, 300, 24, 50, "Env polarity", "+|-", two));
    v.push_back (fader (152, 279, 23, 104, "Env amount", "Env1"));
    v.push_back (fader (178, 279, 23, 104, "VCF LFO1", "LFO1"));
    v.push_back (fader (204, 279, 23, 104, "VCF LFO2", "LFO2"));
    v.push_back (fader (230, 279, 23, 104, "Key follow", TR_NOOP ("Kybd")));
    v.push_back (radio (258, 272, 62, 112, "Filter FM shape", TR_NOOP ("Sine|Tri|Pulse|Saw|Noise|DCO1"), six));
    v.push_back (fader (323, 279, 23, 104, "Filter FM", TR_NOOP ("FM")));
    v.push_back (toggle (364, 256, 38, 14, "HPF on", TR_NOOP ("On")));
    v.push_back (fader (371, 279, 23, 104, "High-pass", TR_NOOP ("Freq")));
    v.push_back (radio (410, 274, 50, 66, "VCA source", TR_NOOP ("Env1|Gate|Env2"), three));
    v.push_back (dial (434, 352, 22, "Velocity to volume", TR_NOOP ("Vel")));
    v.push_back (toggle (408, 386, 56, 14, "VCA smooth", TR_NOOP ("Smooth")));
    v.push_back (fader (475, 279, 23, 104, "Env1 attack", "A"));
    v.push_back (fader (500, 279, 23, 104, "Env1 decay", "D"));
    v.push_back (fader (526, 279, 23, 104, "Env1 sustain", "S"));
    v.push_back (fader (552, 279, 23, 104, "Env1 release", "R"));
    v.push_back (fader (596, 279, 23, 104, "Env2 attack", "A"));
    v.push_back (fader (621, 279, 23, 104, "Env2 decay", "D"));
    v.push_back (fader (647, 279, 23, 104, "Env2 sustain", "S"));
    v.push_back (fader (673, 279, 23, 104, "Env2 release", "R"));
    v.push_back (fader (712, 279, 23, 104, "LFO1 rate", TR_NOOP ("Rate")));
    v.push_back (toggle (710, 256, 38, 14, "LFO1 sync", TR_NOOP ("Sync")));
    v.push_back (fader (740, 279, 23, 104, "LFO1 delay", TR_NOOP ("Delay")));
    v.push_back (radio (770, 296, 44, 48, "LFO1 trig mode", TR_NOOP ("Auto|Man"), two));
    v.push_back (toggle (808, 256, 44, 14, "LFO1 retrig", TR_NOOP ("Phase")));
    v.push_back (fader (821, 279, 23, 104, "LFO1 phase", nullptr));
    v.push_back (fader (862, 279, 23, 104, "LFO2 rate", TR_NOOP ("Rate")));
    v.push_back (toggle (858, 256, 38, 14, "LFO2 sync", TR_NOOP ("Sync")));
    v.push_back (fader (890, 279, 23, 104, "LFO2 delay", TR_NOOP ("Delay")));
    v.push_back (radio (922, 296, 44, 48, "LFO2 trig mode", TR_NOOP ("Auto|Man"), two));
    v.push_back (toggle (964, 256, 44, 14, "LFO2 retrig", TR_NOOP ("Phase")));
    v.push_back (fader (975, 279, 23, 104, "LFO2 phase", nullptr));
    v.push_back (radio (1004, 272, 58, 112, "LFO2 shape", TR_NOOP ("Tri|Ramp up|Ramp dn|Sine|S&H|Rnd glide"), six));
    v.push_back (toggle (26, 424, 56, 20, "LFO trigger", TR_NOOP ("LFO trig"), boxed));
    v.push_back (tabs (26, 446, 50, 18, "Portamento mode", TR_NOOP ("Exp|Lin"), two));
    v.push_back (dial (49.5, 527, 34, "Portamento", TR_NOOP ("Glide")));
    v.push_back (toggle (82, 465, 52, 14, "Mono", TR_NOOP ("Mono")));
    v.push_back (toggle (82, 489, 52, 14, "Legato", TR_NOOP ("Legato")));
    v.push_back (toggle (82, 514, 52, 14, "Retrigger", TR_NOOP ("Retrig")));
    v.push_back (toggle (82, 539, 56, 14, "Release mode", TR_NOOP ("Release")));
    v.push_back (toggle (151, 459, 33, 33, "Arp on", TR_NOOP ("On"), boxed));
    v.push_back (radio (152, 498, 32, 62, "Arp range", "1|2|3", three));
    v.push_back (menu (190, 428, 62, 36, "Arp mode", TR_NOOP ("Mode"), TR_NOOP ("Up|Up/down|Down|Random 1|Random 2|Random 3|As played up|As played up/down|As played down"), nine));
    v.push_back (fader (263, 453, 23, 104, "Arp rate", TR_NOOP ("Rate")));
    v.push_back (toggle (256, 424, 38, 14, "Arp sync", TR_NOOP ("Sync")));
    v.push_back (fader (291, 453, 23, 104, "Arp gate", TR_NOOP ("Gate")));
    v.push_back (dial (577, 453, 30, "Chorus noise", TR_NOOP ("Noise"))); // (its caption clear of the modes, round 168q2)
    v.push_back (toggle (596, 424, 38, 14, "Chorus gate", TR_NOOP ("Gate")));
    v.push_back (radio (560, 488, 70, 74, "Chorus", TR_NOOP ("Off|I|II|I+II"), four));
    v.push_back (dial (349, 459, 26, "Mod 1 amount", "1"));
    v.push_back (dial (401, 459, 26, "Mod 2 amount", "2"));
    v.push_back (dial (453, 459, 26, "Mod 3 amount", "3"));
    v.push_back (dial (505, 459, 26, "Mod 4 amount", "4"));
    v.push_back (dial (349, 519, 26, "Mod 5 amount", "5"));
    v.push_back (dial (401, 519, 26, "Mod 6 amount", "6"));
    v.push_back (dial (453, 519, 26, "Mod 7 amount", "7"));
    v.push_back (dial (505, 519, 26, "Mod 8 amount", "8"));
    v.push_back (onPage (toggle (650, 424, 40, 14, "Saturation on", TR_NOOP ("On")), 0));
    v.push_back (onPage (fader (732, 453, 23, 104, "Saturation", TR_NOOP ("Drive")), 0));
    v.push_back (onPage (tabs (670, 496, 54, 20, "Saturation post", TR_NOOP ("Pre|Post"), two), 0));
    v.push_back (onPage (toggle (684, 544, 40, 14, "Saturation HQ", TR_NOOP ("HQ")), 0));
    v.push_back (onPage (toggle (650, 424, 40, 14, "Hyper chorus on", TR_NOOP ("On")), 1));
    v.push_back (onPage (fader (655, 453, 23, 104, "Hyper chorus delay", TR_NOOP ("Delay")), 1));
    v.push_back (onPage (fader (692, 453, 23, 104, "Hyper chorus feedback", TR_NOOP ("Fdbk")), 1));
    v.push_back (onPage (fader (730, 453, 23, 104, "Hyper chorus depth", TR_NOOP ("Depth")), 1));
    v.push_back (onPage (fader (767, 453, 23, 104, "Hyper chorus rate", TR_NOOP ("Rate")), 1));
    v.push_back (onPage (fader (805, 453, 23, 104, "Hyper chorus movement", TR_NOOP ("Move")), 1));
    v.push_back (onPage (fader (843, 453, 23, 104, "Hyper chorus movement rate", TR_NOOP ("M.rate")), 1));
    v.push_back (onPage (radio (876, 480, 34, 60, "Hyper chorus lines", "2|4|6", three), 1));
    v.push_back (onPage (fader (919, 453, 23, 104, "Hyper chorus low cut", TR_NOOP ("LoCut")), 1));
    v.push_back (onPage (fader (955, 453, 23, 104, "Hyper chorus high cut", TR_NOOP ("HiCut")), 1));
    v.push_back (onPage (fader (991, 453, 23, 104, "Hyper chorus spread", TR_NOOP ("Spread")), 1));
    v.push_back (onPage (fader (1027, 453, 23, 104, "Hyper chorus mix", TR_NOOP ("Mix")), 1));
    v.push_back (onPage (toggle (650, 424, 40, 14, "EQ on", TR_NOOP ("On")), 2));
    v.push_back (onPage (dial (684, 468, 30, "EQ low gain", nullptr, bipolar), 2));
    v.push_back (onPage (dial (715, 468, 30, "EQ mid gain", nullptr, bipolar), 2));
    v.push_back (onPage (dial (746, 468, 30, "EQ high gain", nullptr, bipolar), 2));
    v.push_back (onPage (dial (684, 504, 30, "EQ low freq", nullptr), 2));
    v.push_back (onPage (dial (715, 504, 30, "EQ mid freq", nullptr), 2));
    v.push_back (onPage (dial (746, 504, 30, "EQ high freq", nullptr), 2));
    v.push_back (onPage (dial (684, 540, 30, "EQ low Q", nullptr), 2));
    v.push_back (onPage (dial (715, 540, 30, "EQ mid Q", nullptr), 2));
    v.push_back (onPage (dial (746, 540, 30, "EQ high Q", nullptr), 2));
    v.push_back (onPage (toggle (650, 424, 40, 14, "Delay on", TR_NOOP ("On")), 3));
    v.push_back (onPage (toggle (744, 424, 40, 14, "Delay sync", TR_NOOP ("Sync")), 3));
    v.push_back (onPage (fader (709, 453, 23, 104, "Delay amount", TR_NOOP ("Input")), 3));
    v.push_back (onPage (fader (748, 453, 23, 104, "Delay time", TR_NOOP ("Time")), 3));
    v.push_back (onPage (fader (788, 453, 23, 104, "Delay right tap", TR_NOOP ("R tap")), 3));
    v.push_back (onPage (fader (825, 453, 23, 104, "Delay feedback", TR_NOOP ("Fdbk")), 3));
    v.push_back (onPage (fader (862, 453, 23, 104, "Delay feedback cutoff", TR_NOOP ("Cutoff")), 3));
    v.push_back (onPage (radio (894, 474, 44, 60, "Delay feedback filter", "LP|HP|BP", three), 3));
    v.push_back (onPage (fader (947, 453, 23, 104, "Delay stereo", TR_NOOP ("Stereo")), 3));
    v.push_back (onPage (fader (987, 453, 23, 104, "Delay blur", TR_NOOP ("Blur")), 3));
    v.push_back (onPage (fader (1027, 453, 23, 104, "Delay tone", TR_NOOP ("Tone")), 3));
    v.push_back (onPage (toggle (650, 424, 40, 14, "Reverb on", TR_NOOP ("On")), 4));
    v.push_back (onPage (fader (670, 453, 25, 104, "Reverb low cut", TR_NOOP ("LoCut")), 4));
    v.push_back (onPage (fader (697, 453, 25, 104, "Reverb high cut", TR_NOOP ("HiCut")), 4));
    v.push_back (onPage (fader (725, 453, 25, 104, "Reverb predelay", TR_NOOP ("Pre")), 4));
    v.push_back (onPage (fader (753, 453, 25, 104, "Reverb predelay feedback", TR_NOOP ("P.fb")), 4));
    v.push_back (onPage (fader (807, 453, 25, 104, "Reverb size", TR_NOOP ("Size")), 4));
    v.push_back (onPage (fader (834, 453, 25, 104, "Reverb diffusion", TR_NOOP ("Diff")), 4));
    v.push_back (onPage (fader (862, 453, 25, 104, "Reverb decay", TR_NOOP ("Decay")), 4));
    v.push_back (onPage (fader (890, 453, 25, 104, "Reverb damping", TR_NOOP ("Damp")), 4));
    v.push_back (onPage (fader (943, 453, 25, 104, "Reverb width", TR_NOOP ("Width")), 4));
    v.push_back (onPage (fader (971, 453, 25, 104, "Reverb dry", TR_NOOP ("Dry")), 4));
    v.push_back (onPage (fader (999, 453, 25, 104, "Reverb early", TR_NOOP ("ER")), 4));
    v.push_back (onPage (fader (1027, 453, 25, 104, "Reverb wet", TR_NOOP ("Wet")), 4));
    return l;
}

// --- Sawer ----------------------------------------------------------------------------------------------------
// FL's panel (897 × 622, the on-screen keyboard left out): MASTER, PITCH, GLIDE, VOICE and UNISON on top; the
// oscillator (noise, sync) and the sub-saw, the filter with its envelope, the LFO; chorus, phaser, delay and reverb;
// the six-slot modulation matrix and the arpeggiator. Values: the plugin's P[0…95] floats (REA_rodada65 §3.3) — 0…1
// for the controls, the stored whole numbers for polyphony, bend range, arp division and the matrix's sources /
// destinations.
namespace
{
const char* sawerSourceList()
{
    static const std::string list = [] {
        std::string s = "---|Velocity|Aftertouch|Pitch wheel";
        for (int cc = 0; cc < 128; ++cc)
            s += "|CC " + std::to_string (cc);
        return s;
    }();
    return list.c_str();
}

const char* sawerDestinationList()
{
    // "---" (−1), then the parameters 0…72 in the plugin's order (the names of the Sawer table, core/Sawer.cpp)
    static const char* const names[] = {
        "Level", "Pan", "Attack", "Decay", "Sustain", "Release", "Transpose", "Fine tune", "Mono", "Glide time mode",
        "Glide key mode", "Glide time", "Octaver", "Unison retrig", "Unison voices", "Unison detune", "Unison pan", "Sub phase",
        "Sub harmonic", "Sub level", "Sub detune", "Sub retrig", "Sub invert", "Sub pulse", "Sync frequency", "Sync on", "Noise",
        "Cutoff", "Resonance", "Key tracking", "Velocity tracking", "Filter mode", "EGF attack", "EGF decay", "EGF sustain",
        "EGF release", "EGF amount", "EGF destination", "LFO shape", "LFO speed", "LFO tempo sync", "LFO attack", "LFO release",
        "LFO retrig", "LFO invert", "LFO amount", "LFO destination", "Chorus on", "Chorus depth", "Chorus rate", "Chorus mix",
        "Phaser on", "Phaser center", "Phaser rate", "Phaser depth", "Phaser feedback", "Phaser mix", "Delay on", "Delay left",
        "Delay right", "Delay feedback", "Delay depth", "Delay low cut", "Delay high cut", "Delay mix", "Reverb on",
        "Reverb decay", "Reverb high cut", "Reverb damp", "Reverb mix", "Arp on", "Arp mode", "Arp range" };
    static const std::string list = [] {
        std::string s = "---";
        for (const auto* n : names)
            s += std::string ("|") + n;
        return s;
    }();
    return list.c_str();
}
} // namespace

Layout sawerLayout()
{
    Layout l;
    l.fl = "Sawer";
    l.width = 897;
    l.height = 612;
    auto& v = l.items;
    const auto* sources = sawerSourceList();
    const auto* destinations = sawerDestinationList();
    v.push_back (label (24, 4, 200, 36, "Sawer", dim | alignLeft));
    v.push_back (label (676, 16, 42, 16, TR_NOOP ("Voices")));
    v.push_back (label (776, 16, 42, 16, TR_NOOP ("Bend")));
    v.push_back (section (20, 46, 272, 126, TR_NOOP ("Master")));
    v.push_back (section (296, 46, 100, 126, TR_NOOP ("Pitch")));
    v.push_back (section (400, 46, 148, 126, TR_NOOP ("Glide")));
    v.push_back (section (550, 46, 122, 126, TR_NOOP ("Voice")));
    v.push_back (section (676, 46, 198, 126, TR_NOOP ("Unison")));
    v.push_back (section (20, 176, 280, 96, TR_NOOP ("Oscillator")));
    v.push_back (section (302, 176, 316, 96, TR_NOOP ("Filter")));
    v.push_back (section (620, 176, 254, 208, TR_NOOP ("LFO")));
    v.push_back (section (20, 276, 280, 108, TR_NOOP ("Sub saw")));
    v.push_back (section (302, 276, 316, 108, TR_NOOP ("Filter envelope")));
    v.push_back (section (20, 412, 134, 106, nullptr));
    v.push_back (section (156, 412, 226, 106, nullptr));
    v.push_back (section (384, 412, 306, 106, nullptr));
    v.push_back (section (692, 412, 182, 106, nullptr));
    v.push_back (section (20, 520, 644, 92, TR_NOOP ("Modulation")));
    v.push_back (section (666, 520, 208, 92, TR_NOOP ("Arpeggio")));
    v.push_back (label (24, 392, 200, 16, TR_NOOP ("Effects"), alignLeft));
    v.push_back (label (150, 296, 94, 14, TR_NOOP ("Harmonic")));
    v.push_back (label (780, 296, 90, 14, TR_NOOP ("Destination")));
    v.push_back (dial (61, 107, 52, "Level", TR_NOOP ("Level")));
    v.push_back (dial (125, 107, 52, "Pan", TR_NOOP ("Pan"), bipolar));
    v.push_back (fader (175, 67, 22, 86, "Attack", "A"));
    v.push_back (fader (205, 67, 22, 86, "Decay", "D"));
    v.push_back (fader (235, 67, 22, 86, "Sustain", "S"));
    v.push_back (fader (265, 67, 22, 86, "Release", "R"));
    v.push_back (fader (322, 67, 19, 86, "Transpose", TR_NOOP ("Coarse"), bipolar));
    v.push_back (fader (368, 67, 19, 86, "Fine tune", TR_NOOP ("Fine"), bipolar));
    v.push_back (radio (406, 70, 34, 60, "Glide time mode", TR_NOOP ("Rate|Time"), two));
    v.push_back (radio (443, 70, 40, 60, "Glide key mode", TR_NOOP ("Always|Held"), two));
    v.push_back (dial (516, 107, 52, "Glide time", TR_NOOP ("Time")));
    v.push_back (radio (552, 70, 40, 60, "Mono", TR_NOOP ("Poly|Mono"), two));
    v.push_back (radio (594, 70, 32, 60, "Octaver", TR_NOOP ("Off|On"), two));
    v.push_back (radio (630, 70, 36, 60, "Unison retrig", TR_NOOP ("Free|Sync"), two));
    v.push_back (radio (680, 64, 58, 70, "Unison voices", "1|2|4|8", four));
    v.push_back (dial (776, 107, 52, "Unison detune", TR_NOOP ("Detune")));
    v.push_back (dial (837, 107, 52, "Unison pan", TR_NOOP ("Spread")));
    v.push_back (dial (61, 223, 52, "Noise", TR_NOOP ("Noise")));
    v.push_back (dial (125, 223, 52, "Sync frequency", TR_NOOP ("Sync")));
    v.push_back (toggle (250, 180, 44, 14, "Sync on", TR_NOOP ("On"))); // (clear of the section title, round 168q2)
    v.push_back (radio (188, 200, 36, 60, "Sub retrig", TR_NOOP ("Free|Sync"), two));
    v.push_back (radio (228, 200, 30, 60, "Sub invert", "+|-", two));
    v.push_back (radio (262, 200, 36, 60, "Sub pulse", TR_NOOP ("Saw|Pulse"), two));
    v.push_back (dial (334, 223, 52, "Cutoff", TR_NOOP ("Cutoff")));
    v.push_back (dial (394, 223, 52, "Resonance", TR_NOOP ("Reso")));
    v.push_back (dial (455, 223, 52, "Key tracking", TR_NOOP ("Key")));
    v.push_back (dial (516, 223, 52, "Velocity tracking", TR_NOOP ("Vel")));
    v.push_back (radio (552, 190, 58, 78, "Filter mode", TR_NOOP ("Low|Band|High|Notch"), four));
    v.push_back (dial (655, 223, 52, "LFO speed", TR_NOOP ("Speed")));
    v.push_back (dial (715, 223, 52, "LFO attack", TR_NOOP ("Attack")));
    v.push_back (dial (776, 223, 52, "LFO release", TR_NOOP ("Release")));
    v.push_back (dial (837, 223, 52, "LFO amount", TR_NOOP ("Amount")));
    v.push_back (dial (62, 320, 52, "Sub level", TR_NOOP ("Level")));
    v.push_back (dial (125, 320, 52, "Sub phase", TR_NOOP ("Phase")));
    v.push_back (menu (164, 312, 66, 22, "Sub harmonic", nullptr, "1/4|1/3|1/2|2/3|1|3/2|2|3|4", nine));
    v.push_back (dial (265, 320, 52, "Sub detune", TR_NOOP ("Detune")));
    v.push_back (fader (313, 289, 22, 76, "EGF attack", "A"));
    v.push_back (fader (345, 289, 22, 76, "EGF decay", "D"));
    v.push_back (fader (378, 289, 22, 76, "EGF sustain", "S"));
    v.push_back (fader (411, 289, 22, 76, "EGF release", "R"));
    v.push_back (fader (454, 289, 22, 76, "EGF amount", TR_NOOP ("Amount")));
    v.push_back (radio (500, 284, 80, 84, "EGF destination", TR_NOOP ("Cutoff|Sync|Sub phase|Pitch"), four));
    v.push_back (radio (624, 290, 36, 60, "LFO retrig", TR_NOOP ("Free|Sync"), two));
    v.push_back (radio (662, 290, 34, 60, "LFO tempo sync", TR_NOOP ("Hz|Beat"), two));
    v.push_back (toggle (700, 276, 36, 14, "LFO invert", TR_NOOP ("Inv")));
    v.push_back (radio (702, 296, 66, 72, "LFO shape", TR_NOOP ("Tri|Saw|Square|S&H"), four));
    v.push_back (menu (780, 312, 90, 22, "LFO destination", nullptr, TR_NOOP ("Cutoff|Sync|Sub phase|Pitch|Noise|Volume|Pan"), seven));
    v.push_back (toggle (26, 430, 80, 14, "Chorus on", TR_NOOP ("Chorus")));
    v.push_back (dial (50, 483, 36, "Chorus depth", TR_NOOP ("Depth")));
    v.push_back (dial (92, 483, 36, "Chorus rate", TR_NOOP ("Rate")));
    v.push_back (dial (134, 483, 36, "Chorus mix", TR_NOOP ("Mix")));
    v.push_back (toggle (166, 430, 80, 14, "Phaser on", TR_NOOP ("Phaser")));
    v.push_back (dial (190, 483, 36, "Phaser center", TR_NOOP ("Center")));
    v.push_back (dial (232, 483, 36, "Phaser rate", TR_NOOP ("Rate")));
    v.push_back (dial (274, 483, 36, "Phaser depth", TR_NOOP ("Depth")));
    v.push_back (dial (316, 483, 36, "Phaser feedback", TR_NOOP ("Fdbk")));
    v.push_back (dial (358, 483, 36, "Phaser mix", TR_NOOP ("Mix")));
    v.push_back (toggle (388, 430, 80, 14, "Delay on", TR_NOOP ("Delay")));
    v.push_back (dial (414, 483, 36, "Delay left", TR_NOOP ("Left")));
    v.push_back (dial (457, 483, 36, "Delay right", TR_NOOP ("Right")));
    v.push_back (dial (499, 483, 36, "Delay feedback", TR_NOOP ("Fdbk")));
    v.push_back (dial (541, 483, 36, "Delay depth", TR_NOOP ("Mod")));
    v.push_back (dial (583, 483, 36, "Delay low cut", TR_NOOP ("LoCut")));
    v.push_back (dial (625, 483, 36, "Delay high cut", TR_NOOP ("HiCut")));
    v.push_back (dial (667, 483, 36, "Delay mix", TR_NOOP ("Mix")));
    v.push_back (toggle (698, 430, 80, 14, "Reverb on", TR_NOOP ("Reverb")));
    v.push_back (dial (723, 483, 36, "Reverb decay", TR_NOOP ("Decay")));
    v.push_back (dial (765, 483, 36, "Reverb high cut", TR_NOOP ("HiCut")));
    v.push_back (dial (807, 483, 36, "Reverb damp", TR_NOOP ("Damp")));
    v.push_back (dial (850, 483, 36, "Reverb mix", TR_NOOP ("Mix")));
    v.push_back (toggle (748, 542, 50, 14, "Arp on", TR_NOOP ("On"))); // (below the section title, round 168q2)
    v.push_back (radio (682, 536, 52, 74, "Arp mode", TR_NOOP ("Up|Down|Up/dn|Rnd"), four));
    v.push_back (radio (818, 536, 36, 74, "Arp range", "1|2|3|4", four));
    v.push_back (menu (746, 568, 58, 22, "Arp division", nullptr, "1/32|1/16|2/16|3/16|1/4|2/4|3/4", {}));
    v.push_back (menu (720, 14, 48, 20, "Polyphony", nullptr, "4|8|12|16|20|24", {}));
    v.push_back (menu (820, 14, 48, 20, "Bend range", nullptr, "---|1|2|3|4|5|6|7|8|9|10|12", {}));
    v.push_back (menu (34, 536, 60, 22, "Mod 1 source", nullptr, sources, {}));
    v.push_back (dial (118, 546, 28, "Mod 1 amount", nullptr, bipolar));
    v.push_back (menu (138, 536, 90, 22, "Mod 1 destination", nullptr, destinations, {}));
    v.push_back (menu (34, 566, 60, 22, "Mod 2 source", nullptr, sources, {}));
    v.push_back (dial (118, 576, 28, "Mod 2 amount", nullptr, bipolar));
    v.push_back (menu (138, 566, 90, 22, "Mod 2 destination", nullptr, destinations, {}));
    v.push_back (menu (246, 536, 60, 22, "Mod 3 source", nullptr, sources, {}));
    v.push_back (dial (329, 546, 28, "Mod 3 amount", nullptr, bipolar));
    v.push_back (menu (350, 536, 90, 22, "Mod 3 destination", nullptr, destinations, {}));
    v.push_back (menu (246, 566, 60, 22, "Mod 4 source", nullptr, sources, {}));
    v.push_back (dial (329, 576, 28, "Mod 4 amount", nullptr, bipolar));
    v.push_back (menu (350, 566, 90, 22, "Mod 4 destination", nullptr, destinations, {}));
    v.push_back (menu (457, 536, 60, 22, "Mod 5 source", nullptr, sources, {}));
    v.push_back (dial (540, 546, 28, "Mod 5 amount", nullptr, bipolar));
    v.push_back (menu (564, 536, 90, 22, "Mod 5 destination", nullptr, destinations, {}));
    v.push_back (menu (457, 566, 60, 22, "Mod 6 source", nullptr, sources, {}));
    v.push_back (dial (540, 576, 28, "Mod 6 amount", nullptr, bipolar));
    v.push_back (menu (564, 566, 90, 22, "Mod 6 destination", nullptr, destinations, {}));
    return l;
}

// --- Transistor Bass ------------------------------------------------------------------------------------------
// FL's panel (700 × 600): the 303 voice (tuning, waveform with the "303" pulse switch, cutoff, resonance, env mod,
// decay, accent, volume), the tweaks row and the distortion, delay and reverb, the program's 16-step sequence below.
// Values: parameters 0…41 of the current program's working patch, 0…1 (REA_rodada57 §2.2).
Layout transistorBassLayout()
{
    Layout l;
    l.fl = "Transistor Bass";
    l.width = 700;
    l.height = 600;
    auto& v = l.items;
    v.push_back (label (14, 6, 300, 34, "Transistor Bass", dim | alignLeft));
    v.push_back (display (360, 12, 326, 22, "tbprogram"));
    v.push_back (section (10, 62, 680, 98, TR_NOOP ("Voice")));
    v.push_back (section (10, 166, 392, 94, TR_NOOP ("Tweaks")));
    v.push_back (section (436, 166, 254, 94, nullptr));
    v.push_back (section (10, 264, 230, 148, nullptr));
    v.push_back (section (328, 264, 362, 148, nullptr));
    v.push_back (section (10, 416, 680, 178, TR_NOOP ("Sequence")));
    // FL's 16-step editor (round 168q2): the sequencer switch and the length on top, per step the note (its octave
    // below, FL's pitch editor), gate, slide and accent; CLEAR and the octave buttons at the right, the step numbers
    // (lit up to the length) under the steps
    v.push_back (toggle (20, 436, 96, 14, "Sequencer", TR_NOOP ("Sequencer")));
    v.push_back (label (500, 434, 80, 18, TR_NOOP ("Length"), dim | alignLeft));
    v.push_back (stepper (580, 433, 50, 20, "Sequence length", nullptr));
    static const char* const rows[] = { TR_NOOP ("Pitch"), TR_NOOP ("Octave"), TR_NOOP ("Gate"), TR_NOOP ("Slide"), TR_NOOP ("Accent") };
    static const float rowY[] = { 458, 479, 499, 519, 539 };
    for (int r = 0; r < 5; ++r)
        v.push_back (label (14, rowY[r], 42, 18, rows[r], dim | alignLeft));
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        static const char* const fields[] = { "note", "octave", "gate", "slide", "accent" };
        for (int i = 0; i < 16; ++i)
            for (const auto* f : fields)
                n.push_back ("Step " + std::to_string (i + 1) + " " + f);
        return n;
    }();
    for (int i = 0; i < 16; ++i)
    {
        const auto x = 58.0f + 36.0f * (float) i;
        const auto* step = &names[(std::size_t) (5 * i)];
        v.push_back (stepper (x, 458, 34, 19, step[0].c_str(), nullptr));
        v.push_back (stepper (x, 479, 34, 18, step[1].c_str(), nullptr));
        v.push_back (toggle (x, 499, 34, 19, step[2].c_str(), nullptr, boxed));
        v.push_back (toggle (x, 519, 34, 19, step[3].c_str(), nullptr, boxed));
        v.push_back (toggle (x, 539, 34, 19, step[4].c_str(), nullptr, boxed));
    }
    v.push_back (display (58, 562, 576, 26, "tbsteps"));
    v.push_back (display (640, 458, 44, 130, "tbseqtools"));
    v.push_back (dial (52, 108, 56, "Tuning", TR_NOOP ("Tune"), bipolar));
    v.push_back (dial (156, 108, 56, "Waveform", TR_NOOP ("Wave")));
    v.push_back (dial (228, 108, 56, "Cutoff", TR_NOOP ("Cutoff")));
    v.push_back (dial (300, 108, 56, "Resonance", TR_NOOP ("Reso")));
    v.push_back (dial (371, 108, 56, "Env mod", TR_NOOP ("Env mod")));
    v.push_back (dial (442, 108, 56, "Decay", TR_NOOP ("Decay")));
    v.push_back (dial (514, 108, 56, "Accent", TR_NOOP ("Accent")));
    v.push_back (dial (633, 109, 70, "Volume", TR_NOOP ("Volume")));
    v.push_back (dial (44, 212, 46, "High-pass", TR_NOOP ("HP")));
    v.push_back (dial (98, 212, 46, "Min decay", TR_NOOP ("Min dec")));
    v.push_back (dial (152, 212, 46, "Key follow", TR_NOOP ("Key")));
    v.push_back (dial (206, 212, 46, "LFO to PW", TR_NOOP ("LFO PW")));
    v.push_back (dial (260, 212, 46, "LFO rate", TR_NOOP ("LFO rate")));
    v.push_back (dial (314, 212, 46, "Pulse width", TR_NOOP ("PW")));
    v.push_back (dial (369, 212, 46, "Clicks", TR_NOOP ("Clicks")));
    v.push_back (toggle (445, 166, 96, 14, "Distortion on", TR_NOOP ("Distortion")));
    v.push_back (dial (479, 212, 46, "Distortion HP", TR_NOOP ("HP")));
    v.push_back (dial (534, 212, 46, "Drive", TR_NOOP ("Drive")));
    v.push_back (dial (589, 212, 46, "Distortion tone", TR_NOOP ("Tone")));
    v.push_back (dial (645, 212, 46, "Distortion volume", TR_NOOP ("Volume")));
    v.push_back (toggle (14, 265, 96, 14, "Delay on", TR_NOOP ("Delay")));
    v.push_back (dial (44, 311, 46, "Delay amount", TR_NOOP ("Amount")));
    v.push_back (dial (98, 311, 46, "Delay time", TR_NOOP ("Time")));
    v.push_back (dial (152, 311, 46, "Delay right tap", TR_NOOP ("R tap")));
    v.push_back (dial (44, 372, 46, "Delay feedback", TR_NOOP ("Fdbk")));
    v.push_back (dial (98, 372, 46, "Delay tone", TR_NOOP ("Tone")));
    v.push_back (dial (152, 372, 46, "Delay stereo", TR_NOOP ("Stereo")));
    v.push_back (toggle (206, 301, 34, 28, "Delay sync", TR_NOOP ("Sync"), boxed));
    v.push_back (toggle (332, 265, 96, 14, "Reverb on", TR_NOOP ("Reverb")));
    v.push_back (dial (369, 311, 46, "Reverb low cut", TR_NOOP ("LoCut")));
    v.push_back (dial (424, 311, 46, "Reverb high cut", TR_NOOP ("HiCut")));
    v.push_back (dial (479, 311, 46, "Reverb predelay", TR_NOOP ("Pre")));
    v.push_back (dial (534, 311, 46, "Reverb predelay feedback", TR_NOOP ("Pre fb")));
    v.push_back (dial (589, 311, 46, "Reverb size", TR_NOOP ("Size")));
    v.push_back (dial (645, 311, 46, "Reverb diffusion", TR_NOOP ("Diff")));
    v.push_back (dial (369, 372, 46, "Reverb decay", TR_NOOP ("Decay")));
    v.push_back (dial (424, 372, 46, "Reverb damping", TR_NOOP ("Damp")));
    v.push_back (dial (479, 372, 46, "Reverb width", TR_NOOP ("Width")));
    v.push_back (dial (534, 372, 46, "Reverb dry", TR_NOOP ("Dry")));
    v.push_back (dial (589, 372, 46, "Reverb early", TR_NOOP ("ER")));
    v.push_back (dial (645, 372, 46, "Reverb wet", TR_NOOP ("Wet")));
    v.push_back (toggle (88, 94, 34, 28, "303 pulse", "303", boxed));
    return l;
}

// --- BeepMap --------------------------------------------------------------------------------------------------
// FL's panel (380 × 170): Freq and Length wheels, the Scale combo, the four switches, the bitmap's Open / Copy /
// Paste row with Max height, the bitmap below. Values: the int32 settings (Frequency 24…9600 cents / Hz, Length
// 0…4096, Scale 0 Log / 1 Linear / 2 Harmonics, switches 0 / 1, Max height 3…999 rows).
Layout beepMapLayout()
{
    Layout l;
    l.fl = "BeepMap";
    l.width = 380;
    l.height = 176;
    auto& v = l.items;
    v.push_back (dial (27, 35, 40, "Frequency", TR_NOOP ("Freq")));
    v.push_back (dial (79, 35, 40, "Length", TR_NOOP ("Length")));
    v.push_back (menu (117, 18, 98, 32, "Scale", TR_NOOP ("Scale"), TR_NOOP ("Log|Linear|Harmonics")));
    v.push_back (toggle (228, 20, 68, 14, "Use blue", TR_NOOP ("Use blue")));
    v.push_back (toggle (301, 20, 72, 14, "Grainy", TR_NOOP ("Grainy")));
    v.push_back (toggle (228, 40, 68, 14, "Loop", TR_NOOP ("Loop")));
    v.push_back (toggle (301, 40, 72, 14, "Widen", TR_NOOP ("Widen")));
    // the bitmap's actions (open a .bmp / paste), its name and size: drawn and handled by the editor
    v.push_back (display (6, 82, 230, 34, "beepmapfile"));
    v.push_back (label (244, 88, 88, 22, "BEEP · MAP", dim));
    v.push_back (stepper (336, 80, 38, 36, "Max height", TR_NOOP ("Rows")));
    v.push_back (display (6, 124, 368, 46, "beepmapimage"));
    return l;
}

// --- Fruity Granulizer ----------------------------------------------------------------------------------------
// FL's panel (392 × 429): GRAINS and EFFECTS rows of four wheels, TRANSIENTS (hold, source) and TIME (start, Loop,
// Hold, Key to, Tempo locked), the sample's waveform at the bottom. Values: the plugin's whole-number knob values.
Layout granulizerLayout()
{
    Layout l;
    l.fl = "Fruity Granulizer";
    l.width = 392;
    l.height = 429;
    auto& v = l.items;
    v.push_back (section (6, 6, 380, 99, TR_NOOP ("Grains")));
    v.push_back (dial (47, 58, 46, "Grain attack", TR_NOOP ("Attack")));
    v.push_back (dial (111, 58, 46, "Grain hold", TR_NOOP ("Hold")));
    v.push_back (dial (177, 58, 46, "Grain spacing", TR_NOOP ("Gr. spacing")));
    v.push_back (dial (246, 58, 46, "Wave spacing", TR_NOOP ("Wave spacing"), bipolar));
    v.push_back (display (305, 27, 71, 66, "granulizergrains"));
    v.push_back (section (6, 112, 380, 99, TR_NOOP ("Effects")));
    v.push_back (dial (47, 164, 46, "Stereo separation", TR_NOOP ("Pan")));
    v.push_back (dial (111, 164, 46, "Effect depth", TR_NOOP ("Depth")));
    v.push_back (dial (177, 164, 46, "Effect speed", TR_NOOP ("Speed")));
    v.push_back (dial (246, 164, 46, "Randomness", TR_NOOP ("Rand")));
    v.push_back (section (6, 218, 184, 99, TR_NOOP ("Transients")));
    v.push_back (dial (47, 270, 46, "Transient hold", TR_NOOP ("Hold")));
    v.push_back (radio (100, 240, 84, 60, "Transients source", TR_NOOP ("Off|Markers|Auto slice")));
    v.push_back (section (196, 218, 190, 99, TR_NOOP ("Time")));
    v.push_back (dial (236, 270, 46, "Sample start", TR_NOOP ("Start")));
    v.push_back (toggle (270, 252, 40, 14, "Loop", TR_NOOP ("Loop")));
    v.push_back (toggle (316, 252, 62, 20, "Hold", TR_NOOP ("Hold"), boxed));
    v.push_back (toggle (270, 234, 112, 14, "Tempo locked", TR_NOOP ("Tempo locked")));
    v.push_back (menu (306, 278, 74, 34, "Key to", TR_NOOP ("Key to"), TR_NOOP ("Pitch|Percent|Step|Transient")));
    v.push_back (display (6, 324, 380, 99, "granulizerwave"));
    return l;
}

// --- Wasp / Wasp XT -------------------------------------------------------------------------------------------
// FL 2026 ships Wasp and Wasp XT as a 64-bit placeholder (its "Dummy x64" form, 356 × 201: the plugin's name and three
// lines saying it has no 64-bit version, the settings are kept, and the project opens in 32-bit FL). LYNX keeps the
// state the same way and says so.
Layout waspLayout()
{
    Layout l;
    l.fl = "Wasp";
    l.width = 356;
    l.height = 201;
    auto& v = l.items;
    v.push_back (label (5, 40, 346, 24, "Wasp", 0));
    v.push_back (label (5, 72, 346, 16, TR_NOOP ("This plugin has no 64-bit version: it makes no sound.")));
    v.push_back (label (5, 92, 346, 16, TR_NOOP ("Its settings are kept in the project."), dim));
    v.push_back (label (5, 112, 346, 16, TR_NOOP ("Open the project in 32-bit FL Studio to play it."), dim));
    return l;
}
} // namespace lynx::plugui
