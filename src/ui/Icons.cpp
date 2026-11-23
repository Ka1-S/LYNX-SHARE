#include "ui/Icons.h"
#include "ui/Motion.h"
#include "ui/ImageCaches.h"
#include "ui/PaintAB.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "ui/Theme.h"

namespace lynx
{
namespace icons
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

void line (juce::Path& p, float x1, float y1, float x2, float y2)
{
    p.startNewSubPath (x1, y1);
    p.lineTo (x2, y2);
}

void roundRect (juce::Path& p, float x, float y, float w, float h, float r)
{
    p.addRoundedRectangle (x, y, w, h, r);
}

void polyline (juce::Path& p, std::initializer_list<juce::Point<float>> points, bool close = false)
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

Shape make (Icon icon)
{
    Shape s;
    auto& st = s.stroke;
    auto& fi = s.fill;

    // a page with a folded corner (file icons)
    auto page = [&st] {
        juce::Path p;
        polyline (p, { { 6.0f, 3.5f }, { 13.5f, 3.5f }, { 18.0f, 8.0f }, { 18.0f, 20.5f }, { 6.0f, 20.5f } }, true);
        st.addPath (p.createPathWithRoundedCorners (1.5f));
        polyline (st, { { 13.5f, 3.5f }, { 13.5f, 8.0f }, { 18.0f, 8.0f } });
    };

    // a speaker cone (mute and playback tools)
    auto speaker = [&st] {
        juce::Path p;
        polyline (p, { { 3.5f, 9.5f }, { 7.0f, 9.5f }, { 11.5f, 5.5f }, { 11.5f, 18.5f }, { 7.0f, 14.5f }, { 3.5f, 14.5f } }, true);
        st.addPath (p.createPathWithRoundedCorners (1.2f));
    };

    // a window outline with a title band (view icons)
    auto window = [&st] {
        roundRect (st, 3.5f, 4.5f, 17.0f, 15.0f, 2.0f);
    };

    // a horseshoe magnet with solid poles, opening downwards (the way FL users know the snap), scaled by `k` and moved
    // by (dx, dy) (snap icons)
    auto magnet = [&st, &fi] (float k, float dx, float dy) {
        const auto t = juce::AffineTransform::verticalFlip (24.0f).scaled (k).translated (dx, dy);
        juce::Path m;
        m.startNewSubPath (4.5f, 4.0f);
        m.lineTo (4.5f, 12.5f);
        m.addCentredArc (12.0f, 12.5f, 7.5f, 7.5f, 0.0f, pi * 1.5f, pi * 0.5f, false);
        m.lineTo (19.5f, 4.0f);
        m.lineTo (15.0f, 4.0f);
        m.lineTo (15.0f, 12.5f);
        m.addCentredArc (12.0f, 12.5f, 3.0f, 3.0f, 0.0f, pi * 0.5f, pi * 1.5f, false);
        m.lineTo (9.0f, 4.0f);
        m.closeSubPath();
        st.addPath (m, t);
        juce::Path poles;
        poles.addRectangle (4.5f, 4.0f, 4.5f, 3.5f);
        poles.addRectangle (15.0f, 4.0f, 4.5f, 3.5f);
        fi.addPath (poles, t);
    };

    switch (icon)
    {
        case Icon::none:
            break;

        // --- transport (solid) ---
        case Icon::play:
        {
            juce::Path tri;
            tri.addTriangle (7.0f, 4.5f, 7.0f, 19.5f, 19.5f, 12.0f);
            fi = tri.createPathWithRoundedCorners (2.0f);
            break;
        }
        case Icon::pause:
            fi.addRoundedRectangle (6.0f, 5.0f, 4.5f, 14.0f, 1.25f);
            fi.addRoundedRectangle (13.5f, 5.0f, 4.5f, 14.0f, 1.25f);
            break;
        case Icon::stop:
            fi.addRoundedRectangle (5.5f, 5.5f, 13.0f, 13.0f, 2.25f);
            break;
        case Icon::record:
            fi.addEllipse (5.0f, 5.0f, 14.0f, 14.0f);
            break;

        // --- small glyphs ---
        case Icon::chevronDown:
            polyline (st, { { 7.0f, 9.5f }, { 12.0f, 14.5f }, { 17.0f, 9.5f } });
            break;
        case Icon::more:
            for (float x : { 5.5f, 12.0f, 18.5f })
                fi.addEllipse (x - 1.75f, 10.25f, 3.5f, 3.5f);
            break;
        case Icon::plus:
            line (st, 12.0f, 5.0f, 12.0f, 19.0f);
            line (st, 5.0f, 12.0f, 19.0f, 12.0f);
            break;
        case Icon::check:
            polyline (st, { { 5.0f, 12.5f }, { 9.5f, 17.0f }, { 19.0f, 7.0f } });
            break;
        case Icon::close:
            line (st, 6.5f, 6.5f, 17.5f, 17.5f);
            line (st, 17.5f, 6.5f, 6.5f, 17.5f);
            break;
        case Icon::minimise:
            line (st, 6.0f, 12.0f, 18.0f, 12.0f);
            break;
        case Icon::maximise:
            roundRect (st, 5.5f, 5.5f, 13.0f, 13.0f, 2.0f);
            line (st, 5.5f, 9.0f, 18.5f, 9.0f);
            break;
        case Icon::restore: // two stacked windows
            roundRect (st, 4.5f, 8.5f, 11.0f, 11.0f, 1.75f);
            polyline (st, { { 8.5f, 4.5f }, { 17.5f, 4.5f }, { 19.5f, 6.5f }, { 19.5f, 15.5f } });
            break;
        case Icon::panelMenu: // three bars of decreasing length: "this window's options"
            line (st, 5.0f, 7.0f, 19.0f, 7.0f);
            line (st, 5.0f, 12.0f, 16.0f, 12.0f);
            line (st, 5.0f, 17.0f, 13.0f, 17.0f);
            break;

        // --- modes ---
        case Icon::pattern: // a 2x2 block of steps, one lit
            roundRect (st, 4.0f, 4.0f, 6.5f, 6.5f, 1.5f);
            roundRect (st, 4.0f, 13.5f, 6.5f, 6.5f, 1.5f);
            roundRect (st, 13.5f, 13.5f, 6.5f, 6.5f, 1.5f);
            fi.addRoundedRectangle (12.75f, 3.25f, 8.0f, 8.0f, 2.0f);
            break;
        case Icon::song: // clips staggered along three lanes
            fi.addRoundedRectangle (3.5f, 4.5f, 9.0f, 4.0f, 1.25f);
            fi.addRoundedRectangle (9.0f, 10.0f, 11.5f, 4.0f, 1.25f);
            fi.addRoundedRectangle (5.5f, 15.5f, 8.0f, 4.0f, 1.25f);
            break;
        case Icon::metronome:
        {
            juce::Path body;
            polyline (body, { { 9.0f, 3.5f }, { 15.0f, 3.5f }, { 19.0f, 20.5f }, { 5.0f, 20.5f } }, true);
            st.addPath (body.createPathWithRoundedCorners (2.0f));
            line (st, 12.0f, 16.5f, 17.5f, 6.0f);
            line (st, 7.0f, 16.5f, 17.0f, 16.5f);
            fi.addEllipse (14.0f, 8.0f, 3.5f, 3.5f);
            break;
        }

        // --- file ---
        case Icon::newFile:
            page();
            line (st, 12.0f, 11.0f, 12.0f, 17.0f);
            line (st, 9.0f, 14.0f, 15.0f, 14.0f);
            break;
        case Icon::file:
            page();
            break;
        case Icon::folder:
        {
            juce::Path f;
            polyline (f, { { 3.5f, 5.5f }, { 9.0f, 5.5f }, { 11.0f, 7.5f }, { 20.5f, 7.5f }, { 20.5f, 18.5f }, { 3.5f, 18.5f } }, true);
            st.addPath (f.createPathWithRoundedCorners (1.75f));
            break;
        }
        case Icon::open: // a folder with its front flap tipped open
        {
            juce::Path back;
            polyline (back, { { 4.0f, 18.5f }, { 3.5f, 5.5f }, { 9.0f, 5.5f }, { 11.0f, 7.5f }, { 18.0f, 7.5f }, { 18.0f, 10.5f } });
            st.addPath (back.createPathWithRoundedCorners (1.5f));
            juce::Path flap;
            polyline (flap, { { 4.0f, 18.5f }, { 7.0f, 10.5f }, { 21.0f, 10.5f }, { 18.0f, 18.5f } }, true);
            st.addPath (flap.createPathWithRoundedCorners (1.5f));
            break;
        }
        case Icon::save: // a disk
        {
            juce::Path body;
            polyline (body, { { 4.0f, 4.0f }, { 16.5f, 4.0f }, { 20.0f, 7.5f }, { 20.0f, 20.0f }, { 4.0f, 20.0f } }, true);
            st.addPath (body.createPathWithRoundedCorners (2.0f));
            polyline (st, { { 8.0f, 4.0f }, { 8.0f, 8.5f }, { 15.0f, 8.5f }, { 15.0f, 4.0f } });
            roundRect (st, 7.5f, 13.0f, 9.0f, 7.0f, 1.0f);
            break;
        }
        case Icon::exportAudio: // a waveform leaving a tray
            polyline (st, { { 3.5f, 14.0f }, { 3.5f, 19.5f }, { 20.5f, 19.5f }, { 20.5f, 14.0f } });
            line (st, 12.0f, 15.5f, 12.0f, 4.0f);
            polyline (st, { { 8.0f, 8.0f }, { 12.0f, 4.0f }, { 16.0f, 8.0f } });
            break;
        case Icon::settings: // three sliders
            line (st, 4.0f, 7.0f, 20.0f, 7.0f);
            line (st, 4.0f, 12.0f, 20.0f, 12.0f);
            line (st, 4.0f, 17.0f, 20.0f, 17.0f);
            fi.addEllipse (6.5f, 4.5f, 5.0f, 5.0f);
            fi.addEllipse (13.0f, 9.5f, 5.0f, 5.0f);
            fi.addEllipse (8.5f, 14.5f, 5.0f, 5.0f);
            break;
        case Icon::help:
            st.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
            st.startNewSubPath (9.5f, 10.0f);
            st.addCentredArc (12.0f, 10.0f, 2.5f, 2.5f, 0.0f, -pi * 0.5f, pi * 0.7f, false);
            st.lineTo (12.0f, 13.5f);
            fi.addEllipse (10.9f, 15.6f, 2.2f, 2.2f);
            break;
        case Icon::undo:
        case Icon::redo:
        {
            juce::Path p;
            p.startNewSubPath (19.0f, 18.5f);
            p.cubicTo (19.0f, 12.0f, 15.0f, 9.0f, 5.5f, 9.0f);
            polyline (p, { { 9.5f, 5.0f }, { 5.5f, 9.0f }, { 9.5f, 13.0f } });
            if (icon == Icon::redo)
                p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 12.0f, 12.0f));
            st.addPath (p);
            break;
        }

