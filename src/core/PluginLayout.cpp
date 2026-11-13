#include "core/PluginLayout.h"
#include "core/PluginLayoutSynths.h"

#include <list>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "core/EchoSuite.h"
#include "core/EngineLayouts.h"
#include "core/Effects.h"
#include "core/MaximusFx.h"
#include "core/MixSuite.h"
#include "core/MultibandDelayFx.h"
#include "core/StateParams.h"
#include "core/Toxic.h"

// (the i18n test scans the sources for TR_NOOP: captions below are translated by the editor)
#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::plugui
{
namespace
{
// --- builders (positions in FL's window pixels, body origin) -----------------------------------------

LayoutItem section (float x, float y, float w, float h, const char* title, std::uint32_t style = 0)
{
    LayoutItem i;
    i.type = Item::section;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.label = title;
    i.style = style;
    return i;
}

// a knob centred at (cx, cy), `d` wide, its caption centred at `captionY`
LayoutItem knob (float cx, float cy, float d, const char* param, const char* caption, float captionY, std::uint32_t style = 0, int steps = 0)
{
    LayoutItem i;
    i.type = Item::knob;
    i.x = cx - d * 0.5f;
    i.y = cy - d * 0.5f - ((style & valueAbove) != 0 ? 16.0f : 0.0f);
    i.w = d;
    i.h = (captionY + 6.0f) - i.y;
    if ((style & noCaption) != 0)
        i.h = d + ((style & valueAbove) != 0 ? 16.0f : 0.0f);
    i.param = param;
    i.label = caption;
    i.style = style;
    i.steps = steps;
    return i;
}

LayoutItem label (float x, float y, float w, float h, const char* text, std::uint32_t style = 0)
{
    LayoutItem i;
    i.type = Item::label;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.label = text;
    i.style = style;
    return i;
}

LayoutItem value (float x, float y, float w, float h, const char* param, std::uint32_t style = alignLeft)
{
    LayoutItem i;
    i.type = Item::value;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.style = style;
    return i;
}

LayoutItem slider (Item type, float x, float y, float w, float h, const char* param, const char* caption = nullptr, std::uint32_t style = 0)
{
    LayoutItem i;
    i.type = type;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.style = style;
    return i;
}

LayoutItem radio (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {})
{
    LayoutItem i;
    i.type = Item::radio;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.options = options;
    i.values = std::move (values);
    return i;
}

LayoutItem tabs (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {}, std::uint32_t style = 0)
{
    LayoutItem i;
    i.type = Item::tabs;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.options = options;
    i.values = std::move (values);
    i.style = style;
    return i;
}

LayoutItem pageTabs (float x, float y, float w, float h, const char* options)
{
    auto i = tabs (x, y, w, h, nullptr, options, {}, plugui::pageTabs);
    return i;
}

LayoutItem toggle (float x, float y, float w, float h, const char* param, const char* caption, std::uint32_t style = 0)
{
    LayoutItem i;
    i.style = style;
    i.type = Item::toggle;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    return i;
}

LayoutItem stepper (float x, float y, float w, float h, const char* param, const char* caption, int steps)
{
    LayoutItem i;
    i.type = Item::stepper;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.steps = steps;
    return i;
}

LayoutItem display (float x, float y, float w, float h, const char* id, const char* param = nullptr, const char* param2 = nullptr)
{
    LayoutItem i;
    i.type = Item::display;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.display = id;
    i.param = param;
    i.param2 = param2;
    return i;
}

LayoutItem onPage (LayoutItem i, int page)
{
    i.page = page;
    return i;
}

// shown only while `param` is within [lo, hi] (two conditions at most)
LayoutItem when (LayoutItem i, const char* param, float lo, float hi)
{
    if (i.whenParam == nullptr)
    {
        i.whenParam = param;
        i.whenMin = lo;
        i.whenMax = hi;
    }
    else if (i.when2Param == nullptr)
    {
        i.when2Param = param;
        i.when2Min = lo;
        i.when2Max = hi;
    }
    else
    {
        i.when3Param = param;
        i.when3Min = lo;
        i.when3Max = hi;
    }
    return i;
}

// --- FL's generic "Fruity" editor: cells of 160 × 45 in three columns, 50 px apart ---------------------
// (knob at the left, the caption on a plate, the value or a slider under it)
enum class Cell { value, slider, radio };

void fruityCell (std::vector<LayoutItem>& items, int column, int row, const char* param, const char* caption, Cell kind = Cell::value,
                 int steps = 0, const char* options = nullptr)
{
    const auto x = 6.0f + 165.0f * (float) column, y = 5.0f + 50.0f * (float) row;
    items.push_back (section (x, y, 160.0f, 45.0f, nullptr, boxed));

    if (kind == Cell::radio) // half cell: plate + Off / On
        return;

    items.push_back (knob (x + 25.0f, y + 22.5f, 38.0f, param, caption, 0.0f, noCaption, steps));
    items.push_back (label (x + 54.0f, y + 3.0f, 102.0f, 15.0f, caption, boxed));
    if (kind == Cell::slider)
        items.push_back (slider (Item::hslider, x + 54.0f, y + 25.0f, 102.0f, 16.0f, param, nullptr, noCaption));
    else
        items.push_back (value (x + 57.0f, y + 23.0f, 99.0f, 18.0f, param));
    (void) options;
}

// a half-width cell with a plate and an Off / On switch (Fruity Filter's X2 / CENTER)
void fruityHalfCell (std::vector<LayoutItem>& items, int column, int row, int half, const char* param, const char* caption)
{
    const auto x = 6.0f + 165.0f * (float) column + 82.5f * (float) half, y = 5.0f + 50.0f * (float) row;
    items.push_back (section (x, y, 77.5f, 45.0f, nullptr, boxed));
    items.push_back (label (x + 18.0f, y + 3.0f, 56.0f, 15.0f, caption, boxed));
    items.push_back (radio (x + 4.0f, y + 19.0f, 44.0f, 24.0f, param, TR_NOOP ("Off|On")));
}

Layout fruityGrid (const char* fl, int rows, int columns = 3)
{
    Layout l;
    l.fl = fl;
    l.width = 6 + 165 * columns;
    l.height = 50 * rows + 5;
    return l;
}

// --- the layouts ------------------------------------------------------------------------------------

Layout fruityLimiter()
{
    // REA/FL capture: display on top (with its view strip at the left), LOUDNESS / ENVELOPE / NOISE GATE headers,
    // the LIMIT | COMP page switch at the bottom of the middle panel
    Layout l;
    l.fl = "Fruity Limiter";
    l.width = 633;
    l.height = 306;
    l.pages = { TR_NOOP ("Limit"), TR_NOOP ("Comp") };
    auto& v = l.items;
    v.push_back (display (7, 2, 623, 158, "limiter"));
    v.push_back (section (2, 161, 629, 143, nullptr, boxed));
    v.push_back (section (32, 161, 255, 16, TR_NOOP ("Loudness")));
    v.push_back (section (288, 161, 176, 16, TR_NOOP ("Envelope")));
    v.push_back (section (465, 161, 166, 16, TR_NOOP ("Noise gate")));
    v.push_back (section (117, 180, 345, 120, nullptr, boxed | raised));
    v.push_back (knob (34, 205, 44, "Gain", TR_NOOP ("Gain"), 240));
    v.push_back (knob (88, 205, 44, "Saturation", TR_NOOP ("Sat"), 240));
    v.push_back (pageTabs (119, 277, 82, 20, TR_NOOP ("Limit|Comp")));
    v.push_back (knob (492, 205, 44, "Gate release", TR_NOOP ("Rel"), 240));
    v.push_back (knob (546, 205, 44, "Gate gain", TR_NOOP ("Gain"), 240));
    v.push_back (knob (600, 205, 44, "Gate threshold", TR_NOOP ("Thres"), 240));

    // LIMIT page
    v.push_back (onPage (knob (148, 205, 44, "Ceiling", TR_NOOP ("Ceil"), 240), 0));
    v.push_back (onPage (knob (316, 205, 44, "Attack", TR_NOOP ("Att"), 240), 0));
    v.push_back (onPage (knob (372, 205, 44, "Release", TR_NOOP ("Rel"), 240), 0));
    v.push_back (onPage (knob (432, 205, 44, "Sustain", TR_NOOP ("Sustain"), 240), 0));
    v.push_back (onPage (stepper (302, 259, 28, 30, "Attack curve", TR_NOOP ("Curve"), 7), 0));
    v.push_back (onPage (stepper (358, 259, 28, 30, "Release curve", TR_NOOP ("Curve"), 7), 0));

    l.menuSwitches = { "Exact ceiling" };

    // COMP page
    v.push_back (onPage (knob (148, 205, 44, "Comp. threshold", TR_NOOP ("Thres"), 240), 1));
    v.push_back (onPage (knob (202, 205, 44, "Knee", TR_NOOP ("Knee"), 240, bipolar), 1));
    v.push_back (onPage (knob (256, 205, 44, "Ratio", TR_NOOP ("Ratio"), 240, bipolar), 1));
    v.push_back (onPage (knob (316, 205, 44, "Comp. attack", TR_NOOP ("Att"), 240), 1));
    v.push_back (onPage (knob (372, 205, 44, "Comp. release", TR_NOOP ("Rel"), 240), 1));
    // FL captions the compressor's detector window SUSTAIN too (the limiter's is its peak window)
    v.push_back (onPage (knob (432, 205, 44, "RMS window", TR_NOOP ("Sustain"), 240), 1));
    v.push_back (onPage (stepper (302, 259, 28, 30, "Comp. curve", TR_NOOP ("Curve"), 7), 1));
    return l;
}

Layout fruityCompressor()
{
    auto l = fruityGrid ("Fruity Compressor", 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Threshold", TR_NOOP ("Threshold"));
    fruityCell (v, 1, 0, "Ratio", TR_NOOP ("Ratio"));
    fruityCell (v, 2, 0, "Gain", TR_NOOP ("Gain"));
    fruityCell (v, 0, 1, "Attack", TR_NOOP ("Attack"));
    fruityCell (v, 1, 1, "Release", TR_NOOP ("Release"));
    fruityCell (v, 2, 1, "Type", TR_NOOP ("Type"), Cell::value, 7);
    l.menuSwitches = { "Look-ahead" };
    return l;
}

Layout fruityFilter()
{
    auto l = fruityGrid ("Fruity Filter", 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Cutoff", TR_NOOP ("Cutoff freq"), Cell::slider);
    fruityCell (v, 1, 0, "Resonance", TR_NOOP ("Resonance"), Cell::slider);
    fruityCell (v, 2, 0, "Low pass", TR_NOOP ("Low pass"), Cell::slider);
    fruityCell (v, 0, 1, "Band pass", TR_NOOP ("Band pass"), Cell::slider);
    fruityCell (v, 1, 1, "High pass", TR_NOOP ("High pass"), Cell::slider);
    fruityHalfCell (v, 2, 1, 0, "x2", TR_NOOP ("X 2"));
    fruityHalfCell (v, 2, 1, 1, "Center", TR_NOOP ("Center"));
    return l;
}

Layout fruityChorus()
{
    auto l = fruityGrid ("Fruity Chorus", 4);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Delay", TR_NOOP ("Delay"));
    fruityCell (v, 1, 0, "Depth", TR_NOOP ("Depth"));
    fruityCell (v, 2, 0, "Stereo", TR_NOOP ("Stereo"));
    fruityCell (v, 0, 1, "LFO 1 freq", TR_NOOP ("LFO 1 freq"));
    fruityCell (v, 1, 1, "LFO 2 freq", TR_NOOP ("LFO 2 freq"));
    fruityCell (v, 2, 1, "LFO 3 freq", TR_NOOP ("LFO 3 freq"));
    fruityCell (v, 0, 2, "LFO 1 wave", TR_NOOP ("LFO 1 wave"));
    fruityCell (v, 1, 2, "LFO 2 wave", TR_NOOP ("LFO 2 wave"));
    fruityCell (v, 2, 2, "LFO 3 wave", TR_NOOP ("LFO 3 wave"));
    fruityCell (v, 0, 3, "Cross type", TR_NOOP ("Cross type"));
    fruityCell (v, 1, 3, "Cross cutoff", TR_NOOP ("Cross cutoff"));
    fruityCell (v, 2, 3, "Wet only", TR_NOOP ("Wet only"));
    return l;
}

Layout fruityFlanger()
{
    auto l = fruityGrid ("Fruity Flanger", 4);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Delay", TR_NOOP ("Delay"));
    fruityCell (v, 1, 0, "Depth", TR_NOOP ("Depth"));
    fruityCell (v, 2, 0, "Rate", TR_NOOP ("Rate"));
    fruityCell (v, 0, 1, "Phase", TR_NOOP ("Phase"));
    fruityCell (v, 1, 1, "Damp", TR_NOOP ("Damp"));
    fruityCell (v, 2, 1, "Shape", TR_NOOP ("Shape"));
    fruityCell (v, 0, 2, "Feed", TR_NOOP ("Feed"));
    fruityCell (v, 1, 2, "Invert feed", TR_NOOP ("Inv feedback"), Cell::value, 1);
    fruityCell (v, 2, 2, "Invert wet", TR_NOOP ("Inv wet"), Cell::value, 1);
    fruityCell (v, 0, 3, "Dry", TR_NOOP ("Dry"));
    fruityCell (v, 1, 3, "Wet", TR_NOOP ("Wet"));
    fruityCell (v, 2, 3, "Cross", TR_NOOP ("Cross"));
    return l;
}

Layout fruityPhaser()
{
    // FL's generic panel, three rows of three (round 105: FL 2026 capture of the Default preset)
    auto l = fruityGrid ("Fruity Phaser", 3);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Sweep freq.", TR_NOOP ("Sweep freq."));
    fruityCell (v, 1, 0, "Min depth", TR_NOOP ("Min depth"));
    fruityCell (v, 2, 0, "Max depth", TR_NOOP ("Max depth"));
    fruityCell (v, 0, 1, "Freq. range", TR_NOOP ("Freq. range"), Cell::value, 1);
    fruityCell (v, 1, 1, "Stereo", TR_NOOP ("Stereo"));
    fruityCell (v, 2, 1, "Stages", TR_NOOP ("Nr. stages"), Cell::value, 22);
    fruityCell (v, 0, 2, "Feedback", TR_NOOP ("Feedback"));
    fruityCell (v, 1, 2, "Dry-wet", TR_NOOP ("Dry-wet"));
    fruityCell (v, 2, 2, "Out gain", TR_NOOP ("Out gain"));
    return l;
}

Layout fruityFastDist()
{
    // four knobs and the A/B switch on the left panel, the transfer curve on the right
    Layout l;
    l.fl = "Fruity Fast Dist";
    l.width = 386;
    l.height = 122;
    auto& v = l.items;
    v.push_back (section (0, 0, 262, 96, nullptr, boxed));
    v.push_back (knob (34, 42, 40, "Pre amp", TR_NOOP ("Pre"), 75));
    v.push_back (knob (88, 42, 40, "Threshold", TR_NOOP ("Thresh"), 75));
    v.push_back (radio (122, 30, 30, 26, "Type", "A|B"));
    v.push_back (knob (179, 42, 40, "Mix", TR_NOOP ("Mix"), 75));
    v.push_back (knob (233, 42, 40, "Post gain", TR_NOOP ("Post"), 75));
    v.push_back (label (0, 99, 258, 20, TR_NOOP ("Fast dist"), dim));
    v.push_back (display (264, 0, 122, 122, "clip", "Threshold"));
    return l;
}

Layout stereoEnhancer()
{
    // STEREO SEP | phase offset (pre/post, knob, invert) | pan and volume
    Layout l;
    l.fl = "Fruity Stereo Enhancer";
    l.width = 405;
    l.height = 116;
    auto& v = l.items;
    v.push_back (section (0, 0, 82, 116, nullptr, boxed));
    v.push_back (label (2, 14, 78, 14, TR_NOOP ("Stereo sep")));
    v.push_back (knob (44, 62, 48, "Separation", nullptr, 0, noCaption | bipolar));
    v.push_back (label (14, 92, 54, 14, "+          -", dim));
    v.push_back (section (83, 0, 190, 116, nullptr, boxed));
    v.push_back (radio (93, 20, 50, 32, "Effects post", TR_NOOP ("Pre|Post")));
    v.push_back (knob (178, 57, 48, "Phase offset", nullptr, 0, noCaption | bipolar));
    v.push_back (label (152, 86, 52, 14, "L         R", dim));
    v.push_back (radio (223, 20, 50, 48, "Invert", TR_NOOP ("None|Left|Right")));
    v.push_back (label (88, 96, 100, 18, TR_NOOP ("Phase offset"), dim | alignLeft));
    v.push_back (label (222, 96, 50, 18, TR_NOOP ("Invert"), dim | alignLeft));
    v.push_back (section (274, 0, 131, 116, nullptr, boxed));
    v.push_back (label (276, 4, 127, 16, TR_NOOP ("Stereo enhancer"), dim));
    v.push_back (knob (312, 77, 46, "Pan", TR_NOOP ("Pan"), 0, bipolar | noCaption));
    v.push_back (label (290, 38, 44, 14, TR_NOOP ("Pan")));
    v.push_back (knob (374, 77, 46, "Volume", nullptr, 0, noCaption));
    v.push_back (label (352, 38, 44, 14, TR_NOOP ("Vol")));
    return l;
}

Layout fruityDelay3()
{
    Layout l;
    l.fl = "Fruity Delay 3";
    l.width = 752;
    l.height = 220;
    auto& v = l.items;
    v.push_back (section (2, 2, 58, 216, TR_NOOP ("Input")));
    v.push_back (slider (Item::vslider, 17, 26, 30, 160, "Send", TR_NOOP ("Wet")));

    v.push_back (section (62, 2, 193, 110, TR_NOOP ("Delay time")));
    v.push_back (knob (95, 58, 56, "Time", TR_NOOP ("Time"), 104, big));
    v.push_back (toggle (142, 37, 80, 14, "Tempo sync", TR_NOOP ("Tempo sync")));
    v.push_back (toggle (142, 55, 80, 14, "Keep pitch", TR_NOOP ("Keep pitch")));
    v.push_back (slider (Item::hslider, 145, 80, 60, 30, "Time smoothing", TR_NOOP ("Smoothing")));
    v.push_back (knob (233, 82, 34, "Time R / pan", TR_NOOP ("Offset"), 104, bipolar));

    v.push_back (section (257, 2, 121, 110, TR_NOOP ("Delay model")));
    v.push_back (radio (268, 36, 62, 52, "Delay type", TR_NOOP ("Mono|Stereo|Ping pong|Off"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (knob (356, 80, 34, "Stereo spread", TR_NOOP ("Stereo"), 104));

    v.push_back (section (380, 2, 305, 110, TR_NOOP ("Feedback")));
    v.push_back (knob (418, 70, 56, "Feedback", TR_NOOP ("Level"), 104, big));
    v.push_back (knob (469, 80, 34, "Cutoff", TR_NOOP ("Cutoff"), 104));
    v.push_back (knob (516, 80, 34, "Resonance", TR_NOOP ("Res"), 104));
    v.push_back (radio (557, 36, 40, 52, "Filter mode", TR_NOOP ("LP|HP|BP|Off"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    v.push_back (knob (620, 80, 34, "Rate reduce", TR_NOOP ("Smp rate"), 104));
    v.push_back (knob (666, 80, 34, "Bit reduce", TR_NOOP ("Bits"), 104));

    v.push_back (section (688, 2, 62, 216, TR_NOOP ("Output")));
    v.push_back (knob (719, 48, 34, "Wet", TR_NOOP ("Wet"), 80));
    v.push_back (knob (719, 110, 34, "Colour", TR_NOOP ("Tone"), 142, bipolar));
    v.push_back (knob (719, 172, 34, "Dry", TR_NOOP ("Dry"), 204));

    v.push_back (section (62, 114, 193, 104, TR_NOOP ("Modulation")));
    v.push_back (knob (94, 172, 44, "Mod rate", TR_NOOP ("Rate"), 206, big));
    v.push_back (knob (180, 172, 34, "Mod amount", TR_NOOP ("Time"), 206));
    v.push_back (knob (231, 172, 34, "Cutoff mod", TR_NOOP ("Cutoff"), 206));

    v.push_back (section (257, 114, 121, 104, TR_NOOP ("Diffusion")));
    v.push_back (knob (292, 172, 34, "Diffusion", TR_NOOP ("Level"), 206));
    v.push_back (knob (343, 172, 34, "Diffusion spread", TR_NOOP ("Spread"), 206));

    v.push_back (section (380, 114, 305, 104, TR_NOOP ("Feedback distortion")));
    v.push_back (radio (392, 160, 44, 28, "Saturation type", TR_NOOP ("Limit|Sat")));
    v.push_back (knob (476, 172, 34, "Distortion", TR_NOOP ("Level"), 206));
    v.push_back (knob (524, 172, 34, "Hardness", TR_NOOP ("Knee"), 206));
    v.push_back (knob (572, 172, 34, "Symmetry", TR_NOOP ("Symmetry"), 206, bipolar));
    v.push_back (display (614, 140, 68, 72, "shaper", "Distortion"));
    return l;
}

Layout fruityMultiband()
{
    // input meter strip, IIR/FIR, limiter, output gain | band graph with the four crossover knobs | LOW, MID and
    // HIGH bands (A/M/B mode, threshold, ratio, knee, attack, release, gain), each followed by its meters
    Layout l;
    l.fl = "Fruity Multiband Compressor";
    l.width = 805;
    l.height = 160;
    auto& v = l.items;
    v.push_back (display (8, 7, 14, 128, "meter"));
    v.push_back (radio (34, 7, 40, 26, "Linear phase", "IIR|FIR"));
    v.push_back (label (26, 44, 48, 13, TR_NOOP ("Limiter")));
    v.push_back (toggle (40, 58, 20, 14, "Limiter", nullptr));
    v.push_back (knob (50, 118, 34, "Output gain", TR_NOOP ("Gain"), 144, bipolar));
    v.push_back (section (79, 7, 200, 142, nullptr, boxed));
    v.push_back (display (83, 11, 192, 78, "multiband"));
    v.push_back (label (86, 90, 186, 12, TR_NOOP ("Crossovers"), dim));
    v.push_back (knob (103, 121, 34, "Low crossover", nullptr, 0, noCaption));
    v.push_back (knob (153, 121, 34, "Mid lower crossover", nullptr, 0, noCaption));
    v.push_back (knob (203, 121, 34, "Mid upper crossover", nullptr, 0, noCaption));
    v.push_back (knob (254, 121, 34, "High crossover", nullptr, 0, noCaption));

    constexpr int bandColours[] = { 0, 2, 5 }; // low / mid / high: the editor's red, yellow and blue band colours
    v[8].colour = bandColours[0];  // crossover knobs: the band each one bounds from below
    v[9].colour = bandColours[1];
    v[10].colour = bandColours[1];
    v[11].colour = bandColours[2];
    struct Band { const char* title; const char *mode, *threshold, *ratio, *knee, *attack, *release, *gain; };
    const Band bands[] = { { TR_NOOP ("Low band"), "Low mode", "Low threshold", "Low ratio", "Low knee", "Low attack", "Low release", "Low gain" },
                           { TR_NOOP ("Mid band"), "Mid mode", "Mid threshold", "Mid ratio", "Mid knee", "Mid attack", "Mid release", "Mid gain" },
                           { TR_NOOP ("High band"), "High mode", "High threshold", "High ratio", "High knee", "High attack", "High release", "High gain" } };
    for (int b = 0; b < 3; ++b)
    {
        const auto x = 287.0f + 172.0f * (float) b;
        const auto& band = bands[b];
        v.push_back (section (x, 7, 130, 16, nullptr, boxed));
        v.push_back (tabs (x + 2, 8, 48, 14, band.mode, "A|M|B", { 0.0f, 0.5f, 1.0f }));
        v.push_back (label (x + 52, 8, 76, 14, band.title));
        const auto first = v.size();
        v.push_back (knob (x + 19, 58, 32, band.threshold, TR_NOOP ("Thres"), 84));
        v.push_back (knob (x + 65, 58, 32, band.ratio, TR_NOOP ("Ratio"), 84));
        v.push_back (knob (x + 111, 58, 32, band.knee, TR_NOOP ("Knee"), 84));
        v.push_back (knob (x + 19, 120, 32, band.attack, TR_NOOP ("Att"), 144));
        v.push_back (knob (x + 65, 120, 32, band.release, TR_NOOP ("Rel"), 144));
        v.push_back (knob (x + 111, 120, 32, band.gain, TR_NOOP ("Gain"), 144, bipolar));
        for (auto k = first; k < v.size(); ++k)
            v[k].colour = bandColours[b];
        v.push_back (display (x + 140, 7, 24, 128, "meter"));
    }
    return l;
}

Layout reeverb2()
{
    Layout l;
    l.fl = "Fruity Reeverb 2";
    l.width = 538;
    l.height = 166;
    auto& v = l.items;
    v.push_back (display (2, 2, 160, 160, "room", "Room size"));
    v.push_back (section (164, 2, 58, 160, nullptr, boxed));
    v.push_back (radio (176, 20, 44, 28, "Mid/side input", TR_NOOP ("Mid|Side"), { 0.0f, 1.0f }));
    v.push_back (knob (193, 75, 30, "High cut", TR_NOOP ("H.cut"), 100));
    v.push_back (knob (193, 126, 30, "Low cut", TR_NOOP ("L.cut"), 151));
    v.push_back (section (224, 2, 102, 160, nullptr, boxed));
    v.push_back (knob (251, 23, 30, "Predelay", TR_NOOP ("Delay"), 48));
    v.push_back (toggle (290, 26, 20, 14, "Tempo predelay", nullptr));
    v.push_back (label (281, 42, 36, 12, TR_NOOP ("Tempo")));
    v.push_back (knob (251, 74, 30, "Room size", TR_NOOP ("Size"), 100));
    v.push_back (knob (297, 74, 30, "Mod depth", TR_NOOP ("Mod"), 100));
    v.push_back (knob (251, 125, 30, "Diffusion", TR_NOOP ("Diff"), 151));
    v.push_back (knob (297, 125, 30, "Mod speed", TR_NOOP ("Speed"), 151));
    v.push_back (section (328, 2, 100, 160, nullptr, boxed));
    v.push_back (knob (355, 23, 30, "Bass mult.", TR_NOOP ("Bass"), 48));
    v.push_back (knob (398, 23, 30, "Decay", TR_NOOP ("Dec"), 48));
    v.push_back (knob (355, 74, 30, "Crossover", TR_NOOP ("Cross"), 100));
    v.push_back (knob (398, 74, 30, "High damping", TR_NOOP ("Damp"), 100));
    v.push_back (label (334, 118, 90, 36, "Reeverb 2", dim));
    v.push_back (section (430, 2, 106, 160, nullptr, boxed));
    v.push_back (slider (Item::vslider, 440, 6, 20, 98, "Dry", TR_NOOP ("Dry")));
    v.push_back (slider (Item::vslider, 471, 6, 20, 98, "Early ref.", TR_NOOP ("ER")));
    v.push_back (slider (Item::vslider, 503, 6, 20, 98, "Wet", TR_NOOP ("Wet")));
    v.push_back (knob (481, 124, 30, "Stereo sep.", TR_NOOP ("Sep"), 151, bipolar));
    return l;
}

LayoutItem menuOf (float x, float y, float w, float h, const char* param, const char* caption, const char* options); // (below)

Layout luxeVerb()
{
    // INPUT | REVERB | PITCH | OUTPUT on top, ENVELOPE and the analyser below; every knob has its value on a plate above
    Layout l;
    l.fl = "LuxeVerb";
    l.width = 947;
    l.height = 456;
    auto& v = l.items;
    const auto tile = [&v] (float cx, float cy, const char* param, const char* caption, float captionY, std::uint32_t style = 0) {
        v.push_back (knob (cx, cy, 34, param, caption, captionY, style | valueAbove));
    };
    const auto fader = [&v] (float cx, const char* param, const char* caption) {
        v.push_back (value (cx - 19, 46, 38, 13, param, 0));
        v.push_back (slider (Item::vslider, cx - 12, 62, 24, 145, param, caption));
    };

    v.push_back (section (8, 14, 116, 200, TR_NOOP ("Input")));
    fader (37, "Input wet gain", TR_NOOP ("Wet gain"));
    tile (93, 82, "Input high cut", TR_NOOP ("High cut"), 108);
    tile (93, 174, "Input low cut", TR_NOOP ("Low cut"), 200);

    v.push_back (section (130, 14, 326, 200, TR_NOOP ("Reverb")));
    v.push_back (toggle (430, 16, 22, 12, "Reverb", nullptr));
    v.push_back (value (140, 46, 70, 13, "Decay", 0));
    v.push_back (knob (174, 125, 62, "Decay", TR_NOOP ("Decay"), 200, big));
    tile (244, 82, "Brightness", TR_NOOP ("Brightness"), 108);
    tile (303, 82, "Size", TR_NOOP ("Size"), 108);
    tile (363, 82, "Diffusion", TR_NOOP ("Diffusion"), 108);
    tile (422, 82, "Character", TR_NOOP ("Character"), 108);
    v.push_back (when (knob (244, 174, 34, "Pre-delay", TR_NOOP ("P. delay"), 200, valueAbove), "Pre-delay sync", 0.0f, 0.5f));
    v.push_back (when (knob (244, 174, 34, "Pre-delay (sync)", TR_NOOP ("P. delay"), 200, valueAbove), "Pre-delay sync", 0.5f, 1.0f));
    v.push_back (toggle (262, 194, 11, 11, "Pre-delay sync", "T", boxed));
    tile (303, 174, "Mod amount", TR_NOOP ("Mod amp"), 200);
    tile (363, 174, "Mod frequency", TR_NOOP ("Mod freq"), 200);
    v.push_back (radio (397, 130, 56, 54, "Freeze mode", TR_NOOP ("Normal|Freeze|Sustain")));
    v.push_back (toggle (407, 194, 44, 12, "High quality", TR_NOOP ("HQ")));

    v.push_back (section (462, 14, 238, 200, TR_NOOP ("Pitch")));
    v.push_back (tabs (530, 16, 120, 14, "Pitch type", TR_NOOP ("Feedback|Feedforward")));
    v.push_back (toggle (672, 16, 22, 12, "Feedback", nullptr));
    // feed-forward shifters
    {
        const auto ff = [] (LayoutItem i) { return when (std::move (i), "Pitch type", 0.5f, 1.0f); };
        v.push_back (ff (value (476, 46, 38, 13, "Feed-forward unpitched", 0)));
        v.push_back (ff (slider (Item::vslider, 483, 62, 24, 145, "Feed-forward unpitched", TR_NOOP ("Direct amp"))));
        v.push_back (ff (knob (552, 82, 34, "Feed-forward pitch 1", TR_NOOP ("Pitch 1"), 108, valueAbove | bipolar)));
        v.push_back (ff (knob (607, 82, 34, "Feed-forward amount 1", TR_NOOP ("Amp 1"), 108, valueAbove)));
        v.push_back (ff (when (knob (664, 82, 34, "Feed-forward delay 1", TR_NOOP ("Delay 1"), 108, valueAbove), "Feed-forward delay 1 sync", 0.0f, 0.5f)));
        v.push_back (ff (knob (552, 174, 34, "Feed-forward pitch 2", TR_NOOP ("Pitch 2"), 200, valueAbove | bipolar)));
        v.push_back (ff (knob (607, 174, 34, "Feed-forward amount 2", TR_NOOP ("Amp 2"), 200, valueAbove)));
        v.push_back (ff (when (knob (664, 174, 34, "Feed-forward delay 2", TR_NOOP ("Delay 2"), 200, valueAbove), "Feed-forward delay 2 sync", 0.0f, 0.5f)));
        // (the synced delays replace the free ones while T is on)
        v.push_back (ff (when (knob (664, 82, 34, "Feed-forward delay 1 (sync)", TR_NOOP ("Delay 1"), 108, valueAbove), "Feed-forward delay 1 sync", 0.5f, 1.0f)));
        v.push_back (ff (when (knob (664, 174, 34, "Feed-forward delay 2 (sync)", TR_NOOP ("Delay 2"), 200, valueAbove), "Feed-forward delay 2 sync", 0.5f, 1.0f)));
        v.push_back (ff (toggle (683, 102, 11, 11, "Feed-forward delay 1 sync", "T", boxed)));
        v.push_back (ff (toggle (683, 194, 11, 11, "Feed-forward delay 2 sync", "T", boxed)));
    }
    // feedback shifter: cuts, pitch shift and gain faders, delay, reverb mix
    {
        const auto fb = [] (LayoutItem i) { return when (std::move (i), "Pitch type", 0.0f, 0.5f); };
        v.push_back (fb (knob (496, 82, 34, "Feedback high cut", TR_NOOP ("High cut"), 108, valueAbove)));
        v.push_back (fb (knob (496, 174, 34, "Feedback low cut", TR_NOOP ("Low cut"), 200, valueAbove)));
        v.push_back (fb (value (534, 46, 38, 13, "Feedback pitch", 0)));
        v.push_back (fb (slider (Item::vslider, 541, 62, 24, 145, "Feedback pitch", TR_NOOP ("Pitch shift"), bipolar)));
        v.push_back (fb (value (589, 46, 38, 13, "Feedback gain", 0)));
        v.push_back (fb (slider (Item::vslider, 596, 62, 24, 145, "Feedback gain", TR_NOOP ("Gain"))));
        v.push_back (fb (when (knob (665, 82, 34, "Feedback delay", TR_NOOP ("Delay"), 108, valueAbove), "Feedback delay sync", 0.0f, 0.5f)));
        v.push_back (fb (when (knob (665, 82, 34, "Feedback delay (sync)", TR_NOOP ("Delay"), 108, valueAbove), "Feedback delay sync", 0.5f, 1.0f)));
        v.push_back (fb (toggle (683, 102, 11, 11, "Feedback delay sync", "T", boxed)));
        v.push_back (fb (knob (665, 174, 34, "Feedback reverb mix", TR_NOOP ("Reverb mix"), 200, valueAbove)));
    }

    v.push_back (section (706, 14, 234, 200, TR_NOOP ("Output")));
    fader (736, "Dry level", TR_NOOP ("Dry"));
    fader (791, "Wet level", TR_NOOP ("Wet"));
    v.push_back (toggle (802, 194, 11, 11, "Limiter", "L", boxed));
    tile (847, 82, "Peak frequency", TR_NOOP ("Peak freq"), 108);
    tile (907, 82, "Peak gain", TR_NOOP ("Peak gain"), 108, bipolar);
    tile (847, 174, "Peak Q", TR_NOOP ("Peak Q"), 200);
    tile (907, 174, "Stereo width", TR_NOOP ("Width"), 200);

    v.push_back (section (8, 236, 932, 216, TR_NOOP ("Envelope")));
    v.push_back (radio (16, 278, 48, 54, "Envelope mode", TR_NOOP ("Off|Wet|Decay")));
    tile (93, 309, "Envelope threshold", TR_NOOP ("Threshold"), 335);
    tile (150, 309, "Envelope scale", TR_NOOP ("Scale"), 335, bipolar);
    tile (207, 309, "Envelope low cut", TR_NOOP ("Low cut"), 335);
    tile (264, 309, "Envelope offset", TR_NOOP ("Offset"), 335, bipolar);
    v.push_back (stepper (20, 380, 32, 34, "Envelope sidechain", TR_NOOP ("Sidechain"), 0));
    tile (93, 399, "Envelope attack", TR_NOOP ("Attack"), 425);
    tile (150, 399, "Envelope decay", TR_NOOP ("Decay"), 425);
    tile (207, 399, "Envelope smoothing", TR_NOOP ("Smooth"), 425);
    v.push_back (display (298, 262, 636, 184, "envelope", "Decay"));

    // round tp172b: the Space engine (core/SpaceVerb.h, the Pro-R role) on a second page; the page tabs also switch
    // the engine, so the page shown is the engine heard
    for (auto& i : v)
        i.page = 0;
    l.pages = { "LuxeVerb", TR_NOOP ("Space") };
    auto engineTabs = pageTabs (380, 0, 188, 13, TR_NOOP ("LuxeVerb|Space"));
    engineTabs.param = "Space engine";
    v.push_back (engineTabs);
    const auto sp = [&v] (LayoutItem i) { v.push_back (onPage (std::move (i), 1)); };
    // round tp172b4: the Pro-R role's arrangement — one strip of knobs across the top (pre-delay, character,
    // thickness, distance, the big SPACE with its decay rate beside it, brightness, width, mix; the style above
    // character and thickness), the decay-rate EQ curve filling the rest with its four knobs in its lower corners.
    // Values under the captions (and the decay time inside SPACE)
    const auto dial = [&] (float cx, float cy, float d, const char* param, const char* caption, std::uint32_t style = 0, float w = 76.0f) {
        auto i = knob (cx, cy, d, param, caption, cy + d * 0.5f + 20.0f, style | hoverValue);
        i.w = std::max (d, std::min (w, i.h)); // room for the caption and the value (the circle keeps its size: the height
                                                // bounds it; never wider than tall, so no caption hides under a knob)
        i.x = cx - i.w * 0.5f;
        sp (std::move (i));
    };
    sp (menuOf (118, 22, 104, 18, "Space style", nullptr, TR_NOOP ("Modern|Vintage|Plate"))); // the reference's mode drop-down
    dial (62, 84, 40, "Space pre-delay", TR_NOOP ("P. delay"));
    dial (144, 84, 40, "Space character", TR_NOOP ("Character"));
    dial (226, 84, 40, "Thickness", TR_NOOP ("Thickness"), bipolar);
    dial (322, 80, 58, "Distance", TR_NOOP ("Distance"));
    sp (knob (470, 92, 116, "Space", TR_NOOP ("Space"), 170, big));
    v.push_back (onPage (value (420, 80, 100, 24, "Space", big), 1));
    dial (566, 46, 32, "Decay rate", TR_NOOP ("Decay rate"), 0, 64.0f);
    dial (636, 80, 58, "Space brightness", TR_NOOP ("Brightness"), bipolar);
    dial (742, 84, 44, "Space width", TR_NOOP ("Width"));
    dial (866, 84, 44, "Space mix", TR_NOOP ("Mix"));
    sp (display (8, 186, 932, 266, "decayeq", "Decay rate"));
    dial (64, 384, 32, "Low decay", TR_NOOP ("Low decay"), bipolar);
    dial (140, 384, 32, "Low decay freq.", TR_NOOP ("Low freq"));
    dial (808, 384, 32, "High decay", TR_NOOP ("High decay"), bipolar);
    dial (884, 384, 32, "High decay freq.", TR_NOOP ("High freq"));
    return l;
}

Layout parametricEq2()
{
    // the response curve (octave and band-name rows above, frequencies below, dB scale at its right), the output
    // fader, then one column per band: type button, gain fader, FREQ and BW knobs
    Layout l;
    l.fl = "Fruity Parametric EQ 2";
    l.width = 707;
    l.height = 356;
    auto& v = l.items;
    v.push_back (display (8, 4, 524, 292, "eq"));
    v.push_back (slider (Item::vslider, 532, 22, 16, 270, "Output", nullptr, noCaption | bipolar));
    v.push_back (label (510, 310, 38, 12, TR_NOOP ("Freq"), dim));
    v.push_back (label (510, 334, 38, 12, TR_NOOP ("BW"), dim));
    static const char* const names[peq::bands][5] = {
        { "B1 type", "B1 freq.", "B1 gain", "B1 Q", "B1 slope" }, { "B2 type", "B2 freq.", "B2 gain", "B2 Q", "B2 slope" },
        { "B3 type", "B3 freq.", "B3 gain", "B3 Q", "B3 slope" }, { "B4 type", "B4 freq.", "B4 gain", "B4 Q", "B4 slope" },
        { "B5 type", "B5 freq.", "B5 gain", "B5 Q", "B5 slope" }, { "B6 type", "B6 freq.", "B6 gain", "B6 Q", "B6 slope" },
        { "B7 type", "B7 freq.", "B7 gain", "B7 Q", "B7 slope" } };
    std::vector<float> typeValues;
    for (int t = 0; t < peq::bandTypes; ++t)
        typeValues.push_back (peq::typeValue ((peq::BandType) t));
    for (int b = 0; b < peq::bands; ++b)
    {
        const auto x = 552.0f + 21.0f * (float) b;
        LayoutItem type;
        type.type = Item::menu;
        type.x = x, type.y = 4, type.w = 16, type.h = 24;
        type.param = names[b][0];
        type.options = TR_NOOP ("Off|Peak|Low shelf|High shelf|Low cut|High cut|Band-pass|Notch");
        type.values = typeValues;
        type.colour = b;
        v.push_back (type);
        auto gain = slider (Item::vslider, x, 30, 16, 262, names[b][2], nullptr, noCaption | bipolar);
        gain.colour = b;
        v.push_back (gain);
        auto freq = knob (x + 8, 316, 19, names[b][1], nullptr, 0, noCaption);
        freq.colour = b;
        v.push_back (freq);
        auto width = knob (x + 8, 340, 19, names[b][3], nullptr, 0, noCaption);
        width.colour = b;
        v.push_back (width);
    }
    v.push_back (display (8, 300, 492, 52, "eqinfo"));
    for (int b = 0; b < peq::bands; ++b)
        l.editedElsewhere.push_back (names[b][4]); // the band menu (right-click a handle or the type button)
    return l;
}

Layout grossBeat()
{
    // left: the time panel (mix knob), HOLD, the time slots, the volume slots, ATT / REL / TNS, the volume panel; right:
    // the envelope of the selected slots and the slot's settings under it
    Layout l;
    l.fl = "Gross Beat";
    l.width = 836;
    l.height = 563;
    auto& v = l.items;
    v.push_back (section (0, 0, 268, 563, nullptr, boxed));
    v.push_back (section (6, 7, 128, 46, nullptr, boxed | raised));
    v.push_back (knob (31, 30, 40, "Time mix", nullptr, 0, noCaption));
    v.push_back (label (56, 20, 70, 20, TR_NOOP ("Time"), alignLeft));
    v.push_back (display (189, 12, 72, 72, "gbjog"));
    v.push_back (toggle (113, 62, 62, 28, "Hold", TR_NOOP ("Hold"), boxed));
    v.push_back (display (9, 66, 20, 20, "gbopts")); // round 190ee: FL's Options button (DFM OptionsBtn at 7, 66)
    // round 168m: the two slot grids draw each slot's name over a thumbnail of its envelope (FL's grids of 4 × 9)
    v.push_back (display (13, 103, 246, 162, "gbtimegrid"));
    v.push_back (display (13, 276, 246, 162, "gbvolumegrid"));
    v.push_back (knob (80, 467, 38, "Attack", TR_NOOP ("Att"), 492));
    v.push_back (knob (130, 467, 38, "Release", TR_NOOP ("Rel"), 492));
    v.push_back (knob (186, 467, 38, "Tension", TR_NOOP ("Tns"), 492, bipolar));
    v.push_back (section (6, 509, 128, 46, nullptr, boxed | raised));
    v.push_back (knob (31, 531, 40, "Volume mix", nullptr, 0, noCaption));
    v.push_back (label (56, 521, 70, 20, TR_NOOP ("Volume"), alignLeft));
    // round 168m (FL's DFM TSynthEditorForm): the tool column (Options, Snap, Envelope time / volume), the envelope
    // editor, and the selected slot's name and settings under it
    v.push_back (display (270, 4, 24, 555, "gbtools"));
    v.push_back (display (298, 4, 532, 26, "gbtabs")); // Time / Volume tabs, the slot's name, the factory slot browser
    v.push_back (display (298, 33, 532, 492, "grossbeat"));
    v.push_back (display (298, 529, 532, 30, "gbslot"));
    l.editedElsewhere.push_back ("Time slot"); // the slot grids
    l.editedElsewhere.push_back ("Volume slot");
    return l;
}

Layout reeverb1()
{
    auto l = fruityGrid ("Fruity Reeverb", 5, 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Low cut", TR_NOOP ("Lowcut"));
    fruityCell (v, 1, 0, "High cut", TR_NOOP ("Highcut"));
    fruityCell (v, 0, 1, "Predelay", TR_NOOP ("Predelay"));
    fruityCell (v, 1, 1, "Room size", TR_NOOP ("Room size"));
    fruityCell (v, 0, 2, "Diffusion", TR_NOOP ("Diffusion"));
    fruityCell (v, 1, 2, "Color", TR_NOOP ("Color"));
    fruityCell (v, 0, 3, "Decay", TR_NOOP ("Decay"));
    fruityCell (v, 1, 3, "High damping", TR_NOOP ("High damping"));
    fruityCell (v, 0, 4, "Dry", TR_NOOP ("Dry"));
    fruityCell (v, 1, 4, "Wet", TR_NOOP ("Reverb"));
    return l;
}

Layout fruityFlangus()
{
    // the order bar, then seven tall faders in their columns
    Layout l;
    l.fl = "Fruity Flangus";
    l.width = 366;
    l.height = 176;
    auto& v = l.items;
    auto order = slider (Item::vslider, 8, 75, 31, 76, "Order", nullptr, noCaption);
    order.steps = 8;
    v.push_back (order);
    v.push_back (label (2, 158, 44, 16, TR_NOOP ("Ord")));
    static const char* const params[] = { "Depth", "Speed", "Delay", "Spread", "Cross", "Dry", "Wet" };
    static const char* const captions[] = { TR_NOOP ("Depth"), TR_NOOP ("Spd"), TR_NOOP ("Del"), TR_NOOP ("Sprd"), TR_NOOP ("Cross"),
                                            TR_NOOP ("Dry"), TR_NOOP ("Wet") };
    for (int i = 0; i < 7; ++i)
    {
        const auto x = 43.0f + 46.0f * (float) i;
        v.push_back (section (x, 9, 40, 142, nullptr, boxed));
        v.push_back (slider (Item::vslider, x + 6, 14, 28, 132, params[i], nullptr, noCaption));
        v.push_back (label (x - 4, 158, 48, 16, captions[i]));
    }
    return l;
}

Layout fruitySoftClipper()
{
    // THRES and POST, the plugin's name plate, the output meter strip and the transfer curve
    Layout l;
    l.fl = "Fruity Soft Clipper";
    l.width = 274;
    l.height = 133;
    auto& v = l.items;
    v.push_back (section (0, 0, 116, 133, nullptr, boxed));
    v.push_back (knob (37, 55, 36, "Threshold", TR_NOOP ("Thres"), 88));
    v.push_back (knob (89, 55, 36, "Post", TR_NOOP ("Post"), 88));
    v.push_back (label (6, 102, 104, 24, TR_NOOP ("Soft clipper"), boxed));
    v.push_back (display (118, 3, 24, 127, "meter"));
    v.push_back (display (144, 3, 130, 127, "softclip", "Threshold"));
    return l;
}

Layout fruityBloodOverdrive()
{
    // PRE BAND and COLOR | the big PRE AMP with its x100 switch | POST FILTER and POST GAIN; captions on plates
    Layout l;
    l.fl = "Fruity Blood Overdrive";
    l.width = 428;
    l.height = 186;
    auto& v = l.items;
    v.push_back (section (0, 0, 133, 186, nullptr, boxed));
    v.push_back (section (135, 0, 158, 186, nullptr, boxed | raised));
    v.push_back (section (295, 0, 133, 186, nullptr, boxed));
    v.push_back (knob (69, 37, 52, "Pre band", nullptr, 0, noCaption));
    v.push_back (label (30, 69, 78, 15, TR_NOOP ("Pre band"), boxed));
    v.push_back (knob (69, 124, 52, "Color", nullptr, 0, noCaption | bipolar));
    v.push_back (label (30, 157, 78, 15, TR_NOOP ("Color"), boxed));
    v.push_back (knob (209, 86, 96, "Pre amp", nullptr, 0, noCaption | big));
    v.push_back (label (174, 157, 66, 15, TR_NOOP ("Pre amp"), boxed));
    v.push_back (toggle (244, 157, 44, 15, "x100", "x100"));
    v.push_back (knob (362, 36, 52, "Post filter", nullptr, 0, noCaption));
    v.push_back (label (323, 68, 78, 15, TR_NOOP ("Post filter"), boxed));
    v.push_back (knob (362, 124, 52, "Post gain", nullptr, 0, noCaption));
    v.push_back (label (323, 157, 78, 15, TR_NOOP ("Post gain"), boxed));
    return l;
}

Layout fruityPanOMatic()
{
    // PAN and VOL; the LFO (shape buttons, amount, speed, off / pan / volume target); the pan / volume pad
    Layout l;
    l.fl = "Fruity PanOMatic";
    l.width = 307;
    l.height = 166;
    auto& v = l.items;
    v.push_back (section (0, 0, 144, 166, nullptr, boxed));
    v.push_back (knob (34, 32, 30, "Pan", TR_NOOP ("Pan"), 53, bipolar));
    v.push_back (knob (77, 32, 30, "Volume", TR_NOOP ("Vol"), 53));
    v.push_back (section (0, 66, 144, 100, nullptr, boxed | raised));
    v.push_back (label (4, 70, 40, 20, TR_NOOP ("LFO"), alignLeft));
    auto shapes = radio (60, 80, 78, 24, "LFO shape", TR_NOOP ("Sine|Triangle|Square"));
    shapes.columns = 3;
    v.push_back (shapes);
    v.push_back (knob (34, 124, 30, "LFO amount", TR_NOOP ("Amt"), 150, bipolar));
    v.push_back (knob (77, 124, 30, "LFO speed", TR_NOOP ("Spd"), 150));
    v.push_back (radio (101, 112, 40, 40, "LFO target", TR_NOOP ("Off|Pan|Vol")));
    v.push_back (display (146, 3, 158, 160, "panpad"));
    return l;
}

Layout stereoShaper()
{
    // MIXER MATRIX: the four faders (R→L, left, right, L→R) with the LEFT / RIGHT sums under them; EFFECTS: pre / post,
    // DELAY and PHASE; the in / out difference switch
    Layout l;
    l.fl = "Fruity Stereo Shaper";
    l.width = 276;
    l.height = 316;
    auto& v = l.items;
    v.push_back (label (6, 0, 110, 24, TR_NOOP ("Mixer matrix"), alignLeft));
    v.push_back (label (190, 0, 80, 24, TR_NOOP ("Effects")));
    v.push_back (section (20, 30, 160, 270, nullptr, boxed));
    static const char* const faders[] = { "R > L", "Left", "Right", "L > R" };
    for (int i = 0; i < 4; ++i)
        v.push_back (slider (Item::vslider, 31.0f + 39.0f * (float) i, 64, 20, 216, faders[i], nullptr, noCaption | bipolar));
    v.push_back (label (30, 286, 60, 14, TR_NOOP ("Left")));
    v.push_back (label (108, 286, 60, 14, TR_NOOP ("Right")));
    v.push_back (radio (190, 44, 84, 26, "Effects post", TR_NOOP ("Pre|Post")));
    v.push_back (knob (233, 129, 50, "Delay", TR_NOOP ("Delay"), 164, bipolar));
    v.push_back (knob (233, 221, 50, "Phase", TR_NOOP ("Phase"), 257, bipolar));
    v.push_back (label (178, 280, 60, 26, TR_NOOP ("In/out difference"), alignLeft));
    v.push_back (toggle (238, 284, 34, 18, "Difference", TR_NOOP ("On"), boxed));
    return l;
}

Layout transientProcessor()
{
    // IN meter | SPLIT FREQ; ATTACK panel (knob, shape); DRIVE, SPLIT BALANCE; RELEASE panel (knob, shape); GAIN | OUT
    // meter; on / off; the gain view under it
    Layout l;
    l.fl = "Transient Processor";
    l.width = 553;
    l.height = 325;
    auto& v = l.items;
    v.push_back (display (2, 3, 30, 172, "meter"));
    v.push_back (display (518, 3, 32, 172, "meter"));
    v.push_back (section (34, 3, 482, 172, nullptr, boxed));
    v.push_back (label (150, 6, 250, 20, "Transient Processor", dim));
    v.push_back (knob (80, 52, 40, "Split freq.", TR_NOOP ("Split freq."), 86));
    v.push_back (section (126, 27, 110, 148, nullptr, boxed | raised));
    v.push_back (knob (181, 72, 66, "Attack", TR_NOOP ("Attack"), 129, big | bipolar));
    v.push_back (slider (Item::hslider, 156, 143, 50, 22, "Attack shape", nullptr, noCaption | bipolar));
    v.push_back (knob (279, 52, 40, "Drive", TR_NOOP ("Drive"), 86));
    v.push_back (knob (279, 140, 40, "Split balance", TR_NOOP ("Split balance"), 168, bipolar));
    v.push_back (section (322, 27, 110, 148, nullptr, boxed | raised));
    v.push_back (knob (377, 72, 66, "Release", TR_NOOP ("Release"), 129, big | bipolar));
    v.push_back (slider (Item::hslider, 352, 143, 50, 22, "Release shape", nullptr, noCaption | bipolar));
    v.push_back (knob (469, 52, 40, "Gain", TR_NOOP ("Gain"), 86, bipolar));
    v.push_back (radio (40, 146, 44, 28, "On", TR_NOOP ("Off|On")));
    v.push_back (display (8, 180, 542, 142, "envelope", "Release"));
    return l;
}

Layout fruityFreeFilter()
{
    auto l = fruityGrid ("Fruity Free Filter", 2, 3);
    l.width = 418;
    auto& v = l.items;
    fruityCell (v, 0, 0, "Type", TR_NOOP ("Type"));
    fruityCell (v, 1, 0, "Frequency", TR_NOOP ("Freq"));
    fruityHalfCell (v, 2, 0, 0, "Center", TR_NOOP ("Center"));
    fruityCell (v, 0, 1, "Q", TR_NOOP ("Q"), Cell::slider);
    fruityCell (v, 1, 1, "Gain", TR_NOOP ("Gain"));
    v.push_back (display (340, 60, 70, 40, "filter"));
    return l;
}

Layout fruityWaveShaper()
{
    // the shaping curve; the plugin's name, unipolar / bipolar, PRE, MIX, POST; smooth, oversample, HQ, center
    Layout l;
    l.fl = "Fruity WaveShaper";
    l.width = 277;
    l.height = 358;
    auto& v = l.items;
    v.push_back (display (8, 3, 258, 234, "waveshaper"));
    v.push_back (section (0, 254, 277, 104, nullptr, boxed));
    v.push_back (label (4, 262, 62, 40, "WaveShaper", dim));
    v.push_back (radio (72, 266, 46, 34, "Unipolar", TR_NOOP ("Bipolar|Unipolar")));
    v.push_back (knob (165, 279, 30, "Pre amp", TR_NOOP ("Pre"), 302));
    v.push_back (knob (210, 279, 30, "Mix", TR_NOOP ("Mix"), 302));
    v.push_back (knob (253, 279, 30, "Post gain", TR_NOOP ("Post"), 302));
    // (round 165: the switches start further left, so "SMOOTH" fits beside its switch instead of "S...")
    v.push_back (toggle (112, 312, 72, 14, "Smooth", TR_NOOP ("Smooth")));
    v.push_back (toggle (112, 332, 72, 14, "HQ", TR_NOOP ("HQ")));
    v.push_back (toggle (190, 332, 60, 14, "Center", TR_NOOP ("Center")));
    v.push_back (label (188, 312, 60, 14, TR_NOOP ("Oversample"), alignLeft));
    v.push_back (stepper (250, 310, 24, 18, "Oversampling", nullptr, 4));
    return l;
}

Layout vintageChorus()
{
    // MODE (I / II / edit) | DELAYS (time 1, time 2, feedback, high-pass faders) | MODULATION (start phase, wave,
    // L/R phase, tempo sync, speed fader) | LEVELS (mono input, invert wet, noise gate, noise, gain, mix fader)
    Layout l;
    l.fl = "Vintage Chorus";
    l.width = 754;
    l.height = 214;
    auto& v = l.items;
    v.push_back (label (420, 2, 326, 36, "Vintage Chorus", dim));
    v.push_back (section (14, 47, 173, 160, TR_NOOP ("Mode")));
    v.push_back (tabs (28, 90, 146, 64, "Mode", TR_NOOP ("I|II|Edit")));
    v.push_back (section (190, 47, 174, 160, TR_NOOP ("Delays")));
    static const char* const delays[] = { "Delay time 1", "Delay time 2", "Feedback", "Wet HP freq" };
    static const char* const delayCaptions[] = { "1", "2", TR_NOOP ("Feedback"), TR_NOOP ("H.pass") };
    for (int i = 0; i < 4; ++i)
    {
        const auto x = i < 2 ? 210.0f + 33.0f * (float) i : 282.0f + 39.0f * (float) (i - 2);
        v.push_back (slider (Item::vslider, x, 70, 20, 112, delays[i], nullptr, noCaption));
        v.push_back (label (x - 14, 183, 48, 12, delayCaptions[i]));
    }
    v.push_back (label (214, 195, 48, 10, TR_NOOP ("Time"), dim));
    v.push_back (section (367, 47, 195, 160, TR_NOOP ("Modulation")));
    v.push_back (knob (401, 96, 30, "LFO start phase", TR_NOOP ("Start phase"), 127));
    v.push_back (radio (444, 82, 40, 34, "LFO wave", TR_NOOP ("Triangle|Sine")));
    v.push_back (knob (401, 158, 30, "LFO L/R phase", TR_NOOP ("L/R phase"), 189));
    v.push_back (toggle (436, 160, 70, 14, "LFO sync", TR_NOOP ("Tempo sync")));
    v.push_back (slider (Item::vslider, 516, 70, 20, 124, "LFO speed", TR_NOOP ("Speed")));
    v.push_back (section (565, 47, 177, 160, TR_NOOP ("Levels")));
    v.push_back (toggle (574, 118, 76, 14, "Mono input", TR_NOOP ("Mono input")));
    v.push_back (toggle (574, 140, 76, 14, "Wet polarity", TR_NOOP ("Invert wet")));
    v.push_back (toggle (574, 162, 76, 14, "Noise gate", TR_NOOP ("Noise gate")));
    v.push_back (knob (663, 96, 30, "Hiss", TR_NOOP ("Noise"), 127));
    v.push_back (knob (663, 158, 30, "Gain", TR_NOOP ("Gain"), 189));
    v.push_back (slider (Item::vslider, 698, 70, 20, 124, "Wet/dry mix", TR_NOOP ("Mix")));
    l.menuSwitches = { "Link Chorus 1&2" };
    return l;
}

Layout hyperChorus()
{
    // three movement pads and the voices' view on top, one row of knobs under them
    Layout l;
    l.fl = "Hyper Chorus";
    l.width = 754;
    l.height = 214;
    auto& v = l.items;
    v.push_back (label (500, 2, 246, 32, "Hyper Chorus", dim));
    v.push_back (display (27, 38, 100, 102, "xy", "Delay", "Feedback"));
    v.push_back (display (157, 38, 100, 102, "xy", "Mod amount", "Mod rate"));
    v.push_back (display (288, 38, 100, 102, "xy", "Movement", "Movement speed"));
    v.push_back (display (420, 38, 300, 102, "voices"));
    v.push_back (section (2, 146, 750, 66, nullptr, boxed | raised));
    struct K { float x; const char* param; const char* caption; };
    const K knobs[] = { { 44, "Delay", TR_NOOP ("Delay") }, { 108, "Feedback", TR_NOOP ("Feedback") }, { 175, "Mod amount", TR_NOOP ("Modulation") },
                        { 239, "Mod rate", TR_NOOP ("Mod rate") }, { 304, "Movement", TR_NOOP ("Movement") },
                        { 369, "Movement speed", TR_NOOP ("Move rate") }, { 514, "High pass", TR_NOOP ("High pass") },
                        { 569, "Low pass", TR_NOOP ("Low pass") }, { 644, "LFO phase difference", TR_NOOP ("LFO phase dif") },
                        { 710, "Dry/wet", TR_NOOP ("Mix") } };
    for (const auto& k : knobs)
        v.push_back (knob (k.x, 171, 32, k.param, k.caption, 202));
    v.push_back (toggle (436, 162, 30, 18, "ArachnaChor mode", nullptr));
    v.push_back (label (420, 196, 60, 12, TR_NOOP ("Delay lines")));
    return l;
}

Layout fruityBalance()
{
    // Balance and Volume knobs (captions above, values under them) around the level meter
    Layout l;
    l.fl = "Fruity Balance";
    l.width = 232;
    l.height = 150;
    auto& v = l.items;
    v.push_back (section (0, 0, 232, 150, nullptr, boxed));
    v.push_back (label (8, 18, 80, 20, TR_NOOP ("Balance")));
    v.push_back (knob (48, 77, 46, "Balance", nullptr, 0, noCaption | bipolar));
    v.push_back (value (8, 112, 80, 20, "Balance", 0));
    v.push_back (display (90, 16, 40, 118, "meter"));
    v.push_back (label (130, 18, 80, 20, TR_NOOP ("Volume")));
    v.push_back (knob (170, 77, 46, "Volume", nullptr, 0, noCaption));
    v.push_back (value (130, 112, 80, 20, "Volume", 0));
    return l;
}

Layout fruityMute2Layout()
{
    // round 165g, FL Studio 2026's window: two knobs side by side, each with its caption box (MUTE / CHANNEL) and its value
    // under it ("On" = the sound passes, "Off" = muted; L, L + R, R = the side(s) silenced)
    Layout l;
    l.fl = "Fruity Mute 2";
    l.width = 300;
    l.height = 64;
    auto& v = l.items;
    v.push_back (section (0, 0, 300, 64, nullptr, boxed));
    v.push_back (knob (28, 32, 36, "Mute", nullptr, 0, noCaption, 1));
    v.push_back (label (52, 8, 92, 16, TR_NOOP ("MUTE")));
    v.push_back (value (52, 30, 92, 18, "Mute"));
    v.push_back (knob (172, 32, 36, "Channel", nullptr, 0, noCaption, 3));
    v.push_back (label (196, 8, 96, 16, TR_NOOP ("CHANNEL")));
    v.push_back (value (196, 30, 96, 18, "Channel"));
    return l;
}

LayoutItem menuOf (float x, float y, float w, float h, const char* param, const char* caption, const char* options); // (below)

Layout transporterLayout()
{
    // round 167f (REA_rodada167f-transporter), our arrangement of FL's sections: LEVELS (dry, loop A, loop B), LOOP
    // (length, trigger and detection), BIAS (the probabilities); the MODE strip below with the loop mode, the tempo note
    // values, probabilistic / deterministic, the update / freeze / flush buttons, the detector's source and stereo
    Layout l;
    l.fl = "Transporter";
    l.width = 640;
    l.height = 262;
    auto& v = l.items;
    v.push_back (section (0, 0, 170, 150, TR_NOOP ("LEVELS")));
    v.push_back (knob (36, 62, 36, "Dry level", TR_NOOP ("Dry"), 92, showValue));
    v.push_back (knob (86, 62, 36, "Loop A level", TR_NOOP ("Loop A"), 92, showValue));
    v.push_back (knob (136, 62, 36, "Loop B level", TR_NOOP ("Loop B"), 92, showValue));
    v.push_back (section (170, 0, 300, 150, TR_NOOP ("LOOP")));
    v.push_back (knob (210, 62, 36, "Minimum length A", TR_NOOP ("Min length A"), 92, showValue));
    v.push_back (knob (260, 62, 36, "A/B ratio", TR_NOOP ("A/B ratio"), 92, showValue));
    v.push_back (knob (310, 62, 36, "Transient sensitivity", TR_NOOP ("Sensitivity"), 92, showValue));
    v.push_back (knob (360, 62, 36, "Spread / auto-pan", TR_NOOP ("Spread"), 92, bipolar | showValue));
    v.push_back (knob (420, 62, 36, "Bouncing rate", TR_NOOP ("Bouncing rate"), 92, bipolar | showValue));
    v.push_back (section (470, 0, 170, 150, TR_NOOP ("BIAS")));
    v.push_back (knob (502, 50, 30, "Reverse probability", TR_NOOP ("Reverse"), 74, showValue));
    v.push_back (knob (566, 50, 30, "Octave shift probability", TR_NOOP ("Octave shift"), 74, showValue));
    v.push_back (knob (502, 112, 30, "Octave down/up probability", TR_NOOP ("Down/up"), 136, showValue));
    v.push_back (knob (566, 112, 30, "Ignore probability", TR_NOOP ("Ignore"), 136, showValue));
    v.push_back (section (0, 150, 640, 112, TR_NOOP ("MODE")));
    v.push_back (tabs (10, 176, 300, 24, "Loop mode", TR_NOOP ("Auto|Bouncing|Adaptive|Tempo")));
    v.push_back (menuOf (320, 176, 150, 24, "Tempo length", TR_NOOP ("Length"),
                         "1/1|1/2|1/4|1/8|1/16|1/32|1/1 dotted|1/2 dotted|1/4 dotted|1/8 dotted|1/16 dotted|1/1 triplet|1/2 triplet|"
                         "1/4 triplet|1/8 triplet|1/16 triplet|1/1 quint.|1/2 quint.|1/4 quint.|1/8 quint.|1/16 quint."));
    v.push_back (menuOf (480, 176, 150, 24, "Tempo trigger", TR_NOOP ("Tempo trigger"),
                         "1/1|1/2|1/4|1/8|1/16|1/32|1/1 dotted|1/2 dotted|1/4 dotted|1/8 dotted|1/16 dotted|1/1 triplet|1/2 triplet|"
                         "1/4 triplet|1/8 triplet|1/16 triplet|1/1 quint.|1/2 quint.|1/4 quint.|1/8 quint.|1/16 quint.|Off"));
    v.push_back (tabs (10, 210, 220, 22, "Deterministic", TR_NOOP ("Probabilistic|Deterministic")));
    v.push_back (toggle (240, 210, 70, 22, "Update loop A", TR_NOOP ("Update A"), momentary));
    v.push_back (toggle (314, 210, 70, 22, "Update loop B", TR_NOOP ("Update B"), momentary));
    v.push_back (toggle (388, 210, 82, 22, "Update loops A/B", TR_NOOP ("Update A/B"), momentary));
    v.push_back (toggle (480, 210, 70, 22, "Freeze", TR_NOOP ("Freeze")));
    v.push_back (toggle (560, 210, 70, 22, "Flush", TR_NOOP ("Flush"), momentary));
    v.push_back (menuOf (10, 236, 220, 22, "Source", TR_NOOP ("Detector source"), TR_NOOP ("Left|Right|Left + right")));
    v.push_back (toggle (240, 236, 100, 22, "Stereo", TR_NOOP ("Stereo")));
    LayoutItem send; // round 167n: the loops' send (A left, B right) to a mixer insert; the editor lists the song's inserts
    send.type = Item::menu;
    send.x = 350, send.y = 236, send.w = 200, send.h = 22;
    send.param = "Send to";
    send.label = TR_NOOP ("Send to");
    send.options = "None|Insert 1";
    send.values = { 0.0f, 1.0f / (float) (maxMixerTracks - 1) };
    v.push_back (send);
    v.push_back (toggle (560, 236, 70, 22, "Bypass", TR_NOOP ("Bypass")));
    return l;
}

Layout transmitterLayout()
{
    // round 165g (fltrans165), FL Studio 2026's arrangement: DETECTION along the top (sensitivity, hold, release, the
    // envelope mode, lookahead and stereo link), then the TRANSIENT and SUSTAIN columns side by side, each with its
    // on switch, level fader, tilt and one section effect whose knobs swap in place with the effect menu
    Layout l;
    l.fl = "Transmitter";
    l.width = 620;
    l.height = 432;
    auto& v = l.items;
    v.push_back (section (0, 0, 620, 200, TR_NOOP ("DETECTION")));
    v.push_back (knob (52, 52, 40, "Sensitivity", TR_NOOP ("Sensitivity"), 92, showValue));
    v.push_back (knob (126, 52, 40, "Hold", TR_NOOP ("Hold"), 92, showValue));
    v.push_back (knob (200, 52, 40, "Release", TR_NOOP ("Release"), 92, showValue));
    v.push_back (label (262, 26, 190, 16, TR_NOOP ("Mode")));
    v.push_back (tabs (262, 46, 190, 24, "Mode", TR_NOOP ("Smooth|Balanced|Dense")));
    v.push_back (toggle (474, 34, 132, 22, "Lookahead", TR_NOOP ("Lookahead")));
    v.push_back (toggle (474, 64, 132, 22, "Stereo link", TR_NOOP ("Stereo link")));
    // round 167b: FL's envelope view (the output and the detector's transient / sustain envelopes, scrolling;
    // the mouse wheel sets its speed)
    v.push_back (display (12, 108, 596, 82, "tssenvelope"));

    struct Part
    {
        float x;
        const char *title, *on, *level, *tilt, *effect;
        const char *satMix, *drive, *warmth, *bounceMix, *ampCurve, *delayCurve, *diffMix, *depth, *diffDelay;
        const char *delayMix, *time, *synced, *feedback, *sync;
    };
    const Part parts[] = {
        { 0, TR_NOOP ("TRANSIENT"), "Transient on", "Transient level", "Transient tilt", "Transient effect",
          "Transient saturation mix", "Transient saturation drive", "Transient saturation warmth", "Transient bounce mix",
          "Transient bounce amplitude curve", "Transient bounce delay curve", "Transient diffusion mix", "Transient diffusion depth",
          "Transient diffusion delay", "Transient delay mix", "Transient delay time", "Transient delay synced time",
          "Transient delay feedback", "Transient delay tempo sync" },
        { 310, TR_NOOP ("SUSTAIN"), "Sustain on", "Sustain level", "Sustain tilt", "Sustain effect",
          "Sustain saturation mix", "Sustain saturation drive", "Sustain saturation warmth", "Sustain bounce mix",
          "Sustain bounce amplitude curve", "Sustain bounce delay curve", "Sustain diffusion mix", "Sustain diffusion depth",
          "Sustain diffusion delay", "Sustain delay mix", "Sustain delay time", "Sustain delay synced time",
          "Sustain delay feedback", "Sustain delay tempo sync" },
    };
    for (const auto& p : parts)
    {
        const float x = p.x;
        v.push_back (section (x, 204, 310, 228, p.title));
        v.push_back (toggle (x + 12, 234, 76, 22, p.on, TR_NOOP ("On")));
        v.push_back (slider (Item::vslider, x + 30, 266, 32, 124, p.level, TR_NOOP ("Level")));
        v.push_back (value (x + 8, 404, 82, 18, p.level, 0));
        v.push_back (knob (x + 112, 284, 40, p.tilt, TR_NOOP ("Tilt"), 324, bipolar | showValue));
        v.push_back (menuOf (x + 146, 234, 152, 24, p.effect, TR_NOOP ("Effect"), TR_NOOP ("Saturation|Bounce|Diffusion|Blur|Delay|Off")));
        const float k1 = x + 172, k2 = x + 222, k3 = x + 272, ky = 302, cy = 340;
        // Saturation [0, 0.1), Bounce [0.1, 0.3), Diffusion [0.3, 0.5), Blur [0.5, 0.7), Delay [0.7, 0.9), Off [0.9, 1]
        v.push_back (when (knob (k1, ky, 36, p.satMix, TR_NOOP ("Mix"), cy, showValue), p.effect, 0.0f, 0.1f));
        v.push_back (when (knob (k2, ky, 36, p.drive, TR_NOOP ("Drive"), cy, showValue), p.effect, 0.0f, 0.1f));
        v.push_back (when (knob (k3, ky, 36, p.warmth, TR_NOOP ("Warmth"), cy, showValue), p.effect, 0.0f, 0.1f));
        v.push_back (when (knob (k1, ky, 36, p.bounceMix, TR_NOOP ("Mix"), cy, showValue), p.effect, 0.1f, 0.3f));
        v.push_back (when (knob (k2, ky, 36, p.ampCurve, TR_NOOP ("Amp curve"), cy, bipolar | showValue), p.effect, 0.1f, 0.3f));
        v.push_back (when (knob (k3, ky, 36, p.delayCurve, TR_NOOP ("Delay curve"), cy, bipolar | showValue), p.effect, 0.1f, 0.3f));
        v.push_back (when (knob (k1, ky, 36, p.diffMix, TR_NOOP ("Mix"), cy, showValue), p.effect, 0.3f, 0.5f));
        v.push_back (when (knob (k2, ky, 36, p.depth, TR_NOOP ("Depth"), cy, showValue), p.effect, 0.3f, 0.5f));
        v.push_back (when (knob (k3, ky, 36, p.diffDelay, TR_NOOP ("Delay"), cy, showValue), p.effect, 0.3f, 0.5f));
        v.push_back (when (label (x + 146, 292, 152, 20, TR_NOOP ("No settings"), dim), p.effect, 0.5f, 0.7f));
        v.push_back (when (knob (k1, ky, 36, p.delayMix, TR_NOOP ("Mix"), cy, showValue), p.effect, 0.7f, 0.9f));
        v.push_back (when (when (knob (k2, ky, 36, p.time, TR_NOOP ("Time"), cy, showValue), p.effect, 0.7f, 0.9f), p.sync, 0.0f, 0.5f));
        v.push_back (when (when (knob (k2, ky, 36, p.synced, TR_NOOP ("Time"), cy, showValue), p.effect, 0.7f, 0.9f), p.sync, 0.5f, 1.0f));
        v.push_back (when (knob (k3, ky, 36, p.feedback, TR_NOOP ("Feedback"), cy, showValue), p.effect, 0.7f, 0.9f));
        v.push_back (when (toggle (x + 150, 374, 148, 22, p.sync, TR_NOOP ("Tempo sync")), p.effect, 0.7f, 0.9f));
        v.push_back (when (label (x + 146, 292, 152, 20, TR_NOOP ("No settings"), dim), p.effect, 0.9f, 1.0f));
        // round 167b: FL's send of this part to another mixer track
        v.push_back (display (x + 150, 402, 148, 22, p.x == 0 ? "tsssend0" : "tsssend1"));
    }
    return l;
}

Layout fruitySend()
{
    // one row: DRY, the SEND TO box (the destination insert, a menu of the mixer's inserts), PAN and VOLUME
    // (FL Studio 2026, 263 × 89; REA_rodada105_send §4)
    Layout l;
    l.fl = "Fruity Send";
    l.width = 263;
    l.height = 89;
    auto& v = l.items;
    v.push_back (section (0, 0, 263, 89, nullptr, boxed));
    v.push_back (knob (37, 30, 46, "Dry", TR_NOOP ("Dry"), 72));
    LayoutItem to;
    to.type = Item::menu;
    to.x = 78, to.y = 17, to.w = 48, to.h = 28;
    to.param = "Send to";
    to.label = TR_NOOP ("Send to");
    to.options = "None|Insert 1"; // (not shown: the editor lists the song's inserts by name)
    to.values = { 0.0f, 1.0f / (float) (maxMixerTracks - 1) };
    v.push_back (to);
    v.push_back (label (70, 52, 64, 14, TR_NOOP ("Send to")));
    v.push_back (knob (158, 30, 46, "Pan", TR_NOOP ("Pan"), 72, bipolar));
    v.push_back (knob (226, 30, 46, "Volume", TR_NOOP ("Volume"), 72));
    return l;
}

Layout lynxTune()
{
    // LYNX Tune (own design, REA rounds 95 / 120 / 128). The workflow of the automatic pitch correctors' "auto" mode it
    // stands in for (Auto-Tune Pro, studied on the official trial — layout logic only, LYNX's own drawing): the input /
    // key / scale bar on top with AUTO KEY under the scale and the formant / transpose / A4 / mix group on the right;
    // the correction row in the middle — RETUNE SPEED big on the left, FLEX-TUNE beside it, the live correction meter
    // (note name, ±100 cent ring; click = HOLD) in the centre, NATURAL VIBRATO and HUMANIZE big on the right; the pitch
    // graph or the ADVANCED page (created vibrato, ignore vibrato) below; the keyboard at the bottom with its
    // BYPASS / REMOVE modes and LATCH (round 128).
    Layout l;
    l.width = 800;
    l.height = 622;
    l.pages = { TR_NOOP ("Pitch graph"), TR_NOOP ("Advanced"), TR_NOOP ("Real-time") }; // round 135w: + Real-time
    auto& v = l.items;
    auto menu = [&v] (float x, float y, float w, const char* param, const char* caption, const char* options, int steps = 0) {
        LayoutItem m;
        m.type = Item::menu;
        m.x = x, m.y = y, m.w = w, m.h = 22;
        m.param = param;
        m.label = caption;
        m.options = options;
        m.steps = steps;
        v.push_back (m);
        v.push_back (label (x, y - 16, w, 14, caption));
    };
    // --- top bar: input
    v.push_back (section (0, 0, 334, 92, TR_NOOP ("INPUT")));
    v.push_back (knob (34, 44, 30, "Tracking", TR_NOOP ("TRACKING"), 70, 0));
    menu (66, 40, 106, "Range", TR_NOOP ("Input type"), TR_NOOP ("Soprano|Alto / Tenor|Low male|Instrument"));
    menu (178, 40, 52, "Key", TR_NOOP ("Key"), "C|C#|D|D#|E|F|F#|G|G#|A|A#|B");
    menu (236, 40, 92, "Scale", TR_NOOP ("Scale"),
          TR_NOOP ("Chromatic|Major|Minor|Harmonic minor|Melodic minor|Major pentatonic|Minor pentatonic|Blues|Dorian|Mixolydian|Phrygian|Lydian|Locrian|Custom"));
    v.push_back (display (236, 66, 92, 20, "tuneautokey"));
    // --- mode: latency and MIDI targets
    v.push_back (section (338, 0, 140, 92, TR_NOOP ("MODE")));
    v.push_back (toggle (346, 24, 124, 18, "Low latency", TR_NOOP ("Low latency")));
    v.push_back (stepper (346, 54, 124, 24, "MIDI", TR_NOOP ("MIDI channel"), 129));
    // --- output: formant, transpose, reference, mix
    v.push_back (section (482, 0, 318, 92, TR_NOOP ("OUTPUT")));
    v.push_back (toggle (490, 22, 82, 18, "Formant preserve", TR_NOOP ("Formant")));
    // round 166c: SHIFT and the THROAT beside it (the modelled throat length, formant on only); Transpose, or the
    // steps of the scale while IN SCALE is on
    // round 190bb (issue #103): + BREATH (Breathiness: aspiration noise with the voice's formants), the third knob of
    // the voice-character group; the transpose column moves 16 px right and loses 8 px of width to make room
    v.push_back (knob (506, 60, 22, "Formant shift", TR_NOOP ("SHIFT"), 82, bipolar));
    v.push_back (knob (542, 60, 22, "Throat length", TR_NOOP ("THROAT"), 82, 0));
    v.push_back (knob (578, 60, 22, "Breathiness", TR_NOOP ("BREATH"), 82, 0));
    v.push_back (when (stepper (600, 40, 70, 22, "Transpose", TR_NOOP ("Transpose"), 25), "Transpose in scale", 0.0f, 0.49f));
    v.push_back (when (stepper (600, 40, 70, 22, "Scale transpose", TR_NOOP ("Scale steps"), 15), "Transpose in scale", 0.5f, 1.0f));
    v.push_back (toggle (600, 66, 70, 18, "Transpose in scale", TR_NOOP ("In scale")));
    v.push_back (knob (700, 44, 30, "Reference", TR_NOOP ("A4"), 70, showValue));
    v.push_back (knob (760, 44, 30, "Mix", TR_NOOP ("MIX"), 70, showValue));
    // --- correction row
    v.push_back (section (0, 96, 800, 236, TR_NOOP ("CORRECTION")));
    v.push_back (knob (100, 200, 104, "Retune", TR_NOOP ("RETUNE SPEED"), 284, big | showValue));
    v.push_back (knob (226, 224, 60, "Flex-Tune", TR_NOOP ("FLEX-TUNE"), 284, showValue));
    v.push_back (display (300, 116, 200, 200, "pitchring"));
    v.push_back (knob (574, 224, 60, "Natural vibrato", TR_NOOP ("NATURAL VIBRATO"), 284, bipolar | showValue));
    v.push_back (knob (700, 200, 104, "Humanize", TR_NOOP ("HUMANIZE"), 284, big | showValue));
    // round 135: CLASSIC (the older correctors' retune) and LEARN (the scale from the notes played on the MIDI channel)
    v.push_back (toggle (12, 116, 90, 18, "Classic", TR_NOOP ("Classic")));
    v.push_back (toggle (690, 116, 104, 18, "Learn MIDI", TR_NOOP ("Learn scale")));
    // --- pitch graph / advanced (pages)
    v.push_back (section (0, 336, 800, 142, nullptr, boxed));
    v.push_back (pageTabs (8, 340, 330, 20, TR_NOOP ("Pitch graph|Advanced|Real-time")));
    v.push_back (onPage (display (6, 366, 788, 106, "pitchtrace"), 0));
    auto adv = [&v] (LayoutItem i) { v.push_back (onPage (std::move (i), 1)); };
    adv (label (14, 368, 300, 16, TR_NOOP ("Create vibrato"), alignLeft));
    {
        LayoutItem m;
        m.type = Item::menu;
        m.x = 14, m.y = 404, m.w = 110, m.h = 22;
        m.param = "Vibrato shape";
        m.label = TR_NOOP ("Shape");
        m.options = TR_NOOP ("No vibrato|Sine|Square|Sawtooth");
        adv (m);
        adv (label (14, 388, 110, 14, TR_NOOP ("Shape")));
    }
    // round 166c: + the created vibrato's AMPLITUDE (level swing), FORMANT (how far the formants follow it) and
    // VARIATION (random drift); Ignore vibrato moves under the shape menu
    adv (knob (164, 420, 30, "Vibrato rate", TR_NOOP ("RATE"), 452, showValue));
    adv (knob (230, 420, 30, "Vibrato depth", TR_NOOP ("DEPTH"), 452, showValue));
    adv (knob (296, 420, 30, "Vibrato amplitude", TR_NOOP ("AMPLITUDE"), 452, showValue));
    adv (knob (362, 420, 30, "Vibrato formant", TR_NOOP ("FORMANT"), 452, showValue));
    adv (knob (428, 420, 30, "Vibrato variation", TR_NOOP ("VARIATION"), 452, showValue));
    adv (knob (494, 420, 30, "Onset delay", TR_NOOP ("ONSET DELAY"), 452, showValue));
    adv (knob (564, 420, 30, "Onset rate", TR_NOOP ("ONSET RATE"), 452, showValue));
    adv (toggle (14, 440, 120, 20, "Ignore vibrato", TR_NOOP ("Ignore vibrato")));
    // round 136: the voice-shifter extras (the Drive stage it measured, a ±12 st formant range, an output trim)
    adv (label (640, 368, 150, 16, TR_NOOP ("Voice shifter"), alignLeft));
    adv (toggle (640, 386, 150, 18, "Drive stage", TR_NOOP ("Drive stage")));
    adv (toggle (640, 408, 150, 18, "Wide formant", TR_NOOP ("Formant ±12 st")));
    adv (knob (672, 446, 22, "Drive", TR_NOOP ("DRIVE"), 462, showValue));
    adv (knob (748, 446, 22, "Output", TR_NOOP ("OUTPUT"), 462, showValue | bipolar));
    // round 135w: the real-time corrector's controls (the Waves Tune Real-Time role): Speed with its link to the
    // retune (Note Transition), the tolerances, vibrato on / amount, correction %, input / output meters
    auto rt = [&v] (LayoutItem i) { v.push_back (onPage (std::move (i), 2)); };
    rt (knob (50, 412, 34, "Speed", TR_NOOP ("SPEED"), 444, showValue));
    rt (toggle (14, 450, 74, 18, "Speed link", TR_NOOP ("Link")));
    rt (knob (140, 412, 34, "Tolerance cents", TR_NOOP ("TOL. CENTS"), 444, showValue));
    rt (knob (224, 412, 34, "Tolerance time", TR_NOOP ("TOL. TIME"), 444, showValue));
    rt (toggle (284, 372, 110, 18, "Keep vibrato", TR_NOOP ("Vibrato")));
    rt (knob (330, 422, 30, "Vibrato amount", TR_NOOP ("VIB. DEPTH"), 452, showValue));
    rt (knob (430, 412, 34, "Correction", TR_NOOP ("CORRECTION"), 444, showValue));
    rt (label (494, 376, 214, 14, TR_NOOP ("Speed: in-note drift and vibrato"), alignLeft | dim));
    rt (label (494, 394, 214, 14, TR_NOOP ("Link: the retune speed does it"), alignLeft | dim));
    rt (label (494, 412, 214, 14, TR_NOOP ("Tolerance: how far / how long a note"), alignLeft | dim));
    rt (label (494, 430, 214, 14, TR_NOOP ("may stray before the target changes"), alignLeft | dim));
    // round 152: the MIDI reference tone (the notes held on the MIDI channel as sines) and its level
    rt (toggle (494, 452, 104, 18, "Reference tone", TR_NOOP ("Ref. tone")));
    rt (knob (618, 461, 13, "Tone level", nullptr, 461, noCaption | bipolar));
    {
        auto io = display (712, 366, 80, 106, "iometer");
        io.steps = 3; // LYNX Tune's input / output peaks are its meter outputs 3 and 4
        rt (io);
    }
    // --- the keyboard: BYPASS / REMOVE modes, LATCH, four octaves (a key edits its note name in every octave)
    // round 147: + the range bar, the range presets, Reset scale and the view's navigation (the real-time corrector's)
    v.push_back (section (0, 482, 800, 140, TR_NOOP ("TARGET NOTES")));
    v.push_back (display (8, 502, 784, 114, "tunekeys"));
    return l;
}

Layout emphasizer()
{
    // round 154: INPUT GAIN and the big EMPHASIS knob over their captions, OUTPUT, the meter strip on the right; the
    // bottom strip carries the name plate and the SOFT CLIPPING / LEVEL MATCHING / BYPASS switches
    Layout l;
    l.fl = "Emphasizer";
    l.width = 512;
    l.height = 98;
    auto& v = l.items;
    v.push_back (section (0, 0, 236, 74, nullptr, boxed));
    v.push_back (knob (40, 30, 34, "Input gain", TR_NOOP ("INPUT GAIN"), 60, showValue));
    v.push_back (knob (118, 30, 40, "Emphasis", TR_NOOP ("EMPHASIS"), 60, showValue));
    v.push_back (knob (196, 30, 34, "Output", TR_NOOP ("OUTPUT"), 60, showValue));
    v.push_back (display (240, 4, 268, 66, "meter"));
    v.push_back (label (4, 78, 120, 16, "EMPHASIZER", alignLeft));
    v.push_back (toggle (236, 78, 96, 16, "Soft clipping", TR_NOOP ("SOFT CLIPPING")));
    v.push_back (toggle (336, 78, 104, 16, "Level matching", TR_NOOP ("LEVEL MATCHING")));
    v.push_back (toggle (444, 78, 64, 16, "Bypass", TR_NOOP ("BYPASS")));
    return l;
}

Layout flEmphasis()
{
    // round 167f: FL's Emphasis interpreted (its controls and captions, LYNX's drawing): INPUT GAIN, the big EMPHASIS
    // knob and CEILING on top with the meter strip; the mode, ENVELOPE order, HARDNESS and LINKING below; the bottom
    // strip carries OVERSAMPLING, CHANNEL MODE and the option switches
    Layout l;
    l.fl = "Emphasis";
    l.width = 560;
    l.height = 196;
    auto& v = l.items;
    v.push_back (section (0, 0, 300, 96, nullptr, boxed));
    v.push_back (knob (50, 38, 34, "Input gain", TR_NOOP ("INPUT GAIN"), 82, showValue));
    v.push_back (knob (150, 38, 44, "Emphasis", TR_NOOP ("EMPHASIS"), 82, showValue));
    v.push_back (knob (250, 38, 34, "Ceiling", TR_NOOP ("CEILING"), 82, showValue));
    v.push_back (display (304, 4, 252, 92, "meter"));
    v.push_back (section (0, 100, 556, 50, nullptr, boxed));
    v.push_back (label (8, 102, 120, 14, TR_NOOP ("MODE"), alignLeft));
    v.push_back (radio (8, 118, 220, 26, "Mode", TR_NOOP ("Versatile|Loud|Transient|Steady")));
    v.push_back (stepper (240, 116, 50, 30, "Envelope order", TR_NOOP ("ENVELOPE"), 4));
    v.push_back (stepper (304, 116, 50, 30, "Hardness", TR_NOOP ("HARDNESS"), 10));
    v.push_back (knob (400, 118, 22, "Linking", TR_NOOP ("LINKING"), 138, showValue));
    v.push_back (label (450, 102, 100, 14, TR_NOOP ("CHANNEL MODE"), alignLeft));
    v.push_back (radio (450, 118, 100, 26, "Channel mode", "L/R|M/S"));
    v.push_back (label (4, 154, 100, 16, "EMPHASIS", alignLeft));
    v.push_back (label (110, 154, 96, 14, TR_NOOP ("OVERSAMPLING"), alignLeft));
    v.push_back (radio (206, 152, 200, 18, "Oversampling", TR_NOOP ("Off|2x|4x|8x|16x")));
    v.push_back (toggle (410, 152, 70, 18, "True peak", TR_NOOP ("TRUE PEAK")));
    v.push_back (toggle (484, 152, 72, 18, "Bypass", TR_NOOP ("BYPASS")));
    v.push_back (toggle (4, 174, 104, 18, "Level matching", TR_NOOP ("LEVEL MATCHING")));
    v.push_back (toggle (112, 174, 60, 18, "Delta", TR_NOOP ("DELTA")));
    v.push_back (toggle (176, 174, 120, 18, "Instant recovery", TR_NOOP ("INSTANT RECOVERY")));
    v.push_back (toggle (300, 174, 80, 18, "DC block", TR_NOOP ("DC BLOCK")));
    v.push_back (toggle (384, 174, 172, 18, "Loudness compensation", TR_NOOP ("LOUDNESS COMPENSATION")));
    return l;
}

Layout fruitySqueeze()
{
    // SQUARIZE (amount), PUNCHER (preserve, impact, relation, amount) on plates; FILTER (on, type, freq, res, pre / post);
    // MIX and GAIN faders
    Layout l;
    l.fl = "Fruity Squeeze";
    l.width = 282;
    l.height = 223;
    auto& v = l.items;
    v.push_back (section (2, 2, 105, 52, nullptr, boxed));
    v.push_back (label (10, 6, 90, 16, TR_NOOP ("Squarize"), alignLeft));
    const auto row = [&v] (float y, const char* param, const char* caption) {
        v.push_back (label (13, y - 9, 60, 17, caption, boxed));
        v.push_back (knob (87, y, 24, param, nullptr, 0, noCaption));
    };
    row (39, "Steps", TR_NOOP ("Amount"));
    v.push_back (section (2, 58, 105, 162, nullptr, boxed));
    v.push_back (label (10, 62, 90, 16, TR_NOOP ("Puncher"), alignLeft));
    row (100, "Preserve", TR_NOOP ("Preserve"));
    row (133, "Impact", TR_NOOP ("Impact"));
    row (165, "Relation", TR_NOOP ("Relation"));
    row (198, "Amount", TR_NOOP ("Amount"));
    v.push_back (section (110, 2, 100, 164, nullptr, boxed));
    v.push_back (toggle (116, 6, 90, 16, "Filter", TR_NOOP ("Filter")));
    v.push_back (section (116, 30, 88, 19, nullptr, boxed | raised));
    v.push_back (value (116, 30, 88, 19, "Filter type", 0));
    v.push_back (label (116, 66, 40, 14, TR_NOOP ("Freq.")));
    v.push_back (knob (136, 102, 34, "Cutoff", nullptr, 0, noCaption));
    v.push_back (label (162, 66, 40, 14, TR_NOOP ("Res.")));
    v.push_back (knob (182, 102, 34, "Resonance", nullptr, 0, noCaption));
    v.push_back (tabs (117, 137, 88, 18, "Filter post", TR_NOOP ("Pre|Pos")));
    v.push_back (section (213, 2, 67, 164, nullptr, boxed));
    v.push_back (slider (Item::vslider, 219, 12, 20, 148, "Mix", TR_NOOP ("Mix")));
    v.push_back (slider (Item::vslider, 251, 12, 20, 148, "Volume", TR_NOOP ("Gain")));
    v.push_back (label (112, 170, 168, 48, "Fruity Squeeze", dim));
    return l;
}

Layout fruityDelay2()
{
    // INPUT (pan, volume) | FEEDBACK (normal / invert / ping-pong, volume, cutoff) | TIME (time, offset) | DRY (volume)
    Layout l;
    l.fl = "Fruity Delay 2";
    l.width = 404;
    l.height = 102;
    auto& v = l.items;
    v.push_back (section (2, 2, 99, 74, TR_NOOP ("Input")));
    v.push_back (knob (28, 45, 33, "Input pan", TR_NOOP ("Pan"), 68, bipolar));
    v.push_back (knob (73, 45, 33, "Input volume", TR_NOOP ("Vol"), 68));
    v.push_back (section (103, 2, 148, 74, TR_NOOP ("Feedback")));
    v.push_back (radio (109, 24, 52, 46, "Mode", TR_NOOP ("Normal|Invert|P.pong")));
    v.push_back (knob (183, 45, 33, "Feedback", TR_NOOP ("Vol"), 68));
    v.push_back (knob (223, 45, 33, "Cutoff", TR_NOOP ("Cut"), 68));
    v.push_back (section (253, 2, 97, 74, TR_NOOP ("Time")));
    v.push_back (knob (279, 45, 33, "Time", TR_NOOP ("Time"), 68));
    v.push_back (knob (325, 45, 33, "Stereo offset", TR_NOOP ("Ofs"), 68, bipolar));
    v.push_back (section (352, 2, 50, 74, TR_NOOP ("Dry")));
    v.push_back (knob (378, 45, 33, "Dry", TR_NOOP ("Vol"), 68));
    v.push_back (label (104, 78, 296, 22, "Fruity Delay 2", dim));
    return l;
}

// --- generators (the channel's plugin page): items name the knobs of ChannelSettings' model panel ------------

Layout threeOsc()
{
    // three oscillator rows: shape buttons (sine, square, rounded, custom / triangle, saw, noise) and the invert switch,
    // PHASE OFS and DETUNE faders, COARSE, FINE, pan and (oscillators 2 and 3) volume; AM OSC 3, HQ and PHASE RAND below
    Layout l;
    l.fl = "3x Osc";
    l.width = 560;
    l.height = 390;
    auto& v = l.items;
    v.push_back (section (0, 0, 560, 310, nullptr, boxed));
    static const char* const names[3][7] = { { "Osc 1 shape", "Osc 1 phase", "Osc 1 detune", "Osc 1 coarse", "Osc 1 fine", "Osc 1 pan", nullptr },
                                             { "Osc 2 shape", "Osc 2 phase", "Osc 2 detune", "Osc 2 coarse", "Osc 2 fine", "Osc 2 pan", "Osc 2 mix" },
                                             { "Osc 3 shape", "Osc 3 phase", "Osc 3 detune", "Osc 3 coarse", "Osc 3 fine", "Osc 3 pan", "Osc 3 mix" } };
    static const char* const inverts[3] = { "Invert 1", "Invert 2", "Invert 3" };
    static const char* const numbers[3] = { "1", "2", "3" };
    for (int o = 0; o < 3; ++o)
    {
        const auto dy = 107.5f * (float) o;
        const auto captions = o < 2; // FL writes the captions between the rows
        v.push_back (label (2, 20 + dy, 30, 36, numbers[o], dim));
        // shape buttons in FL's order; the values are the engine's shapes 0…6 over 0..1
        auto shape = radio (37, 9 + dy, 152, 76, names[o][0], "Sine|Square|Rounded|Custom|Triangle|Saw|Noise",
                            { 0.0f, 2.0f / 6.0f, 4.0f / 6.0f, 1.0f, 1.0f / 6.0f, 3.0f / 6.0f, 5.0f / 6.0f });
        shape.columns = 4;
        v.push_back (shape);
        v.push_back (toggle (153, 52 + dy, 32, 28, inverts[o], "+/-", boxed));
        v.push_back (slider (Item::vslider, 222, 6 + dy, 20, 82, names[o][1], nullptr, noCaption | bipolar));
        v.push_back (slider (Item::vslider, 252, 6 + dy, 20, 82, names[o][2], nullptr, noCaption | bipolar));
        if (captions)
        {
            v.push_back (label (190, 95 + dy, 58, 14, TR_NOOP ("Phase ofs"), dim));
            v.push_back (label (250, 95 + dy, 40, 14, TR_NOOP ("Detune"), dim));
        }
        v.push_back (knob (338, 48 + dy, 46, names[o][3], captions ? TR_NOOP ("Coarse") : nullptr, 102 + dy, captions ? 0 : noCaption));
        v.push_back (knob (401, 48 + dy, 34, names[o][4], captions ? TR_NOOP ("Fine") : nullptr, 102 + dy, captions ? 0 : noCaption));
        v.push_back (knob (465, 48 + dy, 34, names[o][5], captions ? TR_NOOP ("Pan") : nullptr, 102 + dy, bipolar | (captions ? 0 : noCaption)));
        if (names[o][6] != nullptr)
            v.push_back (knob (523, 48 + dy, 46, names[o][6], o == 1 ? TR_NOOP ("Vol") : nullptr, 102 + dy, o == 1 ? 0 : noCaption));
    }
    v.push_back (section (0, 312, 560, 78, nullptr, boxed | raised));
    v.push_back (toggle (14, 342, 72, 16, "Osc 3 AM", TR_NOOP ("AM osc 3")));
    v.push_back (toggle (94, 342, 44, 16, "HQ", TR_NOOP ("HQ")));
    v.push_back (knob (165, 350, 44, "Phase rand", nullptr, 0, noCaption));
    v.push_back (label (192, 342, 80, 16, TR_NOOP ("Phase rand"), alignLeft));
    v.push_back (label (380, 326, 170, 50, "3x Osc", dim));
    return l;
}

Layout flKeys()
{
    // ENVIRONMENT, MISC, VELOCITY and TUNING over two rows of knobs; the sample set's name at the top right
    Layout l;
    l.fl = "FL Keys";
    l.width = 480;
    l.height = 186;
    auto& v = l.items;
    v.push_back (label (8, 2, 200, 34, "FL Keys", dim | alignLeft));
    v.push_back (display (333, 9, 141, 18, "flkeysname"));
    v.push_back (section (12, 37, 110, 145, TR_NOOP ("Environment")));
    v.push_back (section (132, 37, 111, 145, TR_NOOP ("Misc")));
    v.push_back (section (253, 37, 159, 145, TR_NOOP ("Velocity")));
    v.push_back (section (422, 37, 52, 145, TR_NOOP ("Tuning")));
    v.push_back (knob (37, 87, 34, "Decay", TR_NOOP ("Decay"), 110, bipolar));
    v.push_back (knob (95, 87, 34, "Release", TR_NOOP ("Release"), 110, bipolar));
    v.push_back (knob (37, 149, 34, "Pan/Trem", TR_NOOP ("Pan"), 172, bipolar));
    v.push_back (knob (95, 149, 34, "Stereo", TR_NOOP ("Stereo"), 172));
    v.push_back (knob (158, 87, 34, "Overdrive", TR_NOOP ("Overdrive"), 110));
    v.push_back (knob (216, 87, 34, "LFO rate", TR_NOOP ("LFO"), 110));
    v.push_back (knob (158, 149, 34, "Treble", TR_NOOP ("Treble"), 172, bipolar));
    v.push_back (knob (216, 149, 34, "Stretch", TR_NOOP ("Stretch"), 172, bipolar));
    v.push_back (knob (278, 87, 34, "Vel > Muffle", TR_NOOP ("Vel"), 110));
    v.push_back (knob (336, 87, 34, "Muffle", TR_NOOP ("Muffle"), 110));
    v.push_back (knob (278, 149, 34, "Vel > Hard", TR_NOOP ("Vel"), 172, bipolar));
    v.push_back (knob (336, 149, 34, "Hardness", TR_NOOP ("Hardness"), 172, bipolar));
    v.push_back (knob (394, 149, 34, "Vel sens", TR_NOOP ("Sens"), 172, bipolar));
    v.push_back (knob (447, 87, 34, "Tune", TR_NOOP ("Tune"), 110, bipolar));
    v.push_back (knob (447, 149, 34, "Detune", TR_NOOP ("Detune"), 172));
    return l;
}

Layout booBass()
{
    // BASS, MID and TREBLE stepping down along the instrument's body
    Layout l;
    l.fl = "BooBass";
    l.width = 343;
    l.height = 154;
    auto& v = l.items;
    v.push_back (label (180, 0, 160, 34, "BooBass", dim));
    v.push_back (section (40, 36, 300, 116, nullptr, boxed));
    v.push_back (knob (79, 74, 40, "Bass", TR_NOOP ("Bass"), 107));
    v.push_back (knob (151, 92, 40, "Mid", TR_NOOP ("Mid"), 128));
    v.push_back (knob (222, 102, 40, "Treble", TR_NOOP ("Treble"), 138));
    return l;
}

Layout sytrusMain()
{
    // Sytrus' MAIN page: master faders, volume and filter envelopes, unison, the modulation X/Y pad, quality and porta,
    // and the FM / RM matrix (round 172a). The operator, filter and effect pages are not editable in LYNX (left out).
    Layout l;
    l.fl = "Sytrus";
    l.width = 850;
    l.height = 346;
    auto& v = l.items;
    v.push_back (section (6, 14, 538, 122, TR_NOOP ("Main")));
    const auto fader = [&v] (float cx, const char* param, const char* caption, bool bipolarFader) {
        v.push_back (slider (Item::vslider, cx - 9, 54, 18, 70, param, nullptr, noCaption | (bipolarFader ? bipolar : 0)));
        v.push_back (label (cx - 16, 38, 32, 13, caption));
    };
    v.push_back (section (12, 34, 136, 98, nullptr, boxed));
    fader (38, "Master level", TR_NOOP ("Vol"), false);
    fader (81, "Master LFO", TR_NOOP ("LFO"), true);
    fader (124, "Master pitch", TR_NOOP ("Pitch"), true);
    v.push_back (section (152, 34, 98, 98, nullptr, boxed));
    fader (165, "Vol attack", "A", false);
    fader (189, "Vol decay", "D", false);
    fader (213, "Vol sustain", "S", true);
    fader (237, "Vol release", "R", false);
    v.push_back (label (156, 122, 80, 10, TR_NOOP ("Vol"), dim | alignLeft));
    v.push_back (section (256, 34, 98, 98, nullptr, boxed));
    fader (269, "Flt attack", "A", false);
    fader (293, "Flt decay", "D", false);
    fader (317, "Flt sustain", "S", true);
    fader (341, "Flt release", "R", false);
    v.push_back (label (260, 122, 80, 10, TR_NOOP ("Filter"), dim | alignLeft));
    v.push_back (section (360, 34, 180, 98, nullptr, boxed));
    v.push_back (stepper (366, 38, 30, 34, "Unison", TR_NOOP ("Ord"), 8));
    fader (410, "Uni pan", TR_NOOP ("Pn"), false);
    fader (434, "Uni volume", TR_NOOP ("Vl"), false);
    fader (458, "Uni pitch", TR_NOOP ("Pt"), false);
    fader (481, "Uni sub", TR_NOOP ("Sb"), false);
    fader (504, "Uni phase", TR_NOOP ("Ph"), false);
    fader (527, "Uni env var", TR_NOOP ("En"), false);
    v.push_back (label (470, 122, 66, 10, TR_NOOP ("Unison"), dim));
    v.push_back (section (12, 138, 220, 160, TR_NOOP ("Modulation")));
    v.push_back (display (18, 160, 150, 130, "xy", "Mod X", "Mod Y"));
    v.push_back (knob (200, 185, 26, "Mod X", "X", 206));
    v.push_back (knob (200, 240, 26, "Mod Y", "Y", 261));
    v.push_back (section (236, 138, 304, 160, TR_NOOP ("Quality")));
    v.push_back (label (412, 158, 44, 12, TR_NOOP ("Draft")));
    v.push_back (label (462, 158, 54, 12, TR_NOOP ("Render")));
    v.push_back (label (246, 176, 150, 16, TR_NOOP ("HQ envelopes"), alignLeft));
    v.push_back (toggle (426, 177, 16, 14, "Live env accuracy", nullptr));
    v.push_back (toggle (481, 177, 16, 14, "Export env accuracy", nullptr));
    v.push_back (label (246, 204, 150, 16, TR_NOOP ("Oversampling"), alignLeft));
    v.push_back (stepper (418, 202, 32, 22, "Live oversampling", nullptr, 4));
    v.push_back (stepper (473, 202, 32, 22, "Export oversampling", nullptr, 4));
    v.push_back (toggle (246, 246, 110, 16, "Porta", TR_NOOP ("Porta")));
    // round 172a: the FM / RM modulation matrix at FL's place (REA_rodada11 §1.5): rows = destinations (operators 1–6,
    // filters 1–3), columns = sources (operators 1–6), then PAN, FX and OUT; the FM / RM tabs switch the operator
    // columns' layer (PAN, FX and OUT are the FM layer's, shown on both)
    l.pages = { "FM", "RM" };
    v.push_back (section (553, 34, 294, 266, TR_NOOP ("Matrix")));
    static const char* const rows[9] = { "1", "2", "3", "4", "5", "6", "F1", "F2", "F3" };
    static const char* const columns[9] = { "1", "2", "3", "4", "5", "6", TR_NOOP ("Pan"), TR_NOOP ("FX"), TR_NOOP ("Out") };
    static const std::vector<std::string> cells = [] {
        std::vector<std::string> n; // the names of core/SytrusEngine.cpp's matrix settings
        for (int layer = 0; layer < 2; ++layer)
            for (int row = 0; row < 9; ++row)
                for (int column = 0; column < 9; ++column)
                {
                    const auto unit = row < 6 ? "op " + std::to_string (row + 1) : "filter " + std::to_string (row - 5);
                    auto upper = unit;
                    upper[0] = (char) std::toupper ((unsigned char) upper[0]);
                    static const char* const outs[3] = { " pan", " FX", " out" };
                    n.push_back (column < 6 ? std::string (layer == 0 ? "FM" : "RM") + " op " + std::to_string (column + 1) + " to " + unit
                                            : upper + outs[column - 6]);
                }
        return n;
    }();
    for (int c = 0; c < 9; ++c)
        v.push_back (label (576 + 29.0f * (float) c, 52, 28, 12, columns[c]));
    for (int r = 0; r < 9; ++r)
    {
        const auto cy = 78.0f + 25.0f * (float) r;
        v.push_back (label (557, cy - 6, 20, 12, rows[r]));
        for (int layer = 0; layer < 2; ++layer)
            for (int c = 0; c < 9; ++c)
            {
                if (layer == 1 && c >= 6)
                    continue;
                auto k = knob (590 + 29.0f * (float) c, cy, 22, cells[(std::size_t) (81 * layer + 9 * r + c)].c_str(), nullptr, 0, noCaption | bipolar);
                k.page = c >= 6 ? -1 : layer;
                v.push_back (k);
            }
    }
    v.push_back (pageTabs (553, 306, 90, 22, "FM|RM"));
    v.push_back (label (650, 300, 196, 42, "Sytrus", dim));
    return l;
}

Layout maximus()
{
    // FL Studio 2026 Maximus (measured on its window, body origin under the wrapper title): the selected band's
    // compression curve at the top-left and the bands view across the rest of the top; under them the band selector
    // (LOW / MID / HIGH / MASTER), the band's mode switch, PRE / POST gain, stereo separation, THRES / CEIL saturation
    // inset, the envelope (ATT, REL, SUSTAIN, the two CURVE steppers, PEAK / RMS, REL 2); LMH DEL / LMH MIX; the
    // crossover (LOW / HIGH frequency, 12 / 24 dB slopes) and LOW CUT. The values live in the slot's Maximus state
    // (core/MaximusFx.h stateParams: "Low mode", "Master pre gain", ...).
    Layout l;
    l.fl = "Maximus";
    l.width = 633;
    l.height = 391;
    l.pages = { TR_NOOP ("Low"), TR_NOOP ("Mid"), TR_NOOP ("High"), TR_NOOP ("Master") };
    auto& v = l.items;
    v.push_back (display (8, 4, 181, 181, "maxcurve"));
    v.push_back (display (192, 4, 433, 181, "maxbands"));
    v.push_back (section (2, 205, 248, 183, nullptr, boxed));
    v.push_back (section (252, 205, 176, 183, nullptr, boxed));
    v.push_back (section (430, 205, 67, 183, nullptr, boxed));
    v.push_back (section (499, 205, 132, 183, nullptr, boxed));
    auto bandSelector = pageTabs (4, 209, 70, 99, TR_NOOP ("Low|Mid|High|Master"));
    v.push_back (bandSelector);
    v.push_back (section (142, 294, 106, 74, nullptr, boxed | raised));

    static const char* bandNames[4] { "Low", "Mid", "High", "Master" };
    static std::vector<std::string> names; // "<Band> <control>" (core/MaximusFx.h), kept for the items' pointers
    if (names.empty())
    {
        static const char* controls[13] { "mode", "pre gain", "post gain", "stereo separation", "saturation threshold",
                                          "saturation ceiling", "attack", "release 2", "attack curve", "release",
                                          "release curve", "sustain", "envelope mode" };
        names.reserve (52);
        for (const auto* b : bandNames)
            for (const auto* c : controls)
                names.push_back (std::string (b) + " " + c);
    }
    for (int b = 0; b < 4; ++b)
    {
        auto n = [&] (int j) { return names[(std::size_t) (13 * b + j)].c_str(); };
        auto add = [&] (LayoutItem i) { i.colour = b; v.push_back (onPage (std::move (i), b)); };
        add (radio (81, 216, 64, 60, n (0), TR_NOOP ("On|Comp off|Muted|Off")));
        add (knob (169.5f, 243, 42, n (1), TR_NOOP ("Pre"), 272));
        add (knob (223, 243, 42, n (2), TR_NOOP ("Post"), 272));
        add (label (185, 266, 24, 13, TR_NOOP ("Gain"), boxed));
        add (knob (105.5f, 323, 38, n (3), TR_NOOP ("Sep"), 349, bipolar));
        add (knob (168.5f, 322, 36, n (4), TR_NOOP ("Thres"), 349, bipolar));
        add (knob (223, 322, 36, n (5), TR_NOOP ("Ceil"), 349));
        add (label (186, 343, 22, 13, TR_NOOP ("Sat"), boxed));
        add (knob (280.5f, 243, 42, n (6), TR_NOOP ("Att"), 272));
        add (knob (343.5f, 243, 42, n (9), TR_NOOP ("Rel"), 272));
        add (knob (395.5f, 243, 42, n (11), TR_NOOP ("Sustain"), 272));
        add (label (300, 266, 24, 13, TR_NOOP ("Env"), boxed));
        add (stepper (268, 292, 27, 22, n (8), nullptr, 7));
        add (label (296, 297, 34, 13, TR_NOOP ("Curve")));
        add (stepper (331, 292, 27, 22, n (10), nullptr, 7));
        add (radio (368, 292, 46, 30, n (12), TR_NOOP ("Peak|RMS")));
        add (knob (281, 353, 34, n (7), TR_NOOP ("Rel 2"), 377));
    }
    v.push_back (knob (463, 243, 42, "LMH lookahead delay", TR_NOOP ("LMH del"), 272));
    v.push_back (knob (463, 323, 42, "LMH mix", TR_NOOP ("LMH mix"), 349));
    v.push_back (knob (534, 243, 42, "Low band frequency", TR_NOOP ("Low"), 272));
    v.push_back (knob (593, 243, 42, "High band frequency", TR_NOOP ("High"), 272));
    v.push_back (label (551, 266, 25, 13, TR_NOOP ("Freq"), boxed));
    v.push_back (radio (503, 292, 42, 28, "Low band slope", "12dB|24dB"));
    v.push_back (radio (584, 292, 42, 28, "High band slope", "12dB|24dB"));
    v.push_back (knob (538, 349, 34, "Low cut", TR_NOOP ("Low cut"), 377));
    return l;
}

Layout vintagePhaser()
{
    // PHASING (MIN, MAX faders, HQ, MANUAL, FEEDBACK fader) | MODULATION (START PHASE, TYPE switch, L/R PHASE,
    // TENSION, SPEED fader with T) | DELAY (on LED; MIX, STEREO, big TIME with T; FEEDBACK, LP, HP, KEEP PITCH) |
    // LEVELS (NOISE, NOISE GATE, MONO INPUT, MIX fader). The engine's remaining controls (FL keeps them out of the
    // panel; where FL shows them is not confirmed) are on a second page.
    Layout l;
    l.fl = "Vintage Phaser";
    l.width = 754;
    l.height = 214;
    l.pages = { TR_NOOP ("Main"), TR_NOOP ("Advanced") };
    auto& v = l.items;
    v.push_back (pageTabs (13, 14, 120, 20, TR_NOOP ("Main|Advanced")));
    v.push_back (label (420, 6, 326, 34, "Vintage Phaser", dim));
    auto main = [&] (LayoutItem i) { v.push_back (onPage (std::move (i), 0)); };
    auto fader = [&] (float x, const char* param, const char* caption) {
        main (slider (Item::vslider, x - 10, 72, 20, 104, param, nullptr, noCaption));
        main (label (x - 24, 180, 48, 12, caption));
    };
    main (section (13, 45, 162, 165, TR_NOOP ("Phasing")));
    fader (38, "Min freq.", TR_NOOP ("Min"));
    fader (71, "Max freq.", TR_NOOP ("Max"));
    main (toggle (95, 132, 20, 12, "HQ", nullptr));
    main (label (88, 146, 34, 11, TR_NOOP ("HQ")));
    main (toggle (95, 164, 20, 12, "Manual", nullptr));
    main (label (82, 180, 46, 12, TR_NOOP ("Manual")));
    fader (143, "Feedback", TR_NOOP ("Feedback"));

    main (section (181, 45, 209, 165, TR_NOOP ("Modulation")));
    main (knob (220, 92, 30, "Start phase", TR_NOOP ("Start phase"), 124));
    main (radio (256, 76, 52, 30, "Sine", TR_NOOP ("Triangle|Sine")));
    main (label (252, 112, 50, 12, TR_NOOP ("Type")));
    main (knob (220, 157, 30, "L/R phase", TR_NOOP ("L/R phase"), 186));
    main (knob (281, 157, 30, "Tension", TR_NOOP ("Tension"), 186));
    fader (348, "LFO speed", TR_NOOP ("Speed"));
    main (toggle (364, 180, 14, 12, "Tempo sync", "T", boxed));

    main (section (396, 45, 217, 165, TR_NOOP ("Delay")));
    main (toggle (582, 49, 20, 10, "Delay", nullptr));
    main (knob (428, 92, 30, "Delay mix", TR_NOOP ("Mix"), 124));
    main (knob (472, 92, 30, "Delay stereo", TR_NOOP ("Stereo"), 124, bipolar));
    main (knob (548, 100, 44, "Delay time", TR_NOOP ("Time"), 136, big));
    main (toggle (576, 130, 14, 12, "Delay sync", "T", boxed));
    main (knob (428, 157, 30, "Delay fdbk", TR_NOOP ("Feedback"), 186));
    main (knob (472, 157, 30, "Delay LP", TR_NOOP ("LP"), 186));
    main (knob (527, 157, 30, "Delay HP", TR_NOOP ("HP"), 186));
    main (toggle (562, 164, 20, 12, "Keep pitch", nullptr));
    main (label (550, 180, 56, 12, TR_NOOP ("Keep pitch")));

    main (section (619, 45, 119, 165, TR_NOOP ("Levels")));
    main (knob (654, 90, 28, "Noise", TR_NOOP ("Noise"), 120));
    main (toggle (644, 134, 20, 12, "Gate", nullptr));
    main (label (626, 148, 56, 11, TR_NOOP ("Noise gate")));
    main (toggle (644, 164, 20, 12, "Mono input", nullptr));
    main (label (626, 180, 56, 12, TR_NOOP ("Mono input")));
    fader (708, "Mix", TR_NOOP ("Mix"));

    // second page: the engine's other controls
    auto adv = [&] (int k, const char* param, const char* caption, std::uint32_t style = 0) {
        const auto cx = 50.0f + 92.0f * (float) (k % 8), cy = 82.0f + 70.0f * (float) (k / 8);
        v.push_back (onPage (knob (cx, cy, 34, param, caption, cy + 26, style | showValue), 1));
    };
    v.push_back (onPage (section (13, 45, 725, 165, nullptr, boxed), 1));
    adv (0, "Manual freq.", TR_NOOP ("Manual freq."));
    adv (1, "Rate 1", TR_NOOP ("Rate 1"));
    adv (2, "Rate 2", TR_NOOP ("Rate 2"));
    adv (3, "Depth scale", TR_NOOP ("Depth scale"));
    adv (4, "Pull-down", TR_NOOP ("Pull-down"));
    adv (5, "Drive", TR_NOOP ("Drive"), bipolar);
    adv (6, "Colour gain", TR_NOOP ("Colour gain"));
    adv (7, "Colour freq.", TR_NOOP ("Colour freq."));
    adv (8, "Input HP", TR_NOOP ("Input HP"));
    adv (9, "Fdbk HP", TR_NOOP ("Feedback HP"));
    v.push_back (onPage (toggle (230, 140, 110, 14, "Engine V2", TR_NOOP ("Engine V2")), 1));
    v.push_back (onPage (toggle (230, 166, 110, 14, "Invert fdbk", TR_NOOP ("Invert feedback")), 1));
    return l;
}

Layout soundgoodizer()
{
    // one big Amount knob and the four presets A–D (the slot's state, core/MaximusFx.h)
    Layout l;
    l.fl = "Soundgoodizer";
    l.width = 215;
    l.height = 300;
    auto& v = l.items;
    v.push_back (label (10, 6, 195, 36, "Soundgoodizer", dim));
    v.push_back (display (28, 64, 152, 152, "sgamount"));
    v.push_back (display (14, 228, 186, 50, "sgpresets"));
    return l;
}

#include "core/PluginLayoutFx.inc"

// --- generated layouts ------------------------------------------------------------------------------

// --- LYNX originals: the mixing suite (REA round 96). Not FL arrangements: LYNX's own panels, laid out with the
// same conventions (titled sections, knobs with captions and read-outs, the graph on top, switches as tabs). -------

// a knob with its caption and its value under it (`d` wide, centred at cx, cy)
LayoutItem dial (float cx, float cy, float d, const char* param, const char* caption, std::uint32_t style = 0)
{
    return knob (cx, cy, d, param, caption, cy + d * 0.5f + 20.0f, (std::uint32_t) (style | showValue));
}

// round tp172d (look slice): a hardware original's knob — the caption above, no value text (the drag tip shows it);
// its printed range words go under it as labels
LayoutItem hwKnob (float cx, float cy, float d, const char* param, const char* caption, std::uint32_t style = 0)
{
    auto k = knob (cx, cy, d, param, caption, cy + d * 0.5f + 7.0f, (std::uint32_t) (style | captionAbove));
    k.y -= 14.0f;
    k.h += 14.0f;
    return k;
}

// the two range words under a hardware knob, at its left and right
std::array<LayoutItem, 2> rangeWords (float cx, float y, float d, const char* low, const char* high)
{
    return { label (cx - d * 0.5f - 22.0f, y, 44.0f, 12.0f, low, dim), label (cx + d * 0.5f - 22.0f, y, 44.0f, 12.0f, high, dim) };
}

// a live meter of the effect's control output `output` (0 = gain reduction, 1 = second gain reduction)
LayoutItem meter (float x, float y, float w, float h, int output, const char* caption)
{
    auto i = display (x, y, w, h, "grmeter");
    i.steps = output;
    i.label = caption;
    return i;
}

Layout ridgeStrip()
{
    // round 114: the classic console channel strip's layout (the role: Waves SSL E-Channel, measured on the official
    // trial): two columns read top to bottom. Left: FILTERS, then the four EQ bands HF, HMF, LMF, LF (gain on the
    // left, frequency in the middle, Q / bell on the right), the EQ curve under them. Right: DYNAMICS (the
    // compressor, then the expander / gate, their gain-reduction meters), the routing (EQ and dynamics in, order,
    // filters to the side chain), and the GAIN block (input, phase, output) at the bottom.
    // round tp172c: three columns like the reference console channel strip — the filters and EQ bands on the left
    // (HF at the top down to LF), the input / output block in the middle (the EQ curve over the I/O meters, input trim
    // and phase, the output fader between the meters), the dynamics on the right (compressor, expander / gate, their
    // LED ladders and the routing)
    Layout l;
    l.width = 780;
    l.height = 716;
    auto& v = l.items;
    // --- left column: filters and EQ
    v.push_back (section (4, 4, 276, 104, TR_NOOP ("Filters")));
    v.push_back (dial (54, 50, 36, "HPF", TR_NOOP ("High-pass")));
    v.push_back (dial (140, 50, 36, "LPF", TR_NOOP ("Low-pass")));
    // round 165: the old "filters to the side chain" switch only shows while it is on (older projects); the side chain
    // now takes the filters and the EQ together ("EQ to SC", the Dynamics section), as on the reference
    v.push_back (when (toggle (186, 40, 88, 20, "Filters to SC", TR_NOOP ("To sidechain")), "Filters to SC", 0.5f, 1.0f));
    // round 123: the E voicing's filter split (filters before the dynamics); round 165: only where it acts
    v.push_back (when (when (toggle (186, 66, 88, 20, "Filter split", TR_NOOP ("Split")), "4K voicing", 0.0f, 0.5f), "Voicing", 0.5f, 1.0f));
    const char* names[] = { TR_NOOP ("HF"), TR_NOOP ("HMF"), TR_NOOP ("LMF"), TR_NOOP ("LF") };
    const char* gains[] = { "HF gain", "HMF gain", "LMF gain", "LF gain" };
    const char* freqs[] = { "HF freq.", "HMF freq.", "LMF freq.", "LF freq." };
    const char* extra[] = { "HF bell", "HMF Q", "LMF Q", "LF bell" };
    for (int b = 0; b < 4; ++b)
    {
        const auto y = 112.0f + 102.0f * (float) b;
        v.push_back (section (4, y, 276, 98, names[b]));
        v.push_back (dial (54, y + 44.0f, 36, gains[b], TR_NOOP ("Gain"), bipolar));
        v.push_back (dial (140, y + 44.0f, 36, freqs[b], TR_NOOP ("Freq")));
        if (b == 1 || b == 2)
            v.push_back (dial (226, y + 44.0f, 30, extra[b], TR_NOOP ("Q")));
        else
            v.push_back (toggle (194, y + 34.0f, 72, 20, extra[b], TR_NOOP ("Bell")));
    }
    v.push_back (section (4, 520, 276, 124, TR_NOOP ("Equalizer")));
    // round 165: the Brown / Black curves are Ridge's own engine's (the E console voicing has the console's one curve)
    // (round tp172c: two conditions at most — the 4K and Modern voicings; in the E voicing the curve has no effect)
    v.push_back (when (when (tabs (10, 548, 150, 20, "EQ curve", TR_NOOP ("Brown|Black")), "4K voicing", 0.0f, 0.5f), "Modern voicing", 0.0f, 0.5f));
    // round tp172c: the Modern voicing (the console measured on the SSL Native Channel Strip 2 trial) — the curve picks its
    // E / G EQ type; its compressor's peak detector, the gate's hold and the make-up offset join the dynamics
    v.push_back (when (tabs (10, 548, 150, 20, "EQ curve", TR_NOOP ("E type|G type")), "Modern voicing", 0.5f, 1.0f));
    v.push_back (toggle (170, 620, 104, 20, "Modern voicing", TR_NOOP ("Modern console")));
    v.push_back (when (dial (652, 52, 24, "Make-up offset", TR_NOOP ("Offset"), bipolar), "Modern voicing", 0.5f, 1.0f));
    v.push_back (when (toggle (516, 100, 64, 20, "Peak detect", TR_NOOP ("Peak")), "Modern voicing", 0.5f, 1.0f));
    v.push_back (when (dial (722, 372, 20, "Gate hold", TR_NOOP ("Hold")), "Modern voicing", 0.5f, 1.0f));
    // round 148: the 4K voicing (the console measured on the SSL 4K E trial) — its three EQ colours, the mic pre and
    // its gain, the make-up offset; the rest of the panel drives it with the console's own scales
    v.push_back (when (when (tabs (10, 548, 150, 20, "EQ colour", TR_NOOP ("Brown|Black|Orange")), "4K voicing", 0.5f, 1.0f), "Modern voicing", 0.0f, 0.5f));
    v.push_back (toggle (170, 584, 104, 20, "4K voicing", TR_NOOP ("4K console")));
    v.push_back (when (when (dial (652, 52, 24, "Make-up offset", TR_NOOP ("Offset"), bipolar), "4K voicing", 0.5f, 1.0f), "Modern voicing", 0.0f, 0.5f));
    v.push_back (toggle (170, 548, 104, 20, "EQ on", TR_NOOP ("EQ in")));
    // round 123: the voicing — Ridge's own engine, or the measured E-type console (substitutes start there)
    v.push_back (tabs (10, 584, 150, 20, "Voicing", TR_NOOP ("Classic|E console")));
    // round tp172c6: the DRIVE section under the EQ — in, the model (measured saturators, consoles, tube, amps), its
    // drive, the level into it, the trim after it and the mix
    v.push_back (section (4, 648, 276, 64, TR_NOOP ("Drive")));
    v.push_back (toggle (10, 666, 50, 18, "Drive in", TR_NOOP ("In")));
    {
        LayoutItem m;
        m.type = Item::menu;
        m.x = 10, m.y = 688, m.w = 96, m.h = 18;
        m.param = "Drive model";
        m.options = TR_NOOP ("Saturate|Console E|Console G|Console A|Console N|Console Y|Tube|Guitar amp|Bass amp");
        for (int k = 0; k < mixsuite::ridge::driveModels; ++k)
            m.values.push_back (mixsuite::ridge::driveModelValue (k));
        v.push_back (m);
    }
    v.push_back (dial (130, 676, 22, "Drive amount", TR_NOOP ("Drive")));
    v.push_back (dial (174, 676, 20, "Drive input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (216, 676, 20, "Drive trim", TR_NOOP ("Trim"), bipolar));
    v.push_back (dial (256, 676, 20, "Drive mix", TR_NOOP ("Mix")));

    // --- right column: dynamics, routing
    v.push_back (section (504, 4, 272, 236, TR_NOOP ("Compressor")));
    v.push_back (display (510, 24, 120, 70, "ridgecomp"));
    v.push_back (dial (700, 62, 46, "Comp. threshold", TR_NOOP ("Threshold"), big));
    v.push_back (dial (550, 150, 32, "Ratio", TR_NOOP ("Ratio")));
    v.push_back (dial (630, 150, 32, "Comp. release", TR_NOOP ("Release")));
    v.push_back (dial (710, 150, 28, "Comp. mix", TR_NOOP ("Mix")));
    v.push_back (toggle (516, 206, 110, 20, "Fast attack", TR_NOOP ("Fast attack")));
    v.push_back (toggle (634, 206, 136, 20, "Auto make-up", TR_NOOP ("Auto make-up"))); // round 147b: the console's
    v.push_back (section (504, 244, 272, 196, TR_NOOP ("Expander / gate")));
    v.push_back (dial (700, 296, 46, "Gate threshold", TR_NOOP ("Threshold"), big));
    v.push_back (dial (550, 290, 32, "Gate range", TR_NOOP ("Range")));
    v.push_back (dial (550, 370, 32, "Gate release", TR_NOOP ("Release")));
    v.push_back (toggle (616, 380, 70, 20, "Gate fast", TR_NOOP ("Fast")));
    v.push_back (tabs (616, 408, 154, 20, "Expander", TR_NOOP ("Gate|Expand")));
    // round 165: the reference's two LED ladders (compressor, expander / gate: 3 … 20 dB of reduction) on the left, the
    // routing on the right — the dynamics in, where they sit (before / after the EQ), the EQ into their side chain
    v.push_back (section (504, 444, 272, 268, TR_NOOP ("Dynamics")));
    {
        auto comp = display (512, 462, 30, 236, "grleds");
        comp.steps = 0;
        comp.label = TR_NOOP ("Comp");
        v.push_back (comp);
        auto gate = display (546, 462, 30, 236, "grleds");
        gate.steps = 1;
        gate.label = TR_NOOP ("Exp");
        v.push_back (gate);
    }
    v.push_back (toggle (586, 464, 84, 20, "Dynamics on", TR_NOOP ("Dyn in")));
    v.push_back (toggle (676, 464, 92, 20, "Analog", TR_NOOP ("Analog"))); // round 147b: the console emulation's colour
    v.push_back (tabs (586, 494, 182, 20, "Order", TR_NOOP ("EQ>Dyn|Dyn>EQ")));
    v.push_back (when (when (toggle (586, 524, 182, 20, "EQ to SC", TR_NOOP ("EQ to side chain")), "4K voicing", 0.0f, 0.5f), "Modern voicing", 0.0f, 0.5f));
    // --- centre: the EQ curve over the input / output block (round tp172c: the reference's middle module)
    v.push_back (section (284, 4, 216, 168, TR_NOOP ("Curve")));
    v.push_back (display (290, 26, 204, 140, "ridgeeq"));
    // the GAIN block (round 129, the reference's): input trim and phase (and the 4K's mic pre), the output fader between
    // the input / output level meters with their peak read-outs
    v.push_back (section (284, 176, 216, 536, TR_NOOP ("Gain")));
    v.push_back (dial (330, 224, 36, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (toggle (400, 200, 90, 20, "Phase", TR_NOOP ("Phase")));
    v.push_back (when (toggle (400, 226, 90, 20, "Mic pre", TR_NOOP ("Mic pre")), "4K voicing", 0.5f, 1.0f));
    v.push_back (when (dial (444, 258, 24, "Mic gain", TR_NOOP ("Mic")), "4K voicing", 0.5f, 1.0f));
    v.push_back (display (294, 300, 150, 404, "iometer"));
    v.push_back (slider (Item::vslider, 452, 310, 40, 390, "Output", TR_NOOP ("Output")));
    return l;
}

Layout compressorLayout()
{
    // round 129: LYNX's basic compressor in the role of the Waves Renaissance Compressor (docs/PARITY.md; the official
    // trial's panel): the threshold fader beside the input / output meters, the gain-reduction meter in the middle with
    // the ratio under it, the make-up gain fader on the right, attack and release along the bottom
    Layout l;
    l.width = 420;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (4, 4, 412, 292, TR_NOOP ("Compressor")));
    v.push_back (slider (Item::vslider, 16, 30, 34, 172, "Threshold", TR_NOOP ("Threshold")));
    v.push_back (display (58, 26, 112, 180, "iometer"));
    // round 152: RComp's bipolar gain-change meter (reduction down, the expansion's boost up), the held change under it
    v.push_back (display (184, 26, 66, 180, "gcmeter"));
    // round 150: with the RComp ranges the RC controls take the places of LYNX's (gain −30 … +30, ratio 0.5 … 50 : 1,
    // attack 0.5 … 500 ms, release 5 … 5000 ms)
    v.push_back (when (slider (Item::vslider, 268, 30, 34, 172, "Makeup", TR_NOOP ("Gain")), "Ranges", 0.0f, 0.5f));
    v.push_back (when (slider (Item::vslider, 268, 30, 34, 172, "RC gain", TR_NOOP ("Gain")), "Ranges", 0.5f, 1.0f));
    v.push_back (toggle (314, 30, 94, 20, "Auto gain", TR_NOOP ("Auto gain")));
    // round 123: the voicing — LYNX's classic compressor, or the measured program-dependent one (substitutes start there)
    v.push_back (tabs (314, 60, 94, 40, "Voicing", TR_NOOP ("Classic|Vintage")));
    // round 135w: the reference's ARC / Manual, Electro / Opto and Warm / Smooth switches (the vintage voicing's), and
    // its output limiter with the activity pill
    v.push_back (when (tabs (314, 108, 94, 20, "Release mode", TR_NOOP ("ARC|Manual")), "Voicing", 0.5f, 1.0f));
    // round 190ar: the classic voicing's law (clean, or the FET-style knee and program-dependent release)
    v.push_back (when (tabs (314, 108, 94, 20, "Classic law", TR_NOOP ("Clean|FET")), "Voicing", 0.0f, 0.5f));
    v.push_back (when (tabs (314, 132, 94, 20, "Release curve", TR_NOOP ("Electro|Opto")), "Voicing", 0.5f, 1.0f));
    v.push_back (when (tabs (314, 156, 94, 20, "Character", TR_NOOP ("Warm|Smooth")), "Voicing", 0.5f, 1.0f));
    v.push_back (toggle (314, 182, 94, 20, "Limiter", TR_NOOP ("Limiter")));
    v.push_back (display (330, 206, 62, 12, "limitpill"));
    v.push_back (when (dial (60, 252, 34, "Attack", TR_NOOP ("Attack")), "Ranges", 0.0f, 0.5f));
    v.push_back (when (dial (140, 252, 34, "Release", TR_NOOP ("Release")), "Ranges", 0.0f, 0.5f));
    v.push_back (when (dial (217, 252, 34, "Ratio", TR_NOOP ("Ratio")), "Ranges", 0.0f, 0.5f));
    v.push_back (when (dial (60, 252, 34, "RC attack", TR_NOOP ("Attack")), "Ranges", 0.5f, 1.0f));
    v.push_back (when (dial (140, 252, 34, "RC release", TR_NOOP ("Release")), "Ranges", 0.5f, 1.0f));
    v.push_back (when (dial (217, 252, 34, "RC ratio", TR_NOOP ("Ratio")), "Ranges", 0.5f, 1.0f));
    v.push_back (tabs (318, 7, 90, 16, "Ranges", TR_NOOP ("LYNX|RComp")));
    // round 152: the vintage voicing's static curve — measured at every ratio (new instances) or round 150's (older projects)
    v.push_back (when (tabs (12, 7, 104, 16, "Vintage curve", TR_NOOP ("Old|Measured")), "Voicing", 0.5f, 1.0f));
    v.push_back (dial (300, 252, 30, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (370, 252, 30, "Trim", TR_NOOP ("Trim"), bipolar));
    return l;
}

Layout glowLeveler()
{
    // round 114: the optical levelers' classic front panel (the role: Waves CLA-2A / LA-2A): GAIN on the left, PEAK
    // REDUCTION on the right, the VU meter between them, the COMPRESS / LIMIT switch under the meter.
    // Round 129 (docs/PARITY.md, the official trial's panel): the VU DISPLAY selector (input / GR / output) top right,
    // AUTO MAKEUP under it, and the bottom strip of the reference — HF emphasis (its HI FREQ), MIX, TRIM.
    // Round tp172d (slice 8): the reference's proportions and hardware — a wide grey faceplate, COMPRESS / LIMIT and
    // AUTO MAKEUP as metal bat toggles with their words printed above and under them, the VU DISPLAY selector as a black
    // pointer knob with INPUT / GR / OUTPUT round it, the model name in big type under the meter, and a charcoal strip
    // with gold keys (50Hz / 60Hz / OFF) and big ANALOG / HI FREQ / MIX / TRIM captions. LYNX's own extras (the voicing,
    // the classic opto law and release, the make-up reference) stay as small keys where the original has its logo.
    Layout l;
    l.width = 660;
    l.height = 312;
    auto& v = l.items;
    v.push_back (section (4, 4, 652, 304, "")); // (slice 8: one faceplate, no title, as on the original)
    // the reference's charcoal lower strip (the theme fills this second section)
    v.push_back (section (4, 236, 652, 72, ""));
    // round 123: the voicing — Glow's own engine, or the measured classic opto leveler (substitutes start there);
    // round 152 / 165w: the classic voicing's opto law and release (Old = a project saved before that round)
    // (review: one compact group of small muted keys where the original has only its badge)
    v.push_back (tabs (14, 18, 112, 14, "Voicing", TR_NOOP ("Modern|Opto classic"), {}, compactKeys));
    v.push_back (when (tabs (14, 36, 112, 14, "Opto law", TR_NOOP ("Old|Measured"), {}, compactKeys), "Voicing", 0.5f, 1.0f));
    v.push_back (when (label (14, 54, 112, 9, TR_NOOP ("Attack / release"), dim), "Voicing", 0.5f, 1.0f)); // round 165x: both measured
    v.push_back (when (tabs (14, 64, 112, 14, "Opto release", TR_NOOP ("Old|Measured"), {}, compactKeys), "Voicing", 0.5f, 1.0f));
    // round 190dm: the cell's memory (release slower after long exposure; Compress)
    v.push_back (when (label (14, 82, 112, 9, TR_NOOP ("Cell memory"), dim), "Voicing", 0.5f, 1.0f));
    v.push_back (when (tabs (14, 92, 112, 14, "Opto memory", TR_NOOP ("Off|On"), {}, compactKeys), "Voicing", 0.5f, 1.0f));
    // COMPRESS / LIMIT: a bat toggle (up = compress), its two words above and under it
    v.push_back (tabs (40, 136, 52, 84, "Limit", TR_NOOP ("Compress|Limit"), {}, batToggle));
    // the two big knobs, their printed 0 … 100 round them; no value text (the drag tip shows it)
    v.push_back (knob (180, 119, 88, "Gain", TR_NOOP ("Gain"), 192, big));
    v.push_back (knob (482, 119, 88, "Peak reduction", TR_NOOP ("Peak reduction"), 192, big));
    auto vu = display (245, 54, 174, 115, "vumeter");
    vu.steps = 0; // live output 0: gain reduction
    v.push_back (vu);
    v.push_back (label (262, 176, 140, 34, "GLOW", bigCaption | big)); // the model name under the meter (LYNX's own)
    // round 165x: the VU DISPLAY selector is a parameter (saved with the project, translated from the reference's state)
    v.push_back (label (540, 26, 100, 12, TR_NOOP ("VU display")));
    v.push_back (tabs (540, 42, 100, 68, "VU display", TR_NOOP ("Input|GR|Output"), { 0.5f, 0.0f, 1.0f }, rotarySelector));
    v.push_back (toggle (564, 128, 60, 88, "Auto makeup", TR_NOOP ("Auto makeup"), batToggle));
    // the strip: round 152's ANALOG keys (hum and noise, measured), HF emphasis (the reference's HI FREQ), MIX, TRIM
    v.push_back (label (56, 260, 64, 24, TR_NOOP ("Analog"), bigCaption));
    v.push_back (tabs (124, 263, 112, 20, "Analog", TR_NOOP ("50Hz|60Hz|Off"), { 0.5f, 1.0f, 0.0f }));
    // (review: the original's sizes — HI FREQ a big cream pointer knob, MIX and TRIM medium knurled knobs)
    v.push_back (label (244, 260, 68, 24, TR_NOOP ("Hi freq"), bigCaption));
    v.push_back (knob (336, 272, 42, "Emphasis", TR_NOOP ("HF emphasis"), 0, noCaption));
    v.push_back (label (362, 266, 46, 12, TR_NOOP ("[ Flat ]")));
    v.push_back (label (406, 260, 44, 24, TR_NOOP ("Mix"), bigCaption));
    v.push_back (knob (474, 270, 46, "Mix", TR_NOOP ("Mix"), 0, noCaption));
    v.push_back (label (438, 294, 24, 10, "0")); // the printed scale under the knob
    v.push_back (label (486, 294, 28, 10, "100"));
    v.push_back (label (498, 260, 52, 24, TR_NOOP ("Trim"), bigCaption));
    v.push_back (knob (574, 270, 46, "Trim", TR_NOOP ("Trim"), 0, (std::uint32_t) (bipolar | noCaption)));
    v.push_back (label (536, 294, 24, 10, "-18"));
    v.push_back (label (588, 294, 24, 10, "18"));
    // round 165x: where Auto makeup was switched on (the reference's make-up follows the dial from there) — LYNX's own
    v.push_back (when (knob (634, 264, 22, "Makeup ref", TR_NOOP ("Makeup ref"), 290), "Voicing", 0.5f, 1.0f));
    return l;
}

Layout hushDeEsser()
{
    Layout l;
    l.width = 460;
    l.height = 432; // round 165x: + the extended law's row; round 190dm: + Air / Mix
    auto& v = l.items;
    v.push_back (section (4, 4, 452, 424, TR_NOOP ("De-esser")));
    v.push_back (display (12, 26, 200, 78, "hushband"));
    v.push_back (meter (12, 110, 200, 12, 0, TR_NOOP ("Reduction")));
    v.push_back (tabs (12, 130, 98, 20, "Wideband", TR_NOOP ("Split|Wide")));
    v.push_back (tabs (114, 130, 98, 20, "Band detector", TR_NOOP ("High|Band")));
    v.push_back (toggle (12, 162, 84, 20, "Listen", TR_NOOP ("Listen")));
    v.push_back (toggle (100, 162, 112, 20, "Lookahead", TR_NOOP ("Lookahead")));
    // round 123: the voicing — Hush's own engine, or the measured classic split de-esser (substitutes start there)
    v.push_back (tabs (12, 192, 200, 20, "Voicing", TR_NOOP ("Modern|Classic split")));
    v.push_back (dial (262, 58, 46, "Frequency", TR_NOOP ("Frequency"), big));
    v.push_back (dial (338, 58, 46, "Threshold", TR_NOOP ("Threshold"), big));
    v.push_back (dial (412, 58, 40, "Range", TR_NOOP ("Range")));
    v.push_back (dial (300, 142, 32, "Release", TR_NOOP ("Release")));
    v.push_back (dial (376, 142, 32, "Output", TR_NOOP ("Output"), bipolar));
    // round 165w: the reference's input / output pan (balance)
    v.push_back (dial (240, 142, 28, "Input pan", TR_NOOP ("In pan"), bipolar));
    v.push_back (dial (430, 142, 28, "Output pan", TR_NOOP ("Out pan"), bipolar));
    // round 128: the reference de-esser's other controls
    v.push_back (tabs (12, 222, 200, 20, "Allround", TR_NOOP ("Single vocal|Allround")));
    v.push_back (toggle (12, 252, 64, 20, "Link mid", TR_NOOP ("Mid (M/S)")));
    v.push_back (toggle (80, 252, 64, 20, "External SC", TR_NOOP ("Ext")));
    v.push_back (toggle (148, 252, 64, 20, "Display", TR_NOOP ("Display")));
    v.push_back (dial (250, 228, 30, "Low-pass freq.", TR_NOOP ("Low pass")));
    v.push_back (dial (310, 228, 30, "Lookahead time", TR_NOOP ("Lookahead")));
    v.push_back (dial (370, 228, 30, "Stereo link", TR_NOOP ("Stereo link")));
    v.push_back (dial (430, 228, 30, "Input", TR_NOOP ("Input"), bipolar));
    // round 135: oversampling (Hush's own voicing)
    v.push_back (label (12, 286, 90, 20, TR_NOOP ("Oversampling")));
    v.push_back (tabs (104, 286, 176, 20, "Oversampling", TR_NOOP ("Off|2x|4x|8x")));
    // round 148: the measured single-vocal detector (the role: a vocal de-esser's Single Vocal mode)
    v.push_back (toggle (290, 286, 158, 20, "Vocal voicing", TR_NOOP ("Vocal detector")));
    // round 165x: the reference's ranges and link (Old = a project saved before: ±12 dB, 2 … 16 kHz, Hush's link) and
    // the link's 100 … 200 % half (the share processed on the mid or side signal only)
    v.push_back (tabs (12, 318, 200, 20, "Range law", TR_NOOP ("Old ranges|Extended")));
    v.push_back (tabs (222, 318, 110, 20, "Link only side", TR_NOOP ("Mid only|Side only")));
    v.push_back (dial (380, 330, 24, "Link only", TR_NOOP ("Link only")));
    // round 190dm: Air (a high shelf on the processed signal) and Mix (dry / processed)
    v.push_back (dial (290, 390, 24, "Air", TR_NOOP ("Air")));
    v.push_back (dial (380, 390, 24, "Mix", TR_NOOP ("Mix")));
    return l;
}

Layout aetherLayout()
{
    // round 114: the "air" processors' front panel (the role: Slate Digital Fresh Air), from the official product page:
    // two big knobs side by side — the presence / exciter one (MID) on the left, the high shelf (HIGH) on the right —
    // and the output trim between them; LYNX's extra settings (frequencies, character, mix) small under each
    Layout l;
    l.width = 530;
    l.height = 270;
    auto& v = l.items;
    v.push_back (section (4, 4, 432, 262, TR_NOOP ("Air")));
    // round 124: the dynamic air model (Dynamic on) has its own two amounts
    v.push_back (section (440, 4, 86, 262, TR_NOOP ("Dynamic")));
    v.push_back (toggle (446, 26, 74, 24, "Dynamic", TR_NOOP ("Dynamic")));
    v.push_back (dial (483, 100, 30, "Mid air", TR_NOOP ("Mid air")));
    v.push_back (dial (483, 186, 30, "High air", TR_NOOP ("High air")));
    v.push_back (dial (104, 96, 104, "Harmonics", TR_NOOP ("Mid air"), big));
    v.push_back (dial (336, 96, 104, "Air", TR_NOOP ("High air"), big));
    v.push_back (dial (220, 120, 40, "Output", TR_NOOP ("Trim"), bipolar));
    v.push_back (dial (66, 222, 26, "Exciter freq.", TR_NOOP ("Freq")));
    v.push_back (dial (142, 222, 26, "Character", TR_NOOP ("Even / odd"), bipolar));
    v.push_back (dial (220, 222, 26, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (336, 222, 26, "Air freq.", TR_NOOP ("Freq")));
    return l;
}

// round 136: Ember's Classic voicing (core/EmberClassic.h) — with Voicing on, Ember's own controls hide and the
// Classic ones (the reference's laws and texts) show; added around emberLayout() so that layout stays as it is.
// Round 165 (REA_rodada165ha §3): the reference's one-screen arrangement in LYNX's widgets — Punish top left, the VU
// (output level) top centre, Output with its Auto link top right, the big Drive left, Low cut (+ Thump), Tone,
// High cut (+ Steep) and Mix in a row, the five styles one click each along the bottom
Layout withEmberClassic (Layout l)
{
    // every item of Ember's own page hides in Classic (round 165: also the band rows, which carry their own condition)
    for (auto& it : l.items)
        if (it.whenParam == nullptr || it.when2Param == nullptr)
            it = when (it, "Voicing", 0.0f, 0.5f);
    auto add = [&l] (LayoutItem it) { l.items.push_back (when (it, "Voicing", 0.5f, 1.0f)); };
    // round tp172d: the reference's hardware panel, wider than tall (its proportions): Punish top left, the VU in the
    // middle with the name right of it, Output + Auto under the name, the big Drive left, Low cut (Thump under it),
    // Tone, High cut (Steep under it) and Mix in one row, the five style buttons along the bottom; "Advanced" opens
    // Ember's own multiband page (the Voicing switch, lit while it is off)
    l.width = 760; // (Ember's own page stays 520 wide at the left)
    add (section (34, 8, 692, 364, nullptr, boxed)); // (inside the wooden cheeks)
    add (toggle (56, 46, 132, 30, "Classic punish", TR_NOOP ("Punish")));
    auto vu = display (262, 16, 236, 140, "vumeter");
    vu.steps = 0;
    add (vu);
    add (label (506, 38, 214, 40, "EMBER", dim | alignLeft)); // (tp172d: the Ember theme sets it between two rules)
    // round tp172d (look slice): captions above, the printed range words under, no value text (the drag tip shows it)
    add (hwKnob (582, 132, 54, "Classic output", TR_NOOP ("Output")));
    for (auto& w : rangeWords (582, 162, 54, "-24 dB", "0 dB")) add (w);
    add (toggle (630, 120, 90, 22, "Classic auto", TR_NOOP ("Auto")));
    add (hwKnob (118, 222, 112, "Classic drive", TR_NOOP ("Drive"), big));
    add (hwKnob (300, 238, 58, "Classic low cut", TR_NOOP ("Low cut")));
    for (auto& w : rangeWords (300, 270, 58, "20 Hz", "1 kHz")) add (w);
    add (hwKnob (424, 238, 58, "Classic tone", TR_NOOP ("Tone"), bipolar));
    for (auto& w : rangeWords (424, 270, 58, TR_NOOP ("Dark"), TR_NOOP ("Bright"))) add (w);
    add (hwKnob (548, 238, 58, "Classic high cut", TR_NOOP ("High cut")));
    for (auto& w : rangeWords (548, 270, 58, "1 kHz", "20 kHz")) add (w);
    add (hwKnob (672, 238, 58, "Classic mix", TR_NOOP ("Mix")));
    for (auto& w : rangeWords (672, 270, 58, TR_NOOP ("Dry"), TR_NOOP ("Wet"))) add (w);
    add (toggle (264, 302, 80, 22, "Classic thump", TR_NOOP ("Thump")));
    add (toggle (512, 302, 80, 22, "Classic steep", TR_NOOP ("Steep")));
    add (tabs (196, 330, 330, 34, "Classic style", TR_NOOP ("A|E|N|T|P")));
    add (label (536, 330, 80, 34, TR_NOOP ("Style"), alignLeft));
    // projects saved before round 165 keep round 136's Punish until switched to the measured law
    add (when (tabs (40, 336, 150, 22, "Classic law", TR_NOOP ("Old|Measured")), "Classic law", 0.0f, 0.5f));
    // … and those saved before round 190ar keep round trial187b's high-drive tables until switched (the row above it)
    add (when (tabs (40, 310, 150, 22, "Classic hot law", TR_NOOP ("Old|Measured")), "Classic hot law", 0.0f, 0.5f));
    add (toggle (604, 336, 116, 24, "Voicing", TR_NOOP ("Advanced"), inverted));
    return l;
}

// round tp172c7: Kiln Channel, LYNX's console channel — the compact dark single-channel console of its role (the
// classic console-emulation channel): the main input level and its meter in the left column, the main output on the
// right; the centre module carries the VU (output), the name plate, Input / Output, the console selector with the six
// models printed round it, Drive and Group, and the Noise reduction / Group bypass keys
Layout kilnLayout()
{
    Layout l;
    l.width = 460;
    l.height = 640;
    auto& v = l.items;
    v.push_back (section (6, 6, 92, 628, "", boxed));
    v.push_back (section (104, 6, 252, 628, "", boxed));
    v.push_back (section (362, 6, 92, 628, "", boxed));
    auto inMeter = display (14, 24, 76, 420, "iometer");
    inMeter.colour = 0; // the input bar alone
    v.push_back (inMeter);
    auto outMeter = display (370, 24, 76, 420, "iometer");
    outMeter.colour = 1; // the output bar alone
    v.push_back (outMeter);
    v.push_back (dial (52, 530, 50, "Main in", TR_NOOP ("In"), bipolar));
    v.push_back (dial (408, 530, 50, "Main out", TR_NOOP ("Out"), bipolar));
    auto vu = display (126, 40, 208, 116, "vumeter");
    vu.steps = 0;
    v.push_back (vu);
    v.push_back (label (104, 166, 252, 20, TR_NOOP ("Kiln Channel"), boxed));
    v.push_back (dial (166, 230, 46, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (294, 230, 46, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (toggle (204, 204, 52, 18, "Group I/O", TR_NOOP ("Group I/O")));
    v.push_back (toggle (204, 240, 52, 18, "Link I/O", TR_NOOP ("Link I/O")));
    // the console selector: six stops 54 degrees apart over the knob's 270-degree throw, the model printed at each stop
    const float cx = 230.0f, cy = 360.0f, rr = 66.0f;
    v.push_back (knob (cx, cy, 64, "Console", nullptr, cy + 40.0f, noCaption | big, 6));
    static const char* models[6] { TR_NOOP ("British E"), TR_NOOP ("British G"), TR_NOOP ("American A"), TR_NOOP ("British N"), TR_NOOP ("Y"), TR_NOOP ("Tube") };
    for (int k = 0; k < 6; ++k)
    {
        const auto a = (-135.0f + 54.0f * (float) k) * 3.14159265f / 180.0f;
        const auto px = cx + rr * std::sin (a), py = cy - rr * std::cos (a);
        const auto w = 66.0f;
        const auto x = std::sin (a) < -0.3f ? px - w + 8.0f : std::sin (a) > 0.3f ? px - 8.0f : px - w * 0.5f;
        v.push_back (label (x, py - 7.0f, w, 14, models[k], (std::uint32_t) (std::sin (a) > 0.3f ? alignLeft : 0)));
    }
    v.push_back (dial (166, 488, 50, "Drive", TR_NOOP ("Drive")));
    v.push_back (knob (294, 488, 50, "Group", TR_NOOP ("Group"), 488 + 45.0f, showValue, 9));
    v.push_back (toggle (116, 576, 108, 26, "Noise reduction", TR_NOOP ("Noise reduction")));
    v.push_back (toggle (236, 576, 108, 26, "Group bypass", TR_NOOP ("Group bypass")));
    return l;
}

// round tp172c8: Fret Channel, LYNX's guitar / bass channel strip (its role: the guitar channel strip measured on the
// SSL Guitarstrip trial). The generic layout the parameter view falls back to (the window is core/ui ProEditors' own
// panel): input column (meter, high-pass, input), PHASE CORRECTION (align to, delay, polarity, all-pass), EQUALISER
// (low / mid / high, low-pass, pre / post), COMP (mode, threshold, mix, auto gain), DRIVE (amp model, pre-amp, drive,
// the voicings, mix), output column (meter, output mix, output)
Layout fretLayout()
{
    Layout l;
    l.width = 976;
    l.height = 540;
    auto& v = l.items;
    v.push_back (section (0, 0, 100, 540, TR_NOOP ("Input"), boxed));
    v.push_back (section (100, 56, 386, 306, TR_NOOP ("Phase correction")));
    v.push_back (section (486, 56, 390, 306, TR_NOOP ("Equaliser")));
    v.push_back (section (100, 362, 290, 178, TR_NOOP ("Comp")));
    v.push_back (section (390, 362, 486, 178, TR_NOOP ("Drive")));
    v.push_back (section (876, 0, 100, 540, TR_NOOP ("Output"), boxed));
    auto inMeter = display (20, 40, 60, 320, "iometer");
    inMeter.colour = 0;
    v.push_back (inMeter);
    auto outMeter = display (896, 40, 60, 320, "iometer");
    outMeter.colour = 1;
    v.push_back (outMeter);
    v.push_back (dial (50, 400, 34, "High-pass", TR_NOOP ("High pass")));
    v.push_back (dial (50, 480, 40, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (926, 400, 34, "Output mix", TR_NOOP ("Mix")));
    v.push_back (dial (926, 480, 40, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (toggle (116, 66, 60, 22, "Phase correction", TR_NOOP ("On")));
    v.push_back (menuOf (118, 118, 96, 22, "Align to", TR_NOOP ("Align to"), TR_NOOP ("Side chain|Left|Right")));
    v.push_back (toggle (364, 66, 52, 26, "Side-chain listen", TR_NOOP ("S/C listen")));
    v.push_back (toggle (422, 66, 52, 26, "Mono listen", TR_NOOP ("Mono listen")));
    v.push_back (toggle (132, 196, 46, 22, "Delay in", TR_NOOP ("In")));
    v.push_back (toggle (222, 196, 46, 22, "Phase invert", TR_NOOP ("Invert")));
    v.push_back (dial (200, 262, 36, "Delay", TR_NOOP ("Time"), bipolar));
    v.push_back (toggle (320, 196, 46, 22, "All-pass in", TR_NOOP ("In")));
    v.push_back (toggle (406, 196, 52, 22, "All-pass order", TR_NOOP ("2nd order")));
    v.push_back (dial (342, 300, 30, "All-pass freq.", TR_NOOP ("Freq")));
    v.push_back (dial (432, 300, 30, "All-pass Q", TR_NOOP ("Q")));
    v.push_back (toggle (502, 66, 40, 22, "EQ in", TR_NOOP ("On")));
    v.push_back (toggle (752, 66, 52, 26, "EQ auto listen", TR_NOOP ("Auto listen")));
    v.push_back (toggle (812, 66, 52, 26, "EQ post", TR_NOOP ("Pre / post")));
    v.push_back (dial (536, 200, 36, "Low gain", TR_NOOP ("LF gain"), bipolar));
    v.push_back (dial (607, 152, 30, "Mid freq.", TR_NOOP ("MF freq")));
    v.push_back (dial (668, 200, 36, "Mid gain", TR_NOOP ("MF gain"), bipolar));
    v.push_back (dial (750, 200, 36, "High gain", TR_NOOP ("HF gain"), bipolar));
    v.push_back (dial (816, 152, 36, "Low-pass", TR_NOOP ("Low pass")));
    v.push_back (toggle (116, 384, 40, 22, "Compressor", TR_NOOP ("On")));
    v.push_back (menuOf (210, 384, 80, 22, "Comp. mode", TR_NOOP ("Mode"), TR_NOOP ("Fastest|Fast|Medium|Slow|Slowest")));
    v.push_back (toggle (320, 388, 50, 26, "Auto gain", TR_NOOP ("Auto gain")));
    v.push_back (meter (222, 436, 46, 92, 0, TR_NOOP ("Reduction")));
    v.push_back (dial (148, 480, 36, "Threshold", TR_NOOP ("Threshold")));
    v.push_back (dial (342, 480, 36, "Comp. mix", TR_NOOP ("Mix")));
    v.push_back (toggle (408, 396, 40, 22, "Drive in", TR_NOOP ("On")));
    v.push_back (menuOf (622, 394, 100, 22, "Amp model", TR_NOOP ("Model"), TR_NOOP ("Guitar amp|Bass amp")));
    v.push_back (dial (440, 480, 36, "Pre-amp", TR_NOOP ("Pre-amp")));
    v.push_back (dial (516, 480, 36, "Guitar drive", TR_NOOP ("Drive")));
    v.push_back (when (dial (600, 480, 36, "Boost", TR_NOOP ("Boost"), bipolar), "Amp model", 0.0f, 0.49f));
    v.push_back (when (dial (672, 480, 36, "Punch", TR_NOOP ("Punch"), bipolar), "Amp model", 0.0f, 0.49f));
    v.push_back (when (dial (744, 480, 36, "Edge", TR_NOOP ("Edge")), "Amp model", 0.0f, 0.49f));
    v.push_back (when (dial (600, 480, 36, "Bass drive", TR_NOOP ("Drive")), "Amp model", 0.5f, 1.0f));
    v.push_back (when (dial (672, 480, 36, "Bass punch", TR_NOOP ("Punch"), bipolar), "Amp model", 0.5f, 1.0f));
    v.push_back (when (dial (744, 480, 36, "Bass shape", TR_NOOP ("Shape")), "Amp model", 0.5f, 1.0f));
    v.push_back (dial (830, 480, 36, "Drive mix", TR_NOOP ("Mix")));
    return l;
}

// round trans189d (issue #94): Strike Shaper, LYNX's level-independent transient designer. The generic layout keeps the
// two-knob front panel: ATTACK and SUSTAIN big on either side, Output, Guard and Link between them, Mix and the
// detector's high-pass small underneath (the editor, ui/ProEditors.cpp StrikeEditor, draws the hardware look)
Layout strikeLayout()
{
    Layout l;
    l.width = 540;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (4, 4, 532, 292, TR_NOOP ("Transients")));
    v.push_back (dial (130, 110, 110, "Attack", TR_NOOP ("Attack"), bipolar | big));
    v.push_back (dial (410, 110, 110, "Sustain", TR_NOOP ("Sustain"), bipolar | big));
    v.push_back (dial (270, 76, 44, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (toggle (236, 150, 68, 24, "Guard", TR_NOOP ("Guard")));
    v.push_back (toggle (236, 182, 68, 24, "Link", TR_NOOP ("Link")));
    v.push_back (dial (200, 250, 28, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (340, 250, 28, "Detect high-pass", TR_NOOP ("Detect HP")));
    return l;
}

// round 190fk: Anvil Bus, LYNX's drum-bus processor. The generic layout: DRIVE (type keys, Drive big, Crunch, Damp) on
// the left, TRANSIENTS big in the middle, BOOM (amount big, frequency, decay) on the right, Comp, Trim, Output and Dry/Wet
// along the bottom (the editor, ui/ProEditors.cpp AnvilEditor, draws the forged-steel look)
Layout anvilLayout()
{
    Layout l;
    l.width = 600;
    l.height = 330;
    auto& v = l.items;
    v.push_back (section (4, 4, 196, 250, TR_NOOP ("Drive")));
    v.push_back (tabs (16, 30, 172, 22, "Drive type", TR_NOOP ("Soft|Medium|Hard"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (dial (102, 100, 64, "Drive", TR_NOOP ("Drive"), big));
    v.push_back (dial (56, 196, 36, "Crunch", TR_NOOP ("Crunch")));
    v.push_back (dial (148, 196, 36, "Damp", TR_NOOP ("Damp")));
    v.push_back (section (204, 4, 192, 250, TR_NOOP ("Transients")));
    v.push_back (dial (300, 110, 80, "Transients", TR_NOOP ("Transients"), bipolar | big));
    v.push_back (section (400, 4, 196, 250, TR_NOOP ("Boom")));
    v.push_back (dial (498, 100, 64, "Boom", TR_NOOP ("Boom"), big));
    v.push_back (dial (452, 196, 36, "Boom freq.", TR_NOOP ("Freq.")));
    v.push_back (dial (544, 196, 36, "Boom decay", TR_NOOP ("Decay")));
    v.push_back (section (4, 258, 592, 68, "", boxed));
    v.push_back (toggle (24, 278, 80, 26, "Comp", TR_NOOP ("Comp")));
    v.push_back (dial (220, 280, 28, "Trim", TR_NOOP ("Trim")));
    v.push_back (dial (360, 280, 28, "Output", TR_NOOP ("Output")));
    v.push_back (dial (500, 280, 28, "Dry/Wet", TR_NOOP ("Dry/Wet")));
    return l;
}

// round tp172c8: Shell Channel, LYNX's drum channel strip (its role: the drum channel strip measured on the SSL Native
// Drumstrip trial). The generic layout: input column, GATE and TRANSIENT SHAPER on top, the LOW / HIGH FREQ ENHANCERS and
// the LISTEN MIC COMPRESSOR below, the process order under them, output column
Layout shellLayout()
{
    Layout l;
    l.width = 1000;
    l.height = 500;
    auto& v = l.items;
    v.push_back (section (0, 0, 110, 500, TR_NOOP ("Input"), boxed));
    v.push_back (section (110, 20, 486, 214, TR_NOOP ("Gate")));
    v.push_back (section (596, 20, 294, 214, TR_NOOP ("Transient shaper")));
    v.push_back (section (110, 234, 244, 168, TR_NOOP ("Low freq enhancer")));
    v.push_back (section (354, 234, 244, 168, TR_NOOP ("High freq enhancer")));
    v.push_back (section (598, 234, 292, 168, TR_NOOP ("Listen mic compressor")));
    v.push_back (section (890, 0, 110, 500, TR_NOOP ("Output"), boxed));
    auto inMeter = display (30, 30, 50, 260, "iometer");
    inMeter.colour = 0;
    v.push_back (inMeter);
    auto outMeter = display (920, 30, 50, 260, "iometer");
    outMeter.colour = 1;
    v.push_back (outMeter);
    v.push_back (dial (55, 400, 48, "Input", TR_NOOP ("Gain"), bipolar));
    v.push_back (dial (945, 330, 36, "Output mix", TR_NOOP ("Mix")));
    v.push_back (dial (945, 420, 36, "Output", TR_NOOP ("Gain"), bipolar));
    v.push_back (meter (124, 46, 30, 160, 1, TR_NOOP ("Gain reduction")));
    v.push_back (dial (318, 96, 34, "Gate open", TR_NOOP ("Open")));
    v.push_back (dial (318, 186, 34, "Gate close", TR_NOOP ("Close")));
    v.push_back (dial (392, 128, 34, "Gate range", TR_NOOP ("Range")));
    v.push_back (dial (466, 128, 34, "Gate hold", TR_NOOP ("Hold")));
    v.push_back (dial (542, 96, 34, "Gate attack", TR_NOOP ("Attack")));
    v.push_back (dial (542, 186, 34, "Gate release", TR_NOOP ("Release")));
    v.push_back (toggle (432, 210, 40, 22, "Gate", TR_NOOP ("Gate")));
    v.push_back (toggle (616, 106, 44, 22, "Shaper invert", TR_NOOP ("Invert")));
    v.push_back (toggle (616, 154, 44, 22, "Shaper listen", TR_NOOP ("Listen")));
    v.push_back (dial (724, 124, 34, "Shaper speed", TR_NOOP ("Speed")));
    v.push_back (dial (800, 96, 34, "Shaper gain", TR_NOOP ("Gain"), bipolar));
    v.push_back (dial (800, 186, 34, "Shaper amount", TR_NOOP ("Amount")));
    v.push_back (toggle (626, 214, 40, 22, "Transient shaper", TR_NOOP ("On")));
    v.push_back (dial (166, 310, 34, "Low amount", TR_NOOP ("Amount")));
    v.push_back (dial (232, 356, 34, "Low freq.", TR_NOOP ("Hz")));
    v.push_back (dial (302, 356, 34, "Low drive", TR_NOOP ("Drive")));
    v.push_back (toggle (300, 286, 40, 22, "Low enhancer", TR_NOOP ("On")));
    v.push_back (toggle (380, 286, 40, 22, "High enhancer", TR_NOOP ("On")));
    v.push_back (dial (404, 356, 34, "High drive", TR_NOOP ("Drive")));
    v.push_back (dial (474, 356, 34, "High freq.", TR_NOOP ("kHz")));
    v.push_back (dial (544, 310, 34, "High amount", TR_NOOP ("Amount")));
    v.push_back (toggle (624, 286, 40, 22, "Listen mic", TR_NOOP ("On")));
    v.push_back (toggle (624, 346, 40, 22, "Narrow band", TR_NOOP ("Narrow band")));
    v.push_back (dial (700, 356, 34, "LMC comp", TR_NOOP ("Comp")));
    v.push_back (dial (770, 356, 34, "LMC make-up", TR_NOOP ("Makeup gain"), bipolar));
    v.push_back (dial (840, 310, 34, "LMC mix", TR_NOOP ("Mix")));
    v.push_back (knob (505, 446, 34, "Process order", TR_NOOP ("Process order"), 482, showValue, 120));
    return l;
}

Layout emberLayout()
{
    Layout l;
    l.width = 520;
    l.height = 230;
    auto& v = l.items;
    v.push_back (section (4, 4, 200, 222, TR_NOOP ("Style")));
    v.push_back (display (12, 26, 184, 110, "embercurve"));
    auto styles = radio (12, 144, 184, 76, "Style", TR_NOOP ("Tube|Tape|Transistor|Fuzz"));
    styles.columns = 2;
    v.push_back (styles);
    v.push_back (section (208, 4, 308, 222, TR_NOOP ("Drive")));
    v.push_back (dial (266, 86, 70, "Drive", TR_NOOP ("Drive"), big));
    v.push_back (dial (380, 60, 36, "Tone", TR_NOOP ("Tone"), bipolar));
    v.push_back (dial (460, 60, 36, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (340, 148, 30, "Low cut", TR_NOOP ("Low cut")));
    v.push_back (dial (410, 148, 30, "High cut", TR_NOOP ("High cut")));
    v.push_back (dial (480, 148, 30, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (toggle (216, 200, 104, 20, "Auto gain", TR_NOOP ("Auto gain")));
    v.push_back (tabs (340, 200, 168, 20, "Oversampling", TR_NOOP ("2x|4x|8x"), { 0.0f, 0.5f, 1.0f }));

    // multiband mode (round 108): crossovers and a style / drive / mix / level row per band (band 1 = the controls
    // above); the band rows only show with more than one band
    l.height = 440;
    v.push_back (section (4, 234, 512, 202, TR_NOOP ("Bands")));
    v.push_back (display (12, 254, 236, 64, "emberbands"));
    v.push_back (tabs (12, 324, 236, 20, "Bands", TR_NOOP ("1 band|2|3|4")));
    v.push_back (tabs (12, 350, 236, 20, "Crossover slope", TR_NOOP ("12 dB/oct|24 dB/oct|48 dB/oct"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (dial (50, 392, 22, "Crossover 1", TR_NOOP ("Low")));
    v.push_back (dial (130, 392, 22, "Crossover 2", TR_NOOP ("Mid")));
    v.push_back (dial (210, 392, 22, "Crossover 3", TR_NOOP ("High")));
    static const char* heads[4] { TR_NOOP ("Style"), TR_NOOP ("Drive"), TR_NOOP ("Mix"), TR_NOOP ("Level") };
    for (int c = 0; c < 4; ++c)
    {
        auto h = label (300.0f + 56.0f * (float) c - 26.0f, 254, 52, 14, heads[c]);
        h.whenParam = "Bands";
        h.whenMin = 0.17f;
        v.push_back (h);
    }
    static const char* rows[4][4] { { nullptr, nullptr, "B1 mix", "B1 level" },
                                    { "B2 style", "B2 drive", "B2 mix", "B2 level" },
                                    { "B3 style", "B3 drive", "B3 mix", "B3 level" },
                                    { "B4 style", "B4 drive", "B4 mix", "B4 level" } };
    static const char* rowNames[4] { TR_NOOP ("B1"), TR_NOOP ("B2"), TR_NOOP ("B3"), TR_NOOP ("B4") };
    for (int b = 0; b < 4; ++b)
    {
        const auto cy = 290.0f + 38.0f * (float) b;
        auto name = label (256, cy - 8.0f, 24, 16, rowNames[b]);
        name.whenParam = "Bands";
        name.whenMin = b == 0 ? 0.17f : 0.17f + 0.333f * (float) (b - 1);
        name.colour = b;
        v.push_back (name);
        for (int c = 0; c < 4; ++c)
        {
            if (rows[b][c] == nullptr)
                continue;
            auto k = knob (300.0f + 56.0f * (float) c, cy, 22, rows[b][c], nullptr, cy + 11.0f, noCaption | (c == 3 ? bipolar : 0), c == 0 ? 4 : 0);
            k.whenParam = "Bands";
            k.whenMin = name.whenMin;
            v.push_back (k);
        }
    }

    // round 128 (Saturn parity): the input gain and each band's power, solo / mute, tone, feedback and dynamics
    v.push_back (dial (250, 162, 20, "Input", TR_NOOP ("Input"), bipolar));
    l.height = 640;
    v.push_back (section (4, 440, 512, 196, TR_NOOP ("Band character")));
    static const char* extraHeads[] { TR_NOOP ("On"), TR_NOOP ("Solo / mute"), TR_NOOP ("Bass"), TR_NOOP ("Mid"), TR_NOOP ("Treble"),
                                      TR_NOOP ("Presence"), TR_NOOP ("Feedback"), TR_NOOP ("Freq"), TR_NOOP ("Dynamics") };
    static const float extraX[] { 54.0f, 84.0f, 186.0f, 230.0f, 274.0f, 318.0f, 372.0f, 420.0f, 472.0f };
    for (int c = 0; c < 9; ++c)
        v.push_back (label (extraX[c] - (c == 1 ? 0.0f : 24.0f), 458, c == 1 ? 72.0f : 48.0f, 14, extraHeads[c]));
    static const char* extraFieldNames[] { "on", "state", "bass", "mid", "treble", "presence", "feedback", "fb freq.", "dynamics" };
    static std::array<std::array<std::array<char, 24>, 9>, 4> extraNames {};
    for (int b = 0; b < 4; ++b)
    {
        const auto cy = 492.0f + 36.0f * (float) b;
        auto name = label (12, cy - 8.0f, 24, 16, rowNames[b]);
        name.colour = b;
        if (b > 0)
        {
            name.whenParam = "Bands";
            name.whenMin = 0.17f + 0.333f * (float) (b - 1);
        }
        v.push_back (name);
        for (int c = 0; c < 9; ++c)
        {
            auto& n = extraNames[(std::size_t) b][(std::size_t) c];
            std::snprintf (n.data(), n.size(), "B%d %s", b + 1, extraFieldNames[c]);
            LayoutItem item;
            if (c == 0) item = toggle (extraX[0] - 14.0f, cy - 9.0f, 28, 18, n.data(), nullptr);
            else if (c == 1) item = tabs (extraX[1], cy - 9.0f, 90, 18, n.data(), TR_NOOP ("-|S|M"), { 0.0f, 0.5f, 1.0f });
            else item = knob (extraX[c], cy, 20, n.data(), nullptr, cy + 10.0f, noCaption | (c >= 2 && c <= 5 ? bipolar : 0) | (c == 8 ? bipolar : 0));
            if (b > 0)
            {
                item.whenParam = "Bands";
                item.whenMin = name.whenMin;
            }
            v.push_back (item);
        }
    }
    // round 135: the Decapitator role's voicing row, switches and the wide filter ranges
    l.height = 760;
    v.push_back (section (4, 640, 512, 116, TR_NOOP ("Voicing")));
    v.push_back (tabs (12, 664, 496, 20, "Voice", TR_NOOP ("Style menu|Tape pre|Console|Grit|Triode|Pentode"), { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }));
    v.push_back (toggle (12, 696, 80, 20, "Punish", TR_NOOP ("Punish")));
    v.push_back (toggle (96, 696, 76, 20, "Thump", TR_NOOP ("Thump")));
    v.push_back (toggle (176, 696, 76, 20, "Steep", TR_NOOP ("Steep")));
    v.push_back (toggle (12, 726, 110, 20, "Wide cuts", TR_NOOP ("Wide cuts")));
    v.push_back (dial (340, 712, 22, "Low cut (wide)", TR_NOOP ("Low cut")));
    v.push_back (dial (440, 712, 22, "High cut (wide)", TR_NOOP ("High cut")));
    // round 147 (Saturn 2): the channel mode and each band's drive pan / pan
    l.height = 870;
    v.push_back (section (4, 760, 512, 106, TR_NOOP ("Stereo")));
    v.push_back (tabs (12, 784, 140, 20, "Channel mode", TR_NOOP ("L/R|M/S"), { 0.0f, 1.0f }));
    v.push_back (label (170, 778, 80, 14, TR_NOOP ("Drive pan")));
    v.push_back (label (170, 818, 80, 14, TR_NOOP ("Pan")));
    static const char* const drivePans[] { "B1 drive pan", "B2 drive pan", "B3 drive pan", "B4 drive pan" };
    static const char* const pans[] { "B1 pan", "B2 pan", "B3 pan", "B4 pan" };
    for (int b = 0; b < 4; ++b)
        for (int r = 0; r < 2; ++r)
        {
            const auto cy = 792.0f + 40.0f * (float) r;
            auto k = knob (280.0f + 60.0f * (float) b, cy, 16, r == 0 ? drivePans[b] : pans[b], nullptr, cy + 9.0f, noCaption | bipolar, 0);
            k.colour = b;
            if (b > 0)
            {
                k.whenParam = "Bands";
                k.whenMin = 0.17f + 0.333f * (float) (b - 1);
            }
            v.push_back (k);
        }
    // round 150 (Saturn 2): bands 5 / 6, the global input / output pan, each band's measured character
    l.height = 1040;
    v.push_back (section (4, 870, 512, 236, TR_NOOP ("Bands 5 / 6, pans, characters")));
    v.push_back (tabs (12, 894, 150, 20, "More bands", TR_NOOP ("Off|5|6"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (dial (200, 904, 18, "Crossover 4", TR_NOOP ("X-over 4")));
    v.push_back (dial (250, 904, 18, "Crossover 5", TR_NOOP ("X-over 5")));
    v.push_back (dial (330, 904, 18, "Input pan", TR_NOOP ("In pan"), bipolar));
    v.push_back (dial (380, 904, 18, "Output pan", TR_NOOP ("Out pan"), bipolar));
    static const char* const b56[2][6] { { "B5 style", "B5 drive", "B5 mix", "B5 level", "B5 drive pan", "B5 pan" },
                                         { "B6 style", "B6 drive", "B6 mix", "B6 level", "B6 drive pan", "B6 pan" } };
    for (int b = 0; b < 2; ++b)
    {
        const auto cy = 950.0f + 30.0f * (float) b;
        auto name = label (12, cy - 8.0f, 24, 16, b == 0 ? TR_NOOP ("B5") : TR_NOOP ("B6"));
        name.colour = 4 + b;
        v.push_back (when (name, "More bands", b == 0 ? 0.25f : 0.75f, 1.01f));
        for (int c = 0; c < 6; ++c)
        {
            auto k = knob (60.0f + 50.0f * (float) c, cy, 18, b56[b][c], nullptr, cy + 9.0f, noCaption | (c >= 3 ? bipolar : 0), c == 0 ? 4 : 0);
            k.colour = 4 + b;
            v.push_back (when (k, "More bands", b == 0 ? 0.25f : 0.75f, 1.01f));
        }
    }
    // bands 5 / 6's power, solo / mute, tone, feedback and dynamics (the round-128 row's columns)
    l.height = 1110;
    static std::array<std::array<std::array<char, 24>, 9>, 2> extra56 {};
    for (int b = 0; b < 2; ++b)
    {
        const auto cy = 1050.0f + 32.0f * (float) b;
        auto name = label (12, cy - 8.0f, 24, 16, b == 0 ? TR_NOOP ("B5") : TR_NOOP ("B6"));
        name.colour = 4 + b;
        v.push_back (when (name, "More bands", b == 0 ? 0.25f : 0.75f, 1.01f));
        for (int c = 0; c < 9; ++c)
        {
            auto& n = extra56[(std::size_t) b][(std::size_t) c];
            std::snprintf (n.data(), n.size(), "B%d %s", b + 5, extraFieldNames[c]);
            LayoutItem item;
            if (c == 0) item = toggle (extraX[0] - 14.0f, cy - 9.0f, 28, 18, n.data(), nullptr);
            else if (c == 1) item = tabs (extraX[1], cy - 9.0f, 90, 18, n.data(), TR_NOOP ("-|S|M"), { 0.0f, 0.5f, 1.0f });
            else item = knob (extraX[c], cy, 20, n.data(), nullptr, cy + 10.0f, noCaption | (c >= 2 && c <= 5 ? bipolar : 0) | (c == 8 ? bipolar : 0));
            v.push_back (when (item, "More bands", b == 0 ? 0.25f : 0.75f, 1.01f));
        }
    }
    static const char* const chars[] { "B1 character", "B2 character", "B3 character", "B4 character", "B5 character", "B6 character" };
    for (int b = 0; b < 6; ++b)
        v.push_back (stepper (360.0f + 76.0f * (float) (b % 2), 940.0f + 30.0f * (float) (b / 2), 72, 22, chars[b], nullptr, 29));
    // round 152 (Saturn 2): each crossover's own slope, the processing mode, the modulation
    l.height = 1300;
    v.push_back (section (4, 1110, 512, 186, TR_NOOP ("Slopes, processing, modulation")));
    static const char* const slopes[] { "Slope 1", "Slope 2", "Slope 3", "Slope 4", "Slope 5" };
    for (int k = 0; k < 5; ++k)
        v.push_back (stepper (12.0f + 64.0f * (float) k, 1132, 60, 20, slopes[k], nullptr, 6));
    v.push_back (tabs (340, 1132, 168, 20, "Processing", TR_NOOP ("Min. phase|Linear"), { 0.0f, 1.0f }));
    v.push_back (dial (30, 1178, 16, "LFO 1 rate", TR_NOOP ("LFO 1")));
    v.push_back (stepper (6, 1222, 50, 18, "LFO 1 shape", nullptr, 5));
    v.push_back (dial (90, 1178, 16, "LFO 2 rate", TR_NOOP ("LFO 2")));
    v.push_back (stepper (66, 1222, 50, 18, "LFO 2 shape", nullptr, 5));
    v.push_back (dial (150, 1178, 16, "Env attack", TR_NOOP ("Attack")));
    v.push_back (dial (200, 1178, 16, "Env release", TR_NOOP ("Release")));
    static const char* const mods[4][3] { { "Mod 1 source", "Mod 1 target", "Mod 1 amount" }, { "Mod 2 source", "Mod 2 target", "Mod 2 amount" },
                                          { "Mod 3 source", "Mod 3 target", "Mod 3 amount" }, { "Mod 4 source", "Mod 4 target", "Mod 4 amount" } };
    for (int k = 0; k < 4; ++k)
    {
        const auto y = 1162.0f + 32.0f * (float) k;
        v.push_back (stepper (232, y, 70, 22, mods[k][0], nullptr, 4));
        v.push_back (stepper (306, y, 120, 22, mods[k][1], nullptr, 27));
        v.push_back (knob (466, y + 11.0f, 14, mods[k][2], nullptr, y + 11.0f, noCaption | bipolar));
    }
    v.push_back (toggle (6, 1264, 200, 20, "Effect law", TR_NOOP ("Measured effect characters")));
    return l;
}

// --- LYNX originals: the dynamics suite (REA round 108) --------------------------------------------------------
// The layout logic follows how the reference plugins are used (round 108 UI rule, screenshots in tmp/comp108/ref):
// the level / gain-reduction display is the window, the main knobs sit in one row under it with their small
// companions below, the style is one menu button between them, and the rarely touched parts (character, side chain)
// fold into a page at the bottom. Drawn entirely in LYNX's own style.

LayoutItem menuOf (float x, float y, float w, float h, const char* param, const char* caption, const char* options)
{
    LayoutItem i;
    i.type = Item::menu;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.options = options;
    return i;
}

Layout crestCompressor()
{
    Layout l;
    l.width = 840;
    l.height = 604;
    l.pages = { TR_NOOP ("Character"), TR_NOOP ("Side chain"), TR_NOOP ("Options") };
    auto& v = l.items;
    // the display: scrolling input / output level and gain reduction, with the threshold; meters on the right
    v.push_back (display (8, 8, 704, 252, "grhistory"));
    v.push_back (display (718, 8, 114, 252, "crestmeters"));
    // the transfer curve at the left of the knob row
    v.push_back (display (8, 268, 150, 150, "crestcurve"));
    // main knobs, the style button between ratio and attack, small companions under them
    v.push_back (dial (232, 310, 62, "Threshold", TR_NOOP ("Threshold"), big));
    v.push_back (dial (330, 310, 62, "Ratio", TR_NOOP ("Ratio"), big));
    v.push_back (menuOf (382, 296, 112, 26, "Style", TR_NOOP ("Style"),
                         TR_NOOP ("Clean|Allround|Punch|Upward|Two-way|Vari-mu|Classic|Opto|Vocal|Bus|Pumping")));
    v.push_back (toggle (382, 330, 112, 20, "Auto release", TR_NOOP ("Auto release")));
    // round tp172b2: Pro-C 3's Smooth / Op-El / Mastering styles (a voicing over the style)
    v.push_back (menuOf (382, 356, 112, 22, "Voicing", TR_NOOP ("Voicing"), TR_NOOP ("None|Smooth|Op-El|Mastering")));
    v.push_back (dial (544, 310, 56, "Attack", TR_NOOP ("Attack"), big));
    v.push_back (dial (636, 310, 56, "Release", TR_NOOP ("Release"), big));
    v.push_back (dial (206, 408, 30, "Knee", TR_NOOP ("Knee")));
    v.push_back (dial (266, 408, 30, "Range", TR_NOOP ("Range")));
    v.push_back (dial (330, 408, 30, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (520, 408, 30, "Lookahead", TR_NOOP ("Lookahead")));
    v.push_back (dial (580, 408, 30, "Hold", TR_NOOP ("Hold")));
    v.push_back (dial (640, 408, 30, "SC level", TR_NOOP ("SC level"), bipolar));
    // make-up and mix on the right, as one goes to them last
    v.push_back (toggle (712, 268, 120, 20, "Auto gain", TR_NOOP ("Auto gain")));
    v.push_back (dial (742, 318, 34, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (dial (802, 318, 34, "Mix", TR_NOOP ("Mix")));
    // the bottom bar: processing options and the meter
    v.push_back (label (8, 462, 90, 22, TR_NOOP ("Oversampling")));
    v.push_back (tabs (96, 462, 176, 22, "Oversampling", TR_NOOP ("Off|2x|4x|8x")));
    v.push_back (label (284, 462, 76, 22, TR_NOOP ("Lookahead")));
    v.push_back (tabs (358, 462, 270, 22, "Max lookahead", TR_NOOP ("Off|1 ms|2 ms|5 ms|10 ms|20 ms")));
    v.push_back (meter (640, 462, 192, 22, 0, TR_NOOP ("Reduction")));
    // folded parts
    v.push_back (pageTabs (8, 492, 360, 22, TR_NOOP ("Character|Side chain|Options")));
    v.push_back (section (8, 518, 824, 82, nullptr, boxed));
    v.push_back (onPage (tabs (20, 530, 280, 22, "Character", TR_NOOP ("Off|Warm|Crisp|Bright")), 0));
    v.push_back (onPage (tabs (310, 530, 140, 22, "Character pre", TR_NOOP ("Post|Pre")), 0));
    v.push_back (onPage (dial (492, 546, 28, "Drive", TR_NOOP ("Drive"), bipolar), 0));
    // round 128 (Pro-C 3 parity): dry level and auto threshold; link mode and the display options on their own page
    v.push_back (onPage (dial (572, 546, 28, "Dry", TR_NOOP ("Dry")), 0));
    v.push_back (onPage (toggle (630, 530, 160, 20, "Auto threshold", TR_NOOP ("Auto threshold")), 0));
    // round 135: the wet and dry paths' balance
    v.push_back (onPage (dial (680, 566, 20, "Wet pan", TR_NOOP ("Wet pan"), bipolar), 0));
    v.push_back (onPage (dial (750, 566, 20, "Dry pan", TR_NOOP ("Dry pan"), bipolar), 0));
    v.push_back (onPage (label (20, 530, 70, 22, TR_NOOP ("Link mode")), 2));
    v.push_back (onPage (tabs (92, 530, 360, 22, "Link mode", TR_NOOP ("Stereo|Mid only|Side only|Mid > side|Side > mid")), 2));
    v.push_back (onPage (label (470, 530, 50, 22, TR_NOOP ("Scale")), 2));
    v.push_back (onPage (tabs (520, 530, 200, 22, "Meter scale", TR_NOOP ("9 dB|18 dB|36 dB|72 dB|90 dB")), 2));
    // round 190v: the two-way / vari-mu law (classic = round 108's, what a project saved before loads with)
    v.push_back (onPage (tabs (726, 530, 106, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys), 2));
    v.push_back (onPage (toggle (20, 562, 160, 20, "Input meter", TR_NOOP ("Input meter")), 2));
    // round 137 (Pro-C 3's side-chain Host Sync / MIDI inputs): the detector hears a pulse instead of the audio
    v.push_back (onPage (label (192, 562, 52, 20, TR_NOOP ("Trigger")), 2));
    v.push_back (onPage (tabs (246, 562, 196, 20, "Trigger", TR_NOOP ("Off|Host sync|MIDI")), 2));
    v.push_back (onPage (when (menuOf (450, 562, 70, 20, "Trigger sync", TR_NOOP ("Note"), "1/1|1/2|1/4|1/8|1/16"), "Trigger", 0.25f, 0.75f), 2));
    v.push_back (onPage (when (dial (546, 566, 20, "Trigger rate", TR_NOOP ("Rate")), "Trigger", 0.25f, 0.75f), 2));
    v.push_back (onPage (when (dial (600, 566, 20, "Trigger length", TR_NOOP ("Length")), "Trigger", 0.25f, 0.75f), 2));
    v.push_back (onPage (when (stepper (630, 562, 120, 20, "Trigger channel", TR_NOOP ("MIDI channel"), 129), "Trigger", 0.75f, 1.0f), 2));
    v.push_back (onPage (dial (36, 548, 26, "Stereo link", TR_NOOP ("Stereo link")), 1));
    v.push_back (onPage (toggle (72, 528, 120, 20, "External SC", TR_NOOP ("External input")), 1));
    v.push_back (onPage (toggle (72, 554, 120, 20, "SC listen", TR_NOOP ("Listen")), 1));
    v.push_back (onPage (display (200, 524, 236, 72, "crestsceq"), 1));
    static const char* bandTitles[3] { TR_NOOP ("Band 1"), TR_NOOP ("Band 2"), TR_NOOP ("Band 3") };
    static const char* fields[3][5] { { "SC1 shape", "SC1 freq.", "SC1 gain", "SC1 Q", "SC1 slope" },
                                      { "SC2 shape", "SC2 freq.", "SC2 gain", "SC2 Q", "SC2 slope" },
                                      { "SC3 shape", "SC3 freq.", "SC3 gain", "SC3 Q", "SC3 slope" } };
    static const char* captions[5] { TR_NOOP ("Shape"), TR_NOOP ("Freq"), TR_NOOP ("Gain"), TR_NOOP ("Q"), TR_NOOP ("Slope") };
    for (int b = 0; b < 3; ++b)
    {
        const auto x0 = 444.0f + 130.0f * (float) b;
        v.push_back (onPage (label (x0, 522, 124, 14, bandTitles[b], boxed), 1));
        for (int f = 0; f < 5; ++f)
            v.push_back (onPage (knob (x0 + 13.0f + 24.5f * (float) f, 556, 20, fields[b][f], captions[f], 586, f == 2 ? bipolar : 0,
                                       f == 0 || f == 4 ? 9 : 0), 1));
    }
    return l;
}

Layout prismMultiband()
{
    Layout l;
    l.width = 900;
    l.height = 594; // round 144: + the processing / oversampling / pan-mode row (was 560)
    l.pages = { TR_NOOP ("Band 1"), TR_NOOP ("Band 2"), TR_NOOP ("Band 3"), TR_NOOP ("Band 4"), TR_NOOP ("Band 5"), TR_NOOP ("Band 6") };
    auto& v = l.items;
    // the bands are the display: click one to edit it, drag a crossover line to move it
    v.push_back (display (8, 8, 884, 330, "prismbands"));
    // the selected band's panel
    v.push_back (section (8, 344, 884, 126, nullptr, boxed));
    v.push_back (pageTabs (16, 352, 348, 22, TR_NOOP ("Band 1|Band 2|Band 3|Band 4|Band 5|Band 6")));
    static const char* names[6][10] {
        { "B1 threshold", "B1 ratio", "B1 range", "B1 expand", "B1 knee", "B1 attack", "B1 release", "B1 level", "B1 on", "B1 solo" },
        { "B2 threshold", "B2 ratio", "B2 range", "B2 expand", "B2 knee", "B2 attack", "B2 release", "B2 level", "B2 on", "B2 solo" },
        { "B3 threshold", "B3 ratio", "B3 range", "B3 expand", "B3 knee", "B3 attack", "B3 release", "B3 level", "B3 on", "B3 solo" },
        { "B4 threshold", "B4 ratio", "B4 range", "B4 expand", "B4 knee", "B4 attack", "B4 release", "B4 level", "B4 on", "B4 solo" },
        { "B5 threshold", "B5 ratio", "B5 range", "B5 expand", "B5 knee", "B5 attack", "B5 release", "B5 level", "B5 on", "B5 solo" },
        { "B6 threshold", "B6 ratio", "B6 range", "B6 expand", "B6 knee", "B6 attack", "B6 release", "B6 level", "B6 on", "B6 solo" } };
    for (int b = 0; b < 6; ++b) // round 135: up to six bands
    {
        auto add = [&] (LayoutItem i) { i.colour = b; v.push_back (onPage (std::move (i), b)); };
        add (toggle (16, 384, 56, 22, names[b][8], TR_NOOP ("On")));
        add (toggle (78, 384, 60, 22, names[b][9], TR_NOOP ("Solo")));
        add (tabs (16, 414, 160, 22, names[b][3], TR_NOOP ("Compress|Expand")));
        add (dial (312, 398, 46, names[b][0], TR_NOOP ("Threshold"), big));
        add (dial (402, 398, 46, names[b][2], TR_NOOP ("Range"), big | bipolar));
        add (dial (484, 398, 36, names[b][1], TR_NOOP ("Ratio")));
        add (dial (556, 398, 36, names[b][4], TR_NOOP ("Knee")));
        add (dial (628, 398, 36, names[b][5], TR_NOOP ("Attack")));
        add (dial (700, 398, 36, names[b][6], TR_NOOP ("Release")));
        add (dial (788, 398, 40, names[b][7], TR_NOOP ("Level"), bipolar));
        static const char* las[6] { "B1 lookahead", "B2 lookahead", "B3 lookahead", "B4 lookahead", "B5 lookahead", "B6 lookahead" };
        add (dial (858, 398, 22, las[b], TR_NOOP ("Lookahead"))); // round 128: the band's own look-ahead
        static const char* pans[6] { "B1 pan", "B2 pan", "B3 pan", "B4 pan", "B5 pan", "B6 pan" };
        add (dial (200, 395, 22, pans[b], TR_NOOP ("Pan"), bipolar)); // round 135
    }
    // global bar
    v.push_back (label (8, 482, 50, 22, TR_NOOP ("Slope")));
    v.push_back (tabs (56, 482, 200, 22, "Slope", TR_NOOP ("12 dB/oct|24 dB/oct|48 dB/oct")));
    // round 135: the band count, crossovers 4–5, the external side chain
    v.push_back (label (8, 510, 50, 22, TR_NOOP ("Bands")));
    v.push_back (tabs (56, 510, 200, 22, "Bands", "1|2|3|4|5|6"));
    v.push_back (toggle (56, 538, 140, 18, "External SC", TR_NOOP ("External input")));
    v.push_back (dial (290, 504, 24, "Crossover 1", TR_NOOP ("Crossover 1")));
    v.push_back (dial (350, 504, 24, "Crossover 2", TR_NOOP ("Crossover 2")));
    v.push_back (dial (410, 504, 24, "Crossover 3", TR_NOOP ("Crossover 3")));
    v.push_back (dial (470, 504, 24, "Crossover 4", TR_NOOP ("Crossover 4")));
    v.push_back (dial (530, 504, 24, "Crossover 5", TR_NOOP ("Crossover 5")));
    v.push_back (dial (600, 504, 24, "Lookahead", TR_NOOP ("Lookahead")));
    v.push_back (dial (660, 504, 24, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (740, 504, 24, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (820, 504, 24, "Output", TR_NOOP ("Output"), bipolar));
    // round 144 (Pro-MB's bottom bar): processing mode, oversampling, what the bands' pan balances
    v.push_back (label (8, 566, 50, 20, TR_NOOP ("Phase")));
    v.push_back (tabs (56, 566, 240, 20, "Processing", TR_NOOP ("Linear|Dynamic|Minimum"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (label (310, 566, 90, 20, TR_NOOP ("Oversampling")));
    v.push_back (tabs (402, 566, 150, 20, "Oversampling", "Off|2x|4x", { 0.0f, 0.5f, 1.0f }));
    v.push_back (label (570, 566, 40, 20, TR_NOOP ("Pan")));
    v.push_back (tabs (612, 566, 110, 20, "Pan mode", "L/R|M/S"));
    // round 147: Dynamic phase's engine (classic = projects saved before round 147) and each band's expert settings
    v.push_back (tabs (736, 566, 156, 20, "Dynamic engine", TR_NOOP ("Classic|Measured")));
    v.push_back (section (8, 596, 884, 64, TR_NOOP ("Expert (selected band)"), boxed));
    static const char* links[6] { "B1 link", "B2 link", "B3 link", "B4 link", "B5 link", "B6 link" };
    static const char* linkModes[6] { "B1 link mode", "B2 link mode", "B3 link mode", "B4 link mode", "B5 link mode", "B6 link mode" };
    static const char* scFree[6] { "B1 SC filter", "B2 SC filter", "B3 SC filter", "B4 SC filter", "B5 SC filter", "B6 SC filter" };
    static const char* scLow[6] { "B1 SC low", "B2 SC low", "B3 SC low", "B4 SC low", "B5 SC low", "B6 SC low" };
    static const char* scHigh[6] { "B1 SC high", "B2 SC high", "B3 SC high", "B4 SC high", "B5 SC high", "B6 SC high" };
    for (int b = 0; b < 6; ++b)
    {
        auto add = [&] (LayoutItem i) { i.colour = b; v.push_back (onPage (std::move (i), b)); };
        add (tabs (16, 626, 150, 22, scFree[b], TR_NOOP ("Band|Free")));
        add (dial (210, 630, 20, scLow[b], TR_NOOP ("SC low")));
        add (dial (290, 630, 20, scHigh[b], TR_NOOP ("SC high")));
        add (dial (420, 630, 20, links[b], TR_NOOP ("Stereo link")));
        add (tabs (480, 626, 130, 22, linkModes[b], TR_NOOP ("Mid|Side")));
    }
    // round 147: each crossover's own slope (0 = the global one)
    static const char* xs[5] { "Crossover 1 slope", "Crossover 2 slope", "Crossover 3 slope", "Crossover 4 slope", "Crossover 5 slope" };
    static const char* xsCaption[5] { TR_NOOP ("Slope 1"), TR_NOOP ("Slope 2"), TR_NOOP ("Slope 3"), TR_NOOP ("Slope 4"), TR_NOOP ("Slope 5") };
    for (int k = 0; k < 5; ++k)
        v.push_back (dial (660.0f + 52.0f * (float) k, 630, 16, xs[k], xsCaption[k]));
    // round 150: the free layout (each band's own edges and slopes), the band's side-chain input, the detector
    v.push_back (section (8, 666, 884, 64, TR_NOOP ("Free layout (selected band)"), boxed));
    static const char* lo[6] { "B1 low", "B2 low", "B3 low", "B4 low", "B5 low", "B6 low" };
    static const char* hi[6] { "B1 high", "B2 high", "B3 high", "B4 high", "B5 high", "B6 high" };
    static const char* ls[6] { "B1 low slope", "B2 low slope", "B3 low slope", "B4 low slope", "B5 low slope", "B6 low slope" };
    static const char* hs[6] { "B1 high slope", "B2 high slope", "B3 high slope", "B4 high slope", "B5 high slope", "B6 high slope" };
    static const char* sci[6] { "B1 SC input", "B2 SC input", "B3 SC input", "B4 SC input", "B5 SC input", "B6 SC input" };
    for (int b = 0; b < 6; ++b)
    {
        auto add = [&] (LayoutItem i) { i.colour = b; v.push_back (onPage (std::move (i), b)); };
        add (dial (180, 700, 20, lo[b], TR_NOOP ("Low edge")));
        add (dial (250, 700, 20, ls[b], TR_NOOP ("Low slope")));
        add (dial (320, 700, 20, hi[b], TR_NOOP ("High edge")));
        add (dial (390, 700, 20, hs[b], TR_NOOP ("High slope")));
        add (tabs (450, 696, 220, 22, sci[b], TR_NOOP ("Global|Plug-in|External"), { 0.0f, 0.5f, 1.0f }));
    }
    v.push_back (tabs (16, 696, 140, 22, "Band layout", TR_NOOP ("Split|Free")));
    v.push_back (tabs (700, 696, 180, 22, "Detector", TR_NOOP ("Classic|Measured")));
    // round 152: the measured model's revision (gain-stage attack, narrow Dynamic-phase bands, 0.8-octave minimum)
    v.push_back (label (620, 736, 76, 22, TR_NOOP ("Model")));
    v.push_back (tabs (700, 736, 180, 22, "Revision", TR_NOOP ("Round 150|Round 152")));
    // round tp172b6: Audition, off or the band whose detector signal replaces the output (value = band / 6)
    v.push_back (label (16, 736, 76, 22, TR_NOOP ("Audition")));
    v.push_back (menuOf (96, 736, 180, 22, "Audition", nullptr, "Off|1|2|3|4|5|6"));
    l.height = 766;
    return l;
}

Layout sentryGate()
{
    Layout l;
    l.width = 680;
    l.height = 564; // round 190dm: + the trigger row
    auto& v = l.items;
    // left: threshold (big) with ratio and range under it
    v.push_back (dial (86, 84, 92, "Threshold", TR_NOOP ("Threshold"), big));
    v.push_back (dial (44, 214, 50, "Ratio", TR_NOOP ("Ratio")));
    v.push_back (dial (128, 214, 50, "Range", TR_NOOP ("Range")));
    // centre: the style button over the curve and the level / reduction history
    v.push_back (menuOf (180, 8, 330, 24, "Style", TR_NOOP ("Style"), TR_NOOP ("Classic|Clean|Vocal|Guitar|Upward")));
    v.push_back (display (180, 38, 150, 150, "sentrycurve"));
    v.push_back (display (336, 38, 174, 150, "grhistory"));
    v.push_back (meter (180, 194, 330, 16, 0, TR_NOOP ("Reduction")));
    v.push_back (display (180, 216, 330, 26, "levels"));
    // right: attack and release (big), hold / knee / look-ahead (small) beside them
    v.push_back (dial (562, 66, 58, "Attack", TR_NOOP ("Attack"), big));
    v.push_back (dial (562, 186, 58, "Release", TR_NOOP ("Release"), big));
    v.push_back (dial (642, 46, 32, "Hold", TR_NOOP ("Hold")));
    v.push_back (dial (642, 124, 32, "Knee", TR_NOOP ("Knee")));
    v.push_back (dial (642, 202, 32, "Lookahead", TR_NOOP ("Lookahead")));
    v.push_back (toggle (588, 252, 88, 20, "Lookahead on", TR_NOOP ("Lookahead on")));
    // the "expert" strip: side chain, upward expansion, wet / dry
    v.push_back (section (4, 282, 672, 106, TR_NOOP ("Side chain and output")));
    v.push_back (toggle (14, 304, 120, 22, "External SC", TR_NOOP ("External input")));
    v.push_back (toggle (14, 332, 120, 22, "SC listen", TR_NOOP ("Listen")));
    static const std::pair<const char*, const char*> knobs[6] { { "High pass", TR_NOOP ("High pass") }, { "Low pass", TR_NOOP ("Low pass") },
                                                                { "Up threshold", TR_NOOP ("Up thres") }, { "Up ratio", TR_NOOP ("Up ratio") },
                                                                { "Dry", TR_NOOP ("Dry") }, { "Output", TR_NOOP ("Output") } };
    for (int k = 0; k < 6; ++k)
        v.push_back (dial (186.0f + 84.0f * (float) k, 326, 28, knobs[k].first, knobs[k].second, k == 5 ? bipolar : 0));
    // round 128 (Pro-G parity): channel mode, ducking, the side-chain gain / mix per channel, wet / dry pan, input
    v.push_back (section (4, 392, 672, 84, TR_NOOP ("Expert")));
    v.push_back (tabs (14, 414, 160, 22, "Channel mode", TR_NOOP ("Left/Right|Mid/Side")));
    v.push_back (toggle (14, 444, 120, 22, "Ducking", TR_NOOP ("Ducking")));
    static const std::pair<const char*, const char*> expert[7] { { "SC gain L", TR_NOOP ("Gain L") }, { "SC mix L", TR_NOOP ("Mix L") },
                                                                 { "SC gain R", TR_NOOP ("Gain R") }, { "SC mix R", TR_NOOP ("Mix R") },
                                                                 { "Wet pan", TR_NOOP ("Wet pan") }, { "Dry pan", TR_NOOP ("Dry pan") },
                                                                 { "Input", TR_NOOP ("Input") } };
    for (int k = 0; k < 7; ++k)
        v.push_back (dial (220.0f + 68.0f * (float) k, 438, 22, expert[k].first, expert[k].second, bipolar));
    // round 135: oversampling (the reference's bottom bar)
    v.push_back (label (8, 482, 90, 22, TR_NOOP ("Oversampling")));
    v.push_back (tabs (96, 482, 176, 22, "Oversampling", TR_NOOP ("Off|2x|4x|8x")));
    // round 190v: the Vocal style's noise reduction (classic = none, what a project saved before loads with)
    v.push_back (label (300, 482, 56, 22, TR_NOOP ("Model")));
    v.push_back (tabs (360, 482, 180, 22, "Law", TR_NOOP ("Classic|Measured")));
    // round 190dm: the trigger (Transient = a drum gate: an onset opens it, it closes along the release shape)
    v.push_back (label (8, 514, 90, 22, TR_NOOP ("Trigger")));
    v.push_back (tabs (96, 514, 176, 22, "Trigger", TR_NOOP ("Level|Transient")));
    v.push_back (when (label (300, 514, 56, 22, TR_NOOP ("Shape")), "Trigger", 0.5f, 1.0f));
    v.push_back (when (tabs (360, 514, 180, 22, "Release shape", TR_NOOP ("Smooth|Mid|Sharp")), "Trigger", 0.5f, 1.0f));
    v.push_back (when (dial (612, 520, 22, "Sensitivity", TR_NOOP ("Sensitivity")), "Trigger", 0.5f, 1.0f));
    return l;
}

Layout quellDenoiser()
{
    // round 119: the one-knob noise suppressors' front panel (the role: Waves NS1, screenshot of the official trial):
    // the reduction meter on the left, one tall Amount fader on the right with its read-out under it; LYNX's extras
    // (listen to what is removed, output trim) small along the bottom
    Layout l;
    l.width = 300;
    l.height = 420;
    auto& v = l.items;
    v.push_back (section (4, 4, 292, 412, TR_NOOP ("Noise suppressor")));
    v.push_back (meter (30, 34, 70, 300, 0, TR_NOOP ("Reduction")));
    v.push_back (slider (Item::vslider, 170, 30, 70, 316, "Amount", TR_NOOP ("Amount")));
    v.push_back (display (128, 30, 42, 316, "faderscale")); // round 137: the fader's 0 … 100 scale
    auto scaleRight = display (240, 30, 42, 316, "faderscale"); // round tp172d: printed on both sides, as on the reference
    scaleRight.steps = 1;
    v.push_back (scaleRight);
    v.push_back (value (170, 372, 70, 20, "Amount", 0));
    v.push_back (toggle (22, 372, 96, 22, "Listen", TR_NOOP ("Listen")));
    v.push_back (toggle (22, 342, 96, 22, "Matched", TR_NOOP ("Matched"))); // round 124: the engine fitted to NS1
    // round 190dp: projects saved before the matched engine's noise-floor tracker keep the fixed law until switched
    v.push_back (when (tabs (22, 396, 112, 16, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys), "Law", 0.0f, 0.5f));
    v.push_back (dial (256, 378, 28, "Output", TR_NOOP ("Output"), bipolar));
    return l;
}

Layout geminiDoubler()
{
    // round 119: the two-voice doublers' layout (the role: Waves Doubler, screenshot of the official trial): three
    // pictures along the top (where the voices sit in the stereo field, delay against detune, the voices' EQ), the
    // direct signal / feedback / range / EQ row in the middle, then one row per voice (on, gain, pan, delay,
    // feedback, octave, detune, depth, rate, source) and the output fader on the right
    Layout l;
    l.width = 800;
    l.height = 460;
    auto& v = l.items;
    v.push_back (display (12, 8, 236, 150, "geminipan"));
    v.push_back (display (264, 8, 260, 150, "geminimap"));
    v.push_back (display (540, 8, 248, 150, "geminieq"));
    // direct, feedback cut, range, shelves
    v.push_back (section (4, 166, 792, 92, TR_NOOP ("Direct")));
    v.push_back (toggle (14, 208, 64, 22, "Direct", TR_NOOP ("Direct")));
    v.push_back (dial (112, 206, 30, "Direct gain", TR_NOOP ("Gain")));
    v.push_back (dial (170, 206, 30, "Direct pan", TR_NOOP ("Pan"), bipolar));
    v.push_back (toggle (206, 208, 64, 22, "Align", TR_NOOP ("Align")));
    v.push_back (dial (310, 206, 30, "Feedback cut", TR_NOOP ("FB low cut")));
    v.push_back (tabs (350, 208, 100, 22, "Range", TR_NOOP ("80 Hz|20 Hz")));
    v.push_back (toggle (478, 208, 40, 22, "Low shelf", TR_NOOP ("L")));
    v.push_back (dial (546, 206, 30, "Low gain", TR_NOOP ("Gain"), bipolar));
    v.push_back (dial (600, 206, 30, "Low freq", TR_NOOP ("Freq")));
    v.push_back (toggle (632, 208, 40, 22, "High shelf", TR_NOOP ("H")));
    v.push_back (dial (700, 206, 30, "High gain", TR_NOOP ("Gain"), bipolar));
    v.push_back (dial (754, 206, 30, "High freq", TR_NOOP ("Freq")));
    // round 165w: the shelves' law (Measured: −88 … +12 dB, the reference's; Old: round 124's, kept by older projects)
    v.push_back (tabs (656, 168, 132, 18, "EQ law", TR_NOOP ("Old|Measured")));
    // round 190do: the voices' read head with Align off (Measured: one head, the default of new instances; Classic: two
    // crossfaded taps, kept by older projects)
    v.push_back (tabs (520, 168, 132, 18, "Head law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    // one row per voice; round 135w: voices 3 and 4 (the four-voice doubler's) on a second page of the same rows
    v.push_back (section (4, 264, 680, 192, TR_NOOP ("Voices")));
    l.pages = { TR_NOOP ("Voices 1-2"), TR_NOOP ("Voices 3-4") };
    v.push_back (pageTabs (500, 266, 180, 20, TR_NOOP ("Voices 1-2|Voices 3-4")));
    static const char* const names[4][10] {
        { "V1 on", "V1 gain", "V1 pan", "V1 delay", "V1 feedback", "V1 octave", "V1 detune", "V1 depth", "V1 rate", "V1 source" },
        { "V2 on", "V2 gain", "V2 pan", "V2 delay", "V2 feedback", "V2 octave", "V2 detune", "V2 depth", "V2 rate", "V2 source" },
        { "V3 on", "V3 gain", "V3 pan", "V3 delay", "V3 feedback", "V3 octave", "V3 detune", "V3 depth", "V3 rate", "V3 source" },
        { "V4 on", "V4 gain", "V4 pan", "V4 delay", "V4 feedback", "V4 octave", "V4 detune", "V4 depth", "V4 rate", "V4 source" } };
    static const char* const numbers[4] { TR_NOOP ("1"), TR_NOOP ("2"), TR_NOOP ("3"), TR_NOOP ("4") };
    for (int k = 0; k < 4; ++k)
    {
        const auto cy = 316.0f + 76.0f * (float) (k % 2);
        const auto rowStart = v.size();
        v.push_back (toggle (14, cy - 11, 40, 22, names[k][0], numbers[k]));
        v.push_back (dial (90, cy, 30, names[k][1], TR_NOOP ("Gain")));
        v.push_back (dial (148, cy, 30, names[k][2], TR_NOOP ("Pan"), bipolar));
        v.push_back (dial (206, cy, 30, names[k][3], TR_NOOP ("Delay")));
        v.push_back (dial (264, cy, 30, names[k][4], TR_NOOP ("Feedback")));
        v.push_back (toggle (298, cy - 11, 50, 22, names[k][5], TR_NOOP ("Oct")));
        v.push_back (dial (384, cy, 30, names[k][6], TR_NOOP ("Detune"), bipolar));
        v.push_back (dial (442, cy, 30, names[k][7], TR_NOOP ("Depth"), bipolar));
        v.push_back (dial (500, cy, 30, names[k][8], TR_NOOP ("Rate")));
        v.push_back (tabs (540, cy - 11, 136, 22, names[k][9], k % 2 == 0 ? TR_NOOP ("L|L+R|L-R") : TR_NOOP ("R|R+L|R-L"), { 1.0f / 6.0f, 0.5f, 5.0f / 6.0f }));
        for (auto i = rowStart; i < v.size(); ++i)
            v[i].page = k / 2;
    }
    // round 135w: the modulation RESET (every voice's LFO restarts in sync; momentary)
    v.push_back (toggle (420, 266, 70, 20, "Mod reset", TR_NOOP ("Reset"), momentary));
    v.push_back (section (690, 264, 106, 192, TR_NOOP ("Output")));
    v.push_back (slider (Item::vslider, 698, 290, 30, 150, "Output", nullptr));
    v.push_back (display (730, 276, 62, 172, "iometer")); // round 129: the reference's output meters
    return l;
}

Layout autoPanLayout()
{
    // round 135: the rhythmic panners' panel (the role: PanMan, screenshot of the official trial): OFFSET, WIDTH and
    // SMOOTHING over the pan-position strip (L … C … R) on the left; the source, its rate or rhythm steps and the
    // direction (one way or back and forth) in the middle; input / output and the analog stage on the right. FL's
    // own LFO (the Love Philter pan LFO the effect was made for) stays the first source with its period, phase and
    // tension.
    Layout l;
    l.width = 760;
    l.height = 250;
    auto& v = l.items;
    v.push_back (section (4, 4, 330, 242, TR_NOOP ("Pan")));
    v.push_back (dial (60, 76, 52, "Pan", TR_NOOP ("Offset"), bipolar));
    v.push_back (dial (166, 76, 52, "Depth", TR_NOOP ("Width"), bipolar));
    v.push_back (when (dial (272, 76, 52, "Smoothing", TR_NOOP ("Smoothing")), "Source", 0.25f, 1.0f));
    v.push_back (when (dial (272, 76, 52, "Tension", TR_NOOP ("Tension"), bipolar), "Source", 0.0f, 0.25f));
    v.push_back (display (20, 160, 298, 26, "panpos"));
    v.push_back (label (20, 190, 30, 16, TR_NOOP ("L"), alignLeft));
    v.push_back (label (154, 190, 30, 16, TR_NOOP ("C")));
    v.push_back (label (288, 190, 30, 16, TR_NOOP ("R")));
    v.push_back (when (toggle (20, 216, 120, 20, "FL pan law", TR_NOOP ("FL pan law")), "Source", 0.0f, 0.25f));
    v.push_back (toggle (190, 216, 128, 20, "Control only", TR_NOOP ("Control only")));
    v.push_back (section (340, 4, 250, 242, TR_NOOP ("Rate")));
    v.push_back (tabs (350, 28, 230, 22, "Source", TR_NOOP ("FL LFO|LFO|Rhythm steps")));
    v.push_back (when (dial (465, 104, 52, "Rate", TR_NOOP ("Rate"), big), "Source", 0.25f, 0.75f));
    v.push_back (when (dial (410, 104, 40, "Period", TR_NOOP ("Period")), "Source", 0.0f, 0.25f));
    v.push_back (when (dial (520, 104, 40, "Phase", TR_NOOP ("Phase")), "Source", 0.0f, 0.25f));
    v.push_back (when (stepper (366, 84, 90, 22, "Steps", TR_NOOP ("Steps"), 6), "Source", 0.75f, 1.0f));
    v.push_back (when (tabs (466, 84, 110, 22, "Step note", TR_NOOP ("1/1|1/2|1/4|1/8|1/16|1/32")), "Source", 0.75f, 1.0f));
    v.push_back (when (tabs (380, 190, 170, 26, "Direction", TR_NOOP ("<|<  >|>"), { 1.0f, 0.0f, 0.5f }), "Source", 0.25f, 1.0f));
    v.push_back (section (596, 4, 160, 242, TR_NOOP ("Level")));
    v.push_back (dial (636, 62, 32, "Input", TR_NOOP ("Input"), bipolar));
    v.push_back (dial (716, 62, 32, "Output", TR_NOOP ("Output"), bipolar));
    v.push_back (display (616, 116, 120, 92, "iometer"));
    v.push_back (tabs (610, 216, 132, 22, "Analog", TR_NOOP ("Digital|Analog")));
    return l;
}

// round 136 (merged by round 138): Auto-pan's Rhythm voicing — the PanMan model measured on the official trial
// (core/RhythmPan.h, REA_rodada136 §2) — gets its own controls; the panner above shows while it is off. The VOICING
// switch at the bottom flips between them; her saved PanMan states open on the Rhythm voicing.
Layout withRhythmVoicing (Layout l)
{
    for (auto& it : l.items)
    {
        if (it.whenParam != nullptr) // keep the item's own condition as the second one
        {
            it.when3Param = it.when2Param; // round tp172c: and a second one as the third
            it.when3Min = it.when2Min;
            it.when3Max = it.when2Max;
            it.when2Param = it.whenParam;
            it.when2Min = it.whenMin;
            it.when2Max = it.whenMax;
            it.whenParam = nullptr;
        }
        it = when (it, "Voicing", 0.0f, 0.5f);
    }
    l.height = 280;
    auto add = [&l] (LayoutItem it) { l.items.push_back (when (it, "Voicing", 0.5f, 1.0f)); };
    const auto rhythmFirst = l.items.size();
    // round tp172d (look slice): PanMan's face — the name top left, then three bowed pods: PAN (Offset / Width /
    // Smoothing, captions above, scale words under, the red L-C-R strip), RATE (the big Rate with its range, the
    // pattern keys under it: ◀ ramp right to left, ◀▶ left / right, ▶ ramp left to right, plus LYNX's 8 steps), LEVEL
    // (Input, Output, the meter, the analog keys); no value text under the knobs (the tip shows it while dragging)
    add (label (12, 4, 210, 34, "Auto-Pan", dim | alignLeft));
    add (label (12, 34, 300, 14, TR_NOOP ("Rhythmic automatic panner"), alignLeft));
    add (section (4, 54, 330, 192, nullptr, boxed));
    add (hwKnob (62, 104, 46, "Offset", TR_NOOP ("Offset"), bipolar));
    add (hwKnob (168, 98, 46, "Width", TR_NOOP ("Width")));
    add (hwKnob (274, 104, 46, "Rhythm smoothing", TR_NOOP ("Smoothing")));
    add (label (28, 132, 36, 12, TR_NOOP ("Left"), dim));
    add (label (62, 132, 36, 12, TR_NOOP ("Right"), dim));
    add (label (134, 126, 36, 12, TR_NOOP ("Min"), dim));
    add (label (168, 126, 36, 12, TR_NOOP ("Max"), dim));
    add (label (240, 132, 36, 12, TR_NOOP ("Hard"), dim));
    add (label (274, 132, 36, 12, TR_NOOP ("Soft"), dim));
    add (display (24, 166, 290, 24, "panpos"));
    add (label (24, 196, 30, 16, TR_NOOP ("L"), alignLeft));
    add (label (154, 196, 30, 16, TR_NOOP ("C")));
    add (label (284, 196, 30, 16, TR_NOOP ("R")));
    add (section (340, 54, 250, 192, nullptr, boxed));
    add (when (hwKnob (465, 104, 50, "Rhythm rate", TR_NOOP ("Rate"), big), "Sync", 0.0f, 0.5f));
    add (when (hwKnob (465, 104, 50, "Beats", TR_NOOP ("Beats"), big), "Sync", 0.5f, 1.0f));
    add (when (label (400, 134, 50, 12, "100 sec", dim), "Sync", 0.0f, 0.5f));
    add (when (label (482, 134, 50, 12, "10 Hz", dim), "Sync", 0.0f, 0.5f));
    add (tabs (380, 158, 170, 28, "Pattern", "<|<  >|>|8", { 0.5f, 0.0f, 0.75f, 1.0f }));
    add (hwKnob (372, 218, 22, "Start phase", TR_NOOP ("Phase")));
    add (tabs (420, 206, 90, 20, "Sync", TR_NOOP ("Hz|Beats")));
    add (hwKnob (558, 218, 22, "Amount", TR_NOOP ("Amount")));
    add (section (596, 54, 160, 192, nullptr, boxed));
    add (hwKnob (636, 104, 40, "Input", TR_NOOP ("Input"), bipolar)); // PanMan's INPUT (round tp172d: the Rhythm voicing reads it too)
    for (auto& w : rangeWords (636, 128, 40, "-24", "+24")) add (w);
    add (hwKnob (716, 104, 40, "Rhythm output", TR_NOOP ("Output"), bipolar));
    for (auto& w : rangeWords (716, 128, 40, "-24", "+24")) add (w);
    add (display (616, 152, 120, 34, "iometer"));
    add (tabs (610, 194, 132, 20, "Rhythm analog", TR_NOOP ("Digital|Analog|Hot"), { 0.0f, 0.5f, 1.0f }));
    l.items.push_back (toggle (8, 254, 300, 20, "Voicing", TR_NOOP ("Measured rhythm voicing")));
    // round 150: PanMan's Tweak page role — the input's envelope moves the rate, the offset and the width (its threshold
    // and range), Feel / Groove of the steps, and the angle past the speakers
    l.height = 360;
    add (section (4, 280, 752, 76, TR_NOOP ("Tweak")));
    add (dial (60, 318, 30, "Rate mod", TR_NOOP ("Rate"), bipolar));
    add (dial (150, 318, 30, "Offset mod", TR_NOOP ("Offset"), bipolar));
    add (dial (240, 318, 30, "Width mod", TR_NOOP ("Width"), bipolar));
    add (dial (340, 318, 30, "Env threshold", TR_NOOP ("Thresh.")));
    add (dial (430, 318, 30, "Env range", TR_NOOP ("Range")));
    add (dial (530, 318, 30, "Feel", TR_NOOP ("Feel"), bipolar));
    add (dial (610, 318, 30, "Groove", TR_NOOP ("Groove"), bipolar));
    add (toggle (664, 308, 86, 20, "Beyond speakers", TR_NOOP ("Beyond")));
    // round 152: PanMan's trigger sources (the input past a threshold, or the manual trigger, moves the pan every
    // `divider` triggers: to the other side, or to a random place), the detector's filter, and the Random source
    l.height = 444;
    add (section (4, 360, 752, 80, TR_NOOP ("Trigger")));
    add (tabs (14, 390, 230, 22, "Trigger", TR_NOOP ("Clock|Ping-pong|Random step"), { 0.0f, 0.5f, 1.0f }));
    add (dial (284, 398, 30, "Trig threshold", TR_NOOP ("Thresh.")));
    add (stepper (318, 390, 84, 22, "Divider", TR_NOOP ("Divider"), 11));
    add (tabs (412, 390, 132, 22, "Trig filter", TR_NOOP ("Off|HP|LP"), { 0.0f, 0.5f, 1.0f }));
    add (dial (578, 398, 30, "Trig freq", TR_NOOP ("Freq")));
    add (toggle (618, 380, 132, 20, "Manual trigger", TR_NOOP ("Trigger now"), plugui::momentary));
    add (toggle (618, 410, 132, 20, "Random steps", TR_NOOP ("Random steps")));
    // round tp172d: PanMan's look — the Rhythm voicing's Tweak and Trigger rows move to a second page behind the
    // TWEAK key (PanMan's own), so the panel is the three pods and the name; the plain panner (Voicing off) shows on
    // both pages. Room for the walnut cheeks either side
    for (auto i = rhythmFirst; i < l.items.size(); ++i)
    {
        auto& it = l.items[i];
        if (it.whenParam == nullptr) // the Voicing switch itself
            continue;
        it.page = it.y >= 278.0f ? 1 : 0;
        if (it.page == 1)
            it.y -= 276.0f;
    }
    l.height = 290;
    l.pages = { "Main", "Tweak" };
    l.items.push_back (when (pageTabs (610, 222, 132, 20, TR_NOOP ("Main|Tweak")), "Voicing", 0.5f, 1.0f)); // PanMan's TWEAK key
    l.width += 56;
    for (auto& item : l.items)
        item.x += 28.0f;
    return l;
}

// round 145: Ridge Strip's Vocal strip voicing (core/VoxStrip.h; the role: a vocal channel strip with de-esser,
// de-ploser, three-band EQ and compander, measured on the official trial) gets its own panel in the reference's
// reading order: the level block on top, the COMPANDER, then DE-ESSER and DE-PLOSER side by side, the EQUALISER, the
// process order. Ridge's panel shows while it is off; the switch at the bottom flips between them.
LayoutItem choiceMenu (float x, float y, float w, float h, const char* param, const char* caption, const char* options);

Layout withVocalVoicing (Layout l)
{
    for (auto& it : l.items)
    {
        if (it.whenParam != nullptr)
        {
            it.when3Param = it.when2Param; // round tp172c: Ridge's Modern / 4K items carry two conditions already
            it.when3Min = it.when2Min;
            it.when3Max = it.when2Max;
            it.when2Param = it.whenParam;
            it.when2Min = it.whenMin;
            it.when2Max = it.whenMax;
            it.whenParam = nullptr;
        }
        it = when (it, "Vocal strip", 0.0f, 0.5f);
    }
    l.height = std::max (l.height, 744);
    // round tp172c: the panel was drawn for a 560-wide editor; a wider one (Ridge's three columns) spreads it out
    // (positions, and the widths of everything but the knobs)
    const auto sx = (float) l.width / 560.0f;
    auto add = [&l, sx] (LayoutItem it)
    {
        if (it.type == Item::knob)
            it.x = (it.x + it.w * 0.5f) * sx - it.w * 0.5f;
        else
            it.x *= sx, it.w *= sx;
        l.items.push_back (when (it, "Vocal strip", 0.5f, 1.0f));
    };
    add (section (4, 4, 552, 128, TR_NOOP ("Level")));
    add (dial (50, 64, 40, "Vocal input", TR_NOOP ("Input"), bipolar));
    add (display (100, 26, 250, 96, "iometer"));
    add (dial (396, 64, 40, "Vocal output", TR_NOOP ("Output"), bipolar));
    add (choiceMenu (440, 40, 108, 24, "Process order", TR_NOOP ("Order"), "DS>DP>EQ>COMP|DS>DP>COMP>EQ|DS>EQ>DP>COMP|DS>EQ>COMP>DP|DS>COMP>DP>EQ|DS>COMP>EQ>DP|DP>DS>EQ>COMP|DP>DS>COMP>EQ|DP>EQ>DS>COMP|DP>EQ>COMP>DS|DP>COMP>DS>EQ|DP>COMP>EQ>DS|EQ>DS>DP>COMP|EQ>DS>COMP>DP|EQ>DP>DS>COMP|EQ>DP>COMP>DS|EQ>COMP>DS>DP|EQ>COMP>DP>DS|COMP>DS>DP>EQ|COMP>DS>EQ>DP|COMP>DP>DS>EQ|COMP>DP>EQ>DS|COMP>EQ>DS>DP|COMP>EQ>DP>DS"));
    add (section (4, 136, 552, 232, TR_NOOP ("Compander")));
    add (toggle (14, 158, 64, 20, "Compander", TR_NOOP ("On")));
    add (dial (60, 220, 34, "Compander attack", TR_NOOP ("Attack")));
    add (dial (60, 310, 34, "Compander release", TR_NOOP ("Release")));
    add (dial (160, 240, 36, "Compander ratio", TR_NOOP ("Ratio")));
    add (dial (264, 230, 48, "Compander threshold", TR_NOOP ("Threshold"), big));
    add (dial (370, 220, 34, "Expander threshold", TR_NOOP ("Expander")));
    add (dial (370, 310, 34, "Make-up", TR_NOOP ("Make-up")));
    add (toggle (128, 330, 72, 22, "Soft knee", TR_NOOP ("Soft knee")));
    add (toggle (226, 330, 72, 22, "Drive", TR_NOOP ("Drive")));
    add (meter (426, 170, 120, 12, 0, TR_NOOP ("GR")));
    add (section (4, 372, 274, 170, TR_NOOP ("De-esser")));
    add (toggle (14, 394, 64, 20, "De-esser", TR_NOOP ("On")));
    add (dial (70, 456, 34, "De-ess threshold", TR_NOOP ("Threshold")));
    add (dial (170, 456, 34, "De-ess freq.", TR_NOOP ("Frequency")));
    add (tabs (90, 394, 180, 20, "De-ess split", TR_NOOP ("Broadband|Split")));
    add (toggle (226, 440, 46, 20, "De-ess lookahead", TR_NOOP ("Look")));
    add (toggle (226, 470, 46, 20, "De-ess listen", TR_NOOP ("Listen")));
    add (meter (14, 520, 254, 12, 1, TR_NOOP ("GR")));
    add (section (282, 372, 274, 170, TR_NOOP ("De-ploser")));
    add (toggle (292, 394, 64, 20, "De-ploser", TR_NOOP ("On")));
    add (dial (348, 456, 34, "De-plos threshold", TR_NOOP ("Threshold")));
    add (dial (448, 456, 34, "De-plos freq.", TR_NOOP ("Frequency")));
    add (tabs (368, 394, 180, 20, "De-plos split", TR_NOOP ("Broadband|Split")));
    add (toggle (504, 440, 46, 20, "De-plos lookahead", TR_NOOP ("Look")));
    add (toggle (504, 470, 46, 20, "De-plos listen", TR_NOOP ("Listen")));
    add (section (4, 546, 552, 168, TR_NOOP ("Equaliser")));
    add (toggle (14, 568, 64, 20, "Vocal EQ", TR_NOOP ("On")));
    add (toggle (60, 600, 90, 20, "Vocal HPF", TR_NOOP ("High-pass")));
    add (dial (104, 660, 34, "Vocal HPF freq.", TR_NOOP ("Freq")));
    add (toggle (210, 600, 90, 20, "Notch", TR_NOOP ("Notch")));
    add (dial (220, 660, 34, "Notch gain", TR_NOOP ("Gain"), bipolar));
    add (dial (300, 660, 34, "Notch freq.", TR_NOOP ("Freq")));
    add (toggle (390, 600, 90, 20, "High band", TR_NOOP ("High band")));
    add (dial (400, 660, 34, "High gain", TR_NOOP ("Gain"), bipolar));
    add (dial (480, 660, 34, "High freq.", TR_NOOP ("Freq")));
    l.items.push_back (toggle (8, 720, 300, 20, "Vocal strip", TR_NOOP ("Vocal strip voicing")));
    return l;
}

Layout voiceShiftLayout()
{
    // round 135: the voice manipulators' one-screen panel (the role: a small Soundtoys voice shifter, screenshot of the
    // official trial): PITCH and FORMANT side by side with LINK between them and a read-out under each, the three
    // MODE buttons in the middle, DRIVE and MIX on the right. Round tp172d (look slice): the original's black face
    // framing the three printed fields, big legends over the knobs, MIN / MAX and DRY / WET under drive and mix (their
    // value shows while dragged), the name along the bottom of the face
    Layout l;
    l.width = 640;
    l.height = 272;
    auto& v = l.items;
    v.push_back (section (4, 4, 632, 264, nullptr, boxed)); // the face
    v.push_back (section (14, 14, 292, 204, nullptr, boxed));
    v.push_back (label (34, 24, 92, 22, TR_NOOP ("Pitch")));
    v.push_back (knob (80, 102, 74, "Pitch", TR_NOOP ("Pitch"), 0, (std::uint32_t) (big | bipolar | noCaption)));
    v.push_back (value (44, 172, 72, 26, "Pitch", boxed));
    v.push_back (toggle (117, 89, 74, 48, "Link", TR_NOOP ("Link"), captionAbove)); // tp172d: a small key on a line between the knobs, LINK under it
    v.push_back (label (182, 24, 92, 22, TR_NOOP ("Formant")));
    v.push_back (knob (228, 102, 74, "Formant", TR_NOOP ("Formant"), 0, (std::uint32_t) (big | bipolar | noCaption)));
    v.push_back (value (192, 172, 72, 26, "Formant", boxed));
    v.push_back (section (314, 14, 124, 204, TR_NOOP ("Mode")));
    v.push_back (radio (322, 50, 110, 112, "Mode", TR_NOOP ("Transpose|Quantize|Robot")));
    // round 137: MIDI control (the notes held on a Channel Rack channel set the pitch / the robot's note)
    v.push_back (stepper (320, 168, 112, 34, "MIDI", TR_NOOP ("MIDI channel"), 129));
    v.push_back (section (446, 14, 180, 204, nullptr, boxed));
    v.push_back (label (458, 24, 76, 22, TR_NOOP ("Drive")));
    v.push_back (knob (496, 104, 66, "Drive", TR_NOOP ("Drive"), 0, (std::uint32_t) (big | noCaption)));
    v.push_back (label (460, 140, 36, 14, TR_NOOP ("Min"), dim));
    v.push_back (label (498, 140, 36, 14, TR_NOOP ("Max"), dim));
    v.push_back (label (546, 24, 76, 22, TR_NOOP ("Mix")));
    v.push_back (knob (584, 104, 66, "Mix", TR_NOOP ("Mix"), 0, (std::uint32_t) (big | noCaption)));
    v.push_back (label (548, 140, 36, 14, TR_NOOP ("Dry"), dim));
    v.push_back (label (586, 140, 36, 14, TR_NOOP ("Wet"), dim));
    v.push_back (label (14, 226, 300, 34, "voice|shift", plugui::wordmark));
    // round 190dp: projects saved before the measured unvoiced path keep the classic one until switched
    v.push_back (when (tabs (446, 232, 180, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys), "Law", 0.0f, 0.5f));
    return l;
}

// round 164d: the hardware insert's panel (LYNX's own; the roles: Pro Tools' H/W Insert and Nuendo's External FX): left
// to right the way the signal goes — SEND (where it leaves, how loud), RETURN (where it comes back, how loud, phase),
// their levels, the ROUND TRIP (the ping and its read-out) and the MIX.
Layout hardwareInsertLayout()
{
    Layout l;
    l.width = 784;
    l.height = 236;
    auto& v = l.items;
    v.push_back (section (4, 4, 186, 228, TR_NOOP ("Send")));
    v.push_back (label (16, 28, 162, 14, TR_NOOP ("Output"), alignLeft));
    v.push_back (choiceMenu (16, 44, 162, 26, "Output", TR_NOOP ("Output"), "None|Out 1"));
    v.push_back (toggle (16, 80, 162, 18, "Output mono", TR_NOOP ("Mono (L+R to one output)")));
    v.push_back (dial (97, 148, 62, "Send", TR_NOOP ("Send"), big));
    v.push_back (section (194, 4, 186, 228, TR_NOOP ("Return")));
    v.push_back (label (206, 28, 162, 14, TR_NOOP ("Input"), alignLeft));
    v.push_back (choiceMenu (206, 44, 162, 26, "Input", TR_NOOP ("Input"), "None|In 1"));
    v.push_back (toggle (206, 80, 162, 18, "Input stereo", TR_NOOP ("Stereo pair")));
    v.push_back (dial (256, 148, 62, "Return", TR_NOOP ("Return"), big));
    v.push_back (toggle (304, 138, 66, 22, "Invert", TR_NOOP ("Invert")));
    v.push_back (section (384, 4, 114, 228, TR_NOOP ("Levels")));
    auto io = display (392, 26, 98, 198, "iometer");
    io.steps = 2; // send / return peaks
    v.push_back (io);
    v.push_back (section (502, 4, 180, 228, TR_NOOP ("Round trip")));
    v.push_back (display (510, 26, 164, 96, "hwping"));
    v.push_back (toggle (510, 130, 164, 26, "Ping", TR_NOOP ("Ping"), momentary));
    v.push_back (stepper (510, 184, 164, 34, "Round trip", TR_NOOP ("Delay (samples)"), 65536));
    v.push_back (section (686, 4, 94, 228, TR_NOOP ("Mix")));
    v.push_back (dial (733, 100, 62, "Dry/wet", TR_NOOP ("Dry/wet"), big));
    return l;
}

// --- LYNX's reverbs (REA round 100): own panels, the same conventions as the mixing suite. The algorithm / type
// menu sits top-left with the Mix, the time controls (pre-delay, decay, size) in the middle, colour and
// modulation to the right. -------------------------------------------------------------------------------------
LayoutItem choiceMenu (float x, float y, float w, float h, const char* param, const char* caption, const char* options)
{
    LayoutItem i;
    i.type = Item::menu;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.options = options;
    return i;
}

// The reverbs keep the reference plugins' layout logic (REA round 100, screenshots of the official demos in
// tmp/valh100/ref): the same control groups in the same order — mix and pre-delay first, the decay as the largest
// control, then grouped boxes, the algorithm / mode menu along the bottom — drawn in LYNX's own style.
// round 114: the reference reverbs / delays write each knob's name above it and its value under it
Layout captionsAbove (Layout l)
{
    for (auto& i : l.items)
        if (i.type == Item::knob && (i.style & showValue) != 0 && (i.style & noCaption) == 0)
        {
            i.style = (std::uint32_t) (i.style | captionAbove);
            i.y -= 14.0f;
        }
    return l;
}

Layout epochVerbLayout()
{
    // Mix / Predelay · big Decay · Damping (2 × 2) · Shape · Diffusion · Modulation · EQ; Mode and Color menus below
    Layout l;
    l.width = 796;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (8, 8, 92, 244, nullptr, boxed));
    v.push_back (dial (54, 50, 40, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (54, 164, 40, "Predelay", TR_NOOP ("Predelay")));
    v.push_back (section (104, 8, 132, 244, nullptr, boxed));
    v.push_back (dial (170, 104, 92, "Decay", TR_NOOP ("Decay"), big));
    v.push_back (section (240, 8, 176, 244, TR_NOOP ("Damping")));
    v.push_back (dial (284, 64, 36, "High freq", TR_NOOP ("High freq")));
    v.push_back (dial (372, 64, 36, "High shelf", TR_NOOP ("High shelf"), bipolar));
    v.push_back (dial (284, 168, 36, "Bass xover", TR_NOOP ("Bass freq")));
    v.push_back (dial (372, 168, 36, "Bass mult", TR_NOOP ("Bass mult")));
    v.push_back (section (420, 8, 88, 244, TR_NOOP ("Shape")));
    v.push_back (dial (464, 64, 36, "Size", TR_NOOP ("Size")));
    v.push_back (dial (464, 168, 36, "Attack", TR_NOOP ("Attack")));
    v.push_back (section (512, 8, 88, 244, TR_NOOP ("Diffusion")));
    v.push_back (dial (556, 64, 36, "Early diffusion", TR_NOOP ("Early")));
    v.push_back (dial (556, 168, 36, "Late diffusion", TR_NOOP ("Late")));
    v.push_back (section (604, 8, 88, 244, TR_NOOP ("Mod")));
    v.push_back (dial (648, 64, 36, "Mod rate", TR_NOOP ("Rate")));
    v.push_back (dial (648, 168, 36, "Mod depth", TR_NOOP ("Depth")));
    v.push_back (section (696, 8, 92, 244, TR_NOOP ("EQ")));
    v.push_back (dial (742, 64, 36, "High cut", TR_NOOP ("High cut")));
    v.push_back (dial (742, 168, 36, "Low cut", TR_NOOP ("Low cut")));
    v.push_back (label (8, 264, 52, 24, TR_NOOP ("Mode"), alignLeft));
    v.push_back (choiceMenu (60, 264, 200, 24, "Mode", TR_NOOP ("Mode"),
                             TR_NOOP ("Concert Hall|Plate|Room|Chamber|Random Space|Chorus Space|Ambience|Bright Hall|Nave|Grit Hall|Grit Plate|Smooth Plate|Smooth Room|Smooth Space|Gated|Drift Chamber|Drift Hall|Drift Space|Cathedral|Grand Hall|Vintage Chamber|Vintage Hall")));
    v.push_back (label (290, 264, 52, 24, TR_NOOP ("Color"), alignLeft));
    v.push_back (choiceMenu (342, 264, 120, 24, "Color", TR_NOOP ("Color"), TR_NOOP ("1970s|1980s|Now")));
    // round 190bz: the engine law (Measured = the 0.1.27 law, the default of new instances; Classic = the engine of
    // projects saved before it)
    v.push_back (tabs (630, 265, 158, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

Layout atriumLayout()
{
    // six vertical faders on the left (mix, pre-delay, decay, low cut, high cut, depth = early / late balance); on the
    // right the EARLY or the LATE controls, switched by two tabs as on the reference (round 129; the two sections were
    // side by side before); the reverb mode menu below
    Layout l;
    l.width = 640;
    l.height = 300;
    l.pages = { TR_NOOP ("Early"), TR_NOOP ("Late") };
    auto& v = l.items;
    v.push_back (section (8, 8, 330, 244, nullptr, boxed));
    const char* faders[] = { "Mix", "Predelay", "Decay", "Low cut", "High cut", "Early/late" };
    const char* captions[] = { TR_NOOP ("Mix"), TR_NOOP ("Predelay"), TR_NOOP ("Decay"), TR_NOOP ("Low cut"), TR_NOOP ("High cut"), TR_NOOP ("Depth") };
    for (int i = 0; i < 6; ++i)
    {
        const auto x = 16.0f + 53.0f * (float) i;
        v.push_back (slider (Item::vslider, x + 8.0f, 18, 32, 196, faders[i], captions[i]));
        v.push_back (value (x, 222, 48, 16, faders[i], 0));
    }
    v.push_back (section (342, 8, 290, 244, nullptr, boxed));
    v.push_back (pageTabs (352, 16, 270, 22, TR_NOOP ("Early|Late")));
    v.push_back (onPage (dial (392, 84, 34, "Early size", TR_NOOP ("Size")), 0));
    v.push_back (onPage (dial (456, 84, 34, "Early cross", TR_NOOP ("Cross")), 0));
    v.push_back (onPage (dial (520, 84, 34, "Early mod rate", TR_NOOP ("Mod rate")), 0));
    v.push_back (onPage (dial (584, 84, 34, "Early mod depth", TR_NOOP ("Mod depth")), 0));
    v.push_back (onPage (dial (392, 184, 34, "Early send", TR_NOOP ("Send")), 0));
    v.push_back (onPage (dial (456, 184, 34, "Diffusion", TR_NOOP ("Diffusion")), 0));
    v.push_back (onPage (dial (520, 184, 34, "Space", TR_NOOP ("Space")), 0));
    v.push_back (onPage (dial (392, 84, 34, "Late size", TR_NOOP ("Size")), 1));
    v.push_back (onPage (dial (456, 84, 34, "Late cross", TR_NOOP ("Cross")), 1));
    v.push_back (onPage (dial (520, 84, 34, "Late mod rate", TR_NOOP ("Mod rate")), 1));
    v.push_back (onPage (dial (584, 84, 34, "Late mod depth", TR_NOOP ("Mod depth")), 1));
    v.push_back (onPage (dial (392, 184, 34, "Bass mult", TR_NOOP ("Bass mult")), 1));
    v.push_back (onPage (dial (456, 184, 34, "Bass xover", TR_NOOP ("Bass xover")), 1));
    v.push_back (onPage (dial (520, 184, 34, "High mult", TR_NOOP ("High mult")), 1));
    v.push_back (onPage (dial (584, 184, 34, "High xover", TR_NOOP ("High xover")), 1));
    v.push_back (label (8, 264, 92, 24, TR_NOOP ("Reverb mode"), alignLeft));
    v.push_back (choiceMenu (102, 264, 200, 24, "Type", TR_NOOP ("Type"),
                             TR_NOOP ("Large Room|Medium Room|Bright Room|Large Chamber|Dark Room|Dark Chamber|Dark Space|Deep Space|Mirror Space|Vast Space|Distant Space|Dense Room")));
    // round 190bz: the engine law (Measured = the 0.1.27 law, the default of new instances; Classic = the engine of
    // projects saved before it)
    v.push_back (tabs (474, 265, 158, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

Layout foilPlateLayout()
{
    // Mix / Predelay · big Decay · Size / Width · EQ (2 × 2) · Mod; the plate type below. Round 152: a second view,
    // "Plate" — the one-knob panel of the Little Plate role (the reference's Decay in the middle, Low Cut and Mod beside
    // it, Mix), chosen by the View switch (the Little Plate substitute opens in it)
    Layout l;
    l.width = 720;
    l.height = 316;
    auto& v = l.items;
    const auto full = [&] (LayoutItem it) { it.whenParam = "View"; it.whenMin = 0.0f; it.whenMax = 0.5f; v.push_back (it); };
    const auto plate = [&] (LayoutItem it) { it.whenParam = "View"; it.whenMin = 0.5f; it.whenMax = 1.0f; v.push_back (it); };
    full (section (8, 8, 92, 244, nullptr, boxed));
    full (dial (54, 50, 40, "Mix", TR_NOOP ("Mix")));
    full (dial (54, 164, 40, "Predelay", TR_NOOP ("Predelay")));
    full (section (104, 8, 200, 244, nullptr, boxed));
    full (dial (204, 100, 120, "Decay", TR_NOOP ("Decay"), big));
    full (section (308, 8, 92, 244, nullptr, boxed));
    full (dial (354, 50, 40, "Size", TR_NOOP ("Size")));
    full (dial (354, 164, 40, "Width", TR_NOOP ("Width")));
    full (section (404, 8, 200, 244, TR_NOOP ("EQ")));
    full (dial (454, 64, 36, "Low EQ freq", TR_NOOP ("Low freq")));
    full (dial (554, 64, 36, "High EQ freq", TR_NOOP ("High freq")));
    full (dial (454, 168, 36, "Low EQ gain", TR_NOOP ("Low gain"), bipolar));
    full (dial (554, 168, 36, "High EQ gain", TR_NOOP ("High gain"), bipolar));
    full (section (608, 8, 104, 244, TR_NOOP ("Mod")));
    full (dial (660, 64, 36, "Mod rate", TR_NOOP ("Rate")));
    full (dial (660, 168, 36, "Mod depth", TR_NOOP ("Depth")));
    // round 135 (the Little Plate role): the decay's ∞ and a low cut
    full (toggle (330, 266, 90, 22, "Infinite", TR_NOOP ("Infinite")));
    full (dial (470, 274, 22, "Low cut", TR_NOOP ("Low cut")));
    // round 150: the measured plate voicing (the Little Plate role: no plate delay, its decay per band)
    full (toggle (560, 266, 150, 22, "Voicing", TR_NOOP ("Measured plate")));
    full (label (8, 264, 52, 24, TR_NOOP ("Mode"), alignLeft));
    full (choiceMenu (60, 264, 180, 24, "Type", TR_NOOP ("Type"), TR_NOOP ("Silver|Pewter|Nickel|Bronze|Bismuth|Tin|Zinc|Platinum|Tungsten|Iron|Gold|Lead")));
    // round 152: the Plate view — Low Cut on the left, the big Decay (its ∞ under it), Mod and Mix on the right
    plate (section (8, 8, 704, 244, nullptr, boxed));
    plate (dial (140, 120, 52, "Low cut", TR_NOOP ("Low cut")));
    plate (dial (360, 112, 132, "Decay", TR_NOOP ("Decay"), big));
    plate (toggle (315, 228, 90, 20, "Infinite", TR_NOOP ("Infinite")));
    plate (dial (556, 70, 36, "Mod depth", TR_NOOP ("Mod")));
    plate (dial (556, 176, 44, "Mix", TR_NOOP ("Mix")));
    plate (toggle (24, 222, 150, 22, "Voicing", TR_NOOP ("Measured plate")));
    v.push_back (tabs (560, 292, 150, 20, "View", TR_NOOP ("Full|Plate")));
    // round 190bz: the engine law (Measured = the 0.1.27 law, the default of new instances; Classic = the engine of
    // projects saved before it)
    v.push_back (tabs (60, 292, 150, 20, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

Layout nebulaLayout()
{
    // Mix / Width · big Delay and Warp with the sync menu under them · Feedback / Density · Mod · EQ; mode below
    Layout l;
    l.width = 704;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (8, 8, 92, 244, nullptr, boxed));
    v.push_back (dial (54, 50, 40, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (54, 164, 40, "Width", TR_NOOP ("Width"), bipolar));
    v.push_back (section (104, 8, 284, 244, nullptr, boxed));
    v.push_back (dial (176, 84, 84, "Delay", TR_NOOP ("Delay"), big));
    v.push_back (dial (316, 84, 84, "Warp", TR_NOOP ("Warp"), big));
    v.push_back (tabs (114, 216, 168, 24, "Sync", TR_NOOP ("ms|Note|Dotted|Triplet")));
    v.push_back (toggle (290, 186, 88, 22, "Clear", TR_NOOP ("Clear"), plugui::momentary));
    v.push_back (choiceMenu (290, 216, 88, 24, "Note", TR_NOOP ("Note"), "1/64|1/32|1/16|1/8|1/4|1/2|1/1"));
    v.push_back (section (392, 8, 100, 244, nullptr, boxed));
    v.push_back (dial (442, 50, 40, "Feedback", TR_NOOP ("Feedback")));
    v.push_back (dial (442, 164, 40, "Density", TR_NOOP ("Density")));
    v.push_back (section (496, 8, 100, 244, TR_NOOP ("Mod")));
    v.push_back (dial (546, 64, 36, "Mod rate", TR_NOOP ("Rate")));
    v.push_back (dial (546, 168, 36, "Mod depth", TR_NOOP ("Depth")));
    v.push_back (section (600, 8, 96, 244, TR_NOOP ("EQ")));
    v.push_back (dial (648, 64, 36, "High cut", TR_NOOP ("High")));
    v.push_back (dial (648, 168, 36, "Low cut", TR_NOOP ("Low")));
    v.push_back (label (8, 264, 52, 24, TR_NOOP ("Mode"), alignLeft));
    v.push_back (choiceMenu (60, 264, 180, 24, "Mode", TR_NOOP ("Mode"),
                             TR_NOOP ("Ripple|Swell|Fold|Drift|Abyss|Vortex|Glint|Pulse|Expanse|Haze|Veil|Mist|Monolith|Eternity|Tide|Shoal|Spark|Plateau|Dusk|Mirage|Cluster|Beacon")));
    // round 190ca: the cloud law (six of the modes run on core/NebulaCloud; classic = what a project saved before loads with)
    v.push_back (label (264, 264, 56, 24, TR_NOOP ("Model"), alignLeft));
    v.push_back (tabs (320, 264, 180, 24, "Law", TR_NOOP ("Classic|Measured")));
    return l;
}

Layout haloLayout()
{
    // round tp172d (look slice), the original's plain face: a wide-spaced two-tone name across the top, one row of nine
    // big discs in signal order (mix, shift, feedback, diffusion, size, low cut, high cut, mod rate, mod depth) with the
    // caption above and the value under each, the three modes as "name: value" text along the bottom (a click on the
    // value opens its menu)
    Layout l;
    l.width = 700;
    l.height = 262;
    auto& v = l.items;
    v.push_back (label (20, 4, 660, 62, "lynx|halo", plugui::wordmark));
    const char* knobs[] = { "Mix", "Shift", "Feedback", "Diffusion", "Size", "Low cut", "High cut", "Mod rate", "Mod depth" };
    const char* captions[] = { TR_NOOP ("Mix"), TR_NOOP ("Shift"), TR_NOOP ("Feedback"), TR_NOOP ("Diffusion"), TR_NOOP ("Size"),
                               TR_NOOP ("Low cut"), TR_NOOP ("High cut"), TR_NOOP ("Mod rate"), TR_NOOP ("Mod depth") };
    for (int i = 0; i < 9; ++i)
        v.push_back (dial (42.0f + 77.0f * (float) i, 134, 66, knobs[i], captions[i], i == 1 ? bipolar : 0));
    v.push_back (label (16, 216, 104, 28, TR_NOOP ("Reverb mode"), alignLeft));
    // round 135: the reference's four modes (mono in, three stereo sizes) first, LYNX's own three after them
    auto mode = choiceMenu (122, 216, 130, 28, "Reverb mode", TR_NOOP ("Reverb mode"),
                            TR_NOOP ("Mono|Small stereo|Medium stereo|Big stereo|Wide|Dense|Bright"));
    mode.values = { 0.125f, 0.25f, 0.375f, 0.625f, 0.0f, 0.5f, 1.0f }; // verbs::haloModeIndex (Wide / Dense / Bright keep 0 / ½ / 1)
    mode.style = plugui::plainMenu;
    v.push_back (mode);
    v.push_back (label (270, 216, 92, 28, TR_NOOP ("Pitch mode"), alignLeft));
    auto pitch = choiceMenu (364, 216, 130, 28, "Shift mode", TR_NOOP ("Pitch mode"), TR_NOOP ("Single|Dual|Single, bright|Dual, bright|No shift"));
    pitch.style = plugui::plainMenu;
    v.push_back (pitch);
    v.push_back (label (540, 216, 52, 28, TR_NOOP ("Color"), alignLeft));
    auto color = choiceMenu (594, 216, 100, 28, "Color", TR_NOOP ("Color"), TR_NOOP ("Bright|Dark"));
    color.style = plugui::plainMenu;
    v.push_back (color);
    // round 190bz: the engine law (Measured = the 0.1.27 law, the default of new instances; Classic = the engine of
    // projects saved before it)
    v.push_back (tabs (540, 246, 154, 14, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

Layout horizonLayout()
{
    // Mix / Width · Echo box (big Delay with its sync menu, Feedback / Level, Drive / Spread) · Reverb box (big Decay
    // with the routing switch, Size / Level, Early-late / Density) · Mod · EQ; echo mode, reverb mode, color below
    Layout l;
    l.width = 980;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (8, 8, 92, 244, nullptr, boxed));
    v.push_back (dial (54, 50, 40, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (54, 164, 40, "Width", TR_NOOP ("Width"), bipolar));
    v.push_back (section (104, 8, 320, 244, TR_NOOP ("Echo")));
    v.push_back (dial (172, 84, 76, "Echo time", TR_NOOP ("Delay"), big));
    v.push_back (tabs (112, 192, 120, 22, "Echo sync", TR_NOOP ("ms|Note|Dotted|Triplet")));
    v.push_back (choiceMenu (112, 220, 120, 22, "Echo note", TR_NOOP ("Note"), "1/64|1/32|1/16|1/8|1/4|1/2"));
    v.push_back (dial (290, 64, 34, "Echo feedback", TR_NOOP ("Feedback")));
    v.push_back (dial (370, 64, 34, "Echo level", TR_NOOP ("Level")));
    // round 135: only the controls the echo / reverb mode uses are shown, in the same place (the reference: Drive in
    // Clean / Tape / Digital / Analog / Lo-fi, Detune in the pitch modes; Attack instead of Early / late in Space and
    // Freeze) [EXEC: the trial's editor in every mode]
    constexpr float pitchLo = 3.5f / 11.0f, pitchHi = 10.5f / 11.0f; // echo modes 4 … 10 (choice (v, 12))
    v.push_back (when (dial (290, 168, 34, "Echo drive", TR_NOOP ("Drive")), "Echo mode", 0.0f, pitchLo));
    v.push_back (when (dial (290, 168, 34, "Echo drive", TR_NOOP ("Drive")), "Echo mode", pitchHi, 1.0f));
    v.push_back (when (dial (290, 168, 34, "Echo detune", TR_NOOP ("Detune"), bipolar), "Echo mode", pitchLo, pitchHi));
    v.push_back (dial (370, 168, 34, "Echo spread", TR_NOOP ("Spread")));
    v.push_back (section (428, 8, 320, 244, TR_NOOP ("Reverb")));
    v.push_back (dial (496, 84, 76, "Decay", TR_NOOP ("Decay"), big));
    v.push_back (tabs (436, 206, 120, 34, "Routing", TR_NOOP ("Echo > Rev|Rev > Echo")));
    v.push_back (dial (614, 64, 34, "Size", TR_NOOP ("Size")));
    v.push_back (dial (694, 64, 34, "Reverb level", TR_NOOP ("Level")));
    constexpr float attackLo = 4.5f / 7.0f, attackHi = 6.5f / 7.0f; // reverb modes 5 … 6 (choice (v, 8))
    v.push_back (when (dial (614, 168, 34, "Early/late", TR_NOOP ("Early / late"), bipolar), "Reverb mode", 0.0f, attackLo));
    v.push_back (when (dial (614, 168, 34, "Early/late", TR_NOOP ("Early / late"), bipolar), "Reverb mode", attackHi, 1.0f));
    v.push_back (when (dial (614, 168, 34, "Attack", TR_NOOP ("Attack")), "Reverb mode", attackLo, attackHi));
    v.push_back (dial (694, 168, 34, "Density", TR_NOOP ("Density")));
    v.push_back (section (752, 8, 104, 244, TR_NOOP ("Mod")));
    v.push_back (dial (804, 64, 36, "Mod rate", TR_NOOP ("Rate")));
    v.push_back (dial (804, 168, 36, "Mod depth", TR_NOOP ("Depth")));
    v.push_back (section (860, 8, 112, 244, TR_NOOP ("EQ")));
    v.push_back (dial (916, 64, 36, "High EQ freq", TR_NOOP ("High cut")));
    v.push_back (dial (916, 168, 36, "Low EQ freq", TR_NOOP ("Low cut")));
    v.push_back (label (8, 264, 82, 24, TR_NOOP ("Echo mode"), alignLeft));
    v.push_back (choiceMenu (92, 264, 170, 24, "Echo mode", TR_NOOP ("Echo mode"),
                             TR_NOOP ("Clean|Tape|Digital|Analog|Detune|Reverse|Reverse Up|Reverse Down|Reverse Up/Down|Sparkle|Swarm|Lo-fi")));
    v.push_back (label (428, 264, 92, 24, TR_NOOP ("Reverb mode"), alignLeft));
    v.push_back (choiceMenu (522, 264, 150, 24, "Reverb mode", TR_NOOP ("Reverb mode"), TR_NOOP ("Room|Chamber|Plate|Hall|Cathedral|Space|Freeze|Gated")));
    v.push_back (label (752, 264, 52, 24, TR_NOOP ("Color"), alignLeft));
    v.push_back (choiceMenu (806, 264, 166, 24, "Color", TR_NOOP ("Color"), TR_NOOP ("Bright|Neutral|Dark|Studio")));
    // round 190bz: the engine law (Measured = the 0.1.27 law, the default of new instances; Classic = the engine of
    // projects saved before it)
    v.push_back (tabs (272, 265, 148, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

// --- LYNX originals: the delay / modulation suite (REA round 101) --------------------------------------------------

// option values of a parameter read as floor (v · n): the middle of each step
std::vector<float> stepCentres (int n, int first = 0, int count = -1)
{
    std::vector<float> v;
    for (int k = first; k < (count < 0 ? n : first + count); ++k)
        v.push_back (((float) k + 0.5f) / (float) n);
    return v;
}

LayoutItem stepped (LayoutItem i, int steps)
{
    i.steps = steps;
    return i;
}

LayoutItem menuOf (float x, float y, float w, float h, const char* param, const char* caption, const char* options, std::vector<float> values)
{
    LayoutItem i;
    i.type = Item::menu;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.label = caption;
    i.options = options;
    i.values = std::move (values);
    return i;
}

// The suite's panels follow the ergonomics of the plugins they are inspired by (round 101 UI rule: the same grouping
// and the same place for each control, drawn in LYNX's own style): a row of small sections left to right, the
// sections whose meaning depends on the style / mode swap their knobs in place.
Layout echoformLayout()
{
    Layout l;
    l.width = 950;  // round 135w: + the TWEAK section and the output meter (was 780)
    l.height = 446; // round 135w: + a second row of switches (was 300); round 137: + the Rhythm row (was 330);
                    // round 144: + the rhythm editor (was 352)
    auto& v = l.items;
    // round tp172d: EchoBoy's arrangement (its role). The Main page: three rounded pods with big knobs — the echo
    // (Mix, Echo time, Feedback; Low cut, the style's second time, High cut; Prime numbers), the tempo (tap, Groove,
    // Feel, the echo mode) and the saturation (Saturation, Age, Output; the style menus). The Tweak page (EchoBoy hides
    // its Tweak panel the same way): the Tweak strip, the switch rows, Style edit / the rhythm editor
    l.height = 360; // (tp172d slice 8: + the name block, was 330)
    l.pages = { TR_NOOP ("Main"), TR_NOOP ("Tweak") };
    const auto mainFrom = v.size();
    // round tp172d (look slice): EchoBoy's face in LYNX's own drawing — captions above the knobs and the printed scale
    // words under them (no value text: the tip shows it while dragging), the time unit as TIME / NOTE / DOT / TRIP keys
    // over a red LED read-out, a TAP TEMPO pod with its key and LED tempo, the echo mode as a rotary with its modes
    // printed round it, Input / Saturation / Output and the style on LED read-outs in the third pod
    const std::vector<float> syncValues { 0.25f, 0.5f, 0.75f, 1.0f };
    // the echo pod
    v.push_back (section (4, 4, 446, 282, nullptr, boxed));
    v.push_back (label (4, 8, 446, 16, TR_NOOP ("Echo"))); // the pods' spaced small-caps titles
    v.push_back (hwKnob (80, 84, 66, "Mix", TR_NOOP ("Mix")));
    for (auto& w : rangeWords (80, 118, 66, TR_NOOP ("Dry"), TR_NOOP ("Wet"))) v.push_back (w);
    // round 129: "Delay L" when the style has a right time (dual, ping-pong), as on the reference
    v.push_back (when (when (hwKnob (226, 74, 66, "Time L", TR_NOOP ("Echo time"), big), "Sync L", 0.0f, 0.5f), "Style", 0.0f, 0.6f));
    v.push_back (when (when (stepped (hwKnob (226, 74, 66, "Note L", TR_NOOP ("Echo time"), big), 9), "Sync L", 0.5f, 1.0f), "Style", 0.0f, 0.6f));
    v.push_back (when (when (hwKnob (226, 74, 66, "Time L", TR_NOOP ("Delay L"), big), "Sync L", 0.0f, 0.5f), "Style", 0.6f, 1.0f));
    v.push_back (when (when (stepped (hwKnob (226, 74, 66, "Note L", TR_NOOP ("Delay L"), big), 9), "Sync L", 0.5f, 1.0f), "Style", 0.6f, 1.0f));
    v.push_back (tabs (154, 121, 144, 20, "Sync L", TR_NOOP ("Time|Note|Dot|Trip"), syncValues));
    v.push_back (when (value (172, 146, 108, 22, "Time L", boxed), "Sync L", 0.0f, 0.5f));
    v.push_back (when (value (172, 146, 108, 22, "Note L", boxed), "Sync L", 0.5f, 1.0f));
    v.push_back (hwKnob (372, 84, 66, "Feedback", TR_NOOP ("Feedback")));
    for (auto& w : rangeWords (372, 118, 66, TR_NOOP ("Min"), TR_NOOP ("Max"))) v.push_back (w);
    v.push_back (hwKnob (80, 222, 56, "Low cut", TR_NOOP ("LowCut"))); // (slice 8: printed as on the original)
    for (auto& w : rangeWords (80, 251, 56, TR_NOOP ("Min"), TR_NOOP ("Max"))) v.push_back (w);
    // the style's second control (spread, ratio, spacing or the right time) between the filters
    v.push_back (when (hwKnob (226, 212, 40, "Spread", TR_NOOP ("Spread"), bipolar), "Style", 0.0f, 0.2f));
    v.push_back (when (hwKnob (226, 212, 40, "Ratio", TR_NOOP ("Ratio")), "Style", 0.2f, 0.4f));
    v.push_back (when (hwKnob (226, 212, 40, "Spacing", TR_NOOP ("Spacing"), bipolar), "Style", 0.4f, 0.6f));
    v.push_back (when (when (hwKnob (226, 212, 40, "Time R", TR_NOOP ("Delay R")), "Style", 0.6f, 1.0f), "Sync R", 0.0f, 0.5f));
    v.push_back (when (when (stepped (hwKnob (226, 212, 40, "Note R", TR_NOOP ("Delay R")), 9), "Style", 0.6f, 1.0f), "Sync R", 0.5f, 1.0f));
    v.push_back (when (menuOf (258, 202, 70, 20, "Sync R", TR_NOOP ("Sync"), TR_NOOP ("ms|Note|Dotted|Triplet"), syncValues), "Style", 0.6f, 1.0f));
    // quad: Repeat / Swell where the right sync menu sits in the two-time styles
    v.push_back (when (tabs (258, 202, 76, 20, "Swell", TR_NOOP ("Repeat|Swell")), "Style", 0.4f, 0.6f));
    v.push_back (hwKnob (372, 222, 56, "High cut", TR_NOOP ("HighCut")));
    for (auto& w : rangeWords (372, 251, 56, TR_NOOP ("Min"), TR_NOOP ("Max"))) v.push_back (w);
    v.push_back (toggle (150, 256, 150, 24, "Prime numbers", TR_NOOP ("Prime numbers")));
    // the tap tempo pod: the TAP key and its LED tempo (sets the left time), Groove / Feel, the echo mode
    v.push_back (section (454, 4, 232, 282, nullptr, boxed));
    v.push_back (label (454, 8, 232, 16, TR_NOOP ("Tap tempo")));
    v.push_back (display (480, 32, 180, 34, "taptempo", "Time L", "Sync L"));
    v.push_back (hwKnob (514, 116, 50, "Groove", TR_NOOP ("Groove"), bipolar));
    for (auto& w : rangeWords (514, 143, 50, TR_NOOP ("Shuffle"), TR_NOOP ("Swing"))) v.push_back (w);
    v.push_back (hwKnob (626, 116, 50, "Feel", TR_NOOP ("Feel"), bipolar));
    for (auto& w : rangeWords (626, 143, 50, TR_NOOP ("Rushin"), TR_NOOP ("Draggin"))) v.push_back (w);
    // the mode rotary, its five modes printed round it (the knob's five stops: -135, -67.5, 0, 67.5, 135 degrees)
    v.push_back (stepped (knob (570, 226, 44, "Style", TR_NOOP ("Mode"), 226 + 22 + 18), 4));
    v.push_back (label (474, 243, 66, 12, TR_NOOP ("Single"), dim));
    v.push_back (label (468, 202, 66, 12, TR_NOOP ("Ratio"), dim));
    v.push_back (label (537, 180, 66, 12, TR_NOOP ("Quad"), dim));
    v.push_back (label (606, 202, 66, 12, TR_NOOP ("Dual"), dim));
    v.push_back (label (600, 243, 66, 12, TR_NOOP ("Ping-pong"), dim));
    // the style pod: Saturation (the drive) over Input / Output, the echo style (the engine's mode) and the measured
    // tape loops on LED read-outs
    v.push_back (section (690, 4, 256, 282, nullptr, boxed));
    v.push_back (label (690, 8, 256, 16, TR_NOOP ("Style")));
    v.push_back (hwKnob (818, 64, 50, "Drive", TR_NOOP ("Saturation")));
    for (auto& w : rangeWords (818, 91, 50, TR_NOOP ("Min"), TR_NOOP ("Max"))) v.push_back (w);
    v.push_back (hwKnob (746, 132, 44, "Input level", TR_NOOP ("Input"), bipolar)); // round 144's input level (±24 dB)
    for (auto& w : rangeWords (746, 156, 44, "-24", "+24")) v.push_back (w);
    v.push_back (hwKnob (890, 132, 44, "Output", TR_NOOP ("Output"), bipolar)); // round 119
    for (auto& w : rangeWords (890, 156, 44, "-24", "+24")) v.push_back (w);
    v.push_back (hwKnob (818, 140, 30, "Age", TR_NOOP ("Age")));
    v.push_back (menuOf (706, 184, 224, 24, "Mode", TR_NOOP ("Style"),
                         TR_NOOP ("Tape|Lo-Fi|Hi-Fi tape|Bucket brigade|Analog|Digital|Clean|Phaser|Shifter|Pitch|Reverse|Detune"), stepCentres (12)));
    // round 136: the measured tape-echo loops (EchoBoy's role)
    v.push_back (label (706, 214, 50, 22, TR_NOOP ("Tape"), alignLeft));
    v.push_back (menuOf (750, 214, 180, 22, "Tape voicing", TR_NOOP ("Tape voicing"), TR_NOOP ("Off|Ping-pong tape|Studio tape|Studio tape hot"),
                         { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }));
    // round tp172d (slice 8): the name block top left of the faceplate, over the pods (EchoBoy's place for its name;
    // LYNX's name and its own subtitle) — the pods move down under it
    for (auto i = mainFrom; i < v.size(); ++i)
        v[i].y += 30.0f;
    v.push_back (label (12, 1, 112, 32, "Echoform", dim | alignLeft));
    v.push_back (label (130, 12, 260, 14, TR_NOOP ("Character echo machine"), alignLeft));
    for (auto i = mainFrom; i < v.size(); ++i)
        v[i].page = 0;
    // the Tweak page: TWEAK (EchoBoy's Tweak panel) — width, diffusion, the mode's modulation, ducking, accent, the
    // dual-mode cross feedback, the echoes' balance; the echo train; the echoes' output level with a clip lamp
    const auto tweakFrom = v.size();
    v.push_back (section (4, 4, 942, 92, TR_NOOP ("Tweak")));
    const auto ty = 62.0f, td = 30.0f;
    v.push_back (dial (54, ty, td, "Width", TR_NOOP ("Width"), bipolar));
    v.push_back (dial (134, ty, td, "Diffusion", TR_NOOP ("Diffusion")));
    v.push_back (dial (214, ty, td, "Diffusion size", TR_NOOP ("Size")));
    // modulation: wow / flutter (tape), rate / depth, shift / detune (shifter), pitch / detune (pitch, reverse, detune)
    const auto tape = 1.0f / 12.0f, shifter = 8.0f / 12.0f, pitch = 9.0f / 12.0f, detune = 11.0f / 12.0f;
    v.push_back (when (dial (294, ty, td, "Wow", TR_NOOP ("Wow")), "Mode", 0.0f, tape));
    v.push_back (when (dial (374, ty, td, "Flutter", TR_NOOP ("Flutter")), "Mode", 0.0f, tape));
    v.push_back (when (dial (294, ty, td, "Mod rate", TR_NOOP ("Rate")), "Mode", tape, shifter));
    v.push_back (when (dial (374, ty, td, "Mod depth", TR_NOOP ("Depth")), "Mode", tape, shifter));
    v.push_back (when (dial (294, ty, td, "Shift", TR_NOOP ("Shift"), bipolar), "Mode", shifter, pitch));
    v.push_back (when (dial (374, ty, td, "Shift detune", TR_NOOP ("Detune"), bipolar), "Mode", shifter, pitch));
    v.push_back (when (dial (294, ty, td, "Pitch", TR_NOOP ("Pitch"), bipolar), "Mode", pitch, detune));
    v.push_back (when (dial (374, ty, td, "Pitch detune", TR_NOOP ("Detune"), bipolar), "Mode", pitch, 1.0f));
    v.push_back (dial (454, ty, td, "Ducking", TR_NOOP ("Ducking")));
    v.push_back (dial (534, ty, td, "Accent", TR_NOOP ("Accent"), bipolar));
    v.push_back (dial (614, ty, td, "Cross feedback", TR_NOOP ("X-feed")));
    v.push_back (dial (694, ty, td, "Balance", TR_NOOP ("Balance"), bipolar));
    // the echo train (round 137: in Rhythm mode, three passes of the pattern; round 144: edited in the rhythm editor below)
    v.push_back (display (740, 26, 156, 62, "echotrain"));
    v.push_back (display (904, 8, 38, 84, "outmeter"));
    for (auto i = tweakFrom; i < v.size(); ++i)
        v[i].page = 1;
    // the rows below keep their round 119 … 150 places, under the Tweak strip
    const auto lowerFrom = v.size();
    // round 119: ping-pong in pairs (shown with the ping-pong style)
    v.push_back (when (toggle (280, 252, 90, 24, "Pairs", TR_NOOP ("Pairs")), "Style", 0.8f, 1.0f));
    // round 129: the echoes' polarity per side (H-Delay's phase L / R)
    v.push_back (toggle (376, 254, 70, 20, "Phase L", TR_NOOP ("Phase L")));
    v.push_back (toggle (452, 254, 70, 20, "Phase R", TR_NOOP ("Phase R")));
    // round 124: the hybrid engine (H-Delay's measured loop)
    v.push_back (label (620, 254, 48, 20, TR_NOOP ("Hybrid")));
    v.push_back (menuOf (670, 252, 104, 24, "Hybrid", TR_NOOP ("Hybrid"), TR_NOOP ("Off|Clean|Analog"), { 0.0f, 0.5f, 1.0f }));
    // quad: Repeat / Swell where the right sync menu sits in the two-time styles, the four taps on the second row
    v.push_back (when (label (8, 290, 40, 20, TR_NOOP ("Taps")), "Style", 0.4f, 0.6f));
    v.push_back (when (toggle (48, 290, 34, 20, "Tap A", TR_NOOP ("A")), "Style", 0.4f, 0.6f));
    v.push_back (when (toggle (86, 290, 34, 20, "Tap B", TR_NOOP ("B")), "Style", 0.4f, 0.6f));
    v.push_back (when (toggle (124, 290, 34, 20, "Tap C", TR_NOOP ("C")), "Style", 0.4f, 0.6f));
    v.push_back (when (toggle (162, 290, 34, 20, "Tap D", TR_NOOP ("D")), "Style", 0.4f, 0.6f));
    // ping-pong: which side the first echo comes from
    v.push_back (when (tabs (8, 290, 190, 20, "Start side", TR_NOOP ("Start left|Start right")), "Style", 0.8f, 1.0f));
    // the tempo the note values follow: the project's or the echo's own BPM (H-Delay's BPM sync)
    v.push_back (tabs (210, 290, 130, 20, "Tempo source", TR_NOOP ("Project|Own BPM")));
    v.push_back (when (stepper (346, 290, 96, 20, "BPM", TR_NOOP ("BPM"), 270), "Tempo source", 0.5f, 1.0f));
    v.push_back (toggle (452, 290, 110, 20, "Filter link", TR_NOOP ("Link EQ")));
    v.push_back (toggle (570, 290, 100, 20, "Freeze", TR_NOOP ("Freeze")));
    v.push_back (toggle (676, 290, 90, 20, "LoFi", TR_NOOP ("LoFi"))); // round 137: H-Delay's LoFi switch
    // round 190dl: the engine law (Measured = the measured tape stage, the default of new instances; Classic = the tape
    // characters of projects saved before it) and the clocked modulation law (H-Delay's: the time swings by a fraction)
    v.push_back (tabs (772, 290, 172, 20, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    v.push_back (toggle (782, 254, 80, 20, "Mod clock", TR_NOOP ("Clock")));
    // ---- round 137: EchoBoy's Rhythm echo mode (a pattern of evenly spaced taps the feedback replays every Length)
    v.push_back (toggle (8, 320, 74, 20, "Rhythm", TR_NOOP ("Rhythm")));
    v.push_back (when (menuOf (88, 320, 76, 20, "Rhythm sync", TR_NOOP ("Spacing"), TR_NOOP ("ms|Note|Dotted|Triplet"), syncValues), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (when (stepper (170, 320, 90, 20, "Rhythm note", TR_NOOP ("Note"), 9), "Rhythm sync", 0.5f, 1.0f), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (when (slider (Item::hslider, 170, 322, 90, 16, "Rhythm time", nullptr, noCaption), "Rhythm sync", 0.0f, 0.49f), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (stepper (324, 320, 70, 20, "Repeats", TR_NOOP ("Repeats"), 15), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (label (402, 320, 44, 20, TR_NOOP ("Decay")), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (slider (Item::hslider, 448, 322, 100, 16, "Decay", nullptr, noCaption), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (value (552, 320, 56, 20, "Decay"), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (stepper (666, 320, 96, 20, "Length", TR_NOOP ("Length"), 7), "Rhythm", 0.5f, 1.0f));
    // round 137: ValhallaDelay's Era (the loop's bandwidth per mode), bottom right
    v.push_back (tabs (770, 320, 176, 20, "Era", TR_NOOP ("Past|Present|Future"), { 0.0f, 0.5f, 1.0f }));
    // ---- round 144: the rhythm editor (EchoBoy's role: pattern presets, taps at their own times or on a grid): the
    // taps on the pattern's time axis — drag a tap up / down for its level, sideways for its time (Free / Grid)
    v.push_back (when (section (4, 346, 942, 96, TR_NOOP ("Rhythm editor")), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (tabs (10, 366, 156, 20, "Rhythm edit", TR_NOOP ("Even|Free|Grid"), { 0.0f, 0.5f, 1.0f }), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (when (stepper (10, 390, 156, 20, "Rhythm grid", TR_NOOP ("Grid"), 5), "Rhythm edit", 0.67f, 1.0f), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (menuOf (10, 414, 76, 20, "Rhythm shape", TR_NOOP ("Shape"), TR_NOOP ("Decay|Reverse|Swell|Fade|Nonlin"), stepCentres (5)), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (display (90, 414, 76, 20, "rhythmpatterns"), "Rhythm", 0.5f, 1.0f));
    v.push_back (when (display (172, 364, 768, 72, "rhythmedit"), "Rhythm", 0.5f, 1.0f));
    // ---- round 144: with Rhythm off the same row holds the Style Edit role (EchoBoy's): the echo path's input level, the
    // EQ inside the loop, the feedback balance, the diffusion's place and the LFO's wave / sync
    const auto off = [] (LayoutItem i) { return when (std::move (i), "Rhythm", 0.0f, 0.49f); };
    v.push_back (off (section (4, 346, 942, 96, TR_NOOP ("Style edit"))));
    v.push_back (off (dial (52, 396, 24, "Input level", TR_NOOP ("Input"), bipolar)));
    v.push_back (off (dial (132, 396, 24, "Loop bass", TR_NOOP ("Bass"), bipolar)));
    v.push_back (off (dial (202, 396, 24, "Loop mid", TR_NOOP ("Mid"), bipolar)));
    v.push_back (off (dial (272, 396, 24, "Loop treble", TR_NOOP ("Treble"), bipolar)));
    v.push_back (off (dial (352, 396, 24, "FB balance", TR_NOOP ("FB bal."), bipolar)));
    v.push_back (off (label (420, 372, 70, 20, TR_NOOP ("Diffusion"))));
    v.push_back (off (tabs (492, 372, 130, 20, "Diffusion post", TR_NOOP ("Loop|Post"), { 0.0f, 1.0f })));
    v.push_back (off (label (420, 404, 70, 20, TR_NOOP ("Wobble"))));
    v.push_back (off (menuOf (492, 402, 130, 20, "Mod shape", TR_NOOP ("Shape"), TR_NOOP ("Sine|Triangle|Square|Saw|Random"), stepCentres (5))));
    v.push_back (off (label (640, 404, 40, 20, TR_NOOP ("Sync"))));
    v.push_back (off (menuOf (682, 402, 110, 20, "Mod sync", TR_NOOP ("Sync"), TR_NOOP ("Free|1/16|1/8|1/4|1/2|1 bar|2 bars"), stepCentres (7))));
    // ---- round 147: the measured echo voicings (EchoBoy's style role), the saturation curve and PrimalTap's Multiply
    {
        std::vector<float> voicings;
        for (int k = 0; k <= echosuite::echoform::voicingCount; ++k)
            voicings.push_back (echosuite::echoform::voicingValue (k));
        v.push_back (off (label (640, 372, 52, 20, TR_NOOP ("Voicing"))));
        v.push_back (off (menuOf (694, 372, 100, 20, "Echo voicing", TR_NOOP ("Echo voicing"),
                                  TR_NOOP ("Off|Mastering reel|Studio reel|Cartridge echo|Multi-head tape|Valve tape|Worn cassette|Pedal BBD|"
                                           "Compact BBD|Oil drum|Magnetic drum|Phone line|Medium wave|Broadcast FM|Distant station|"
                                           "Walkie-talkie|Pristine digital|Warm analog|Digital ensemble|Analog ensemble|Pedal ensemble|"
                                           "Seasick|Hot glow|Thick|Battered|Squashed|Fuzzed|Honky|Haze|Smeared|Scattered|Roomy"),
                                  voicings)));
        std::vector<float> types;
        for (int k = 0; k <= echosuite::echoform::satTypeCount; ++k)
            types.push_back (echosuite::echoform::satTypeValue (k));
        v.push_back (off (label (800, 372, 38, 20, TR_NOOP ("Sat."))));
        v.push_back (off (menuOf (840, 372, 100, 20, "Saturation type", TR_NOOP ("Saturation type"),
                                  TR_NOOP ("Auto|Clean|Tape|Tube|Squash|Grit|Ceiling"), types)));
        v.push_back (off (label (800, 402, 38, 20, TR_NOOP ("Mult."))));
        v.push_back (off (tabs (840, 402, 100, 20, "Multiply", TR_NOOP ("1x|2x|4x|8x"), { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f })));
        // round 150: the voicing's measured stereo behaviour (L/R offset in Single / Dual, the ping-pong law) and the
        // offset itself (EchoBoy's Tweak L/R Offset), on the Rhythm row while Rhythm is off and a voicing is chosen
        const auto withVoicing = [&off] (LayoutItem i) { return off (when (std::move (i), "Echo voicing", 0.5f / 31.0f, 1.0f)); };
        v.push_back (withVoicing (label (380, 320, 50, 20, TR_NOOP ("Stereo"))));
        v.push_back (withVoicing (tabs (432, 320, 130, 20, "Voicing stereo", TR_NOOP ("Classic|Measured"), { 0.0f, 1.0f })));
        v.push_back (withVoicing (label (572, 320, 68, 20, TR_NOOP ("L/R offset"))));
        v.push_back (withVoicing (slider (Item::hslider, 642, 322, 70, 16, "L/R offset", nullptr, noCaption)));
        v.push_back (withVoicing (value (714, 320, 52, 20, "L/R offset")));
    }
    for (auto i = lowerFrom; i < v.size(); ++i) // (tp172d: the Tweak page, under the Tweak strip)
    {
        v[i].y -= 148.0f;
        v[i].page = 1;
    }
    // the page switch, bottom left (EchoBoy's TWEAK key)
    v.push_back (pageTabs (8, 328, 180, 26, TR_NOOP ("Main|Tweak")));
    // the walnut cheeks either side (EchoBoy's): every item moves in by 28
    l.width = 1006;
    for (auto& item : v)
        item.x += 28.0f;
    // round 170c (the "shell" audit): the Hybrid engine and the tape voicings run their own loops, which read only
    // the times, feedback, mix, output, style and phase — every colour, modulation, diffusion, filter and groove control
    // is dimmed while one of them is on (it used to look live and do nothing)
    {
        namespace ef = echosuite::echoform;
        static constexpr int idleParams[] { ef::width, ef::drive, ef::age, ef::diffusion, ef::diffSize, ef::lowCut, ef::highCut, ef::modRate,
                                            ef::modDepth, ef::wow, ef::flutter, ef::shift, ef::shiftDetune, ef::pitch, ef::pitchDetune, ef::mode,
                                            ef::ducking, ef::groove, ef::feel, ef::accent, ef::primes, ef::crossFeedback };
        for (auto& item : v)
        {
            if (item.param == nullptr || ! isValueControl (item.type)) continue;
            const auto k = paramIndex (EffectKind::echoform, item.param);
            if (std::find (std::begin (idleParams), std::end (idleParams), k) == std::end (idleParams)) continue;
            item.idleParam = "Hybrid";
            item.idleMin = 0.25f;
            item.idleMax = 1.0f;
            item.idle2Param = "Tape voicing";
            item.idle2Min = 1.0f / 6.0f;
            item.idle2Max = 1.0f;
        }
    }
    return l;
}

Layout spiralLayout()
{
    Layout l;
    l.width = 530;
    l.height = 210;
    auto& v = l.items;
    v.push_back (section (4, 4, 522, 202, nullptr, boxed));
    v.push_back (dial (54, 74, 46, "Mix", TR_NOOP ("Mix")));
    v.push_back (dial (134, 74, 46, "Delay", TR_NOOP ("Delay")));
    v.push_back (dial (232, 82, 82, "Shift", TR_NOOP ("Shift"), (std::uint32_t) (big | bipolar)));
    v.push_back (dial (330, 74, 46, "Feedback", TR_NOOP ("Feedback")));
    v.push_back (dial (402, 74, 46, "Low cut", TR_NOOP ("Low cut")));
    v.push_back (dial (474, 74, 46, "High cut", TR_NOOP ("High cut")));
    std::vector<float> syncValues;
    for (int k = 0; k < 24; ++k)
        syncValues.push_back (((float) k + 0.5f) / 23.0f);
    syncValues.back() = 1.0f;
    v.push_back (label (14, 170, 74, 18, TR_NOOP ("Delay sync")));
    v.push_back (menuOf (90, 168, 100, 22, "Sync", TR_NOOP ("Delay sync"),
                         TR_NOOP ("Free|1/64|1/32|1/16|1/8|1/4|1/2|1/1|2/1|1/64.|1/32.|1/16.|1/8.|1/4.|1/2.|1/1.|1/64T|1/32T|1/16T|1/8T|1/4T|1/2T|1/1T|2/1T"),
                         syncValues));
    v.push_back (label (300, 170, 76, 18, TR_NOOP ("Output")));
    v.push_back (menuOf (380, 168, 100, 22, "Stereo", TR_NOOP ("Output"), TR_NOOP ("Mono|Stereo"), { 0.0f, 1.0f }));
    // round 190do: the loop law (Measured = the default of new instances; Classic = the loop of projects saved before)
    v.push_back (tabs (196, 168, 100, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

Layout orbitLayout()
{
    Layout l;
    l.width = 470;
    l.height = 200;
    auto& v = l.items;
    v.push_back (section (4, 4, 462, 192, nullptr, boxed));
    const char* params[5] = { "Mix", "Rate", "Depth", "Feedback", "Manual" };
    const char* captions[5] = { TR_NOOP ("Mix"), TR_NOOP ("Rate"), TR_NOOP ("Depth"), TR_NOOP ("Feedback"), TR_NOOP ("Manual") };
    for (int k = 0; k < 5; ++k)
    {
        auto knobItem = dial (52.0f + 91.5f * (float) k, 70, 52, params[k], captions[k], k == 1 || k >= 3 ? bipolar : 0);
        knobItem.colour = k; // each knob in its own colour, as one row of five distinct controls
        v.push_back (knobItem);
    }
    std::vector<float> modeValues;
    for (int k = 1; k <= 11; ++k)
        modeValues.push_back ((float) k / 11.0f);
    v.push_back (label (20, 152, 46, 20, TR_NOOP ("Mode")));
    v.push_back (menuOf (70, 150, 170, 24, "Mode", TR_NOOP ("Mode"),
                         TR_NOOP ("Rise|Rise / fall|Through-zero +|Through-zero −|Tri chorus|Octo chorus|Doubler|Drift rise|Drift rise / fall|Ensemble|Symphonic"),
                         modeValues));
    // round 190do: the sweep law (Measured = the default of new instances; Classic = the voices of projects saved before)
    v.push_back (tabs (290, 151, 160, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys));
    return l;
}

Layout latticeLayout()
{
    Layout l;
    l.width = 670;
    l.height = 300;
    auto& v = l.items;
    // the five main controls as faders, then the mode (taps) and sync menus under them
    v.push_back (section (4, 4, 300, 292, nullptr, boxed));
    const char* faders[5] = { "Mix", "Depth", "Width", "Delay", "Feedback" };
    const char* captions[5] = { TR_NOOP ("Mix"), TR_NOOP ("Depth"), TR_NOOP ("Width"), TR_NOOP ("Delay"), TR_NOOP ("Feedback") };
    for (int k = 0; k < 5; ++k)
    {
        v.push_back (slider (Item::vslider, 16.0f + 58.0f * (float) k, 14, 40, 196, faders[k], captions[k]));
        v.push_back (value (10.0f + 58.0f * (float) k, 238, 52, 16, faders[k], 0));
    }
    // round 135: the reference's MODE menu — its nine algorithms by their tap layout (LYNX's own names, the reference's
    // type index: lattice::tapCount); the engine plays the three 6-tap ones and the two 16-tap ones alike for now
    v.push_back (label (8, 268, 40, 18, TR_NOOP ("Mode")));
    v.push_back (menuOf (46, 266, 128, 22, "Taps", TR_NOOP ("Mode"),
                         TR_NOOP ("2-tap chorus|4-tap ensemble|6-tap chorus|6-tap random|Dual ensemble|8-tap|16-tap phase|16-tap|32-tap"),
                         { 1.5f / 24.0f, 2.5f / 24.0f, 3.5f / 24.0f, 4.5f / 24.0f, 5.5f / 24.0f, 6.5f / 24.0f, 7.5f / 24.0f, 8.5f / 24.0f, 9.5f / 24.0f }));
    std::vector<float> syncValues;
    for (int k = 0; k <= 17; ++k)
        syncValues.push_back ((float) k / 17.0f);
    v.push_back (label (178, 268, 36, 18, TR_NOOP ("Sync")));
    // round 144: the reference's DelaySync list (straight, dotted, triplet groups — measured); the older by-length list
    // stays in its place for projects that use it, until it is set back to Free
    std::vector<float> refSync { 0.0f };
    for (int k = 2; k <= 17; ++k)
        refSync.push_back (((float) k + 0.5f) / 17.0f);
    v.push_back (when (menuOf (214, 266, 88, 22, "Delay sync", TR_NOOP ("Sync"),
                               "Free|1/16|1/8|1/4|1/2|1/1|2/1|1/16.|1/8.|1/4.|1/2.|1/1.|1/16T|1/8T|1/4T|1/2T|1/1T", refSync),
                       "Sync", 0.0f, 0.5f / 17.0f));
    v.push_back (when (menuOf (214, 266, 88, 22, "Sync", TR_NOOP ("Sync"),
                               "Free|1/64|1/32T|1/64.|1/32|1/16T|1/32.|1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2T|1/4.|1/2|1/1", syncValues),
                       "Sync", 0.5f / 17.0f, 1.0f));
    // the pages: modulation, taps, diffusion, EQ, drive, warp
    l.pages = { TR_NOOP ("Mod"), TR_NOOP ("Taps"), TR_NOOP ("Diff"), TR_NOOP ("EQ"), TR_NOOP ("Drive"), TR_NOOP ("Warp") };
    v.push_back (pageTabs (312, 8, 354, 22, TR_NOOP ("Mod|Taps|Diff|EQ|Drive|Warp")));
    v.push_back (section (312, 36, 354, 186, nullptr, boxed));
    const auto row = [&v] (int page, std::vector<std::pair<const char*, const char*>> knobs, std::vector<LayoutItem> extra = {}) {
        const auto first = 346.0f + 68.0f * (float) extra.size();
        for (auto e : extra)
        {
            e.page = page;
            v.push_back (e);
        }
        for (std::size_t k = 0; k < knobs.size(); ++k)
        {
            auto i = dial (first + 68.0f * (float) k, 112, 38, knobs[k].first, knobs[k].second);
            i.page = page;
            v.push_back (i);
        }
    };
    row (0, { { "Detune rate", TR_NOOP ("Slow rate") }, { "Detune depth", TR_NOOP ("Slow depth") }, { "Vibrato rate", TR_NOOP ("Vib rate") },
              { "Vibrato depth", TR_NOOP ("Vib depth") }, { "Over-mod", TR_NOOP ("Over-mod") } });
    row (1, { { "Spread", TR_NOOP ("Spread") }, { "Slope", TR_NOOP ("Slope") }, { "Skew", TR_NOOP ("Skew") }, { "Random", TR_NOOP ("Random") },
              { "Tap gain", TR_NOOP ("Tap gain") } });
    row (2, { { "Diffusion", TR_NOOP ("Diffusion") }, { "Diffusion size", TR_NOOP ("Size") }, { "Diffusion rate", TR_NOOP ("Mod rate") },
              { "Diffusion depth", TR_NOOP ("Mod depth") } },
         { toggle (322, 104, 56, 20, "Diffusion on", TR_NOOP ("On")) });
    row (3, { { "Low cut", TR_NOOP ("Low cut") }, { "High cut", TR_NOOP ("High cut") }, { "Crossover", TR_NOOP ("Spatial EQ") } },
         { tabs (322, 104, 56, 40, "Colour", TR_NOOP ("Bright|Dark")) });
    row (4, { { "Drive in", TR_NOOP ("In gain") }, { "Drive out", TR_NOOP ("Out gain") }, { "Noise", TR_NOOP ("Noise gain") } },
         { toggle (322, 104, 56, 20, "Drive on", TR_NOOP ("On")) });
    {
        auto prePost = tabs (596, 104, 60, 40, "Drive post", TR_NOOP ("Pre|Post"));
        prePost.page = 4;
        v.push_back (prePost);
    }
    row (5, { { "Rotate", TR_NOOP ("Feedback mix") }, { "Smoothing", TR_NOOP ("Smoothing") } },
         { tabs (322, 104, 56, 40, "Half speed", TR_NOOP ("1x|1/2x")) });
    {
        // round 137: the WARP page's input pan (what enters the taps), measured on the reference
        auto caption = label (552, 82, 104, 16, TR_NOOP ("Input pan"));
        caption.page = 5;
        v.push_back (caption);
        std::vector<float> panValues;
        for (int k = 1; k <= 5; ++k)
            panValues.push_back ((float) k / 8.0f);
        auto pan = menuOf (552, 102, 104, 22, "Input pan", TR_NOOP ("Input pan"), TR_NOOP ("Stereo|Swapped|Mono|Mono left|Mono right"), panValues);
        pan.page = 5;
        v.push_back (pan);
        // round 190do: the detune law (Measured = the default of new instances; Classic = the depth of projects saved before)
        auto law = tabs (552, 150, 104, 22, "Law", TR_NOOP ("Classic|Measured"), {}, compactKeys);
        law.page = 5;
        v.push_back (law);
    }
    v.push_back (display (312, 230, 354, 62, "lattice"));
    return l;
}

#include "core/PluginLayout168r.inc" // round 168r: the FL effects that had a generated page, LYNX's simple built-ins

Layout generated (EffectKind kind)
{
    // knobs in rows of up to 6 (FL's wrapper shows unknown plugins as a list of knobs too)
    const auto& info = effectInfo (kind);
    Layout l;
    const auto count = (int) info.params.size();
    const auto columns = count <= 4 ? std::max (1, count) : count <= 12 ? 6 : 8;
    const auto rows = (count + columns - 1) / columns;
    constexpr float cellW = 74.0f, cellH = 84.0f;
    l.width = (int) (columns * cellW + 12.0f);
    l.height = (int) std::max (60.0f, (float) rows * cellH + 12.0f);

    if (count > 0)
        l.items.push_back (section (4, 4, (float) l.width - 8.0f, (float) l.height - 8.0f, nullptr, boxed));
    for (int i = 0; i < count; ++i)
    {
        const auto cx = 6.0f + cellW * ((float) (i % columns) + 0.5f);
        const auto cy = 6.0f + cellH * (float) (i / columns) + 26.0f;
        l.items.push_back (knob (cx, cy, 36, info.params[(std::size_t) i].name, info.params[(std::size_t) i].name, cy + 30.0f, showValue));
    }
    if (count == 0)
        l.items.push_back (display (6, 6, (float) l.width - 12.0f, (float) l.height - 12.0f, "name"));
    return l;
}
#include "core/PluginLayoutDrums.inc"
} // namespace

const std::vector<EffectKind>& flLayoutKinds()
{
    static const std::vector<EffectKind> kinds { EffectKind::fruityLimiter, EffectKind::fruityDelay3, EffectKind::fruityMultiband,
                                                 EffectKind::reeverb2, EffectKind::luxeVerb, EffectKind::fruityCompressor,
                                                 EffectKind::fruityFilter, EffectKind::fruityFastDist, EffectKind::fruityChorus,
                                                 EffectKind::fruityFlanger, EffectKind::stereoEnhancer, EffectKind::parametricEq,
                                                 EffectKind::grossBeat, EffectKind::fruityDelay2, EffectKind::reeverb1,
                                                 EffectKind::fruityFlangus, EffectKind::fruitySoftClipper,
                                                 EffectKind::fruityBloodOverdrive, EffectKind::fruityPanOMatic, EffectKind::stereoShaper,
                                                 EffectKind::transientProcessor, EffectKind::fruityFreeFilter, EffectKind::fruityWaveShaper,
                                                 EffectKind::vintageChorus, EffectKind::hyperChorus, EffectKind::fruityBalance,
                                                 EffectKind::squeeze, EffectKind::maximus,
                                                 EffectKind::vintagePhaser, EffectKind::fruityPhaser,
                                                 // round 105
                                                 EffectKind::vocoder, EffectKind::frequencySplitter, EffectKind::fruityDelayBank,
                                                 EffectKind::equo, EffectKind::multibandDelay, EffectKind::convolver,
                                                 EffectKind::flPitchShifter, EffectKind::effector, EffectKind::hardcore, EffectKind::vocodex,
                                                 EffectKind::fruitySend,
                                                 // round 154
                                                 EffectKind::emphasizer,
                                                 // round 165g
                                                 EffectKind::fruityMute2, EffectKind::transmitter,
                                                 // round 167f
                                                 EffectKind::flEmphasis, EffectKind::flTransporter,
                                                 // round 168r
                                                 EffectKind::pitchCorrect, EffectKind::peakController, EffectKind::spreader,
                                                 EffectKind::fruityFastLP, EffectKind::flFrequencyShifter, EffectKind::flLowLifter,
                                                 EffectKind::flBassBoost, EffectKind::flDelay1, EffectKind::fl7BandEq, EffectKind::flCenter,
                                                 EffectKind::flPhaseInverter, EffectKind::lovePhilter, EffectKind::distructor };
    return kinds;
}

const std::vector<EffectKind>& ownLayoutKinds()
{
    static const std::vector<EffectKind> kinds { EffectKind::ridgeStrip, EffectKind::glowLeveler, EffectKind::hushDeEsser,
                                                 EffectKind::aether, EffectKind::ember, EffectKind::kilnChannel, EffectKind::fretChannel, EffectKind::shellChannel, EffectKind::strikeShaper, EffectKind::anvilBus, EffectKind::epochVerb, EffectKind::atrium,
                                                 EffectKind::foilPlate, EffectKind::nebula, EffectKind::halo, EffectKind::horizon,
                                                 EffectKind::echoform, EffectKind::spiralEcho, EffectKind::orbit, EffectKind::latticeMod,
                                                 EffectKind::crestCompressor, EffectKind::prismMultiband, EffectKind::sentryGate,
                                                 EffectKind::quellDenoiser, EffectKind::geminiDoubler, EffectKind::compressor,
                                                 EffectKind::voiceShift, EffectKind::autoPan, EffectKind::hardwareInsert,
                                                 // round 168r: the simple built-ins
                                                 EffectKind::eq, EffectKind::delay, EffectKind::reverb, EffectKind::limiter, EffectKind::filter,
                                                 EffectKind::distortion, EffectKind::chorus, EffectKind::utility, EffectKind::lofi,
                                                 EffectKind::crossover, EffectKind::pitchShift, EffectKind::freqShift, EffectKind::rhythmGain,
                                                 EffectKind::volumeGate };
    return kinds;
}

namespace
{
bool generatedOnly = false;
}

void showGeneratedLayoutsOnly (bool on) { generatedOnly = on; }

const Layout& layoutFor (EffectKind kind)
{
    static std::mutex lock;
    static std::map<EffectKind, Layout> cache;
    const std::lock_guard<std::mutex> guard (lock);

    if (auto it = cache.find (kind); it != cache.end())
        return it->second;

    if (generatedOnly) // round 168r: screenshots of the "before" state
        return cache.emplace (kind, generated (kind)).first->second;

    Layout l;
    switch (kind)
    {
        case EffectKind::fruityLimiter: l = fruityLimiter(); break;
        case EffectKind::fruityCompressor: l = fruityCompressor(); break;
        case EffectKind::fruityFilter: l = fruityFilter(); break;
        case EffectKind::fruityChorus: l = fruityChorus(); break;
        case EffectKind::fruityFlanger: l = fruityFlanger(); break;
        case EffectKind::fruityFastDist: l = fruityFastDist(); break;
        case EffectKind::stereoEnhancer: l = stereoEnhancer(); break;
        case EffectKind::fruityDelay3: l = fruityDelay3(); break;
        case EffectKind::fruityMultiband: l = fruityMultiband(); break;
        case EffectKind::reeverb2: l = reeverb2(); break;
        case EffectKind::luxeVerb: l = luxeVerb(); break;
        case EffectKind::parametricEq: l = parametricEq2(); break;
        case EffectKind::grossBeat: l = grossBeat(); break;
        case EffectKind::fruityDelay2: l = fruityDelay2(); break;
        case EffectKind::reeverb1: l = reeverb1(); break;
        case EffectKind::fruityFlangus: l = fruityFlangus(); break;
        case EffectKind::fruitySoftClipper: l = fruitySoftClipper(); break;
        case EffectKind::fruityBloodOverdrive: l = fruityBloodOverdrive(); break;
        case EffectKind::fruityPanOMatic: l = fruityPanOMatic(); break;
        case EffectKind::stereoShaper: l = stereoShaper(); break;
        case EffectKind::transientProcessor: l = transientProcessor(); break;
        case EffectKind::fruityFreeFilter: l = fruityFreeFilter(); break;
        case EffectKind::fruityWaveShaper: l = fruityWaveShaper(); break;
        case EffectKind::vintageChorus: l = vintageChorus(); break;
        case EffectKind::hyperChorus: l = hyperChorus(); break;
        case EffectKind::fruityBalance: l = fruityBalance(); break;
        case EffectKind::squeeze: l = fruitySqueeze(); break;
        case EffectKind::emphasizer: l = emphasizer(); break;
        case EffectKind::flEmphasis: l = flEmphasis(); break;
        case EffectKind::maximus: l = maximus(); break;
        case EffectKind::vintagePhaser: l = vintagePhaser(); break;
        case EffectKind::fruityPhaser: l = fruityPhaser(); break;
        case EffectKind::vocoder: l = fruityVocoder(); break;
        case EffectKind::frequencySplitter: l = frequencySplitterLayout(); break;
        case EffectKind::fruityDelayBank: l = fruityDelayBankLayout(); break;
        case EffectKind::equo: l = equoLayout(); break;
        case EffectKind::multibandDelay: l = multibandDelayLayout(); break;
        case EffectKind::convolver: l = convolverLayout(); break;
        case EffectKind::flPitchShifter: l = pitchShifterLayout(); break;
        case EffectKind::effector: l = effectorLayout(); break;
        case EffectKind::hardcore: l = hardcoreLayout(); break;
        case EffectKind::vocodex: l = vocodexLayout(); break;
        case EffectKind::ridgeStrip: l = withVocalVoicing (ridgeStrip()); break;
        case EffectKind::glowLeveler: l = glowLeveler(); break;
        case EffectKind::compressor: l = compressorLayout(); break;
        case EffectKind::hushDeEsser: l = hushDeEsser(); break;
        case EffectKind::aether: l = aetherLayout(); break;
        case EffectKind::ember: l = withEmberClassic (emberLayout()); break;
        case EffectKind::kilnChannel: l = kilnLayout(); break;
        case EffectKind::fretChannel: l = fretLayout(); break;
        case EffectKind::shellChannel: l = shellLayout(); break;
        case EffectKind::strikeShaper: l = strikeLayout(); break;
        case EffectKind::anvilBus: l = anvilLayout(); break;
        case EffectKind::epochVerb: l = captionsAbove (epochVerbLayout()); break;
        case EffectKind::atrium: l = captionsAbove (atriumLayout()); break;
        case EffectKind::foilPlate: l = captionsAbove (foilPlateLayout()); break;
        case EffectKind::nebula: l = captionsAbove (nebulaLayout()); break;
        case EffectKind::halo: l = captionsAbove (haloLayout()); break;
        case EffectKind::horizon: l = captionsAbove (horizonLayout()); break;
        case EffectKind::lynxTune: l = lynxTune(); break;
        case EffectKind::echoform: l = captionsAbove (echoformLayout()); break;
        case EffectKind::spiralEcho: l = captionsAbove (spiralLayout()); break;
        case EffectKind::orbit: l = captionsAbove (orbitLayout()); break;
        case EffectKind::latticeMod: l = captionsAbove (latticeLayout()); break;
        case EffectKind::fruitySend: l = fruitySend(); break;
        case EffectKind::fruityMute2: l = fruityMute2Layout(); break;
        case EffectKind::transmitter: l = transmitterLayout(); break;
        case EffectKind::flTransporter: l = transporterLayout(); break;
        case EffectKind::crestCompressor: l = crestCompressor(); break;
        case EffectKind::prismMultiband: l = prismMultiband(); break;
        case EffectKind::sentryGate: l = sentryGate(); break;
        case EffectKind::quellDenoiser: l = quellDenoiser(); break;
        case EffectKind::geminiDoubler: l = geminiDoubler(); break;
        case EffectKind::voiceShift: l = voiceShiftLayout(); break;
        case EffectKind::hardwareInsert: l = hardwareInsertLayout(); break;
        case EffectKind::autoPan: l = withRhythmVoicing (autoPanLayout()); break;
        // round 168r
        case EffectKind::pitchCorrect: l = pitcherLayout(); break;
        case EffectKind::peakController: l = peakControllerFl(); break;
        case EffectKind::spreader: l = spreaderLayout(); break;
        case EffectKind::fruityFastLP: l = fruityFastLP(); break;
        case EffectKind::flFrequencyShifter: l = freqShifterFl(); break;
        case EffectKind::flLowLifter: l = lowLifterFl(); break;
        case EffectKind::flBassBoost: l = flBassBoost(); break;
        case EffectKind::flDelay1: l = flDelay1(); break;
        case EffectKind::fl7BandEq: l = fl7BandEq(); break;
        case EffectKind::flCenter: l = flCenter(); break;
        case EffectKind::flPhaseInverter: l = flPhaseInverter(); break;
        case EffectKind::lovePhilter: l = lovePhilterLayout(); break;
        case EffectKind::distructor: l = distructorLayout(); break;
        case EffectKind::eq: l = lynxEq(); break;
        case EffectKind::delay: l = lynxDelay(); break;
        case EffectKind::reverb: l = lynxReverb(); break;
        case EffectKind::limiter: l = lynxLimiter(); break;
        case EffectKind::filter: l = lynxFilter(); break;
        case EffectKind::distortion: l = lynxDistortion(); break;
        case EffectKind::chorus: l = lynxChorus(); break;
        case EffectKind::utility: l = lynxUtility(); break;
        case EffectKind::lofi: l = lynxLofi(); break;
        case EffectKind::crossover: l = lynxCrossover(); break;
        case EffectKind::pitchShift: l = lynxPitchShift(); break;
        case EffectKind::freqShift: l = lynxFreqShift(); break;
        case EffectKind::rhythmGain: l = lynxRhythm(); break;
        case EffectKind::volumeGate: l = lynxStepGate(); break;
        default: l = generated (kind); break;
    }
    return cache.emplace (kind, std::move (l)).first->second;
}

namespace
{
// a parameter name built at run time, kept for the program's life (layout items hold const char*)
const char* stored (std::string text)
{
    static std::list<std::string> names;
    for (const auto& n : names)
        if (n == text)
            return n.c_str();
    names.push_back (std::move (text));
    return names.back().c_str();
}

const char* osc (int o, const char* field) { return stored ("Osc " + std::to_string (o + 1) + " " + field); }
const char* fx (int s, const char* field) { return stored ("FX " + std::to_string (s + 1) + " " + field); }

// Toxic Biohazard (round 168n): FL's arrangement — CONFIGURATION and MASTER ENVELOPE across the top, the six
// oscillators in two columns (envelope faders, wave, pitch, detune, frequency offset, phase, free run), the MOD panel
// in the middle (the 6 × 6 FM grid "row modulates column", MIX / PAN, LFO 1 / 2 per oscillator, master pitch
// modulation), LFO 1 / FILTER / LFO 2 below and the effect slots around the 8-band EQ at the bottom. Reference: FL's own
// window (its installation's Artwork\background.tga, read as a layout only); drawn in LYNX's style.
Layout toxicMain()
{
    Layout l;
    l.fl = "Toxic Biohazard";
    l.width = 920;
    l.height = 668;
    auto& v = l.items;

    // CONFIGURATION
    v.push_back (section (0, 0, 330, 58, TR_NOOP ("Configuration")));
    v.push_back (knob (26, 33, 24, "Transpose", TR_NOOP ("Transp."), 53, bipolar));
    v.push_back (stepper (50, 18, 44, 36, "Polyphony", TR_NOOP ("Poly"), 32));
    v.push_back (stepper (98, 18, 44, 36, "Unison voices", TR_NOOP ("Voices"), 7));
    v.push_back (knob (168, 33, 24, "Unison detune", TR_NOOP ("Detune"), 53));
    v.push_back (knob (210, 33, 24, "Unison pan", TR_NOOP ("Spread"), 53));
    v.push_back (menuOf (234, 18, 52, 36, "Glide mode", TR_NOOP ("Glide"), "Bend VT|Held VT|Bend ST|Held ST"));
    v.push_back (knob (308, 33, 24, "Glide time", TR_NOOP ("Time"), 53));

    v.push_back (label (340, 6, 240, 46, "TOXIC BIOHAZARD", dim));

    // MASTER ENVELOPE
    v.push_back (section (590, 0, 330, 58, TR_NOOP ("Master envelope")));
    static const char* const master[] = { "Attack", "Decay", "Sustain", "Release" };
    static const char* const masterCaps[] = { TR_NOOP ("Att"), TR_NOOP ("Dec"), TR_NOOP ("Sus"), TR_NOOP ("Rel") };
    for (int i = 0; i < 4; ++i)
        v.push_back (knob (614 + 38 * (float) i, 33, 24, master[i], masterCaps[i], 53));
    v.push_back (knob (772, 33, 24, "Velocity curve", TR_NOOP ("Vel curve"), 53));
    v.push_back (knob (830, 33, 24, "Distortion", TR_NOOP ("Drive"), 53));
    v.push_back (knob (888, 33, 24, "Gain", TR_NOOP ("Master"), 53));

    // the six oscillators
    static const char* const titles[] = { "Osc 1", "Osc 2", "Osc 3", "Osc 4", "Osc 5", "Osc 6" };
    static const char* const faders[] = { "velocity", "init", "attack", "decay", "sustain", "release" };
    static const char* const faderCaps[] = { TR_NOOP ("Vel"), TR_NOOP ("Init"), TR_NOOP ("Att"), TR_NOOP ("Dec"), TR_NOOP ("Sus"), TR_NOOP ("Rel") };
    for (int o = 0; o < 6; ++o)
    {
        const auto x0 = o < 3 ? 0.0f : 620.0f, y0 = 64.0f + 112.0f * (float) (o % 3);
        v.push_back (section (x0, y0, 300, 106, titles[o]));
        v.push_back (toggle (x0 + 186, y0 + 1, 52, 15, osc (o, "free run"), TR_NOOP ("Free")));
        v.push_back (toggle (x0 + 244, y0 + 1, 52, 15, osc (o, "on"), TR_NOOP ("On")));
        for (int f = 0; f < 6; ++f)
            v.push_back (slider (Item::vslider, x0 + 6 + 20 * (float) f, y0 + 22, 18, 80, osc (o, faders[f]), faderCaps[f]));
        v.push_back (display (x0 + 130, y0 + 22, 72, 44, "toxicwave", osc (o, "wave")));
        v.push_back (stepper (x0 + 130, y0 + 68, 72, 34, osc (o, "wave"), TR_NOOP ("Wave"), 32));
        v.push_back (knob (x0 + 228, y0 + 38, 26, osc (o, "coarse"), TR_NOOP ("Pitch"), y0 + 60, bipolar));
        v.push_back (knob (x0 + 272, y0 + 38, 26, osc (o, "fine"), TR_NOOP ("Detune"), y0 + 60, bipolar));
        v.push_back (knob (x0 + 228, y0 + 80, 26, osc (o, "freq shift"), TR_NOOP ("Freq ofs"), y0 + 101, bipolar));
        v.push_back (knob (x0 + 272, y0 + 80, 26, osc (o, "phase"), TR_NOOP ("Phase"), y0 + 101));
    }

    // MOD: the FM grid (row = the modulating oscillator, column = the modulated one), mix, pan, LFO amounts, pitch
    // FL's centre panel has three pages and no title, only its MOD / MIDI / SEQ tabs (round 169h): MOD, MIDI (the eight
    // modulation slots) and SEQ (the step sequencer)
    v.push_back (section (308, 64, 304, 336, nullptr));
    l.pages = { TR_NOOP ("Mod"), TR_NOOP ("MIDI"), TR_NOOP ("Seq") };
    v.push_back (pageTabs (385, 66, 150, 15, "Mod|MIDI|Seq"));
    const auto modFrom = v.size();
    static const char* const digits[] = { "1", "2", "3", "4", "5", "6" };
    v.push_back (label (314, 80, 40, 14, TR_NOOP ("FM"), alignLeft));
    for (int j = 0; j < 6; ++j)
        v.push_back (label (352 + 38 * (float) j, 80, 30, 14, digits[j]));
    for (int i = 0; i < 6; ++i)
    {
        v.push_back (label (318, 98 + 25 * (float) i, 24, 16, digits[i]));
        for (int j = 0; j < 6; ++j)
            v.push_back (knob (367 + 38 * (float) j, 106 + 25 * (float) i, 21,
                               stored ("FM " + std::to_string (i + 1) + " to " + std::to_string (j + 1)), nullptr, 0, noCaption));
    }
    struct Row { const char* caption; const char* field; float cy; std::uint32_t style; };
    const Row rows[] = { { TR_NOOP ("Mix"), "mix", 262, 0 }, { TR_NOOP ("Pan"), "pan", 288, bipolar } };
    for (const auto& r : rows)
    {
        v.push_back (label (312, r.cy - 8, 40, 16, r.caption, alignLeft));
        for (int o = 0; o < 6; ++o)
            v.push_back (knob (367 + 38 * (float) o, r.cy, 21, osc (o, r.field), nullptr, 0, noCaption | r.style));
    }
    for (int lfo = 0; lfo < 2; ++lfo)
    {
        const auto cy = 318.0f + 26.0f * (float) lfo;
        v.push_back (label (312, cy - 8, 44, 16, lfo == 0 ? TR_NOOP ("LFO 1") : TR_NOOP ("LFO 2"), alignLeft));
        for (int o = 0; o < 6; ++o)
            v.push_back (knob (367 + 38 * (float) o, cy, 21, stored ("LFO " + std::to_string (lfo + 1) + " to osc " + std::to_string (o + 1)),
                               nullptr, 0, noCaption | bipolar));
    }
    v.push_back (label (312, 360, 84, 32, TR_NOOP ("Pitch mod"), alignLeft));
    v.push_back (knob (430, 372, 20, "LFO 2 to pitch", TR_NOOP ("LFO 2"), 393));
    v.push_back (knob (490, 372, 20, "Filter env to pitch", TR_NOOP ("Filter env"), 393, bipolar));
    v.push_back (knob (550, 372, 20, "Bend range", TR_NOOP ("Bend"), 393));
    for (auto i = modFrom; i < v.size(); ++i)
        v[i].page = 0;

    // MIDI: source, amount and destination of each modulation slot (FL: FUN_1800304b0, eight rows)
    {
        static const std::string sources = [] {
            std::string o;
            for (int t = 0; t < 135; ++t)
                if (t != 6) // (FL's "Channel Controllers" submenu header)
                    o += (o.empty() ? "" : "|") + toxicModSourceName (t);
            return o;
        }();
        static const std::vector<float> sourceValues = [] {
            std::vector<float> o;
            for (int t = 0; t < 135; ++t)
                if (t != 6)
                    o.push_back ((float) t / 134.0f);
            return o;
        }();
        static const std::string targets = [] {
            std::string o = "---";
            for (int i = 0; i < 294; ++i)
            {
                const auto* p = findEngineParam (toxicParams(), i);
                o += "|" + (p != nullptr ? p->name : "Parameter " + std::to_string (i));
            }
            return o;
        }();
        v.push_back (onPage (label (316, 84, 112, 14, TR_NOOP ("Source"), alignLeft | dim), 1));
        v.push_back (onPage (label (432, 84, 52, 14, TR_NOOP ("Amount"), dim), 1));
        v.push_back (onPage (label (490, 84, 116, 14, TR_NOOP ("Destination"), alignLeft | dim), 1));
        for (int k = 0; k < 8; ++k)
        {
            const auto y = 102.0f + 36.0f * (float) k;
            const auto n = std::to_string (k + 1);
            v.push_back (onPage (menuOf (314, y, 112, 28, stored ("Mod " + n + " source"), nullptr, sources.c_str(), sourceValues), 1));
            v.push_back (onPage (stepper (432, y, 52, 28, stored ("Mod " + n + " amount"), nullptr, 200), 1));
            v.push_back (onPage (menuOf (490, y, 116, 28, stored ("Mod " + n + " destination"), nullptr, targets.c_str()), 1));
        }
    }

    // SEQ: the 64 steps in eight rows (two blocks of four rows, as FL's FUN_180030f30), the number of steps and the
    // Play / Swing / Dual / Ping-pong / Random switches
    {
        static const char* const rowLabels[] = { "0", "4", "8", "12", "16", "20", "24", "28", "32", "36", "40", "44", "48", "52", "56", "60" };
        for (int row = 0; row < 8; ++row)
        {
            const auto y = 86.0f + 25.0f * (float) row + (row >= 4 ? 8.0f : 0.0f);
            for (int half = 0; half < 2; ++half)
            {
                const auto x0 = half == 0 ? 314.0f : 462.0f;
                v.push_back (onPage (label (x0, y + 4, 16, 14, rowLabels[2 * row + half], dim), 2));
                for (int j = 0; j < 4; ++j)
                {
                    const auto step = 8 * row + 4 * half + j;
                    v.push_back (onPage (stepper (x0 + 18 + 33 * (float) j, y, 31, 22, stored ("Seq step " + std::to_string (step + 1)), nullptr, 50), 2));
                }
            }
        }
        v.push_back (onPage (stepper (314, 300, 104, 40, "Seq steps", TR_NOOP ("Number of steps"), 63), 2));
        v.push_back (onPage (toggle (436, 306, 52, 16, "Seq play", TR_NOOP ("Play")), 2));
        v.push_back (onPage (toggle (494, 306, 56, 16, "Seq swing", TR_NOOP ("Swing")), 2));
        v.push_back (onPage (toggle (556, 306, 50, 16, "Seq dual", TR_NOOP ("Dual")), 2));
        v.push_back (onPage (toggle (436, 328, 80, 16, "Seq ping-pong", TR_NOOP ("Ping-pong")), 2));
        v.push_back (onPage (toggle (522, 328, 70, 16, "Seq random", TR_NOOP ("Random")), 2));
    }

    // LFO 1 / LFO 2
    static const char* const syncs = "Off|1/16|1/8|3/16|1/4|1/2|3/4|1 bar|2 bars|4 bars";
    for (int lfo = 0; lfo < 2; ++lfo)
    {
        const auto x0 = lfo == 0 ? 0.0f : 720.0f;
        const auto n = std::to_string (lfo + 1);
        v.push_back (section (x0, 406, 200, 112, lfo == 0 ? TR_NOOP ("LFO 1") : TR_NOOP ("LFO 2")));
        v.push_back (toggle (x0 + 140, 407, 56, 15, stored ("LFO " + n + " free"), TR_NOOP ("Free")));
        v.push_back (display (x0 + 8, 426, 74, 44, "toxicwave", stored ("LFO " + n + " wave")));
        v.push_back (stepper (x0 + 8, 474, 74, 36, stored ("LFO " + n + " wave"), TR_NOOP ("Wave"), 32));
        v.push_back (knob (x0 + 143, 444, 28, stored ("LFO " + n + " speed"), TR_NOOP ("Speed"), 466));
        v.push_back (menuOf (x0 + 94, 482, 98, 32, stored ("LFO " + n + " sync"), TR_NOOP ("Sync"), syncs));
    }

    // FILTER
    v.push_back (section (208, 406, 504, 112, TR_NOOP ("Filter")));
    v.push_back (tabs (216, 426, 176, 20, "Filter mode", "Off|LP|BP|HP"));
    struct K { float cx; const char* param; const char* caption; std::uint32_t style; };
    const K filter[] = { { 238, "Cutoff", TR_NOOP ("Cutoff"), big }, { 296, "Resonance", TR_NOOP ("Reso"), 0 },
                         { 350, "Key tracking", TR_NOOP ("Key trk"), 0 }, { 404, "Velocity tracking", TR_NOOP ("Vel trk"), 0 },
                         { 458, "Filter env amount", TR_NOOP ("EG amount"), bipolar }, { 512, "Filter LFO 1 amount", TR_NOOP ("LFO 1"), 0 } };
    for (const auto& k : filter)
        v.push_back (knob (k.cx, 474, 34, k.param, k.caption, 508, k.style));
    static const char* const fenv[] = { "Filter attack", "Filter decay", "Filter sustain", "Filter release" };
    for (int i = 0; i < 4; ++i)
        v.push_back (slider (Item::vslider, 566 + 34 * (float) i, 424, 20, 88, fenv[i], masterCaps[i]));

    // the effect slots and the EQ
    static const char* const delaySyncs = "Off|1/32|1/16|3/32|1/8|1/4|3/8|1/2|1 bar|2 bars";
    for (int s = 0; s < 2; ++s)
    {
        const auto x0 = s == 0 ? 0.0f : 620.0f;
        v.push_back (section (x0, 524, 300, 144, s == 0 ? TR_NOOP ("Effect 1") : TR_NOOP ("Effect 2")));
        v.push_back (toggle (x0 + 244, 525, 52, 15, fx (s, "on"), TR_NOOP ("On")));
        v.push_back (tabs (x0 + 6, 544, 288, 20, fx (s, "type"), "Delay|Chorus|Reverb|Flanger|Phaser|Lo-fi"));
        const auto type = fx (s, "type");
        auto add = [&] (int t, std::initializer_list<std::pair<const char*, const char*>> knobs) {
            float cx = x0 + 30;
            for (const auto& [field, caption] : knobs)
            {
                v.push_back (when (knob (cx, 594, 30, fx (s, field), caption, 622), type, (float) t / 5.0f - 0.1f, (float) t / 5.0f + 0.1f));
                cx += 48;
            }
        };
        add (0, { { "delay time", TR_NOOP ("Time") }, { "delay feedback", TR_NOOP ("Feedback") }, { "delay rate", TR_NOOP ("Rate") },
                  { "delay depth", TR_NOOP ("Depth") }, { "delay blur", TR_NOOP ("Blur") }, { "delay mix", TR_NOOP ("Mix") } });
        v.push_back (when (menuOf (x0 + 8, 630, 110, 34, fx (s, "delay sync"), TR_NOOP ("Sync"), delaySyncs), type, -0.1f, 0.1f));
        add (1, { { "chorus depth", TR_NOOP ("Depth") }, { "chorus rate", TR_NOOP ("Rate") }, { "chorus mix", TR_NOOP ("Mix") } });
        add (2, { { "reverb mix", TR_NOOP ("Mix") }, { "reverb decay", TR_NOOP ("Decay") }, { "reverb damping", TR_NOOP ("Damping") },
                  { "reverb high cut", TR_NOOP ("High cut") } });
        add (3, { { "flanger delay", TR_NOOP ("Delay") }, { "flanger feedback", TR_NOOP ("Feedback") }, { "flanger depth", TR_NOOP ("Depth") },
                  { "flanger rate", TR_NOOP ("Rate") }, { "flanger mix", TR_NOOP ("Mix") } });
        v.push_back (when (toggle (x0 + 8, 638, 120, 16, fx (s, "flanger invert feedback"), TR_NOOP ("Invert feedback")), type, 0.5f, 0.7f));
        v.push_back (when (toggle (x0 + 140, 638, 120, 16, fx (s, "flanger invert mix"), TR_NOOP ("Invert mix")), type, 0.5f, 0.7f));
        add (4, { { "phaser frequency", TR_NOOP ("Freq") }, { "phaser rate", TR_NOOP ("Rate") }, { "phaser depth", TR_NOOP ("Depth") },
                  { "phaser feedback", TR_NOOP ("Feedback") }, { "phaser mix", TR_NOOP ("Mix") } });
        add (5, { { "lo-fi bits", TR_NOOP ("Bits") }, { "lo-fi rate", TR_NOOP ("Rate") }, { "lo-fi filter", TR_NOOP ("Filter") } });
    }
    v.push_back (section (308, 524, 304, 144, TR_NOOP ("EQ")));
    v.push_back (toggle (552, 525, 56, 15, "EQ on", TR_NOOP ("On")));
    static const char* const bands[] = { "EQ 62", "EQ 125", "EQ 250", "EQ 500", "EQ 1k", "EQ 2k", "EQ 4k", "EQ 8k" };
    static const char* const bandCaps[] = { "62", "125", "250", "500", "1k", "2k", "4k", "8k" };
    for (int b = 0; b < 8; ++b)
        v.push_back (slider (Item::vslider, 318 + 36 * (float) b, 546, 22, 116, bands[b], bandCaps[b], bipolar));
    return l;
}

// PoiZone (round 168n): FL's arrangement, read from the plugin's own form (TSynthEditorForm in PoiZone_x64.dll: each
// control's position and the parameter it edits, docs/analise/REA_rodada168n.md §2) — unison / mode switches / master
// across the top, MOD WHEEL, LFO, ENVELOPE and AMP envelopes, the two oscillators, FILTER and ARPEGGIATOR, then DELAY,
// CHORUS and the 16-step GATE. FL's x positions; the rows are re-spaced for LYNX's section titles. Drawn in LYNX's style.
Layout poizoneMain()
{
    Layout l;
    l.fl = "PoiZone";
    l.width = 856;
    l.height = 486;
    auto& v = l.items;
    // a 40-pixel FL wheel at (left, top of the row) → LYNX knob with its caption
    auto wheel = [&v] (float left, float rowTop, const char* param, const char* caption, std::uint32_t style = 0) {
        v.push_back (knob (left + 20, rowTop + 18, 34, param, caption, rowTop + 46, style));
    };
    auto button = [&v] (float left, float rowTop, const char* param, const char* caption) {
        v.push_back (toggle (left - 4, rowTop + 4, 40, 28, param, caption, boxed));
    };

    // row 1: name, unison / polyphony, the mode switches, master
    v.push_back (label (12, 10, 250, 60, "POIZONE", dim | alignLeft));
    v.push_back (section (276, 4, 210, 76, TR_NOOP ("Unison")));
    v.push_back (menuOf (284, 26, 48, 34, "Unison voices", TR_NOOP ("Voices"), "1|2|3|4|8|12|16"));
    v.push_back (stepper (338, 26, 48, 34, "Polyphony", TR_NOOP ("Poly"), 63));
    wheel (392, 24, "Unison detune", TR_NOOP ("Detune"));
    wheel (438, 24, "Unison pan", TR_NOOP ("Pan"));
    v.push_back (section (490, 4, 146, 76, TR_NOOP ("Mode")));
    button (497, 26, "Octaver", TR_NOOP ("Oct"));
    button (531, 26, "Mono", TR_NOOP ("Mono"));
    button (565, 26, "Glide held", TR_NOOP ("Held"));
    button (601, 26, "Glide static", TR_NOOP ("Static"));
    v.push_back (section (640, 4, 212, 76, TR_NOOP ("Master")));
    wheel (643, 24, "Glide time", TR_NOOP ("Glide"));
    wheel (695, 24, "Transpose", TR_NOOP ("Transp."), bipolar);
    wheel (745, 24, "Fine", TR_NOOP ("Fine"), bipolar);
    wheel (800, 24, "Gain", TR_NOOP ("Gain"));

    // row 2: mod wheel, LFO, modulation envelope, amplifier envelope
    const float r2 = 86;
    auto fader = [&v, r2] (float left, const char* param, const char* caption, std::uint32_t style = 0) {
        v.push_back (slider (Item::vslider, left, r2 + 22, 25, 86, param, caption, style));
    };
    auto selector = [&v, r2] (float left, float w, const char* param, const char* options) {
        v.push_back (radio (left, r2 + 22, w, 74, param, options));
    };
    v.push_back (section (6, r2, 134, 112, TR_NOOP ("Mod wheel")));
    selector (12, 58, "Mod wheel destination", "Cutoff|LFO amt|Frq A|PW A|PW AB");
    wheel (80, r2 + 18, "Mod wheel amount", TR_NOOP ("Amount"), bipolar);
    v.push_back (stepper (80, r2 + 72, 52, 34, "Bend range", TR_NOOP ("Bend"), 23));
    v.push_back (section (144, r2, 262, 112, TR_NOOP ("LFO")));
    selector (150, 58, "LFO destination", "Cutoff|Frq A|Frq AB|PW A|PW AB|Pan");
    fader (215, "LFO amount", TR_NOOP ("Amount"), bipolar);
    fader (257, "LFO rate", TR_NOOP ("Rate"));
    selector (292, 54, "LFO shape", "Saw|Pulse|Tri|Sine|Rnd");
    button (362, r2 + 18, "LFO retrigger", TR_NOOP ("Retrig"));
    v.push_back (menuOf (352, r2 + 66, 50, 34, "LFO sync", TR_NOOP ("Sync"), "Free|1|2|3|4|8|12|16|32|64|128"));
    v.push_back (section (410, r2, 258, 112, TR_NOOP ("Envelope")));
    selector (416, 58, "Mod env destination", "Cutoff|Frq A|Frq AB|PW A|PW AB");
    fader (483, "Mod env amount", TR_NOOP ("Amount"), bipolar);
    fader (526, "Mod env attack", TR_NOOP ("Att"));
    fader (561, "Mod env decay", TR_NOOP ("Dec"));
    fader (597, "Mod env sustain", TR_NOOP ("Sus"));
    fader (633, "Mod env release", TR_NOOP ("Rel"));
    v.push_back (section (672, r2, 180, 112, TR_NOOP ("Amp")));
    fader (690, "Amp attack", TR_NOOP ("Att"));
    fader (728, "Amp decay", TR_NOOP ("Dec"));
    fader (766, "Amp sustain", TR_NOOP ("Sus"));
    fader (804, "Amp release", TR_NOOP ("Rel"));

    // row 3: oscillators, filter, arpeggiator
    const float r3 = 204;
    v.push_back (section (6, r3, 456, 80, TR_NOOP ("Oscillators")));
    wheel (14, r3 + 20, "Osc balance", TR_NOOP ("A / B"), bipolar);
    wheel (62, r3 + 20, "Noise", TR_NOOP ("Noise"));
    wheel (116, r3 + 20, "Osc A pulse width", TR_NOOP ("PW A"));
    v.push_back (tabs (160, r3 + 26, 46, 24, "Osc A shape", "Saw|Sq"));
    button (212, r3 + 22, "Ring mod", TR_NOOP ("Ring"));
    button (248, r3 + 22, "Sync", TR_NOOP ("Sync"));
    wheel (286, r3 + 20, "Osc B pulse width", TR_NOOP ("PW B"));
    v.push_back (tabs (330, r3 + 26, 46, 24, "Osc B shape", "Saw|Sq"));
    wheel (378, r3 + 20, "Osc B semitones", TR_NOOP ("Pitch B"), bipolar);
    wheel (420, r3 + 20, "Osc B detune", TR_NOOP ("Detune B"));
    v.push_back (section (466, r3, 232, 80, TR_NOOP ("Filter")));
    wheel (472, r3 + 20, "Cutoff", TR_NOOP ("Cutoff"));
    wheel (514, r3 + 20, "Resonance", TR_NOOP ("Reso"));
    wheel (556, r3 + 20, "Key tracking", TR_NOOP ("Key trk"));
    wheel (598, r3 + 20, "Velocity tracking", TR_NOOP ("Vel trk"));
    v.push_back (radio (646, r3 + 20, 46, 58, "Filter mode", "LP|BP|HP"));
    v.push_back (section (702, r3, 150, 80, TR_NOOP ("Arpeggiator")));
    v.push_back (radio (708, r3 + 18, 50, 62, "Arpeggiator mode", "Off|Up|Down|Up-dn|Rnd"));
    v.push_back (radio (762, r3 + 18, 32, 62, "Arpeggiator range", "1|2|3|4"));
    v.push_back (menuOf (798, r3 + 24, 50, 34, "Arpeggiator speed", TR_NOOP ("Speed"), "1/2|1|2|3|4|8|16"));

    // row 4: delay, chorus, gate
    const float r4 = 290;
    v.push_back (section (6, r4, 364, 86, TR_NOOP ("Delay")));
    button (16, r4 + 24, "Delay on", TR_NOOP ("On"));
    wheel (52, r4 + 22, "Delay time L", TR_NOOP ("Time L"));
    wheel (97, r4 + 22, "Delay time R", TR_NOOP ("Time R"));
    wheel (142, r4 + 22, "Delay feedback", TR_NOOP ("Feedback"));
    wheel (187, r4 + 22, "Delay modulation", TR_NOOP ("Mod"));
    wheel (232, r4 + 22, "Delay low cut", TR_NOOP ("Low cut"));
    wheel (277, r4 + 22, "Delay high cut", TR_NOOP ("High cut"));
    wheel (322, r4 + 22, "Delay wet", TR_NOOP ("Wet"));
    v.push_back (section (374, r4, 182, 86, TR_NOOP ("Chorus")));
    button (382, r4 + 24, "Chorus on", TR_NOOP ("On"));
    wheel (418, r4 + 22, "Chorus depth", TR_NOOP ("Depth"));
    wheel (463, r4 + 22, "Chorus rate", TR_NOOP ("Rate"));
    wheel (508, r4 + 22, "Chorus wet", TR_NOOP ("Wet"));
    v.push_back (section (560, r4, 292, 86, TR_NOOP ("Gate")));
    wheel (565, r4 + 22, "Gate smooth", TR_NOOP ("Smooth"));
    wheel (610, r4 + 22, "Gate mix", TR_NOOP ("Mix"));
    v.push_back (menuOf (656, r4 + 26, 46, 34, "Gate speed", TR_NOOP ("Speed"), "1/2|1|2|3|4|8|16"));
    for (int i = 0; i < 16; ++i)
        v.push_back (toggle (712 + 17 * (float) (i % 8), r4 + 26 + 24 * (float) (i / 8), 16, 22,
                             stored ("Gate step " + std::to_string (i + 1)), nullptr, boxed));

    // row 5: FL's two wheels under the panel (TSynthEditorForm's PitchBendWheel at x 47 and ModWheel at x 97, 19 × 94;
    // the pitch bend springs back to the centre when released, FL's PitchBendReset)
    const float r5 = 382;
    v.push_back (section (6, r5, 134, 102, TR_NOOP ("Wheels")));
    v.push_back (slider (Item::vslider, 40, r5 + 22, 25, 62, "Pitch bend", TR_NOOP ("Bend"), bipolar | springBack));
    v.push_back (slider (Item::vslider, 90, r5 + 22, 25, 62, "Mod wheel", TR_NOOP ("Mod")));
    return l;
}
} // namespace

const Layout* transistorBassFlmLayout()
{
    static const Layout mobile = transistorBassMobileLayout();
    return &mobile;
}

const Layout* instrumentLayout (InstrumentKind kind)
{
    static const Layout osc = threeOsc(), keys = flKeys(), boo = booBass(), sytrus = sytrusMain(), toxic = toxicMain(), poizone = poizoneMain();
    switch (kind)
    {
        case InstrumentKind::toxic: return &toxic;
        case InstrumentKind::poizone: return &poizone;
        case InstrumentKind::threeosc: return &osc;
        case InstrumentKind::flkeys: return &keys;
        case InstrumentKind::boobass: return &boo;
        case InstrumentKind::sytrus: return &sytrus;
        default: break;
    }
    // round 168q (core/EngineLayouts.cpp)
    static const Layout kepler = keplerLayout(), keplerExo = keplerExoLayout(), tb = transistorBassLayout(), sawer = sawerLayout(),
                        beepMap = beepMapLayout(), granulizer = granulizerLayout(), wasp = waspLayout();
    switch (kind)
    {
        case InstrumentKind::kepler: return &kepler;
        case InstrumentKind::keplerExo: return &keplerExo;
        case InstrumentKind::transistorbass: return &tb;
        case InstrumentKind::sawer: return &sawer;
        case InstrumentKind::beepmap: return &beepMap;
        case InstrumentKind::granulizer: return &granulizer;
        case InstrumentKind::wasp: return &wasp;
        default: break;
    }
    if (const auto* d = drumLayout (kind)) // round 168p (PluginLayoutDrums.inc)
        return d;
    switch (kind)
    {
        default: return synthGroupLayout (kind); // round 168o (core/PluginLayoutSynths.cpp)
    }
}

const std::vector<InstrumentKind>& instrumentLayoutKinds()
{
    static const std::vector<InstrumentKind> kinds { InstrumentKind::threeosc, InstrumentKind::flkeys, InstrumentKind::boobass, InstrumentKind::sytrus, InstrumentKind::harmor,
                                                     InstrumentKind::toxic, InstrumentKind::poizone,
                                                     InstrumentKind::kepler, InstrumentKind::keplerExo, InstrumentKind::transistorbass, InstrumentKind::sawer,
                                                     InstrumentKind::beepmap, InstrumentKind::granulizer, InstrumentKind::wasp, // (round 168q)
                                                     // round 168p
                                                     InstrumentKind::fruitkick, InstrumentKind::plucked, InstrumentKind::drumpad, InstrumentKind::drumaxx,
                                                     InstrumentKind::bassdrum, InstrumentKind::drumsynth, InstrumentKind::sakura, InstrumentKind::slayer };
    static const std::vector<InstrumentKind> all = [] {
        auto k = kinds;
        for (const auto s : synthGroupLayoutKinds())
            k.push_back (s);
        return k;
    }();
    return all;
}

const Layout& soundgoodizerLayout()
{
    static const Layout l = soundgoodizer();
    return l;
}

bool displayTakesMouse (const char* display)
{
    if (display == nullptr)
        return false;
    static const char* const ids[] { "eq", "xy", "sgamount", "sgpresets", "prismbands", "emberbands", "tunekeys", "tuneautokey",
                                     "pitchring", "waveshaper", "tssenvelope", "tsssend0", "tsssend1", "maxcurve", "equosend", "lpcurve", "cvcurve", "vxcurve", "vxmapsel", "pitchport" };
    for (const auto* id : ids)
        if (std::strcmp (id, display) == 0)
            return true;
    return false;
}

bool hasFlLayout (EffectKind kind)
{
    return *layoutFor (kind).fl != 0;
}

bool usesStateParams (EffectKind kind) noexcept
{
    return stateparams::tableFor (kind) != nullptr;
}

int paramCount (EffectKind kind)
{
    const auto* table = stateparams::tableFor (kind);
    return (int) effectInfo (kind).params.size() + (table != nullptr ? (int) table->params.size() : 0);
}

int stateIndex (EffectKind kind, int param)
{
    const auto plain = (int) effectInfo (kind).params.size();
    const auto* table = stateparams::tableFor (kind);
    return table != nullptr && param >= plain && param < plain + (int) table->params.size() ? param - plain : -1;
}

const char* paramName (EffectKind kind, int index)
{
    if (index < 0 || index >= paramCount (kind))
        return "";
    if (const auto s = stateIndex (kind, index); s >= 0)
        return stateparams::tableFor (kind)->name (s);
    return effectInfo (kind).params[(std::size_t) index].name;
}

int paramIndex (EffectKind kind, const char* name)
{
    if (name == nullptr)
        return -1;
    const auto& params = effectInfo (kind).params;
    for (int i = 0; i < (int) params.size(); ++i)
        if (std::strcmp (params[(std::size_t) i].name, name) == 0)
            return i;
    if (const auto* table = stateparams::tableFor (kind))
        if (const auto s = table->index (name); s >= 0)
            return (int) params.size() + s;
    return -1;
}

float stateValue01 (EffectKind kind, int param, const std::vector<std::uint8_t>& state)
{
    const auto s = stateIndex (kind, param);
    if (s < 0)
        return 0.0f;
    const auto* table = stateparams::tableFor (kind);
    const auto& p = table->params[(std::size_t) s];
    const auto raw = table->get (state, s).value_or (p.defaultValue);
    return p.maximum > p.minimum ? (float) ((double) (std::clamp (raw, p.minimum, p.maximum) - p.minimum) / (double) (p.maximum - p.minimum)) : 0.0f;
}

int stateRaw (EffectKind kind, int param, float value01)
{
    const auto s = stateIndex (kind, param);
    if (s < 0)
        return 0;
    const auto& p = stateparams::tableFor (kind)->params[(std::size_t) s];
    return p.minimum + (int) std::lround ((double) std::clamp (value01, 0.0f, 1.0f) * (double) (p.maximum - p.minimum));
}

std::vector<std::string> optionLabels (const LayoutItem& item)
{
    std::vector<std::string> out;
    if (item.options == nullptr)
        return out;
    std::string current;
    for (const char* p = item.options; *p != 0; ++p)
    {
        if (*p == '|')
        {
            out.push_back (current);
            current.clear();
        }
        else
            current += *p;
    }
    out.push_back (current);
    return out;
}

float optionValue (const LayoutItem& item, int option)
{
    if (option >= 0 && option < (int) item.values.size())
        return item.values[(std::size_t) option];
    const auto n = (int) optionLabels (item).size();
    return n > 1 ? (float) option / (float) (n - 1) : 0.0f;
}

int optionAt (const LayoutItem& item, float value)
{
    const auto n = (int) optionLabels (item).size();
    auto best = 0;
    auto distance = 2.0f;
    for (int o = 0; o < n; ++o)
        if (const auto d = std::abs (optionValue (item, o) - value); d < distance)
        {
            distance = d;
            best = o;
        }
    return best;
}

bool isShown (EffectKind kind, const LayoutItem& item, int page, const EffectParams& params, const std::vector<std::uint8_t>* state)
{
    if (item.page >= 0 && (item.pageModulo > 0 ? (page / std::max (1, item.pageStride)) % item.pageModulo : page) != item.page)
        return false;
    const auto inRange = [&] (const char* param, float lo, float hi) {
        if (param == nullptr)
            return true;
        const auto i = paramIndex (kind, param);
        const auto v = stateIndex (kind, i) >= 0 ? (state != nullptr ? stateValue01 (kind, i, *state) : 0.0f)
                       : i >= 0 && i < (int) params.size() ? params[(std::size_t) i] : 0.0f;
        return v >= lo && (v < hi || hi >= 1.0f); // [lo, hi), the top end included at 1
    };
    return inRange (item.whenParam, item.whenMin, item.whenMax) && inRange (item.when2Param, item.when2Min, item.when2Max)
           && inRange (item.when3Param, item.when3Min, item.when3Max);
}

bool isIdle (EffectKind kind, const LayoutItem& item, const EffectParams& params)
{
    const auto inRange = [&] (const char* param, float lo, float hi) {
        if (param == nullptr)
            return false;
        const auto i = paramIndex (kind, param);
        const auto v = i >= 0 && i < (int) params.size() ? params[(std::size_t) i] : 0.0f;
        return v >= lo && (v < hi || hi >= 1.0f);
    };
    return inRange (item.idleParam, item.idleMin, item.idleMax) || inRange (item.idle2Param, item.idle2Min, item.idle2Max);
}

bool isValueControl (Item type) noexcept
{
    return type != Item::section && type != Item::label && type != Item::display;
}
} // namespace lynx::plugui
