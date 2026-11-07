#pragma once

// --selftest: the mixer's basics that were FL 2025 regressions (the wheel scrolls the strips, slots reorder by drag, a
// fader never jumps) driven through the real components with synthetic mouse events. Offscreen, no injected input.
// Returns "OK" or the failures. Round 177a (39 pain points, item 5).

#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/AudioEngine.h"
#include "ui/MixerComponent.h"

namespace lynx::mixerbasics
{
inline juce::String run()
{
    juce::StringArray failures;
    const auto check = [&failures] (bool ok, const char* what) { if (! ok) failures.add (what); };

    AudioEngine engine (false);
    engine.editSong ([] (Song& s) {
        if (s.mixer.size() < 30)
            s.mixer = Song::defaultMixer (30);
        s.mixer[3].volume = unityFader;
        s.mixer[3].effects.clear();
        s.mixer[3].effects.push_back (makeEffectSlot (EffectKind::delay));
        s.mixer[3].effects.push_back (makeEffectSlot (EffectKind::compressor));
    });
    const auto volume = [&engine] (int t) { return engine.getSong().mixer[(std::size_t) t].volume; };
    const auto near = [] (float a, float b, float tolerance = 1.0e-4f) { return std::abs (a - b) < tolerance; };

    const auto at = [] (juce::Component& c, juce::Point<int> p, juce::Point<int> down, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down.toFloat(), now, 1, false);
    };

    MixerStrips strips (engine);
    juce::Viewport view;
    view.setViewedComponent (&strips, false);
    strips.setVisibleHeight (700);
    view.setBounds (0, 0, 300, 700);

    const auto rail = strips.faderRailForSnapshot (3);
    const auto capAt = [&rail] (float v) { return juce::roundToInt ((float) rail.getBottom() - v * (float) rail.getHeight()); };
    const juce::Point<int> below (rail.getCentreX(), rail.getBottom() - 4), above (rail.getCentreX(), rail.getY() + 2);

    // a press on the rail: one step towards the mouse, no jump; held, it stops at the mouse; dragging moves the target
    strips.mouseDown (at (strips, below, below));
    check (near (volume (3), unityFader - faderRailStep), "rail press below: not one step");
    strips.mouseUp (at (strips, below, below));
    check (near (volume (3), unityFader - faderRailStep), "rail release moved the fader");
    strips.mouseDown (at (strips, above, above));
    check (near (volume (3), unityFader), "rail press above: not one step");
    strips.mouseDrag (at (strips, below, above));
    for (int i = 0; i < 500; ++i)
        strips.stepHeldRail();
    check (std::abs (capAt (volume (3)) - below.y) <= 1, "rail hold doesn't stop at the mouse");
    strips.mouseUp (at (strips, below, above));
    const auto held = volume (3);
    strips.stepHeldRail();
    check (near (volume (3), held), "the rail goes on after the release");

    // a press on the cap never moves the fader, and a drag is relative to the press
    engine.editSong ([] (Song& s) { s.mixer[3].volume = unityFader; });
    const juce::Point<int> cap (rail.getCentreX(), capAt (unityFader) + 3);
    strips.mouseDown (at (strips, cap, cap));
    strips.mouseUp (at (strips, cap, cap));
    check (near (volume (3), unityFader), "a click on the cap moved the fader");
    strips.mouseDown (at (strips, cap, cap));
    strips.mouseDrag (at (strips, cap.translated (0, 20), cap));
    check (volume (3) < unityFader && std::abs (volume (3) - unityFader) < 0.5f, "a cap drag jumped");
    strips.mouseUp (at (strips, cap.translated (0, 20), cap));

    // the wheel: over a fader it moves that fader one FL step; over the rest of a strip it scrolls the mixer
    engine.editSong ([] (Song& s) { s.mixer[3].volume = unityFader; });
    const juce::MouseWheelDetails notch { 0.0f, 120.0f * 0.5f / 256.0f, false, false, false };
    strips.mouseWheelMove (at (strips, cap, cap, {}), notch);
    check (near (volume (3), unityFader + 0.02f, 1.0e-3f), "the wheel over a fader isn't one step");
    const juce::MouseWheelDetails down { 0.0f, -120.0f * 0.5f / 256.0f, false, false, false };
    view.setViewPosition (0, 0);
    const auto others = volume (3);
    const juce::Point<int> body (rail.getCentreX(), strips.getHeight() - 4);
    for (int i = 0; i < 5; ++i)
        strips.mouseWheelMove (at (strips, body, body, {}), down);
    check (view.getViewPositionX() > 0, "the wheel doesn't scroll the mixer");
    check (near (volume (3), others), "the wheel that scrolls moved a fader");

    // an effect slot reordered by dragging its name
    EffectsPanel panel (engine);
    panel.setBounds (0, 0, 300, 700);
    panel.setTrack (3);
    const auto kindAt = [&engine] (int slot) { return engine.getSong().mixer[3].effects[(std::size_t) slot].kind; };
    const auto first = kindAt (0), second = kindAt (1);
    const auto from = panel.slotBoundsForCheck (0).getCentre(), to = panel.slotBoundsForCheck (1).getCentre();
    panel.mouseDown (at (panel, from, from));
    panel.mouseDrag (at (panel, to, from));
    panel.mouseUp (at (panel, to, from));
    check (kindAt (0) == second && kindAt (1) == first, "dragging a slot doesn't reorder it");

    return failures.isEmpty() ? juce::String ("OK") : "ERROR " + failures.joinIntoString ("; ");
}
} // namespace lynx::mixerbasics