        // --- views (the window buttons) ---
        case Icon::playlist: // clips on lanes inside a window
            window();
            fi.addRoundedRectangle (6.0f, 7.5f, 6.5f, 2.5f, 0.75f);
            fi.addRoundedRectangle (10.5f, 11.0f, 7.5f, 2.5f, 0.75f);
            fi.addRoundedRectangle (6.0f, 14.5f, 4.5f, 2.5f, 0.75f);
            fi.addRoundedRectangle (12.5f, 14.5f, 5.5f, 2.5f, 0.75f);
            break;
        case Icon::channelRack: // three channels: a name pad and a row of steps each
            for (int r = 0; r < 3; ++r)
            {
                const auto y = 4.5f + (float) r * 5.75f;
                roundRect (st, 3.5f, y, 6.5f, 3.5f, 1.0f);
                for (int c = 0; c < 3; ++c)
                {
                    const auto x = 12.5f + (float) c * 3.0f;
                    if ((r + c) % 2 == 0)
                        fi.addRoundedRectangle (x - 0.25f, y - 0.25f, 2.5f, 4.0f, 0.6f);
                    else
                        fi.addRoundedRectangle (x + 0.5f, y + 1.25f, 1.0f, 1.0f, 0.3f);
                }
            }
            break;
        case Icon::pianoRoll: // a keyboard edge and notes
            window();
            line (st, 8.0f, 4.5f, 8.0f, 19.5f);
            line (st, 3.5f, 9.5f, 6.0f, 9.5f);
            line (st, 3.5f, 14.5f, 6.0f, 14.5f);
            fi.addRoundedRectangle (10.0f, 7.0f, 4.5f, 2.5f, 0.75f);
            fi.addRoundedRectangle (13.0f, 10.75f, 5.5f, 2.5f, 0.75f);
            fi.addRoundedRectangle (10.5f, 14.5f, 4.0f, 2.5f, 0.75f);
            break;
        case Icon::mixer: // three faders
            for (int i = 0; i < 3; ++i)
                line (st, 6.0f + (float) i * 6.0f, 4.0f, 6.0f + (float) i * 6.0f, 20.0f);
            fi.addRoundedRectangle (3.5f, 12.0f, 5.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (9.5f, 6.5f, 5.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (15.5f, 14.5f, 5.0f, 3.5f, 1.0f);
            break;
        case Icon::browser: // a tree: a folder and two items under it
            roundRect (st, 3.5f, 4.0f, 6.0f, 5.0f, 1.25f);
            polyline (st, { { 6.5f, 9.0f }, { 6.5f, 17.5f }, { 10.0f, 17.5f } });
            line (st, 6.5f, 12.5f, 10.0f, 12.5f);
            line (st, 12.5f, 6.5f, 20.5f, 6.5f);
            line (st, 13.0f, 12.5f, 20.5f, 12.5f);
            line (st, 13.0f, 17.5f, 18.0f, 17.5f);
            break;
        case Icon::pluginPicker: // a plug
        {
            line (st, 9.0f, 3.5f, 9.0f, 7.5f);
            line (st, 15.0f, 3.5f, 15.0f, 7.5f);
            juce::Path body;
            polyline (body, { { 5.5f, 7.5f }, { 18.5f, 7.5f }, { 18.5f, 11.0f }, { 14.5f, 16.0f }, { 9.5f, 16.0f }, { 5.5f, 11.0f } }, true);
            st.addPath (body.createPathWithRoundedCorners (1.75f));
            line (st, 12.0f, 16.0f, 12.0f, 20.5f);
            break;
        }

        // --- content ---
        case Icon::envelope: // an ADSR line on a base line (attack up, decay, sustain, release)
            polyline (st, { { 3.5f, 18.5f }, { 8.0f, 5.5f }, { 11.5f, 11.0f }, { 15.5f, 11.0f }, { 20.5f, 18.5f } });
            line (st, 3.5f, 20.0f, 20.5f, 20.0f);
            break;
        case Icon::wrench: // an open-end wrench, head at the upper right
        {
            juce::Path w;
            w.startNewSubPath (5.0f, 19.0f);
            w.lineTo (12.5f, 11.5f);
            st.addPath (w);
            // the head: a ring open towards the upper right (the jaws)
            st.addCentredArc (15.5f, 8.5f, 4.5f, 4.5f, 0.0f, pi * 0.55f, pi * 1.95f, true);
            break;
        }
        case Icon::audioFile: // a short waveform
        {
            const float heights[] = { 5.0f, 11.0f, 16.0f, 8.0f, 12.0f, 4.0f };
            for (int i = 0; i < 6; ++i)
            {
                const auto x = 4.5f + (float) i * 3.0f;
                line (st, x, 12.0f - heights[i] / 2.0f, x, 12.0f + heights[i] / 2.0f);
            }
            break;
        }
        case Icon::plugin: // a module with a jack
            roundRect (st, 4.0f, 6.5f, 16.0f, 12.0f, 2.0f);
            line (st, 8.5f, 3.5f, 8.5f, 6.5f);
            line (st, 15.5f, 3.5f, 15.5f, 6.5f);
            st.addEllipse (9.5f, 10.0f, 5.0f, 5.0f);
            break;
        case Icon::preset: // a knob
            st.addCentredArc (12.0f, 12.0f, 8.0f, 8.0f, 0.0f, -pi * 0.75f, pi * 0.75f, true);
            line (st, 12.0f, 12.0f, 15.5f, 8.5f);
            break;
        case Icon::project: // a diamond with a core
        {
            juce::Path d;
            polyline (d, { { 12.0f, 3.0f }, { 20.0f, 12.0f }, { 12.0f, 21.0f }, { 4.0f, 12.0f } }, true);
            st.addPath (d.createPathWithRoundedCorners (2.0f));
            fi.addEllipse (9.75f, 9.75f, 4.5f, 4.5f);
            break;
        }

        // --- edit tools ---
        case Icon::draw: // pencil
        {
            juce::Path p;
            polyline (p, { { 15.5f, 4.0f }, { 20.0f, 8.5f }, { 9.0f, 19.5f }, { 4.0f, 20.0f }, { 4.5f, 15.0f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.25f));
            line (st, 13.0f, 6.5f, 17.5f, 11.0f);
            break;
        }
        case Icon::brush: // a round brush: handle, ferrule and a filled tip
        {
            line (st, 20.0f, 4.0f, 13.5f, 10.5f);
            juce::Path ferrule;
            polyline (ferrule, { { 11.5f, 9.5f }, { 14.5f, 12.5f }, { 12.5f, 14.5f }, { 9.5f, 11.5f } }, true);
            st.addPath (ferrule.createPathWithRoundedCorners (0.75f));
            juce::Path tip;
            tip.startNewSubPath (9.0f, 12.5f);
            tip.quadraticTo (4.0f, 13.0f, 3.5f, 20.5f);
            tip.quadraticTo (11.0f, 20.0f, 11.5f, 15.0f);
            tip.closeSubPath();
            fi.addPath (tip);
            break;
        }
        case Icon::erase: // an eraser on a line
        {
            juce::Path p;
            polyline (p, { { 13.0f, 4.0f }, { 20.0f, 11.0f }, { 12.0f, 19.0f }, { 7.5f, 19.0f }, { 4.0f, 15.5f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.25f));
            line (st, 8.5f, 8.5f, 15.5f, 15.5f);
            line (st, 12.0f, 19.0f, 20.5f, 19.0f);
            break;
        }
        case Icon::mute: // a speaker with a cross
            speaker();
            line (st, 15.0f, 9.0f, 20.5f, 15.0f);
            line (st, 20.5f, 9.0f, 15.0f, 15.0f);
            break;
        case Icon::listen: // a speaker with two waves (playback tool)
            speaker();
            st.addCentredArc (12.0f, 12.0f, 3.5f, 3.5f, 0.0f, pi * 0.25f, pi * 0.75f, true);
            st.addCentredArc (12.0f, 12.0f, 7.5f, 7.5f, 0.0f, pi * 0.22f, pi * 0.78f, true);
            break;
        case Icon::knife: // a blade with a handle, cutting down to the left
        {
            juce::Path blade;
            polyline (blade, { { 4.0f, 20.0f }, { 13.5f, 8.0f }, { 17.0f, 11.5f } }, true);
            st.addPath (blade.createPathWithRoundedCorners (1.25f));
            line (st, 15.5f, 9.5f, 20.0f, 5.0f);
            break;
        }
        case Icon::select: // the four corners of a selection box
        {
            const float a = 4.0f, b = 20.0f, l = 4.5f;
            polyline (st, { { a, a + l }, { a, a }, { a + l, a } });
            polyline (st, { { b - l, a }, { b, a }, { b, a + l } });
            polyline (st, { { b, b - l }, { b, b }, { b - l, b } });
            polyline (st, { { a + l, b }, { a, b }, { a, b - l } });
            fi.addRoundedRectangle (9.5f, 9.5f, 5.0f, 5.0f, 1.0f);
            break;
        }
        case Icon::slice:
        case Icon::scissors:
            st.addEllipse (3.5f, 14.0f, 6.0f, 6.0f);
            st.addEllipse (14.5f, 14.0f, 6.0f, 6.0f);
            line (st, 8.5f, 15.0f, 17.5f, 3.5f);
            line (st, 15.5f, 15.0f, 6.5f, 3.5f);
            break;
        case Icon::zoom: // a magnifier with a plus
            st.addEllipse (3.5f, 3.5f, 13.0f, 13.0f);
            line (st, 14.5f, 14.5f, 20.5f, 20.5f);
            line (st, 10.0f, 7.0f, 10.0f, 13.0f);
            line (st, 7.0f, 10.0f, 13.0f, 10.0f);
            break;
        case Icon::deleteTool: // FL's delete tool: a ring crossed by a slash (round 190ig)
            st.addEllipse (4.5f, 4.5f, 15.0f, 15.0f);
            line (st, 6.9f, 17.1f, 17.1f, 6.9f);
            break;
        case Icon::slipArrows: // FL's slip tool: two solid arrowheads pointing apart (round 190ig)
            fi.addTriangle (3.0f, 12.0f, 10.5f, 6.5f, 10.5f, 17.5f);
            fi.addTriangle (21.0f, 12.0f, 13.5f, 6.5f, 13.5f, 17.5f);
            break;
        case Icon::slip: // a fixed frame with its content sliding inside
            roundRect (st, 3.5f, 5.5f, 17.0f, 13.0f, 2.0f);
            polyline (st, { { 9.0f, 9.5f }, { 6.5f, 12.0f }, { 9.0f, 14.5f } });
            polyline (st, { { 15.0f, 9.5f }, { 17.5f, 12.0f }, { 15.0f, 14.5f } });
            line (st, 7.0f, 12.0f, 17.0f, 12.0f);
            break;
        case Icon::ghost: // two overlapping notes, the back one hollow
            roundRect (st, 3.5f, 5.5f, 12.0f, 5.5f, 1.5f);
            fi.addRoundedRectangle (8.5f, 13.0f, 12.0f, 5.5f, 1.5f);
            break;
        case Icon::link:
            roundRect (st, 3.0f, 8.5f, 11.0f, 7.0f, 3.5f);
            roundRect (st, 10.0f, 8.5f, 11.0f, 7.0f, 3.5f);
            break;
        case Icon::magnet: // snap: a horseshoe magnet, its two poles solid (round 170e: the open "U" read as a letter)
            magnet (1.0f, 0.0f, 0.0f);
            break;

        // --- workspace and control contract ---
        case Icon::detach: // a window with an arrow leaving its corner
            polyline (st, { { 10.5f, 4.5f }, { 6.0f, 4.5f }, { 4.5f, 6.0f }, { 4.5f, 18.0f }, { 6.0f, 19.5f }, { 18.0f, 19.5f }, { 19.5f, 18.0f }, { 19.5f, 13.5f } });
            line (st, 12.0f, 12.0f, 19.5f, 4.5f);
            polyline (st, { { 14.0f, 4.5f }, { 19.5f, 4.5f }, { 19.5f, 10.0f } });
            break;
        case Icon::dock: // an arrow going into a window's corner
            polyline (st, { { 10.5f, 4.5f }, { 6.0f, 4.5f }, { 4.5f, 6.0f }, { 4.5f, 18.0f }, { 6.0f, 19.5f }, { 18.0f, 19.5f }, { 19.5f, 18.0f }, { 19.5f, 13.5f } });
            line (st, 19.5f, 4.5f, 12.0f, 12.0f);
            polyline (st, { { 12.0f, 6.5f }, { 12.0f, 12.0f }, { 17.5f, 12.0f } });
            break;
        case Icon::keyboard: // a small piano: outline, white-key gaps, two black keys
            roundRect (st, 3.0f, 6.0f, 18.0f, 12.0f, 2.0f);
            line (st, 9.0f, 13.0f, 9.0f, 18.0f);
            line (st, 15.0f, 13.0f, 15.0f, 18.0f);
            fi.addRoundedRectangle (7.25f, 6.0f, 3.5f, 7.0f, 0.75f);
            fi.addRoundedRectangle (13.25f, 6.0f, 3.5f, 7.0f, 0.75f);
            break;
        case Icon::automate: // a curve through two points
        {
            juce::Path c;
            c.startNewSubPath (4.0f, 17.0f);
            c.cubicTo (9.0f, 17.0f, 10.0f, 7.0f, 15.0f, 7.0f);
            c.lineTo (20.5f, 7.0f);
            st.addPath (c);
            fi.addEllipse (2.0f, 15.0f, 4.0f, 4.0f);
            fi.addEllipse (13.0f, 5.0f, 4.0f, 4.0f);
            break;
        }
        case Icon::rightClick: // a mouse with the right button lit
        {
            roundRect (st, 6.5f, 3.5f, 11.0f, 17.0f, 5.5f);
            line (st, 6.5f, 10.5f, 17.5f, 10.5f);
            line (st, 12.0f, 3.5f, 12.0f, 10.5f);
            juce::Path b;
            b.startNewSubPath (12.0f, 3.5f);
            b.lineTo (12.0f, 10.5f);
            b.lineTo (17.5f, 10.5f);
            b.lineTo (17.5f, 9.0f);
            b.quadraticTo (17.5f, 3.5f, 12.0f, 3.5f);
            b.closeSubPath();
            fi.addPath (b);
            break;
        }
        case Icon::tap: // a dot with two rings
            fi.addEllipse (9.5f, 9.5f, 5.0f, 5.0f);
            st.addCentredArc (12.0f, 12.0f, 5.5f, 5.5f, 0.0f, -pi * 0.35f, pi * 0.35f, true);
            st.addCentredArc (12.0f, 12.0f, 8.5f, 8.5f, 0.0f, -pi * 0.3f, pi * 0.3f, true);
            break;
        case Icon::mic: // recording options: a microphone
            roundRect (st, 9.0f, 3.0f, 6.0f, 11.0f, 3.0f);
            st.startNewSubPath (18.5f, 11.0f);
            st.addCentredArc (12.0f, 11.0f, 6.5f, 6.0f, 0.0f, pi * 0.5f, pi * 1.5f, false);
            line (st, 12.0f, 17.0f, 12.0f, 20.5f);
            line (st, 8.5f, 20.5f, 15.5f, 20.5f);
            break;
        case Icon::countIn: // a stopwatch: dial, crown and a hand, with the first quarter filled
        {
            st.addEllipse (4.5f, 6.0f, 15.0f, 15.0f);
            line (st, 10.0f, 3.0f, 14.0f, 3.0f);
            line (st, 12.0f, 3.0f, 12.0f, 6.0f);
            line (st, 12.0f, 13.5f, 12.0f, 9.5f);
            juce::Path quarter;
            quarter.startNewSubPath (12.0f, 13.5f);
            quarter.addCentredArc (12.0f, 13.5f, 5.0f, 5.0f, 0.0f, 0.0f, pi * 0.5f, false);
            quarter.closeSubPath();
            fi.addPath (quarter);
            break;
        }
        case Icon::waitInput: // an hourglass: two triangles between two bars, sand in the bottom one
            line (st, 6.0f, 3.5f, 18.0f, 3.5f);
            line (st, 6.0f, 20.5f, 18.0f, 20.5f);
            polyline (st, { { 7.5f, 3.5f }, { 7.5f, 6.5f }, { 12.0f, 12.0f }, { 7.5f, 17.5f }, { 7.5f, 20.5f } });
            polyline (st, { { 16.5f, 3.5f }, { 16.5f, 6.5f }, { 12.0f, 12.0f }, { 16.5f, 17.5f }, { 16.5f, 20.5f } });
            polyline (fi, { { 12.0f, 16.0f }, { 15.0f, 19.5f }, { 9.0f, 19.5f } }, true);
            break;
        case Icon::mouseLeft: // a mouse with the left button lit (the mirror of rightClick)
        {
            roundRect (st, 6.5f, 3.5f, 11.0f, 17.0f, 5.5f);
            line (st, 6.5f, 10.5f, 17.5f, 10.5f);
            line (st, 12.0f, 3.5f, 12.0f, 10.5f);
            juce::Path b;
            b.startNewSubPath (12.0f, 3.5f);
            b.lineTo (12.0f, 10.5f);
            b.lineTo (6.5f, 10.5f);
            b.lineTo (6.5f, 9.0f);
            b.quadraticTo (6.5f, 3.5f, 12.0f, 3.5f);
            b.closeSubPath();
            fi.addPath (b);
            break;
        }
        case Icon::mouseMiddle: // a mouse with its wheel button lit
            roundRect (st, 6.5f, 3.5f, 11.0f, 17.0f, 5.5f);
            line (st, 6.5f, 11.0f, 17.5f, 11.0f);
            fi.addRoundedRectangle (10.25f, 4.5f, 3.5f, 6.0f, 1.75f);
            break;
        case Icon::mouseWheel: // a narrower mouse with its wheel, and up/down chevrons beside it
            roundRect (st, 3.5f, 4.5f, 10.0f, 15.0f, 5.0f);
            line (st, 3.5f, 11.0f, 13.5f, 11.0f);
            fi.addRoundedRectangle (7.25f, 5.5f, 3.5f, 5.0f, 1.75f);
            polyline (st, { { 16.5f, 8.0f }, { 18.5f, 5.5f }, { 20.5f, 8.0f } });
            polyline (st, { { 16.5f, 16.0f }, { 18.5f, 18.5f }, { 20.5f, 16.0f } });
            break;
        case Icon::drumBrush: // a brush laying a row of short hits on the grid
        {
            line (st, 20.5f, 3.5f, 15.0f, 9.0f);
            juce::Path tip;
            tip.startNewSubPath (14.0f, 8.0f);
            tip.lineTo (16.0f, 10.0f);
            tip.quadraticTo (14.5f, 14.0f, 10.0f, 13.5f);
            tip.quadraticTo (10.5f, 9.5f, 14.0f, 8.0f);
            tip.closeSubPath();
            fi.addPath (tip);
            fi.addRoundedRectangle (3.0f, 17.0f, 4.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (9.0f, 17.0f, 4.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (15.0f, 17.0f, 4.0f, 3.5f, 1.0f);
            break;
        }
        case Icon::graph: // bars of different heights standing on a base line (a step graph)
            line (st, 3.5f, 20.5f, 20.5f, 20.5f);
            fi.addRoundedRectangle (4.0f, 11.0f, 3.0f, 8.0f, 1.0f);
            fi.addRoundedRectangle (8.5f, 6.0f, 3.0f, 13.0f, 1.0f);
            fi.addRoundedRectangle (13.0f, 13.5f, 3.0f, 5.5f, 1.0f);
            fi.addRoundedRectangle (17.5f, 9.0f, 3.0f, 10.0f, 1.0f);
            break;
        case Icon::follow: // a playhead line with an arrow carrying it to the right
        {
            line (st, 7.5f, 4.5f, 7.5f, 19.5f);
            juce::Path head;
            head.addTriangle (4.5f, 3.5f, 10.5f, 3.5f, 7.5f, 7.0f);
            fi.addPath (head);
            line (st, 11.5f, 12.0f, 19.5f, 12.0f);
            line (st, 16.5f, 9.0f, 19.5f, 12.0f);
            line (st, 16.5f, 15.0f, 19.5f, 12.0f);
            break;
        }
        case Icon::cycle: // BandLab's repeat glyph: the top run to the right, the bottom run to the left, round corners
        {
            juce::Path a;
            a.startNewSubPath (5.5f, 13.0f);
            a.lineTo (5.5f, 10.0f);
            a.quadraticTo (5.5f, 7.5f, 8.0f, 7.5f);
            a.lineTo (16.0f, 7.5f);
            st.addPath (a);
            juce::Path b;
            b.startNewSubPath (18.5f, 11.0f);
            b.lineTo (18.5f, 14.0f);
            b.quadraticTo (18.5f, 16.5f, 16.0f, 16.5f);
            b.lineTo (8.0f, 16.5f);
            st.addPath (b);
            juce::Path h;
            h.addTriangle (19.0f, 7.5f, 15.0f, 4.5f, 15.0f, 10.5f);
            h.addTriangle (5.0f, 16.5f, 9.0f, 13.5f, 9.0f, 19.5f);
            fi.addPath (h);
            break;
        }
        // round 190lx (DL-61): Live's switch glyphs as measured in capture190ji (13 x 6 px in a 21 x 17 px box: runs of
        // 5 / 5 px and a 3 px slope; the loop box 13 x 9 px open at the bottom for its arrow)
        case Icon::punchIn:
            polyline (st, { { 1.5f, 7.5f }, { 9.5f, 7.5f }, { 14.5f, 16.5f }, { 22.5f, 16.5f } });
            break;
        case Icon::automationArm:
            st.addEllipse (5.0f, 13.5f, 5.5f, 5.5f);
            st.addEllipse (13.5f, 5.0f, 5.5f, 5.5f);
            line (st, 9.5f, 14.5f, 14.5f, 9.5f);
            break;
        case Icon::reEnableAutomation:
            fi.addTriangle (4.0f, 12.0f, 11.0f, 7.0f, 11.0f, 17.0f);
            fi.addRectangle (10.0f, 10.8f, 9.0f, 2.4f);
            break;
        case Icon::search: // round 190on: a lens and its handle
            st.addEllipse (4.0f, 4.0f, 12.0f, 12.0f);
            line (st, 14.3f, 14.3f, 20.0f, 20.0f);
            break;
        case Icon::punchOut:
            polyline (st, { { 1.5f, 16.5f }, { 9.5f, 16.5f }, { 14.5f, 7.5f }, { 22.5f, 7.5f } });
            break;
        case Icon::liveLoop:
            polyline (st, { { 8.0f, 18.5f }, { 1.5f, 18.5f }, { 1.5f, 4.5f }, { 22.5f, 4.5f }, { 22.5f, 18.5f }, { 17.5f, 18.5f } });
            fi.addTriangle (10.5f, 18.5f, 15.5f, 14.5f, 15.5f, 22.5f);
            break;
        case Icon::loop: // two arcs chasing each other around (a loop), arrow heads at their ends
        {
            constexpr float r = 7.0f;
            auto arcWithHead = [&] (float from, float to) {
                juce::Path a;
                a.addCentredArc (12.0f, 12.0f, r, r, 0.0f, from, to, true);
                st.addPath (a);
                // the head at the arc's end, pointing along it (clockwise); angles from 12 o'clock as JUCE's arcs
                const juce::Point<float> end (12.0f + r * std::sin (to), 12.0f - r * std::cos (to));
                const juce::Point<float> along (std::cos (to), std::sin (to)), out (std::sin (to), -std::cos (to));
                juce::Path h;
                h.addTriangle (end + along * 3.6f, end + out * 3.2f - along * 0.6f, end - out * 3.2f - along * 0.6f);
                fi.addPath (h);
            };
            arcWithHead (0.45f, 2.55f);
            arcWithHead (3.6f, 5.7f);
            break;
        }
        case Icon::stretch: // a waveform between two outward arrows (stretching audio)
        {
            juce::Path w;
            w.startNewSubPath (8.0f, 12.0f);
            w.lineTo (9.5f, 8.0f);
            w.lineTo (11.0f, 16.0f);
            w.lineTo (12.5f, 6.5f);
            w.lineTo (14.0f, 17.5f);
            w.lineTo (16.0f, 12.0f);
            st.addPath (w);
            line (st, 2.5f, 12.0f, 6.0f, 12.0f);
            line (st, 2.5f, 12.0f, 4.5f, 10.0f);
            line (st, 2.5f, 12.0f, 4.5f, 14.0f);
            line (st, 18.0f, 12.0f, 21.5f, 12.0f);
            line (st, 21.5f, 12.0f, 19.5f, 10.0f);
            line (st, 21.5f, 12.0f, 19.5f, 14.0f);
            break;
        }
        case Icon::dice: // a die showing five
            st.addRoundedRectangle (4.5f, 4.5f, 15.0f, 15.0f, 3.0f);
            for (const auto& [x, y] : { std::pair { 8.5f, 8.5f }, { 15.5f, 8.5f }, { 12.0f, 12.0f }, { 8.5f, 15.5f }, { 15.5f, 15.5f } })
                fi.addEllipse (x - 1.4f, y - 1.4f, 2.8f, 2.8f);
            break;
        case Icon::tuningFork: // two prongs on a stem, with a weighted foot
        {
            juce::Path f;
            f.startNewSubPath (8.5f, 3.5f);
            f.lineTo (8.5f, 10.0f);
            f.quadraticTo (8.5f, 14.0f, 12.0f, 14.0f);
            f.quadraticTo (15.5f, 14.0f, 15.5f, 10.0f);
            f.lineTo (15.5f, 3.5f);
            st.addPath (f);
            line (st, 12.0f, 14.0f, 12.0f, 18.0f);
            fi.addEllipse (10.0f, 17.0f, 4.0f, 4.0f);
            break;
        }
        case Icon::scaleSnap: // a small magnet over a few piano keys (snap to the scale)
            magnet (0.62f, 1.0f, 1.0f);
            roundRect (st, 11.5f, 12.5f, 9.0f, 8.0f, 1.5f);
            line (st, 14.5f, 16.5f, 14.5f, 20.5f);
            line (st, 17.5f, 16.5f, 17.5f, 20.5f);
            fi.addRoundedRectangle (13.6f, 12.5f, 1.8f, 4.0f, 0.5f);
            fi.addRoundedRectangle (16.6f, 12.5f, 1.8f, 4.0f, 0.5f);
            break;
        case Icon::tweak: // a knob with its pointer, and an arrow turning around it (the last moved control)
        {
            st.addEllipse (6.0f, 8.5f, 11.0f, 11.0f);
            line (st, 11.5f, 14.0f, 14.5f, 11.0f);
            constexpr float cx = 11.5f, cy = 14.0f, r = 9.0f, from = pi * 1.72f, to = pi * 2.28f;
            st.addCentredArc (cx, cy, r, r, 0.0f, from, to, true);
            const juce::Point<float> tip (cx + r * std::sin (to), cy - r * std::cos (to));
            const juce::Point<float> along (std::cos (to), std::sin (to)); // the arc's direction at its end
            const juce::Point<float> side (-along.y, along.x);
            polyline (st, { tip - along * 2.6f + side * 2.2f, tip, tip - along * 2.6f - side * 2.2f });
            break;
        }
        case Icon::slide: // a note with an arrow gliding up from its end (FL's slide notes)
        {
            fi.addRoundedRectangle (3.5f, 14.5f, 9.0f, 5.0f, 1.5f);
            line (st, 12.5f, 15.0f, 20.0f, 6.0f);
            polyline (st, { { 15.5f, 6.0f }, { 20.0f, 6.0f }, { 20.0f, 10.5f } });
            break;
        }
        case Icon::note: // an eighth note: a solid head, its stem and a flag
            fi.addEllipse (4.5f, 14.5f, 7.5f, 5.5f);
            line (st, 11.5f, 17.0f, 11.5f, 4.0f);
            polyline (st, { { 11.5f, 4.0f }, { 17.0f, 7.5f }, { 17.5f, 11.5f } });
            break;
        case Icon::chord: // three notes stacked on one start (a chord)
            fi.addRoundedRectangle (4.0f, 4.5f, 12.0f, 4.0f, 1.25f);
            fi.addRoundedRectangle (4.0f, 10.0f, 16.0f, 4.0f, 1.25f);
            fi.addRoundedRectangle (4.0f, 15.5f, 9.0f, 4.0f, 1.25f);
            break;
        case Icon::span: // a double arrow between two bar lines (a length)
            line (st, 3.5f, 6.0f, 3.5f, 18.0f);
            line (st, 20.5f, 6.0f, 20.5f, 18.0f);
            line (st, 6.5f, 12.0f, 17.5f, 12.0f);
            polyline (st, { { 9.0f, 9.5f }, { 6.5f, 12.0f }, { 9.0f, 14.5f } });
            polyline (st, { { 15.0f, 9.5f }, { 17.5f, 12.0f }, { 15.0f, 14.5f } });
            break;
        case Icon::blend: // two takes (notes) merging into one line (overdub)
        {
            fi.addRoundedRectangle (3.0f, 4.5f, 5.5f, 3.5f, 1.0f);
            fi.addRoundedRectangle (3.0f, 16.0f, 5.5f, 3.5f, 1.0f);
            juce::Path m;
            m.startNewSubPath (8.5f, 6.25f);
            m.quadraticTo (12.5f, 6.25f, 13.5f, 12.0f);
            m.startNewSubPath (8.5f, 17.75f);
            m.quadraticTo (12.5f, 17.75f, 13.5f, 12.0f);
            st.addPath (m);
            line (st, 13.5f, 12.0f, 20.5f, 12.0f);
            polyline (st, { { 17.5f, 9.0f }, { 20.5f, 12.0f }, { 17.5f, 15.0f } });
            break;
        }
        case Icon::filter: // a funnel
        {
            juce::Path f;
            polyline (f, { { 4.0f, 5.0f }, { 20.0f, 5.0f }, { 14.0f, 12.5f }, { 14.0f, 19.0f }, { 10.0f, 17.0f }, { 10.0f, 12.5f } }, true);
            st.addPath (f.createPathWithRoundedCorners (1.25f));
            break;
        }
        case Icon::tapTempo: // a fingertip tapping a pad, with the tap's two impact marks
            fi.addRoundedRectangle (4.0f, 16.0f, 16.0f, 4.0f, 1.5f);
            st.addRoundedRectangle (9.5f, 2.5f, 5.0f, 11.0f, 2.5f);
            line (st, 10.5f, 9.5f, 13.5f, 9.5f);
            line (st, 4.5f, 10.5f, 7.0f, 13.0f);
            line (st, 19.5f, 10.5f, 17.0f, 13.0f);
            break;
        case Icon::loopRecord: // a loop arrow around a record dot
        {
            constexpr float r = 8.0f;
            juce::Path a;
            a.addCentredArc (12.0f, 12.0f, r, r, 0.0f, pi * 0.25f, pi * 1.85f, true);
            st.addPath (a);
            const auto to = pi * 1.85f;
            const juce::Point<float> end (12.0f + r * std::sin (to), 12.0f - r * std::cos (to));
            const juce::Point<float> along (std::cos (to), std::sin (to)), out (std::sin (to), -std::cos (to));
            juce::Path h;
            h.addTriangle (end + along * 3.8f, end + out * 3.3f - along * 0.6f, end - out * 3.3f - along * 0.6f);
            fi.addPath (h);
            fi.addEllipse (8.5f, 8.5f, 7.0f, 7.0f);
            break;
        }
        case Icon::stepEdit: // three steps typed one after another (the last one hollow: the next), an arrow on to it
            fi.addRoundedRectangle (2.5f, 14.0f, 5.0f, 5.0f, 1.0f);
            fi.addRoundedRectangle (9.5f, 14.0f, 5.0f, 5.0f, 1.0f);
            st.addRoundedRectangle (16.5f, 14.0f, 5.0f, 5.0f, 1.0f);
            line (st, 5.0f, 9.0f, 17.5f, 9.0f);
            polyline (st, { { 15.0f, 6.0f }, { 18.5f, 9.0f }, { 15.0f, 12.0f } });
            break;
        case Icon::punch: // a record dot between two brackets (in and out)
            polyline (st, { { 7.0f, 4.5f }, { 4.0f, 4.5f }, { 4.0f, 19.5f }, { 7.0f, 19.5f } });
            polyline (st, { { 17.0f, 4.5f }, { 20.0f, 4.5f }, { 20.0f, 19.5f }, { 17.0f, 19.5f } });
            fi.addEllipse (8.0f, 8.0f, 8.0f, 8.0f);
            break;

        // --- round 190iv: the Simple top bar's save status, the Clip-launch control bar ---
        case Icon::cloud: // a cloud: two bumps on a flat base
        case Icon::cloudCheck: // the cloud with a check mark in it
        {
            juce::Path c;
            c.startNewSubPath (7.0f, 18.5f);
            c.lineTo (17.5f, 18.5f);
            c.addCentredArc (17.5f, 14.75f, 3.75f, 3.75f, 0.0f, pi, pi * 0.05f, false);
            c.addCentredArc (12.5f, 10.5f, 5.5f, 5.5f, 0.0f, pi * 0.42f, -pi * 0.55f, false);
            c.addCentredArc (7.0f, 14.5f, 4.0f, 4.0f, 0.0f, pi * 0.05f, -pi, false);
            c.closeSubPath();
            st.addPath (c);
            if (icon == Icon::cloudCheck)
                polyline (st, { { 9.5f, 14.0f }, { 11.5f, 16.0f }, { 15.0f, 12.0f } });
            break;
        }
        case Icon::sessionView: // three columns
            for (float x : { 6.0f, 11.0f, 16.0f })
                fi.addRectangle (x, 5.0f, 2.5f, 14.0f);
            break;
        case Icon::arrangementView: // three rows
            for (float y : { 6.0f, 11.0f, 16.0f })
                fi.addRectangle (5.0f, y, 14.0f, 2.5f);
            break;
        case Icon::backToArrangement: // an arrow into a dashed line (the clips give the Playlist back)
        {
            juce::Path head;
            head.addTriangle (8.0f, 8.0f, 8.0f, 16.0f, 12.5f, 12.0f);
            fi.addPath (head);
            fi.addRectangle (3.0f, 11.0f, 5.5f, 2.0f);
            for (float x : { 14.0f, 18.0f })
                fi.addRectangle (x, 11.0f, 2.5f, 2.0f);
            break;
        }
        case Icon::capture: // a dashed square
            polyline (st, { { 5.0f, 9.0f }, { 5.0f, 5.0f }, { 9.0f, 5.0f } });
            polyline (st, { { 15.0f, 5.0f }, { 19.0f, 5.0f }, { 19.0f, 9.0f } });
            polyline (st, { { 19.0f, 15.0f }, { 19.0f, 19.0f }, { 15.0f, 19.0f } });
            polyline (st, { { 9.0f, 19.0f }, { 5.0f, 19.0f }, { 5.0f, 15.0f } });
            break;
        case Icon::sessionRecord: // a ring
            st.addEllipse (6.0f, 6.0f, 12.0f, 12.0f);
            break;
        case Icon::browserPanel: // a narrow panel beside a wide one
            fi.addRectangle (3.5f, 5.0f, 5.5f, 14.0f);
            fi.addRectangle (10.5f, 5.0f, 10.0f, 14.0f);
            break;
        case Icon::metronomeDots: // a ring and a dot (the beat and the accent)
            st.addEllipse (3.5f, 8.0f, 8.0f, 8.0f);
            fi.addEllipse (13.0f, 8.0f, 8.0f, 8.0f);
            break;
        case Icon::toStart: // a bar, then a triangle pointing at it (LYNX vector; reads like BandLab's ⏮)
        {
            fi.addRoundedRectangle (5.5f, 5.0f, 3.0f, 14.0f, 1.0f);
            juce::Path tri;
            tri.addTriangle (19.0f, 5.0f, 19.0f, 19.0f, 8.5f, 12.0f);
            fi.addPath (tri.createPathWithRoundedCorners (1.5f));
            break;
        }

        // --- round 190jo (design190ia FW-22): the audio editor's tools, LYNX shapes on Edison's metaphors ---
        case Icon::copy: // two sheets, one over the other
            polyline (st, { { 9.0f, 6.5f }, { 9.0f, 3.5f }, { 20.0f, 3.5f }, { 20.0f, 15.5f }, { 17.0f, 15.5f } });
            roundRect (st, 4.0f, 8.0f, 11.5f, 12.5f, 1.5f);
            break;
        case Icon::paste: // a clipboard with its clip
            roundRect (st, 5.0f, 5.5f, 14.0f, 15.0f, 1.5f);
            fi.addRoundedRectangle (9.0f, 3.0f, 6.0f, 4.0f, 1.0f);
            line (st, 8.5f, 11.5f, 15.5f, 11.5f);
            line (st, 8.5f, 15.5f, 13.5f, 15.5f);
            break;
        case Icon::trash: // a bin with its lid
            line (st, 4.0f, 6.5f, 20.0f, 6.5f);
            polyline (st, { { 9.5f, 6.5f }, { 9.5f, 4.0f }, { 14.5f, 4.0f }, { 14.5f, 6.5f } });
            polyline (st, { { 6.5f, 6.5f }, { 7.5f, 20.5f }, { 16.5f, 20.5f }, { 17.5f, 6.5f } });
            line (st, 10.5f, 10.0f, 10.5f, 17.0f);
            line (st, 13.5f, 10.0f, 13.5f, 17.0f);
            break;
        case Icon::trim: // the selection's brackets keep a piece of wave; the rest is cut off
            polyline (st, { { 9.0f, 5.0f }, { 7.0f, 5.0f }, { 7.0f, 19.0f }, { 9.0f, 19.0f } });
            polyline (st, { { 15.0f, 5.0f }, { 17.0f, 5.0f }, { 17.0f, 19.0f }, { 15.0f, 19.0f } });
            polyline (st, { { 9.5f, 12.0f }, { 10.5f, 8.5f }, { 12.0f, 15.5f }, { 13.5f, 9.5f }, { 14.5f, 12.0f } });
            line (st, 3.0f, 12.0f, 4.5f, 12.0f);
            line (st, 19.5f, 12.0f, 21.0f, 12.0f);
            break;
        case Icon::silence: // a wave, flat in the middle
            polyline (st, { { 3.0f, 12.0f }, { 4.0f, 7.5f }, { 5.5f, 16.5f }, { 7.0f, 12.0f }, { 17.0f, 12.0f }, { 18.5f, 8.0f }, { 20.0f, 16.0f }, { 21.0f, 12.0f } });
            line (st, 9.5f, 8.5f, 14.5f, 8.5f);
            line (st, 9.5f, 15.5f, 14.5f, 15.5f);
            break;
        case Icon::fadeIn: // a ramp up
        {
            juce::Path p;
            polyline (p, { { 3.5f, 19.5f }, { 20.5f, 5.0f }, { 20.5f, 19.5f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.0f));
            fi.addTriangle (12.0f, 19.5f, 20.5f, 12.25f, 20.5f, 19.5f);
            break;
        }
        case Icon::fadeOut: // a ramp down
        {
            juce::Path p;
            polyline (p, { { 3.5f, 19.5f }, { 3.5f, 5.0f }, { 20.5f, 19.5f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.0f));
            fi.addTriangle (3.5f, 12.25f, 12.0f, 19.5f, 3.5f, 19.5f);
            break;
        }
        case Icon::fadeCurve: // a bent ramp with a small menu arrow
        {
            juce::Path c;
            c.startNewSubPath (3.5f, 19.5f);
            c.quadraticTo (14.0f, 19.0f, 17.0f, 4.5f);
            st.addPath (c);
            line (st, 3.5f, 19.5f, 17.0f, 19.5f);
            fi.addTriangle (17.5f, 13.0f, 21.0f, 13.0f, 19.25f, 15.5f);
            break;
        }
        case Icon::normalize: // a wave whose peaks are taken to the two edges
            line (st, 3.5f, 3.5f, 20.5f, 3.5f);
            line (st, 3.5f, 20.5f, 20.5f, 20.5f);
            polyline (st, { { 12.0f, 10.0f }, { 12.0f, 6.0f } });
            polyline (st, { { 10.0f, 8.0f }, { 12.0f, 6.0f }, { 14.0f, 8.0f } });
            polyline (st, { { 12.0f, 14.0f }, { 12.0f, 18.0f } });
            polyline (st, { { 10.0f, 16.0f }, { 12.0f, 18.0f }, { 14.0f, 16.0f } });
            polyline (st, { { 4.0f, 12.0f }, { 6.0f, 9.5f }, { 8.0f, 14.5f }, { 9.5f, 12.0f } });
            polyline (st, { { 14.5f, 12.0f }, { 16.0f, 9.5f }, { 18.0f, 14.5f }, { 20.0f, 12.0f } });
            break;
        case Icon::amp: // up and down beside a level
            line (st, 7.0f, 4.0f, 7.0f, 20.0f);
            polyline (st, { { 4.0f, 7.0f }, { 7.0f, 4.0f }, { 10.0f, 7.0f } });
            polyline (st, { { 4.0f, 17.0f }, { 7.0f, 20.0f }, { 10.0f, 17.0f } });
            fi.addRoundedRectangle (13.0f, 13.0f, 2.5f, 7.0f, 0.75f);
            fi.addRoundedRectangle (17.5f, 8.0f, 2.5f, 12.0f, 0.75f);
            break;
        case Icon::reverse: // an arrow back over a hit whose tail now comes first
            line (st, 4.0f, 5.5f, 20.0f, 5.5f);
            polyline (st, { { 7.0f, 3.0f }, { 4.0f, 5.5f }, { 7.0f, 8.0f } });
            polyline (st, { { 3.5f, 15.0f }, { 6.0f, 14.0f }, { 8.5f, 16.0f }, { 11.0f, 13.0f }, { 13.5f, 17.5f }, { 16.0f, 11.5f }, { 18.0f, 19.5f }, { 20.0f, 10.0f }, { 20.5f, 15.0f } });
            break;
        case Icon::invert: // a wave and its mirror image
        {
            juce::Path a, b;
            a.startNewSubPath (3.5f, 12.0f);
            a.cubicTo (6.0f, 3.0f, 9.5f, 3.0f, 12.0f, 12.0f);
            a.cubicTo (14.5f, 21.0f, 18.0f, 21.0f, 20.5f, 12.0f);
            b.startNewSubPath (3.5f, 12.0f);
            b.cubicTo (6.0f, 21.0f, 9.5f, 21.0f, 12.0f, 12.0f);
            b.cubicTo (14.5f, 3.0f, 18.0f, 3.0f, 20.5f, 12.0f);
            st.addPath (a);
            const float dash[] = { 1.6f, 1.6f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, b, dash, 2);
            fi.addPath (dashed);
            break;
        }
        case Icon::dcCenter: // a wave that sits too high, brought down onto the middle line
            line (st, 3.5f, 16.5f, 20.5f, 16.5f);
            polyline (st, { { 3.5f, 7.0f }, { 6.0f, 4.0f }, { 8.5f, 10.0f }, { 11.0f, 4.0f }, { 13.5f, 10.0f }, { 16.0f, 4.0f }, { 18.5f, 10.0f }, { 20.5f, 7.0f } });
            line (st, 12.0f, 11.5f, 12.0f, 14.0f);
            fi.addTriangle (9.5f, 12.5f, 14.5f, 12.5f, 12.0f, 15.0f);
            break;
        case Icon::resample: // a wave read at points
        {
            juce::Path c;
            c.startNewSubPath (3.5f, 12.0f);
            c.cubicTo (6.5f, 2.5f, 10.0f, 2.5f, 12.0f, 12.0f);
            c.cubicTo (14.0f, 21.5f, 17.5f, 21.5f, 20.5f, 12.0f);
            st.addPath (c);
            for (auto pt : { juce::Point<float> (3.5f, 12.0f), { 7.75f, 5.4f }, { 12.0f, 12.0f }, { 16.25f, 18.6f }, { 20.5f, 12.0f } })
                fi.addEllipse (pt.x - 1.75f, pt.y - 1.75f, 3.5f, 3.5f);
            break;
        }
        case Icon::marker: // a flag on its pole
            line (st, 6.5f, 3.5f, 6.5f, 20.5f);
            fi.addTriangle (6.5f, 3.5f, 18.5f, 7.5f, 6.5f, 11.5f);
            break;
        case Icon::region: // two flags and the span between them
            line (st, 4.5f, 3.5f, 4.5f, 20.5f);
            line (st, 19.5f, 3.5f, 19.5f, 20.5f);
            fi.addTriangle (4.5f, 3.5f, 10.0f, 5.75f, 4.5f, 8.0f);
            fi.addTriangle (19.5f, 3.5f, 14.0f, 5.75f, 19.5f, 8.0f);
            fi.addRectangle (4.5f, 14.0f, 15.0f, 4.0f);
            break;
        case Icon::setLoop: // the loop bar over a span, with a turning arrow
        {
            fi.addRectangle (3.5f, 3.5f, 17.0f, 3.0f);
            line (st, 4.25f, 6.5f, 4.25f, 20.5f);
            line (st, 19.75f, 6.5f, 19.75f, 20.5f);
            juce::Path arc;
            arc.addCentredArc (12.0f, 14.0f, 4.5f, 4.5f, 0.0f, pi * 0.15f, pi * 1.75f, true);
            st.addPath (arc);
            fi.addTriangle (12.2f, 7.4f, 12.2f, 11.6f, 15.4f, 9.5f);
            break;
        }
        case Icon::equalizer: // three faders at different heights
            for (float x : { 6.0f, 12.0f, 18.0f })
                line (st, x, 4.0f, x, 20.0f);
            fi.addRoundedRectangle (3.5f, 13.0f, 5.0f, 3.0f, 1.0f);
            fi.addRoundedRectangle (9.5f, 6.5f, 5.0f, 3.0f, 1.0f);
            fi.addRoundedRectangle (15.5f, 10.5f, 5.0f, 3.0f, 1.0f);
            break;
        case Icon::denoise: // a noisy line that comes out smooth
        {
            polyline (st, { { 3.0f, 12.0f }, { 4.0f, 9.5f }, { 5.0f, 14.0f }, { 6.0f, 10.0f }, { 7.0f, 14.5f }, { 8.0f, 9.0f }, { 9.0f, 13.5f }, { 10.0f, 12.0f } });
            juce::Path c;
            c.startNewSubPath (10.0f, 12.0f);
            c.cubicTo (12.5f, 4.5f, 15.0f, 4.5f, 16.5f, 12.0f);
            c.cubicTo (18.0f, 19.5f, 20.0f, 19.5f, 21.0f, 12.0f);
            st.addPath (c);
            break;
        }
        case Icon::reverb: // a hit and its fading echoes
        {
            const float xs[] = { 4.5f, 8.5f, 12.5f, 16.5f, 20.0f };
            const float hs[] = { 16.0f, 12.0f, 8.5f, 5.5f, 3.0f };
            for (int i = 0; i < 5; ++i)
                line (st, xs[i], 12.0f - hs[i] * 0.5f, xs[i], 12.0f + hs[i] * 0.5f);
            break;
        }
        case Icon::spectrogram: // bars of a spectrum, low to high
        {
            const float xs[] = { 3.5f, 7.0f, 10.5f, 14.0f, 17.5f };
            const float hs[] = { 9.0f, 15.0f, 11.0f, 7.0f, 4.0f };
            for (int i = 0; i < 5; ++i)
                fi.addRoundedRectangle (xs[i], 20.5f - hs[i], 2.5f, hs[i], 0.75f);
            break;
        }
        case Icon::pointer: // round 190jq: a mouse pointer, outlined (the system arrow's shape, LYNX's 24-unit grid)
        {
            juce::Path p;
            polyline (p, { { 6.0f, 3.5f }, { 6.0f, 18.5f }, { 10.0f, 14.75f }, { 12.75f, 20.5f }, { 15.25f, 19.25f }, { 12.5f, 13.5f }, { 18.0f, 13.5f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.0f));
            break;
        }
        case Icon::scrub: // a film reel (LYNX vector; reads like FL Edison's Scrub button): a ring, four holes, a hub
        {
            st.addEllipse (4.5f, 4.5f, 15.0f, 15.0f);
            for (const auto a : { 0.785398f, 2.356194f, 3.926991f, 5.497787f })
                st.addEllipse (12.0f + 3.8f * std::sin (a) - 1.9f, 12.0f - 3.8f * std::cos (a) - 1.9f, 3.8f, 3.8f);
            fi.addEllipse (11.0f, 11.0f, 2.0f, 2.0f);
            break;
        }
    }

    return s;
}
} // namespace

const Shape& get (Icon icon)
{
    static std::map<Icon, Shape> cache;
    auto it = cache.find (icon);
    if (it == cache.end())
        it = cache.emplace (icon, make (icon)).first;
    return it->second;
}

float strokeWidth (float size)
{
    // a fine line that thickens only a little with the size: 1.25 px at 14 px, 1.4 px at 20 px, 1.6 px from
    // 30 px (logical pixels; HiDPI screens draw it with proportionally more device pixels)
    return juce::jlimit (1.2f, 1.6f, 1.0f + size * 0.019f);
}

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (icon == Icon::none || area.isEmpty())
        return;

    // a whole-pixel box on whole-pixel coordinates: the grid's half units then land on the same
    // sub-pixel phase on every button, so strokes look equally sharp everywhere
    const auto size = std::floor (juce::jmin (area.getWidth(), area.getHeight()));
    const auto box = juce::Rectangle<float> (std::round (area.getCentreX() - size * 0.5f), std::round (area.getCentreY() - size * 0.5f), size, size);
    const auto scale = size / 24.0f;
    const auto transform = juce::AffineTransform::scale (scale).translated (box.getX(), box.getY());
    const auto& shape = get (icon);

    g.setColour (colour);

    if (! shape.fill.isEmpty())
        g.fillPath (shape.fill, transform);

    if (! shape.stroke.isEmpty())
        g.strokePath (shape.stroke, juce::PathStrokeType (strokeWidth (size), juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
}
} // namespace icons

IconButton::IconButton (const juce::String& name, Icon i, const juce::String& t)
    : juce::Button (name), icon (i), text (t), onColour (theme::colour::accent), iconOnColour (theme::colour::onAccent)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIconSizing (float fraction, float maxPx)
{
    if (fraction != iconFraction || maxPx != iconMax)
    {
        iconFraction = fraction;
        iconMax = maxPx;
        repaint();
    }
}

void IconButton::setIcon (Icon newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void IconButton::setText (const juce::String& newText)
{
    if (text != newText)
    {
        text = newText;
        repaint();
    }
}

void IconButton::setPrimary (bool b) { primary = b; repaint(); }
void IconButton::setOnColour (juce::Colour c) { onColour = c; onColoursSet = true; repaint(); }
void IconButton::setIconColourWhenOn (juce::Colour c) { iconOnColour = c; onColoursSet = true; repaint(); }
void IconButton::setToggleLook (bool isSwitch) { toggleLook = isSwitch ? 1 : 0; repaint(); }

bool IconButton::actsAsSwitch() const
{
    return toggleLook >= 0 ? toggleLook == 1 : (isToggleable() || getRadioGroupId() != 0 || onColoursSet);
}
void IconButton::setFlat (bool b) { flat = b; repaint(); }
void IconButton::setIconColour (juce::Colour c) { iconColour = c; repaint(); }

void IconButton::setCorners (bool left, bool right)
{
    roundLeft = left;
    roundRight = right;
    repaint();
}

void IconButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
        return; // handled on mouse up, without pressing the button

    juce::Button::mouseDown (e);
}

void IconButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
    {
        if (getLocalBounds().contains (e.getPosition()))
            onRightClick();
        return;
    }

    juce::Button::mouseUp (e);
}

void IconButton::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (onDoubleClick != nullptr && ! e.mods.isPopupMenu())
        onDoubleClick();
}

int IconButton::getIdealWidth (int height) const
{
    const auto iconSize = icon == Icon::none ? 0 : juce::roundToInt (height * 0.55f);
    const auto textWidth = text.isEmpty() ? 0
                                          : juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (theme::type::body)), text));
    const auto gap = iconSize > 0 && textWidth > 0 ? 6 : 0;
    return juce::jmax (height, iconSize + gap + textWidth + (textWidth > 0 ? 20 : 0));
}

void IconButton::buttonStateChanged()
{
    juce::Button::buttonStateChanged();

    const auto target = isOver() || isDown() ? 1.0f : 0.0f;

    if (! ui::motion::enabled()) // Settings › General › Animations off: no fade, no timer
    {
        stopTimer();
        hover = target;
        return;
    }

    if (target != hover && ! isTimerRunning())
        startTimerHz (60);
}

void IconButton::timerCallback()
{
    const auto target = isOver() || isDown() ? 1.0f : 0.0f;
    const auto step = target > hover ? 0.34f : 0.18f; // in quickly, out a little slower
    hover = target > hover ? juce::jmin (target, hover + step) : juce::jmax (target, hover - step);

    if (hover == target)
        stopTimer();

    repaint();
}

void IconButton::setLook (Look newLook, float iconBoxPx)
{
    if (newLook == look && iconBoxPx == lookIconBox)
        return;
    look = newLook;
    lookIconBox = iconBoxPx;
    repaint();
}

// Round 190iv: the Simple capsule segment and Live's control-bar boxes (Icons.h, IconButton::Look)
void IconButton::paintLook (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    const auto on = getToggleState();
    const auto enabled = isEnabled();
    const auto area = getLocalBounds().toFloat();
    const auto h = isTimerRunning() ? hover : (highlighted ? 1.0f : 0.0f);
    juce::Colour fg;

    if (look == Look::capsule)
    {
        // the capsule (drawn behind) is bg4; a segment shows hover and press only (SG-1: hover bg5, pressed bg6;
        // recording: the Record segment filled accentMuted)
        // (round 190jj, capture190ji: BandLab fills no segment while it records; its Record glyph changes instead)
        juce::Colour bg;
        if (down)
            bg = colour::bg6;
        else if (h > 0.0f)
            bg = colour::bg5.withAlpha (h);
        if (! bg.isTransparent())
        {
            // (the capsule's round ends: the first segment rounds its left, the last its right; setCorners)
            const auto r = area.getHeight() / 2.0f;
            juce::Path p;
            p.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, roundLeft, roundRight, roundLeft, roundRight);
            g.setColour (bg);
            g.fillPath (p);
        }
        // round 190jj (capture190ji §1d): Play / Pause and Stop stay white while they run (BandLab), Record is red; while it
        // records, Record is a red rounded square in a dark-red disc, steady (no blink: BandLab and Live; FL's blink is the
        // FL look's only). BandLab's disc is as wide as its idle dot (18 px) and the square 8 px of it (measured at 100 %).
        fg = icon == Icon::record ? colour::record : colour::textPrimary;
        if (icon == Icon::cycle && on) // round 190ld: the Cycle switch lit (the capsule's "on = the accent glyph", round 190iv)
            fg = colour::accent;
        if (icon == Icon::record && on && enabled && text.isEmpty())
        {
            const auto box = lookIconBox > 0.0f ? lookIconBox : juce::jmin (area.getHeight() * iconFraction, iconMax);
            const auto d = std::round (box * 14.0f / 24.0f); // (as wide as the idle dot: 14 / 24 of the icon box)
            const auto disc = area.withSizeKeepingCentre (d, d);
            g.setColour (colour::bg4.overlaidWith (colour::record.withAlpha (0.24f)));
            g.fillEllipse (disc);
            g.setColour (down ? colour::record.darker (0.3f) : colour::record);
            g.fillRoundedRectangle (disc.withSizeKeepingCentre (disc.getWidth() * 0.44f, disc.getWidth() * 0.44f), disc.getWidth() * 0.06f);
            return;
        }
    }
    else if (look == Look::liveBox)
    {
        // Live 12.4 (CL-2, ref/live-session.png): #363636 boxes, hover #464646, pressed #1e1e1e, lit #ffad56
        // (round 190oa, capture 190oa L1: Re-Enable Automation lights red-orange #e76942; idle, its arrow is #696969)
        const auto reEnable = icon == Icon::reEnableAutomation;
        const juce::Colour rest { 0xff363636 }, over { 0xff464646 }, pressed { 0xff1e1e1e }, lit { reEnable ? 0xffe76942u : 0xffffad56u };
        g.setColour (down ? pressed : on ? lit : rest.interpolatedWith (over, h));
        g.fillRect (area);
        fg = on && ! down ? juce::Colour (0xff070707)
             : reEnable   ? juce::Colour (0xff696969).interpolatedWith (juce::Colours::white, h * 0.35f)
                          : juce::Colour (0xffb5b5b5).interpolatedWith (juce::Colours::white, h * 0.35f);
    }
    else if (look == Look::liveSelector)
    {
        // (ref/live-session.png x 1870..1900: the shown view's glyph #ffad56; Live's other glyph is #464646, 1.5:1 on the
        // band; LYNX keeps it at 3.4:1, the contrast floor of design190ia G1)
        fg = on ? juce::Colour (0xffffad56) : juce::Colour (0xff7a7a7a).interpolatedWith (juce::Colour (0xffb5b5b5), h);
    }
    else // liveGlyph: the bare transport glyphs on the band
    {
        const juce::Colour glyph { 0xffb5b5b5 };
        // (Live 12.4, capture190ji: playing = the Play glyph #00d38d, recording = the Record glyph #ff5559, both steady
        // with no plate; Stop stays #b5b5b5)
        fg = on ? (icon == Icon::record ? juce::Colour (0xffff5559) : juce::Colour (0xff00d38d)) // (Live's playing green, CL token table)
                : glyph.interpolatedWith (juce::Colours::white, h * 0.35f);
        if (down)
            fg = fg.darker (0.3f);
    }
    if (! enabled)
        fg = juce::Colour (0xff696969);

    const auto box = lookIconBox > 0.0f ? lookIconBox : juce::jmin (area.getHeight() * iconFraction, iconMax);
    if (text.isEmpty())
    {
        icons::draw (g, icon, area.withSizeKeepingCentre (box, box), fg);
        return;
    }
    // a text box (Live's "Tap", "Key", "MIDI"): the small type, centred, the icon (if any) before it
    const auto f = theme::font (type::caption);
    auto content = area.reduced (3.0f, 0.0f);
    if (icon != Icon::none)
    {
        icons::draw (g, icon, content.removeFromLeft (box).withSizeKeepingCentre (box, box), fg);
        content.removeFromLeft (3.0f);
    }
    g.setColour (fg);
    g.setFont (f);
    g.drawFittedText (text, content.toNearestInt(), icon == Icon::none ? juce::Justification::centred : juce::Justification::centredLeft, 1, 0.8f);
}

// Round 190ki: whether the FL tiles' bodies come from the parent's atlas. LYNX_TILE_MODE=2: everywhere (opt-in on the
// native renderers), 0: nowhere, unset: on JUCE's software renderer only (round 190mh). The atlas gives the toolbar's own
// pixels, but Direct2D anti-aliases a later path a little differently when the frame's draw calls change (the Piano
// roll's 4/4 flag: 8 pixels, up to 44 levels, --paintbench Thief), so it stays opt-in there; it saves ~0.25 ms more per
// whole frame there.
namespace
{
int& tileModeSetting()
{
    static int mode = [] {
        const auto v = juce::SystemStats::getEnvironmentVariable ("LYNX_TILE_MODE", {});
        return v.isEmpty() ? -1 : v.getIntValue() == 2 ? 2 : 0;
    }();
    return mode;
}

struct TileAtlasEntry
{
    juce::Component::SafePointer<juce::Component> parent;
    IconButton::TileAtlas atlas;
};

std::vector<TileAtlasEntry>& tileAtlases()
{
    static std::vector<TileAtlasEntry> list;
    static const bool registered = ui::registerImageCache ([] { tileAtlases().clear(); }); (void) registered;
    return list;
}
} // namespace

int IconButton::tileCacheMode() { return tileModeSetting(); }

// JUCE's software renderer (Linux, a software snapshot). Round 190ki's atlas came out 1 level apart at the last column
// of a button there: JUCE's EdgeTable clamps a path's edges right of its clip to the clip's last column (x = right − 1/256
// of a pixel, EdgeTable's path constructor), so the backdrop's well that runs on past the button covered that column
// 255/256 in the atlas (its clip: the button) and fully in a frame whose clip runs on past the button. (Left, top and
// bottom clip edges change no pixel of a path there.) Round 190mh paints the backdrop into the atlas with the frame's
// clip edge: the parent's clip when it painted this frame (setFlTileBackdrop's `parentClip`) reaching past the button =
// the atlas's clip reaches past it too (one pixel in a spare row below the tiles), ending at its edge = the button alone;
// a clip that ends inside the button (a playhead column) paints it straight.
static bool& tileCacheOnSoftwareSetting()
{
    static bool on = true;
    return on;
}
bool IconButton::tileCacheOnSoftware() { return tileCacheOnSoftwareSetting(); }

// The atlas of a parent's tiles: the parent's width by `bottom` (grown to the lowest tile asked for), at `scale`, of the
// frame's kind (software or native). A new image is a new generation: every button draws its region again.
IconButton::TileAtlas& IconButton::tileAtlasFor (juce::Component& parent, int bottom, int scale, bool software)
{
    auto& list = tileAtlases();
    list.erase (std::remove_if (list.begin(), list.end(), [] (const TileAtlasEntry& e) { return e.parent == nullptr; }), list.end());
    auto it = std::find_if (list.begin(), list.end(), [&parent] (const TileAtlasEntry& e) { return e.parent.getComponent() == &parent; });
    if (it == list.end())
    {
        list.push_back ({ &parent, {} });
        it = std::prev (list.end());
    }
    auto& a = it->atlas;
    // (round 190mh: one spare row below the lowest tile, where a software atlas's clip reaches past a button; no button's
    // region is ever there)
    const auto rows = juce::jmax (bottom + 1, a.image.isNull() ? 0 : a.image.getHeight() / juce::jmax (1, a.scale));
    const auto width = parent.getWidth() * scale, height = rows * scale;
    if (a.image.isNull() || a.scale != scale || a.software != software || a.image.getWidth() != width || a.image.getHeight() < (bottom + 1) * scale)
    {
        static std::uint64_t generations = 0;
        a.image = juce::Image (juce::Image::ARGB, width, height, true,
                               software ? (const juce::ImageType&) juce::SoftwareImageType() : (const juce::ImageType&) juce::NativeImageType());
        a.scale = scale;
        a.software = software;
        a.generation = ++generations;
    }
    return a;
}

// Round 190mh: an FL tile's fill and its stroked edge as JUCE's software renderer builds them for fillRoundedRectangle (tile,
// r) and drawRoundedRectangle (tile.reduced (line / 2), r - line / 2, line) at the context scale `contextScale`, kept per
// (tile, r, line, scale): the tiles of one toolbar share a few sizes. Bit-equal keys only.
static const std::pair<juce::Path, juce::Path>& flTileBodyPaths (juce::Rectangle<float> tile, float r, float line, float contextScale)
{
    struct Entry
    {
        float x, y, w, h, r, line, scale;
        std::pair<juce::Path, juce::Path> paths;
    };
    static std::vector<Entry> cache;
    static const bool registered = ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    const auto same = [] (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; };
    for (const auto& e : cache)
        if (same (e.x, tile.getX()) && same (e.y, tile.getY()) && same (e.w, tile.getWidth()) && same (e.h, tile.getHeight()) && same (e.r, r)
            && same (e.line, line) && same (e.scale, contextScale))
            return e.paths;
    if (cache.size() >= 32)
        cache.clear();
    Entry e { tile.getX(), tile.getY(), tile.getWidth(), tile.getHeight(), r, line, contextScale, {} };
    e.paths.first.addRoundedRectangle (tile, r);
    juce::Path edge;
    edge.addRoundedRectangle (tile.reduced (line * 0.5f), r - line * 0.5f);
    juce::PathStrokeType (line).createStrokedPath (e.paths.second, edge, {}, contextScale);
    cache.push_back (std::move (e));
    return cache.back().paths;
}

// Round 190jo (design190ia FM-1/FM-2/FM-3, decision (d) with FL's metrics; the Pattern toolbar only): FL's tile. FL's
// buttons are 30x33 tiles at a 33 px pitch with a 1 px dark edge (ref/fl-toolbar-hover-hint.png 1184,4,1214,37); LYNX's
// 31 px buttons give a 30x30 tile at the same pitch. The edge and the tile sit on whole device pixels at any scale.
// Round 190ki (perf): at rest, hovered, pressed, on or disabled the tile comes from a picture of the backdrop with the
// tile drawn over it (setFlTileBackdrop); the fade between two states paints straight.
void IconButton::paintFlTile (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    const auto on = getToggleState() || primary;
    const auto enabled = isEnabled();
    const auto h = ! enabled ? 0.0f : isTimerRunning() ? hover : (highlighted ? 1.0f : 0.0f);

    // the fill: the tile at rest, then the state over it
    auto fill = colour::bg3;
    auto edge = colour::border;
    if (down && enabled)
    {
        fill = colour::bg0;
        edge = colour::borderStrong;
    }
    else if (on)
    {
        fill = fill.overlaidWith (onColour).interpolatedWith (fill.overlaidWith (onColour.brighter (0.12f)), h);
        edge = colour::accent.withAlpha (0.5f);
    }
    else if (h > 0.0f)
    {
        if (hoverShowsOn) // a switch: its "on" look, a little lighter
        {
            fill = fill.interpolatedWith (colour::accentMuted, 0.7f * h);
            edge = edge.interpolatedWith (fill.overlaidWith (colour::accent.withAlpha (0.5f)), h);
        }
        else
        {
            fill = fill.interpolatedWith (colour::bg5, h);
            edge = edge.interpolatedWith (colour::borderStrong, h);
        }
    }

    // the glyph
    auto fg = on ? iconOnColour
                 : (! iconColour.isTransparent() ? iconColour : colour::textSecondary.brighter (0.25f));
    if (! on && h > 0.0f)
        fg = fg.interpolatedWith (hoverShowsOn ? colour::accentHover : colour::textPrimary, h);
    if (! enabled)
        fg = fg.withMultipliedAlpha (0.35f);
    const auto area = getLocalBounds().toFloat();
    const auto box = lookIconBox > 0.0f ? lookIconBox : juce::jmin (area.getHeight() * iconFraction, iconMax);

    // the tile's body (fill and edge) and its glyph
    const auto drawBody = [&] (juce::Graphics& k) {
        const auto scale = juce::jmax (1.0f, k.getInternalContext().getPhysicalPixelScaleFactor());
        const auto snap = [scale] (float v) { return std::round (v * scale) / scale; };
        const auto b = area;
        const auto tile = juce::Rectangle<float>::leftTopRightBottom (snap (b.getX() + 0.5f), snap (b.getY() + 0.5f), snap (b.getRight() - 0.5f),
                                                                      snap (b.getBottom() - 0.5f));
        const auto line = std::round (scale) / scale; // one device pixel or more: never a blend
        const auto r = 4.0f;
        // (round 190mh) JUCE's software renderer: the two paths from a cache, filled as its own fillRoundedRectangle and
        // drawRoundedRectangle fill them (LowLevelGraphicsContext's defaults: the same Path, stroked with the context's
        // scale as the extra accuracy, then fillPath with no transform): the same pixels without making them each paint
        if (dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&k.getInternalContext()) != nullptr && ui::paintCache())
        {
            const auto& paths = flTileBodyPaths (tile, r, line, k.getInternalContext().getPhysicalPixelScaleFactor());
            k.setColour (fill);
            k.fillPath (paths.first);
            k.setColour (edge);
            k.fillPath (paths.second);
            return;
        }
        k.setColour (fill);
        k.fillRoundedRectangle (tile, r);
        k.setColour (edge);
        k.drawRoundedRectangle (tile.reduced (line * 0.5f), r - line * 0.5f, line);
    };
    const auto drawGlyph = [&] (juce::Graphics& k) { icons::draw (k, icon, area.withSizeKeepingCentre (box, box), fg); };

