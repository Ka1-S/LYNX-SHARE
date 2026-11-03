#include "ui/TrackIcons.h"

#include <array>
#include <cmath>

#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace lynx::ui
{
namespace
{
using trackicons::Icon;
using P = juce::Point<float>;

struct Shape
{
    juce::Path stroke, fill;
    juce::String text; // the numbers: a digit in a ring
};

void line (juce::Path& p, float x1, float y1, float x2, float y2)
{
    p.startNewSubPath (x1, y1);
    p.lineTo (x2, y2);
}

void poly (juce::Path& p, std::initializer_list<P> points, bool close = false)
{
    bool first = true;
    for (auto pt : points)
    {
        if (first)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
        first = false;
    }
    if (close)
        p.closeSubPath();
}

void circle (juce::Path& p, float cx, float cy, float r) { p.addEllipse (cx - r, cy - r, 2 * r, 2 * r); }
void ellipse (juce::Path& p, float cx, float cy, float rx, float ry) { p.addEllipse (cx - rx, cy - ry, 2 * rx, 2 * ry); }

void quad (juce::Path& p, P a, P c, P b)
{
    p.startNewSubPath (a);
    p.quadraticTo (c, b);
}

// a note head with a stem (music icons)
void note (juce::Path& st, juce::Path& fi, float x, float y)
{
    ellipse (fi, x, y, 2.4f, 1.8f);
    line (st, x + 2.2f, y, x + 2.2f, y - 9.0f);
}

// a head and shoulders (voice icons)
void person (juce::Path& st, float cx)
{
    circle (st, cx, 8.5f, 3.5f);
    quad (st, { cx - 6.5f, 20.5f }, { cx, 10.5f }, { cx + 6.5f, 20.5f });
}

Shape make (Icon icon)
{
    Shape s;
    auto& st = s.stroke;
    auto& fi = s.fill;

    switch (icon)
    {
        case Icon::none:
            break;

        // --- drums and percussion
        case Icon::kick: // a bass drum seen from the front, its beater
            circle (st, 12.0f, 13.0f, 7.5f);
            circle (st, 12.0f, 13.0f, 3.0f);
            line (st, 18.5f, 3.5f, 14.5f, 9.0f);
            circle (fi, 19.0f, 3.5f, 1.6f);
            break;
        case Icon::snare: // a shallow drum and two sticks
            ellipse (st, 12.0f, 11.0f, 8.0f, 2.8f);
            poly (st, { { 4.0f, 11.0f }, { 4.0f, 17.0f } });
            poly (st, { { 20.0f, 11.0f }, { 20.0f, 17.0f } });
            quad (st, { 4.0f, 17.0f }, { 12.0f, 22.0f }, { 20.0f, 17.0f });
            line (st, 6.0f, 3.5f, 11.0f, 9.0f);
            line (st, 18.0f, 3.5f, 13.0f, 9.0f);
            break;
        case Icon::hiHat: // two cymbals on a stand
            ellipse (st, 12.0f, 7.0f, 8.5f, 1.8f);
            ellipse (st, 12.0f, 10.0f, 8.5f, 1.8f);
            line (st, 12.0f, 3.5f, 12.0f, 20.5f);
            poly (st, { { 7.5f, 20.5f }, { 12.0f, 16.5f }, { 16.5f, 20.5f } });
            break;
        case Icon::cymbal: // one tilted cymbal, its stand
            {
                juce::Path c;
                ellipse (c, 12.0f, 9.0f, 9.0f, 2.5f);
                st.addPath (c, juce::AffineTransform::rotation (-0.25f, 12.0f, 9.0f));
            }
            circle (fi, 12.0f, 9.0f, 1.2f);
            line (st, 12.0f, 11.0f, 12.0f, 20.5f);
            poly (st, { { 8.0f, 20.5f }, { 12.0f, 17.0f }, { 16.0f, 20.5f } });
            break;
        case Icon::clap: // two open hands meeting, motion marks
            poly (st, { { 5.0f, 20.5f }, { 7.0f, 11.0f }, { 11.0f, 7.5f }, { 12.5f, 9.0f }, { 10.5f, 13.0f } });
            poly (st, { { 12.5f, 20.5f }, { 13.0f, 12.0f }, { 16.5f, 7.5f }, { 18.0f, 9.0f }, { 16.0f, 14.0f } });
            line (st, 18.5f, 3.5f, 17.5f, 5.5f);
            line (st, 21.0f, 6.0f, 19.5f, 7.0f);
            line (st, 9.0f, 3.5f, 9.5f, 5.0f);
            break;
        case Icon::percussion: // two hand drums (bongos)
            ellipse (st, 8.0f, 8.0f, 4.5f, 1.6f);
            poly (st, { { 3.5f, 8.0f }, { 5.0f, 19.0f }, { 11.0f, 19.0f }, { 12.5f, 8.0f } });
            ellipse (st, 17.0f, 10.0f, 3.5f, 1.3f);
            poly (st, { { 13.5f, 10.0f }, { 14.8f, 19.0f }, { 19.2f, 19.0f }, { 20.5f, 10.0f } });
            line (st, 12.0f, 14.0f, 14.0f, 14.0f);
            break;
        case Icon::shaker: // an egg shaker and its motion lines
            {
                juce::Path egg;
                ellipse (egg, 12.0f, 12.0f, 4.5f, 6.5f);
                st.addPath (egg, juce::AffineTransform::rotation (0.5f, 12.0f, 12.0f));
            }
            line (st, 3.5f, 9.0f, 5.5f, 9.5f);
            line (st, 3.5f, 13.0f, 5.5f, 13.0f);
            line (st, 18.5f, 15.0f, 20.5f, 14.5f);
            line (st, 18.5f, 11.0f, 20.5f, 11.0f);
            break;
        case Icon::tambourine: // a ring with jingles
            circle (st, 12.0f, 12.0f, 8.0f);
            for (int i = 0; i < 6; ++i)
            {
                const auto a = (float) i * juce::MathConstants<float>::twoPi / 6.0f;
                circle (fi, 12.0f + 8.0f * std::cos (a), 12.0f + 8.0f * std::sin (a), 1.6f);
            }
            break;
        case Icon::cowbell: // a bell narrowing up, a handle
            poly (st, { { 8.5f, 6.5f }, { 15.5f, 6.5f }, { 18.5f, 19.5f }, { 5.5f, 19.5f } }, true);
            poly (st, { { 10.5f, 6.5f }, { 10.5f, 3.5f }, { 13.5f, 3.5f }, { 13.5f, 6.5f } });
            break;
        case Icon::drumMachine: // a box with four pads
            st.addRoundedRectangle (3.5f, 5.5f, 17.0f, 13.0f, 2.0f);
            for (int i = 0; i < 4; ++i)
                fi.addRoundedRectangle (5.5f + (float) i * 3.9f, 12.0f, 3.0f, 3.5f, 0.6f);
            line (st, 6.0f, 9.0f, 12.0f, 9.0f);
            circle (st, 17.0f, 9.0f, 1.2f);
            break;

        // --- instruments
        case Icon::acousticGuitar: // a guitar body (two bouts), sound hole, neck
            {
                juce::Path body;
                circle (body, 9.0f, 16.5f, 4.5f);
                circle (body, 13.0f, 12.0f, 3.2f);
                st.addPath (body);
            }
            circle (fi, 10.5f, 15.0f, 1.2f);
            line (st, 14.5f, 10.0f, 20.0f, 4.0f);
            line (st, 19.0f, 3.0f, 21.0f, 5.0f);
            break;
        case Icon::electricGuitar: // a pointed body, pickups, neck
            poly (st, { { 4.0f, 19.0f }, { 5.0f, 13.0f }, { 9.5f, 12.0f }, { 12.0f, 9.5f }, { 13.0f, 14.5f }, { 10.0f, 17.0f }, { 9.0f, 20.5f } }, true);
            line (st, 7.0f, 15.0f, 9.0f, 17.0f);
            line (st, 11.0f, 11.0f, 20.0f, 4.0f);
            line (st, 18.5f, 3.0f, 21.0f, 5.5f);
            break;
        case Icon::strings: // a violin: body, f-holes, neck, bow
            ellipse (st, 10.0f, 15.0f, 4.0f, 5.5f);
            line (st, 10.0f, 9.5f, 10.0f, 3.5f);
            line (st, 8.5f, 14.0f, 8.5f, 17.0f);
            line (st, 11.5f, 14.0f, 11.5f, 17.0f);
            line (st, 15.0f, 20.5f, 20.5f, 5.0f);
            break;
        case Icon::brass: // a trumpet: bell, tube, valves
            poly (st, { { 3.5f, 13.0f }, { 14.0f, 13.0f } });
            poly (st, { { 14.0f, 13.0f }, { 20.5f, 8.5f }, { 20.5f, 17.5f }, { 14.0f, 13.0f } });
            st.addRoundedRectangle (5.5f, 13.0f, 8.0f, 4.0f, 2.0f);
            for (int i = 0; i < 3; ++i)
                line (st, 7.0f + (float) i * 2.5f, 13.0f, 7.0f + (float) i * 2.5f, 9.5f);
            break;
        case Icon::saxophone: // a saxophone's J with its bell
            poly (st, { { 9.0f, 3.5f }, { 9.0f, 15.0f } });
            quad (st, { 9.0f, 15.0f }, { 9.0f, 20.5f }, { 14.0f, 20.5f });
            quad (st, { 14.0f, 20.5f }, { 18.0f, 20.5f }, { 18.0f, 15.0f });
            poly (st, { { 16.0f, 14.0f }, { 20.5f, 14.0f } });
            line (st, 6.5f, 3.5f, 9.0f, 5.0f);
            for (int i = 0; i < 3; ++i)
                circle (fi, 9.0f, 8.0f + (float) i * 2.5f, 0.9f);
            break;
        case Icon::flute: // a long tube, finger holes
            st.addRoundedRectangle (2.5f, 10.5f, 19.0f, 3.0f, 1.5f);
            for (int i = 0; i < 4; ++i)
                circle (fi, 9.0f + (float) i * 3.0f, 12.0f, 0.8f);
            circle (fi, 5.0f, 12.0f, 0.9f);
            break;
        case Icon::organ: // pipes of rising height
            for (int i = 0; i < 5; ++i)
            {
                const auto x = 4.0f + (float) i * 3.5f;
                const auto top = 4.0f + std::abs ((float) i - 2.0f) * 3.0f;
                st.addRoundedRectangle (x, top, 2.5f, 20.5f - top, 1.0f);
            }
            break;
        case Icon::piano: // a grand piano's lid seen from above
            poly (st, { { 4.0f, 20.5f }, { 4.0f, 6.0f }, { 9.0f, 3.5f }, { 13.0f, 6.0f }, { 14.0f, 12.0f }, { 20.0f, 15.0f }, { 20.0f, 20.5f } }, true);
            for (int i = 0; i < 4; ++i)
                line (st, 7.0f + (float) i * 3.5f, 20.5f, 7.0f + (float) i * 3.5f, 17.5f);
            break;
        case Icon::keyboard: // keys
            st.addRoundedRectangle (3.0f, 6.5f, 18.0f, 11.0f, 1.5f);
            for (int i = 1; i < 6; ++i)
                line (st, 3.0f + (float) i * 3.0f, 12.0f, 3.0f + (float) i * 3.0f, 17.5f);
            for (int i : { 1, 2, 4, 5 })
                fi.addRectangle (3.0f + (float) i * 3.0f - 0.9f, 6.5f, 1.8f, 5.5f);
            break;
        case Icon::harp: // a frame and strings
            poly (st, { { 5.0f, 20.5f }, { 5.0f, 4.0f } });
            quad (st, { 5.0f, 4.0f }, { 14.0f, 4.0f }, { 19.5f, 12.0f });
            poly (st, { { 19.5f, 12.0f }, { 19.5f, 20.5f }, { 5.0f, 20.5f } });
            for (int i = 1; i < 4; ++i)
                line (st, 5.0f + (float) i * 3.6f, 5.5f + (float) i * 0.8f, 5.0f + (float) i * 3.6f, 20.5f);
            break;
        case Icon::bell: // a hand bell
            quad (st, { 5.0f, 17.5f }, { 7.0f, 4.0f }, { 12.0f, 4.0f });
            quad (st, { 12.0f, 4.0f }, { 17.0f, 4.0f }, { 19.0f, 17.5f });
            line (st, 4.0f, 17.5f, 20.0f, 17.5f);
            circle (fi, 12.0f, 20.0f, 1.6f);
            break;
        case Icon::tuningFork:
            poly (st, { { 9.0f, 3.5f }, { 9.0f, 12.0f } });
            poly (st, { { 15.0f, 3.5f }, { 15.0f, 12.0f } });
            quad (st, { 9.0f, 12.0f }, { 12.0f, 16.0f }, { 15.0f, 12.0f });
            line (st, 12.0f, 14.5f, 12.0f, 20.5f);
            break;
        case Icon::melody: // two notes joined by a beam
            note (st, fi, 7.0f, 18.0f);
            note (st, fi, 15.5f, 16.0f);
            fi.addQuadrilateral (9.2f, 9.0f, 17.7f, 7.0f, 17.7f, 9.0f, 9.2f, 11.0f);
            break;
        case Icon::bass: // a low wave and a note
            note (st, fi, 6.0f, 18.0f);
            quad (st, { 11.0f, 13.0f }, { 14.0f, 4.0f }, { 17.0f, 13.0f });
            quad (st, { 17.0f, 13.0f }, { 20.0f, 22.0f }, { 23.0f, 13.0f });
            break;
        case Icon::synth: // a saw wave
            poly (st, { { 3.0f, 17.0f }, { 9.0f, 7.0f }, { 9.0f, 17.0f }, { 15.0f, 7.0f }, { 15.0f, 17.0f }, { 21.0f, 7.0f } });
            break;
        case Icon::squareSynth: // a pulse wave
            poly (st, { { 3.0f, 17.0f }, { 3.0f, 7.0f }, { 9.0f, 7.0f }, { 9.0f, 17.0f }, { 15.0f, 17.0f }, { 15.0f, 7.0f }, { 21.0f, 7.0f } });
            break;
        case Icon::pad: // a soft swell
            quad (st, { 3.0f, 18.0f }, { 7.0f, 6.0f }, { 12.0f, 6.0f });
            quad (st, { 12.0f, 6.0f }, { 17.0f, 6.0f }, { 21.0f, 18.0f });
            line (st, 3.0f, 18.0f, 21.0f, 18.0f);
            line (st, 7.0f, 14.0f, 17.0f, 14.0f);
            break;
        case Icon::pluck: // a sharp attack and a quick decay
            poly (st, { { 3.0f, 19.0f }, { 6.0f, 5.0f } });
            quad (st, { 6.0f, 5.0f }, { 8.0f, 17.0f }, { 21.0f, 19.0f });
            break;
        case Icon::arpeggio: // notes going up a staircase
            for (int i = 0; i < 4; ++i)
                fi.addRoundedRectangle (3.5f + (float) i * 4.5f, 16.5f - (float) i * 4.0f, 3.5f, 3.0f, 0.8f);
            break;
        case Icon::wobble: // a wave whose swings grow
            poly (st, { { 3.0f, 12.0f }, { 5.0f, 10.5f }, { 7.0f, 13.5f }, { 9.5f, 8.5f }, { 12.5f, 15.5f }, { 16.0f, 5.5f }, { 19.5f, 18.5f }, { 21.0f, 12.0f } });
            break;

        // --- voice
        case Icon::vocals: // a head singing
            person (st, 10.0f);
            quad (st, { 16.5f, 6.0f }, { 18.5f, 8.5f }, { 16.5f, 11.0f });
            quad (st, { 19.0f, 4.0f }, { 22.0f, 8.5f }, { 19.0f, 13.0f });
            break;
        case Icon::maleVocals: // a head, a low wave
            person (st, 9.0f);
            poly (st, { { 15.5f, 6.0f }, { 17.0f, 4.5f }, { 18.5f, 6.0f }, { 20.0f, 4.5f }, { 21.5f, 6.0f } });
            break;
        case Icon::femaleVocals: // a head with long hair, a high wave
            circle (st, 9.0f, 8.5f, 3.5f);
            quad (st, { 5.5f, 8.5f }, { 4.5f, 14.0f }, { 3.0f, 15.0f });
            quad (st, { 12.5f, 8.5f }, { 13.5f, 14.0f }, { 15.0f, 15.0f });
            quad (st, { 2.5f, 20.5f }, { 9.0f, 11.0f }, { 15.5f, 20.5f });
            poly (st, { { 16.0f, 6.0f }, { 17.0f, 4.0f }, { 18.0f, 6.0f }, { 19.0f, 4.0f }, { 20.0f, 6.0f }, { 21.0f, 4.0f } });
            break;
        case Icon::vocoder: // a head with a robotic mouth (a grid)
            st.addRoundedRectangle (5.0f, 4.0f, 14.0f, 13.0f, 3.0f);
            circle (fi, 9.5f, 9.0f, 1.3f);
            circle (fi, 14.5f, 9.0f, 1.3f);
            for (int i = 0; i < 4; ++i)
                line (st, 9.0f + (float) i * 2.0f, 12.5f, 9.0f + (float) i * 2.0f, 14.5f);
            line (st, 12.0f, 17.0f, 12.0f, 20.5f);
            break;
        case Icon::microphone:
            st.addRoundedRectangle (9.0f, 3.5f, 6.0f, 10.5f, 3.0f);
            quad (st, { 6.0f, 11.0f }, { 6.0f, 17.0f }, { 12.0f, 17.0f });
            quad (st, { 12.0f, 17.0f }, { 18.0f, 17.0f }, { 18.0f, 11.0f });
            line (st, 12.0f, 17.0f, 12.0f, 20.5f);
            line (st, 8.5f, 20.5f, 15.5f, 20.5f);
            break;

        // --- audio and samples
        case Icon::sample: // a waveform in a page
            st.addRoundedRectangle (4.0f, 4.0f, 16.0f, 16.0f, 2.5f);
            for (int i = 0; i < 5; ++i)
            {
                const float h[] { 2.0f, 5.0f, 3.5f, 6.0f, 2.5f };
                line (st, 7.0f + (float) i * 2.5f, 12.0f - h[i], 7.0f + (float) i * 2.5f, 12.0f + h[i]);
            }
            break;
        case Icon::loop: // a circular arrow
            {
                juce::Path a;
                a.addCentredArc (12.0f, 12.0f, 7.5f, 7.5f, 0.0f, 0.5f, 5.6f, true);
                st.addPath (a);
            }
            poly (fi, { { 15.0f, 3.0f }, { 19.5f, 5.5f }, { 15.0f, 8.5f } }, true);
            break;
        case Icon::audio: // a waveform
            for (int i = 0; i < 8; ++i)
            {
                const float h[] { 2.0f, 5.0f, 8.0f, 4.0f, 6.5f, 3.0f, 5.0f, 1.5f };
                line (st, 3.5f + (float) i * 2.4f, 12.0f - h[i], 3.5f + (float) i * 2.4f, 12.0f + h[i]);
            }
            break;
        case Icon::notes: // piano-roll notes
            fi.addRoundedRectangle (3.5f, 6.0f, 7.0f, 3.0f, 1.0f);
            fi.addRoundedRectangle (10.0f, 11.0f, 9.0f, 3.0f, 1.0f);
            fi.addRoundedRectangle (6.0f, 16.0f, 5.0f, 3.0f, 1.0f);
            fi.addRoundedRectangle (14.0f, 4.0f, 6.0f, 3.0f, 1.0f);
            break;
        case Icon::steps: // a step row, some lit
            for (int i = 0; i < 4; ++i)
            {
                const auto r = juce::Rectangle<float> (3.5f + (float) i * 4.5f, 9.5f, 3.5f, 5.0f);
                if (i == 0 || i == 2)
                    fi.addRoundedRectangle (r, 0.8f);
                else
                    st.addRoundedRectangle (r, 0.8f);
            }
            break;
        case Icon::automation: // a curve through points
            poly (st, { { 3.5f, 18.0f }, { 9.0f, 8.0f }, { 15.0f, 14.0f }, { 20.5f, 5.0f } });
            for (auto p : { P { 3.5f, 18.0f }, P { 9.0f, 8.0f }, P { 15.0f, 14.0f }, P { 20.5f, 5.0f } })
                circle (fi, p.x, p.y, 1.7f);
            break;
        case Icon::nature: // a leaf
            quad (st, { 4.0f, 20.0f }, { 4.0f, 5.0f }, { 20.0f, 4.0f });
            quad (st, { 20.0f, 4.0f }, { 19.0f, 20.0f }, { 4.0f, 20.0f });
            line (st, 4.0f, 20.0f, 14.0f, 10.0f);
            break;
        case Icon::film: // a clapper board
            st.addRoundedRectangle (3.5f, 9.5f, 17.0f, 11.0f, 1.5f);
            poly (st, { { 3.5f, 9.5f }, { 19.0f, 5.0f }, { 18.0f, 2.5f }, { 3.0f, 7.0f } });
            line (st, 8.5f, 5.5f, 10.0f, 8.0f);
            line (st, 13.5f, 4.0f, 15.0f, 6.5f);
            break;
        case Icon::vinyl:
            circle (st, 12.0f, 12.0f, 8.5f);
            circle (st, 12.0f, 12.0f, 5.5f);
            circle (fi, 12.0f, 12.0f, 1.6f);
            break;

        // --- effects and routing
        case Icon::effect: // a sparkle
            poly (fi, { { 12.0f, 3.0f }, { 14.0f, 10.0f }, { 21.0f, 12.0f }, { 14.0f, 14.0f }, { 12.0f, 21.0f }, { 10.0f, 14.0f }, { 3.0f, 12.0f }, { 10.0f, 10.0f } }, true);
            break;
        case Icon::reverb: // a source and expanding arcs
            circle (fi, 5.5f, 12.0f, 2.0f);
            for (int i = 0; i < 3; ++i)
            {
                juce::Path a;
                const auto r = 5.0f + (float) i * 4.5f;
                a.addCentredArc (5.5f, 12.0f, r, r, 0.0f, 0.75f, 2.39f, true);
                st.addPath (a);
            }
            break;
        case Icon::equalizer: // three sliders
            for (int i = 0; i < 3; ++i)
            {
                const auto x = 6.0f + (float) i * 6.0f;
                line (st, x, 4.0f, x, 20.0f);
                const float y[] { 14.0f, 8.0f, 12.0f };
                fi.addRoundedRectangle (x - 2.5f, y[i] - 1.5f, 5.0f, 3.0f, 1.0f);
            }
            break;
        case Icon::compressor: // a level squeezed between two bars
            line (st, 4.0f, 4.0f, 20.0f, 4.0f);
            line (st, 4.0f, 20.0f, 20.0f, 20.0f);
            poly (st, { { 8.0f, 7.0f }, { 12.0f, 10.0f }, { 16.0f, 7.0f } });
            poly (st, { { 8.0f, 17.0f }, { 12.0f, 14.0f }, { 16.0f, 17.0f } });
            break;
        case Icon::filter: // a low-pass curve
            poly (st, { { 3.0f, 9.0f }, { 12.0f, 9.0f } });
            quad (st, { 12.0f, 9.0f }, { 16.0f, 9.0f }, { 19.0f, 20.0f });
            break;
        case Icon::riser: // a rising arrow
            poly (st, { { 3.5f, 19.0f }, { 9.0f, 15.0f }, { 13.0f, 16.0f }, { 20.0f, 6.0f } });
            poly (st, { { 14.5f, 5.5f }, { 20.0f, 6.0f }, { 20.5f, 11.5f } });
            break;
        case Icon::send: // an arrow leaving a box
            poly (st, { { 12.0f, 5.0f }, { 4.0f, 5.0f }, { 4.0f, 19.0f }, { 18.0f, 19.0f }, { 18.0f, 13.0f } });
            line (st, 10.0f, 13.0f, 20.0f, 4.0f);
            poly (st, { { 14.5f, 4.0f }, { 20.0f, 4.0f }, { 20.0f, 9.5f } });
            break;
        case Icon::bus: // three lines merging into one
            line (st, 3.5f, 5.0f, 12.0f, 12.0f);
            line (st, 3.5f, 12.0f, 12.0f, 12.0f);
            line (st, 3.5f, 19.0f, 12.0f, 12.0f);
            line (st, 12.0f, 12.0f, 20.5f, 12.0f);
            circle (fi, 12.0f, 12.0f, 2.0f);
            break;
        case Icon::master: // a crown
            poly (st, { { 3.5f, 18.0f }, { 3.5f, 7.0f }, { 8.5f, 12.0f }, { 12.0f, 5.0f }, { 15.5f, 12.0f }, { 20.5f, 7.0f }, { 20.5f, 18.0f } }, true);
            break;
        case Icon::sidechain: // a chain link
            st.addRoundedRectangle (3.0f, 9.0f, 10.0f, 6.0f, 3.0f);
            st.addRoundedRectangle (11.0f, 9.0f, 10.0f, 6.0f, 3.0f);
            break;
        case Icon::plugin: // a plug
            st.addRoundedRectangle (6.5f, 8.0f, 11.0f, 7.0f, 2.0f);
            line (st, 9.5f, 3.5f, 9.5f, 8.0f);
            line (st, 14.5f, 3.5f, 14.5f, 8.0f);
            line (st, 12.0f, 15.0f, 12.0f, 20.5f);
            break;
        case Icon::knob:
            circle (st, 12.0f, 12.0f, 7.5f);
            line (st, 12.0f, 12.0f, 16.0f, 7.0f);
            for (int i = 0; i < 5; ++i)
            {
                const auto a = juce::MathConstants<float>::pi * (0.75f + 0.375f * (float) i);
                circle (fi, 12.0f + 10.0f * std::cos (a), 12.0f + 10.0f * std::sin (a), 0.7f);
            }
            break;
        case Icon::midi: // a round connector, five pins
            circle (st, 12.0f, 12.0f, 8.5f);
            for (int i = 0; i < 5; ++i)
            {
                const auto a = juce::MathConstants<float>::pi * (0.0f + 0.25f * (float) i);
                circle (fi, 12.0f + 4.8f * std::cos (a + juce::MathConstants<float>::pi), 12.0f - 4.8f * std::sin (a), 1.1f);
            }
            line (st, 10.5f, 20.5f, 13.5f, 20.5f);
            break;
        case Icon::headphones:
            quad (st, { 4.5f, 15.0f }, { 4.5f, 4.0f }, { 12.0f, 4.0f });
            quad (st, { 12.0f, 4.0f }, { 19.5f, 4.0f }, { 19.5f, 15.0f });
            st.addRoundedRectangle (3.5f, 13.0f, 4.0f, 7.0f, 1.5f);
            st.addRoundedRectangle (16.5f, 13.0f, 4.0f, 7.0f, 1.5f);
            break;
        case Icon::speaker: // a cabinet with two cones
            st.addRoundedRectangle (5.5f, 3.5f, 13.0f, 17.0f, 2.0f);
            circle (st, 12.0f, 14.5f, 3.5f);
            circle (st, 12.0f, 7.5f, 1.8f);
            break;

        // --- numbers
        case Icon::number1: case Icon::number2: case Icon::number3: case Icon::number4: case Icon::number5:
        case Icon::number6: case Icon::number7: case Icon::number8: case Icon::number9:
            circle (st, 12.0f, 12.0f, 8.5f);
            s.text = juce::String (1 + (int) icon - (int) Icon::number1);
            break;

        // --- misc
        case Icon::smile:
            circle (st, 12.0f, 12.0f, 8.5f);
            circle (fi, 9.0f, 10.0f, 1.2f);
            circle (fi, 15.0f, 10.0f, 1.2f);
            quad (st, { 8.0f, 14.0f }, { 12.0f, 18.0f }, { 16.0f, 14.0f });
            break;
        case Icon::fire:
            quad (st, { 12.0f, 3.0f }, { 19.0f, 10.0f }, { 18.0f, 15.0f });
            quad (st, { 18.0f, 15.0f }, { 16.5f, 21.0f }, { 12.0f, 21.0f });
            quad (st, { 12.0f, 21.0f }, { 6.0f, 21.0f }, { 6.0f, 15.0f });
            quad (st, { 6.0f, 15.0f }, { 6.0f, 11.0f }, { 9.0f, 9.0f });
            quad (st, { 9.0f, 9.0f }, { 10.0f, 13.0f }, { 12.0f, 13.0f });
            quad (st, { 12.0f, 13.0f }, { 13.5f, 8.0f }, { 12.0f, 3.0f });
            break;
        case Icon::cloud:
            {
                juce::Path c;
                circle (c, 8.0f, 14.0f, 4.0f);
                circle (c, 13.0f, 11.0f, 5.0f);
                circle (c, 17.0f, 15.0f, 3.5f);
                c.addRectangle (8.0f, 14.0f, 9.0f, 4.5f);
                fi.addPath (c);
            }
            break;
        case Icon::heart:
            {
                juce::Path h;
                h.startNewSubPath (12.0f, 20.0f);
                h.cubicTo (2.0f, 13.0f, 3.0f, 4.0f, 8.0f, 4.5f);
                h.cubicTo (10.5f, 4.8f, 12.0f, 7.5f, 12.0f, 7.5f);
                h.cubicTo (12.0f, 7.5f, 13.5f, 4.8f, 16.0f, 4.5f);
                h.cubicTo (21.0f, 4.0f, 22.0f, 13.0f, 12.0f, 20.0f);
                h.closeSubPath();
                st.addPath (h);
            }
            break;
        case Icon::clock:
            circle (st, 12.0f, 12.0f, 8.5f);
            poly (st, { { 12.0f, 7.0f }, { 12.0f, 12.0f }, { 15.5f, 14.0f } });
            break;
        case Icon::gear:
            circle (st, 12.0f, 12.0f, 3.0f);
            for (int i = 0; i < 8; ++i)
            {
                const auto a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                line (st, 12.0f + 6.0f * std::cos (a), 12.0f + 6.0f * std::sin (a), 12.0f + 8.5f * std::cos (a), 12.0f + 8.5f * std::sin (a));
            }
            circle (st, 12.0f, 12.0f, 6.0f);
            break;
        case Icon::folder:
            poly (st, { { 3.5f, 18.5f }, { 3.5f, 5.5f }, { 9.0f, 5.5f }, { 11.0f, 8.0f }, { 20.5f, 8.0f }, { 20.5f, 18.5f } }, true);
            break;
        case Icon::lock:
            st.addRoundedRectangle (5.5f, 10.5f, 13.0f, 10.0f, 2.0f);
            quad (st, { 8.5f, 10.5f }, { 8.5f, 3.5f }, { 12.0f, 3.5f });
            quad (st, { 12.0f, 3.5f }, { 15.5f, 3.5f }, { 15.5f, 10.5f });
            circle (fi, 12.0f, 15.5f, 1.4f);
            break;
        case Icon::tag: // an FL icon LYNX has no own one for: a label
            poly (st, { { 3.5f, 12.0f }, { 3.5f, 3.5f }, { 12.0f, 3.5f }, { 20.5f, 12.0f }, { 12.0f, 20.5f } }, true);
            circle (fi, 8.0f, 8.0f, 1.5f);
            break;
    }
    return s;
}

const Shape& get (Icon icon)
{
    static const auto all = [] {
        std::array<Shape, trackicons::iconCount> shapes;
        for (int i = 0; i < trackicons::iconCount; ++i)
            shapes[(std::size_t) i] = make ((Icon) i);
        return shapes;
    }();
    const auto i = juce::jlimit (0, trackicons::iconCount - 1, (int) icon);
    return all[(std::size_t) i];
}

// The grid of the icon picker: LYNX's icons in rows, the current one highlighted, the name of the hovered one below.
class IconGrid final : public juce::Component
{
public:
    IconGrid (int currentFlIndex, std::function<void (int)> pick)
        : current (trackicons::iconFor (currentFlIndex)), onPick (std::move (pick))
    {
        const auto rowCount = ((int) trackicons::entries.size() + columns - 1) / columns;
        setSize (columns * cell + 2 * margin, rowCount * cell + 2 * margin + footer);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        for (int i = 0; i < (int) trackicons::entries.size(); ++i)
        {
            const auto& e = trackicons::entries[(std::size_t) i];
            const auto r = cellBounds (i).toFloat().reduced (2.0f);
            const auto isCurrent = e.icon == current;
            if (isCurrent || i == hovered)
                theme::fillRoundRect (g, r, radius::sm, isCurrent ? colour::accent.withAlpha (0.35f) : colour::bg4);
            if (e.icon == Icon::none)
            {
                g.setColour (colour::textSecondary);
                g.drawLine (r.getX() + 7.0f, r.getBottom() - 7.0f, r.getRight() - 7.0f, r.getY() + 7.0f, 1.3f);
            }
            else
                drawTrackIcon (g, e.icon, r.reduced (5.0f), isCurrent ? colour::textPrimary : colour::textSecondary.brighter (0.25f));
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        const auto shown = hovered >= 0 ? trackicons::entries[(std::size_t) hovered].icon : current;
        g.drawText (tr (juce::String (std::string (trackicons::nameOf (shown)))), getLocalBounds().removeFromBottom (footer).reduced (margin, 0),
                    juce::Justification::centredLeft, true);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (const auto i = indexAt (e.getPosition()); i != hovered)
        {
            hovered = i;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (const auto i = indexAt (e.getPosition()); i >= 0 && onPick)
            onPick (trackicons::entries[(std::size_t) i].flIndex);
    }

private:
    static constexpr int columns = 10, cell = 30, margin = 6, footer = 22;

    juce::Rectangle<int> cellBounds (int i) const
    {
        return { margin + (i % columns) * cell, margin + (i / columns) * cell, cell, cell };
    }
    int indexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) trackicons::entries.size(); ++i)
            if (cellBounds (i).contains (p))
                return i;
        return -1;
    }

    Icon current;
    std::function<void (int)> onPick;
    int hovered = -1;
};
} // namespace

