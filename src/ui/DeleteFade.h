#pragma once

// Issue #250 (round 190oq): FL Studio's fade of deleted clips and notes (REA_rodada190oq §6, FLEngine FUN_00e8fc60 /
// FUN_00e87e60 / FUN_00e884b0 / FUN_00e88030). With Animations at "Make it pretty" (FL's default) a clip or note removed
// by Delete, Cut or an erase stroke (the right-click delete, the Delete tool) leaves a "ghost":
//  - its colour is the item's, its HSL lightness held between 0.55 and 0.80 (hue and saturation kept);
//  - it starts on the item's rectangle and grows outward to 4 px per side as it fades, never moving;
//  - a rounded rectangle (corner radius at most half the shorter side, at most 8 px): outline (3·a + 0.5) px wide at
//    opacity 224·a / 255, fill at opacity (224·a − 96) / 255 while that is above 0, drawn above the remaining items;
//  - its opacity a starts at 1 and falls as 0.01^(t / T): T = 1000 ms in the Playlist, 500 ms in the Piano roll; it goes
//    below 0.025 (about 801 / 401 ms).
// LYNX's Settings › General › Animations is FL's switch (on = FL's default level; off = "Keep it sober": no ghost);
// "Reduced effects" turns it off too. Cost rules (ui/Motion.h): no ghost, no timer, no paint — the views draw exactly as
// before; the timer runs only while a ghost is fading and repaints only the ghosts' area. Message thread only.

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ui
{
class DeleteFade final : private juce::Timer
{
public:
    static constexpr double playlistMs = 1000.0, pianoRollMs = 500.0; // FL's T per editor
    static constexpr double dropAlpha = 0.025;

    DeleteFade (juce::Component& owner, double decayMs) : view (owner), decay (decayMs) {}
    ~DeleteFade() override { stopTimer(); }

    // Whether a deletion fades now (Animations on, Reduced effects off)
    static bool enabled();
    // FL's ghost colour: `c` with its HSL lightness held in 0.55..0.80
    static juce::Colour ghostColour (juce::Colour c);
    // The ghost's opacity `ms` after the deletion (0 once it is gone)
    double alphaAt (double ms) const noexcept;
    // How long a ghost lasts (until its opacity drops below 0.025)
    double lifetimeMs() const noexcept;

    // A deleted item's area (owner coordinates) and colour; nothing when fades are off or the owner is hidden
    void add (juce::Rectangle<float> area, juce::Colour colour);
    // The ghosts, over the owner's own drawing (clipped to `clip` when not empty)
    void paint (juce::Graphics& g, juce::Rectangle<int> clip = {}) const;
    bool active() const noexcept { return ! ghosts.empty(); }
    // Ends every fade now
    void clear();

    // (tests / shots) the ghosts drawn `ms` after their deletion, timer stopped
    void freezeForSnapshot (double ms);
    int count() const noexcept { return (int) ghosts.size(); }

private:
    struct Ghost
    {
        juce::Rectangle<float> area;
        juce::Colour colour;
        double start = 0.0;
    };

    void timerCallback() override;
    juce::Rectangle<int> dirtyArea() const;
    double ageOf (const Ghost& g) const;

    juce::Component& view;
    double decay;
    std::vector<Ghost> ghosts;
    double frozen = -1.0; // (snapshots) the fixed age, < 0 = live
};
} // namespace lynx::ui