    // Round 190ki (perf, opt-in: LYNX_TILE_MODE=2): the body from the parent's tile atlas, the glyph straight over it. The atlas is the parent's
    // band at the parent's own coordinates, each button's region holding its current state: the backdrop and the body
    // drawn there by the same calls, at the same device pixels and under the same clip as in the frame, so a blit gives
    // the frame's pixels. (The glyph stays straight: Direct2D anti-aliases a path drawn into another target a little
    // differently, up to 23 levels at an edge in --selftest.)
    int scale = 0;
    const auto software = dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr;
    const auto mode = tileCacheMode();
    auto backdrop = mode != 0 && (software ? tileCacheOnSoftware() : mode == 2) && tileCacheUsable (g, scale) ? tileBackdrop (getBounds()) : 0;
    // (round 190mh) the software renderer: where the parent's clip ended when it painted under the button this frame
    auto past = false; // (past the button's right edge: else exactly at it)
    if (backdrop != 0 && software)
    {
        const auto clip = tileParentClip != nullptr ? tileParentClip() : juce::Rectangle<int>();
        if (! clip.intersects (getBounds()) || clip.getRight() < getRight())
            backdrop = 0;
        past = clip.getRight() > getRight();
    }
    if (backdrop == 0)
    {
        drawBody (g);
        drawGlyph (g);
        return;
    }
    const auto mix = [] (std::uint64_t a, std::uint64_t v) { return (a ^ v) * 0x100000001b3ull; };
    auto key = mix (mix (mix (0x243f6a8885a308d3ull, fill.getARGB()), edge.getARGB()), (std::uint64_t) scale);
    key = mix (mix (mix (mix (key, (std::uint64_t) (std::uint32_t) getX()), (std::uint64_t) (std::uint32_t) getY()), (std::uint64_t) getWidth()),
               (std::uint64_t) getHeight());
    key = mix (mix (key, backdrop), software ? (past ? 7u : 5u) : 6u);
    auto& atlas = tileAtlasFor (*getParentComponent(), getBottom(), scale, software);
    if (key != atlasKey || atlasGeneration != atlas.generation)
    {
        juce::Graphics k (atlas.image);
        k.addTransform (juce::AffineTransform::scale ((float) scale));
        if (past) // (the clip's bounds reach a pixel past the button, through the spare row: no edge is clamped in the button)
        {
            juce::RectangleList<int> region (getBounds());
            region.addWithoutMerging ({ getRight(), atlas.image.getHeight() / scale - 1, 1, 1 });
            k.reduceClipRegion (region);
        }
        else
            k.reduceClipRegion (getBounds());
        {
            const juce::ScopedValueSetter<bool> inAtlas (paintingTileAtlasFlag(), true); // (the parent's paint into the atlas: not a frame's)
            getParentComponent()->paint (k);
        }
        k.reduceClipRegion (getBounds());
        k.setOrigin (getPosition());
        drawBody (k);
        atlasKey = key;
        atlasGeneration = atlas.generation;
    }
    const auto part = atlas.image.getClippedImage (juce::Rectangle<int> (getX() * scale, getY() * scale, getWidth() * scale, getHeight() * scale));
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setColour (juce::Colours::black); // (opaque: JUCE draws an image with the colour's alpha)
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        if (scale == 1)
            g.drawImageAt (part, 0, 0);
        else
            g.drawImage (part, area, juce::RectanglePlacement::stretchToFit);
    }
    drawGlyph (g);
}