void drawTrackIcon (juce::Graphics& g, trackicons::Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (icon == Icon::none || area.isEmpty())
        return;
    const auto size = std::floor (juce::jmin (area.getWidth(), area.getHeight()));
    const auto box = juce::Rectangle<float> (std::round (area.getCentreX() - size * 0.5f), std::round (area.getCentreY() - size * 0.5f), size, size);
    const auto transform = juce::AffineTransform::scale (size / 24.0f).translated (box.getX(), box.getY());
    const auto& shape = get (icon);
    g.setColour (colour);
    if (! shape.fill.isEmpty())
        g.fillPath (shape.fill, transform);
    if (! shape.stroke.isEmpty())
        g.strokePath (shape.stroke, juce::PathStrokeType (icons::strokeWidth (size), juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
    if (shape.text.isNotEmpty())
    {
        g.setFont (theme::font (size * 0.55f, true));
        g.drawText (shape.text, box, juce::Justification::centred, false);
    }
}

std::unique_ptr<juce::Component> makeTrackIconGrid (int currentFlIndex, std::function<void (int)> onPick)
{
    return std::make_unique<IconGrid> (currentFlIndex, std::move (onPick));
}

void showTrackIconPicker (juce::Rectangle<int> screenArea, int currentFlIndex, std::function<void (int)> onPick)
{
    // the grid's pick closes its box (known only once the box exists)
    auto box = std::make_shared<juce::Component::SafePointer<juce::CallOutBox>>();
    auto grid = makeTrackIconGrid (currentFlIndex, [box, onPick = std::move (onPick)] (int flIndex) {
        if (onPick)
            onPick (flIndex);
        if (*box != nullptr)
            (*box)->dismiss();
    });
    *box = &juce::CallOutBox::launchAsynchronously (std::move (grid), screenArea, nullptr);
}
} // namespace lynx::ui