bool& IconButton::paintingTileAtlasFlag()
{
    static bool painting = false;
    return painting;
}

void IconButton::setFlTileBackdrop (std::function<std::uint64_t (juce::Rectangle<int>)> stamp, std::function<juce::Rectangle<int>()> parentClip)
{
    tileBackdrop = std::move (stamp);
    tileParentClip = std::move (parentClip);
    atlasKey = 0; // (its region of the atlas is drawn again)
    repaint();
}

// The cached tile only where it gives the pixels of painting straight: a backdrop told, the parent's paint the only thing
// under the button (no sibling before it overlaps it), opaque components without transforms, a whole device scale, no fade
bool IconButton::tileCacheUsable (juce::Graphics& g, int& scale) const
{
    if (tileBackdrop == nullptr || ! ui::paintCache() || isTimerRunning() || getWidth() <= 0 || getHeight() <= 0 || getWidth() > 256 || getHeight() > 256)
        return false;
    auto* parent = getParentComponent();
    if (parent == nullptr || getAlpha() < 1.0f || parent->getAlpha() < 1.0f || isTransformed() || parent->isTransformed())
        return false;
    // (round 190mh) the parent at its window's origin: the atlas's paths then get the frame's own float coordinates
    if (auto* top = parent->getTopLevelComponent(); top != parent && top->getLocalPoint (parent, juce::Point<int>()) != juce::Point<int>())
        return false;
    const auto s = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto whole = std::round (s);
    if (whole < 1.0f || whole > 3.0f || std::abs (s - whole) > 1.0e-4f)
        return false;
    for (auto* c : parent->getChildren())
    {
        if (c == this)
            break;
        if (c->isVisible() && c->getBounds().intersects (getBounds()))
            return false;
    }
    scale = (int) whole;
    return true;
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    paintButtonBody (g, highlighted, down);
    if (badge.isNotEmpty()) // (round 190pa)
        paintBadge (g);
}

void IconButton::setBadge (const juce::String& newBadge, juce::Colour colour)
{
    if (newBadge == badge && colour == badgeColour)
        return;
    badge = newBadge;
    badgeColour = colour;
    repaint();
}

void IconButton::paintBadge (juce::Graphics& g)
{
    // a pill in the top-right corner: 10 px high at 100 %, the text in the band's darkest colour on the badge colour. A
    // small box (Live's 17 px control-bar boxes) gets a smaller pill in its bottom-right corner, off the glyph's centre
    const auto small = getHeight() < 22;
    const auto font = theme::font (small ? 7.0f : 8.5f, true);
    const auto h = small ? 8.0f : juce::jmin (10.0f, (float) getHeight() * 0.42f);
    const auto w = juce::jmax (h, juce::GlyphArrangement::getStringWidth (font, badge) + (small ? 2.0f : 4.0f));
    const auto r = small ? juce::Rectangle<float> ((float) getWidth() - w, (float) getHeight() - h, w, h)
                         : juce::Rectangle<float> ((float) getWidth() - w - 1.0f, 1.0f, w, h);
    g.setColour (badgeColour);
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (theme::colour::bg0);
    g.setFont (font);
    g.drawText (badge, r, juce::Justification::centred, false);
}

void IconButton::paintButtonBody (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    if (look == Look::flTile)
    {
        paintFlTile (g, highlighted, down);
        return;
    }
    if (look != Look::standard)
    {
        paintLook (g, highlighted, down);
        return;
    }
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState() || primary;
    const auto enabled = isEnabled();
    // the animated hover level (snaps to the state when nothing animates, e.g. in snapshots)
    const auto h = ! enabled ? 0.0f : isTimerRunning() ? hover : (highlighted ? 1.0f : 0.0f);
    const auto isSwitch = actsAsSwitch();

    // one state rule for every button kind (round 190je, core/ThemePalette.h buttonLook)
    const auto kind = ! flat ? themes::ButtonKind::secondary : isSwitch ? themes::ButtonKind::flatToggle : themes::ButtonKind::flatMomentary;
    auto state = buttonColours (kind, on, h, down, enabled);
    const auto ownOn = onColour != colour::accent; // a button with "on" colours of its own (bg6 + a red icon, a wash)
    if (on && enabled && (ownOn || flat))
    {
        state.fill = down ? colour::accentPressed : onColour.interpolatedWith (onColour.brighter (0.12f), h);
        state.edge = {};
    }
    else if (on && ! enabled && (ownOn || flat))
        state.fill = onColour.withMultipliedAlpha (0.5f);
    else if (enabled && ! on && ! down && isSwitch && flat && ownOn)
    {
        // the preview of this switch's own "on" look (bg6 with a red icon, a wash)
        state.fill = onColour.withMultipliedAlpha (0.7f * h);
        state.edge = iconOnColour.withAlpha (0.5f * h);
    }

    // the icon / text: as at rest before (a light grey), towards white under the mouse, towards the "on" colour on a switch
    const auto idleFg = ! iconColour.isTransparent() ? iconColour : colour::textSecondary.brighter (0.25f);
    const auto hoverFg = isSwitch && flat ? (ownOn ? iconOnColour : colour::accentHover)
                                          : (! iconColour.isTransparent() ? iconColour.brighter (0.2f) : colour::textPrimary);
    state.content = ! enabled ? colour::textDisabled : on ? iconOnColour : idleFg.interpolatedWith (down ? colour::textPrimary : hoverFg, down ? 1.0f : h);

    if (! state.fill.isTransparent() || ! state.edge.isTransparent())
    {
        juce::Path p;
        p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md,
                               roundLeft, roundRight, roundLeft, roundRight);
        if (! state.fill.isTransparent())
        {
            g.setColour (state.fill);
            g.fillPath (p);
        }
        if (! state.edge.isTransparent())
        {
            juce::Path edge; // (on the pixel centres: `bounds` is inset by half a pixel, a crisp 1 px line)
            edge.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md,
                                      roundLeft, roundRight, roundLeft, roundRight);
            g.setColour (state.edge);
            g.strokePath (edge, juce::PathStrokeType (1.0f));
        }
    }

    const auto fg = state.content;

    auto area = getLocalBounds().toFloat();
    const auto iconSize = juce::jmin (area.getHeight() * iconFraction, iconMax);

    if (text.isEmpty())
    {
        icons::draw (g, icon, area.withSizeKeepingCentre (iconSize, iconSize), fg);
        return;
    }

    const auto font = juce::Font (theme::font (type::body));
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, text);
    const auto gap = icon == Icon::none ? 0.0f : 6.0f;
    const auto contentWidth = juce::jmin (area.getWidth() - 12.0f, (icon == Icon::none ? 0.0f : iconSize) + gap + textWidth);
    auto content = area.withSizeKeepingCentre (contentWidth, area.getHeight());

    if (icon != Icon::none)
        icons::draw (g, icon, content.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), fg);

    content.removeFromLeft (gap);
    g.setColour (fg);
    g.setFont (font);
    g.drawFittedText (text, content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}

juce::String flTileCacheSelfTest()
{
    using namespace theme;
    struct Backdrop final : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            if (! IconButton::paintingTileAtlas())
                clip = g.getClipBounds();
            g.fillAll (colour::bg3);
            const auto well = juce::Rectangle<float> (2.0f, 3.0f, 70.5f, 38.0f);
            g.setColour (colour::bg1.withAlpha (0.55f));
            g.fillRoundedRectangle (well, 6.0f);
            g.setColour (colour::border);
            g.drawRoundedRectangle (well.reduced (0.5f), 6.0f, 1.0f);
        }
        juce::Rectangle<int> clip; // (round 190mh: the tiles' parentClip)
    };
    Backdrop parent;
    parent.setSize (132, 46);
    // (no glyphs: they are drawn straight over the cached body either way, and Direct2D anti-aliases a path a little
    // differently after other work in a small target, which says nothing of the cache)
    IconButton rest ("rest", Icon::none), on ("on", Icon::none), off ("off", Icon::none), corner ("corner", Icon::none);
    on.setClickingTogglesState (true);
    on.setToggleState (true, juce::dontSendNotification);
    off.setEnabled (false);
    int x = 0;
    for (auto* b : { &rest, &on, &off, &corner })
    {
        b->setLook (IconButton::Look::flTile, b == &rest ? 0.0f : 24.0f);
        b->setBounds (x, 4, 31, 31);
        parent.addAndMakeVisible (*b);
        x += 33; // (the first and the third over the well's rounded ends)
    }
    // (round 190mh) `clip`: the frame's region (empty: all), e.g. a button's own repaint or a playhead column
    const auto render = [&parent] (int scale, bool native, juce::Rectangle<int> clip = {}) {
        juce::Image image (juce::Image::ARGB, parent.getWidth() * scale, parent.getHeight() * scale, true,
                           native ? (const juce::ImageType&) juce::NativeImageType() : (const juce::ImageType&) juce::SoftwareImageType());
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        if (! clip.isEmpty())
            g.reduceClipRegion (clip);
        parent.paintEntireComponent (g, true);
        return image;
    };
    // the pixels that differ and the largest difference of a channel
    const auto compare = [] (const juce::Image& a, const juce::Image& b, int& worst) {
        const juce::Image::BitmapData p (a, juce::Image::BitmapData::readOnly), q (b, juce::Image::BitmapData::readOnly);
        int n = 0;
        for (int y = 0; y < p.height; ++y)
            for (int x = 0; x < p.width; ++x)
            {
                const auto c = p.getPixelColour (x, y), d = q.getPixelColour (x, y);
                const auto delta = juce::jmax (std::abs ((int) c.getRed() - (int) d.getRed()), std::abs ((int) c.getGreen() - (int) d.getGreen()),
                                               std::abs ((int) c.getBlue() - (int) d.getBlue()), std::abs ((int) c.getAlpha() - (int) d.getAlpha()));
                n += delta > 0 ? 1 : 0;
                worst = juce::jmax (worst, delta);
            }
        return n;
    };
    // LYNX_TILE_DEBUG=<folder>: the pictures as PNGs there
    const auto debugFolder = juce::SystemStats::getEnvironmentVariable ("LYNX_TILE_DEBUG", {});
    const auto save = [&debugFolder] (const juce::Image& image, const juce::String& name) {
        if (debugFolder.isEmpty())
            return;
        const juce::File file (debugFolder + "/" + name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    // the cached bodies must give the straight pixels on JUCE's software renderer (round 190mh: the atlas is on there) and
    // on Direct2D; CoreGraphics (opt-in) is only reported (stderr). Where the native kind is the software renderer
    // (Linux), it is compared once.
    const auto nativeIsSoftware = [] {
        juce::Image probe (juce::Image::ARGB, 4, 4, true, juce::NativeImageType());
        juce::Graphics g (probe);
        return dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr;
    }();
    juce::String failures, info;
    const auto softwareBefore = tileCacheOnSoftwareSetting();
    const auto modeBefore = tileModeSetting();
    tileModeSetting() = 2; // (the atlas, opt-in in the app)
    for (const auto native : { true, false })
        for (const auto scale : { 1, 2 })
        {
            if (native && nativeIsSoftware)
                continue;
            tileCacheOnSoftwareSetting() = ! native;
            const auto tag = juce::String (native ? "native" : "software") + "-x" + juce::String (scale);
            for (auto* b : { &rest, &on, &off, &corner })
                b->setFlTileBackdrop (nullptr);
            const auto straight = render (scale, native), again = render (scale, native);
            // (round 190mh) frames whose clip ends at a button's right edge (its own repaint), inside one (a column: painted
            // straight) and past one, painted straight first
            const juce::Rectangle<int> clips[] { rest.getBounds(), { 20, 0, 30, parent.getHeight() }, { 70, 0, 40, parent.getHeight() } };
            std::vector<juce::Image> straightClipped;
            for (const auto& c : clips)
                straightClipped.push_back (render (scale, native, c));
            for (auto* b : { &rest, &on, &off, &corner })
                b->setFlTileBackdrop ([] (juce::Rectangle<int>) { return (std::uint64_t) 42; }, [&parent] { return parent.clip; });
            const auto made = render (scale, native), blitted = render (scale, native); // (the first paint fills the atlas)
            const auto restUsed = rest.atlasKey != 0;
            int worst = 0, worstAgain = 0;
            auto n = compare (straight, made, worst) + compare (straight, blitted, worst);
            for (std::size_t c = 0; c < straightClipped.size(); ++c)
                n += compare (straightClipped[c], render (scale, native, clips[c]), worst) + compare (straight, render (scale, native), worst);
            tileCacheOnSoftwareSetting() = softwareBefore;
            if (! restUsed)
            {
                tileModeSetting() = modeBefore;
                return "ERROR the FL tile cache was not used (" + tag + ")";
            }
            const auto nAgain = compare (straight, again, worstAgain);
            save (straight, "tile-" + tag + "-straight");
            save (made, "tile-" + tag + "-cached");
            save (IconButton::tileAtlasFor (parent, 35, scale, ! native).image, "tile-" + tag + "-atlas");
            // (rollup 0.1.30 step 7: the exact match is required of Direct2D, which round 190ki measured the atlas on;
            // macOS's CoreGraphics came out 1 level apart at 2 pixels of native-x1 on CI, so there the difference is only
            // reported — the atlas is opt-in there, LYNX_TILE_MODE=2, and the app paints straight. Round 190mh: required of
            // JUCE's software renderer too, where the atlas is on by default)
           #if JUCE_WINDOWS
            constexpr bool nativeExactRequired = true;
           #else
            constexpr bool nativeExactRequired = false;
           #endif
            if (n > 0 || nAgain > 0)
                (! native || nativeExactRequired ? failures : info) << tag << ": " << n << " pixels differ, up to " << worst << " levels (straight twice: " << nAgain
                                                                    << ", " << worstAgain << "); ";
        }
    tileModeSetting() = modeBefore;
    if (info.isNotEmpty())
        std::fprintf (stderr, "fl tiles on CoreGraphics (painted straight in the app): %s\n", info.toRawUTF8());
    if (failures.isNotEmpty())
        return "ERROR cached FL tiles differ from the tiles painted straight: " + failures;
    return "OK";
}
} // namespace lynx
