#pragma once

// "Clean voice" (round 190ch, LYNX's own): one button that puts a classic vocal clean-up chain at the start of a
// track's insert. It is a preset of LYNX's own effects, nothing more: every slot is an ordinary slot named
// "Clean voice" that can be opened, changed, switched off or removed in the Mixer. No model, no service: plain DSP.
//
//   (optional) Quell Denoiser   steady background noise down, lightly (a multiband downward expander, core/VoiceTools.h)
//   Facet EQ                    high-pass at 80 Hz, 12 dB/oct: rumble, pops and handling noise out
//   Sentry Gate                 a downward expander, 2:1 below −36 dB, at most 12 dB: the pauses get quieter, not cut
//   Hush De-esser               harsh "s" sounds held back, up to 6 dB
//   Crest Compressor            gentle levelling: Clean style, 2.5:1 from −18 dB, soft knee, 2 dB of make-up
//
// LYNX has no real-time spectral denoiser: the noise-profile denoiser is a tool of the audio editor (core/
// AudioEditTools.h), so the optional first slot is the multiband one. No neural model (decision for 0.1.27). No JUCE:
// tested in tests/BandlabExtras190chTests.inc.

#include <algorithm>
#include <string>
#include <vector>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"
#include "core/MixerOps.h"
#include "core/Song.h"
#include "core/VoiceTools.h"

namespace lynx::cleanvoice
{
constexpr const char* slotName = "Clean voice"; // EffectSlot::presetName of the chain's slots (how they are found again)

struct Options
{
    bool noiseReduction = false; // the Quell Denoiser slot in front
    bool operator== (const Options&) const = default;
};

// The settings, in real units (the tests read them back through the effects' own laws)
constexpr double highPassHz = 80.0;
constexpr int highPassSlope = 12;          // dB per octave
constexpr double expanderThresholdDb = -36.0, expanderRatio = 2.0, expanderRangeDb = 12.0;
constexpr double expanderAttackMs = 2.0, expanderHoldMs = 80.0, expanderReleaseMs = 220.0;
constexpr double deEssHz = 6000.0, deEssThresholdDb = -30.0, deEssRangeDb = 6.0;
constexpr double compThresholdDb = -18.0, compRatio = 2.5, compKneeDb = 12.0, compAttackMs = 10.0, compReleaseMs = 140.0, compMakeUpDb = 2.0;
constexpr float noiseAmount = 0.3f;

namespace detail
{
// a value 0…1 whose law gives `wanted` (the law rises or falls steadily over 0…1)
template <typename Law>
float valueFor (Law law, double wanted) noexcept
{
    double lo = 0.0, hi = 1.0;
    const auto rising = law (1.0f) >= law (0.0f);
    for (int i = 0; i < 40; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        if ((law ((float) mid) < wanted) == rising)
            lo = mid;
        else
            hi = mid;
    }
    return (float) (0.5 * (lo + hi));
}
} // namespace detail

inline std::vector<EffectSlot> chain (const Options& options = {})
{
    using detail::valueFor;
    std::vector<EffectSlot> out;
    if (options.noiseReduction)
    {
        auto quell = makeEffectSlot (EffectKind::quellDenoiser);
        quell.params[voicetools::quell::amount] = noiseAmount;
        out.push_back (std::move (quell));
    }
    {
        auto eq = makeEffectSlot (EffectKind::facetEq);
        eq.params[(std::size_t) facet::param (0, 0)] = facet::shapeValue (facet::Shape::lowCut);
        eq.params[(std::size_t) facet::param (0, 1)] = facet::hzValue (highPassHz);
        eq.params[(std::size_t) facet::param (0, 3)] = facet::qValue (0.71);
        eq.params[(std::size_t) facet::param (0, 4)] = facet::slopeValue (highPassSlope);
        out.push_back (std::move (eq));
    }
    {
        namespace s = dyn::sentry;
        auto gate = makeEffectSlot (EffectKind::sentryGate);
        gate.params[s::style] = s::styleValue (s::Style::clean);
        gate.params[s::threshold] = valueFor ([] (float v) { return s::thresholdDb (v); }, expanderThresholdDb);
        gate.params[s::ratio] = valueFor ([] (float v) { return s::ratioOf (v); }, expanderRatio);
        gate.params[s::range] = valueFor ([] (float v) { return s::rangeDb (v); }, expanderRangeDb);
        gate.params[s::attack] = valueFor ([] (float v) { return s::attackMs (v); }, expanderAttackMs);
        gate.params[s::hold] = valueFor ([] (float v) { return s::holdMs (v); }, expanderHoldMs);
        gate.params[s::release] = valueFor ([] (float v) { return s::releaseMs (v); }, expanderReleaseMs);
        gate.params[s::knee] = valueFor ([] (float v) { return s::kneeDb (v); }, 12.0);
        out.push_back (std::move (gate));
    }
    {
        namespace h = mixsuite::hush;
        auto deEss = makeEffectSlot (EffectKind::hushDeEsser);
        const auto extended = h::extended (deEss.params);
        deEss.params[h::frequency] = h::hpValue (deEssHz, extended);
        deEss.params[h::threshold] = valueFor ([] (float v) { return h::thresholdDb (v); }, deEssThresholdDb);
        deEss.params[h::range] = valueFor ([] (float v) { return h::rangeDb (v); }, deEssRangeDb);
        out.push_back (std::move (deEss));
    }
    {
        namespace c = dyn::crest;
        auto comp = makeEffectSlot (EffectKind::crestCompressor);
        comp.params[c::style] = c::styleValue (c::Style::clean);
        comp.params[c::threshold] = valueFor ([] (float v) { return c::thresholdDb (v); }, compThresholdDb);
        comp.params[c::ratio] = c::ratioValue (compRatio);
        comp.params[c::knee] = valueFor ([] (float v) { return c::kneeDb (v); }, compKneeDb);
        comp.params[c::attack] = c::attackValue (compAttackMs);
        comp.params[c::release] = c::releaseValue (compReleaseMs);
        comp.params[c::output] = valueFor ([] (float v) { return c::trimDb (v); }, compMakeUpDb);
        out.push_back (std::move (comp));
    }
    for (auto& slot : out)
        slot.presetName = slotName;
    return out;
}

inline bool isCleanSlot (const EffectSlot& slot) noexcept { return slot.presetName == slotName && slot.kind != EffectKind::plugin; }

inline std::vector<int> slotsOf (const Song& song, int insert)
{
    std::vector<int> idx;
    if (insert >= 0 && insert < (int) song.mixer.size())
        for (int i = 0; i < (int) song.mixer[(std::size_t) insert].effects.size(); ++i)
            if (isCleanSlot (song.mixer[(std::size_t) insert].effects[(std::size_t) i]))
                idx.push_back (i);
    return idx;
}

inline bool present (const Song& song, int insert) { return ! slotsOf (song, insert).empty(); }

// the chain on the insert includes the noise-reduction slot
inline bool hasNoiseReduction (const Song& song, int insert)
{
    for (const auto i : slotsOf (song, insert))
        if (song.mixer[(std::size_t) insert].effects[(std::size_t) i].kind == EffectKind::quellDenoiser)
            return true;
    return false;
}

// Takes the chain's slots off the insert (the other effects keep their places; automation follows them).
inline void remove (Song& song, int insert)
{
    auto idx = slotsOf (song, insert);
    for (auto it = idx.rbegin(); it != idx.rend(); ++it)
        mixerops::removeEffectKeepRows (song, insert, *it);
}

// How many slots the insert still has room for, counting the clean-voice slots it already holds as free.
inline int freeSlots (const Song& song, int insert)
{
    if (insert < 0 || insert >= (int) song.mixer.size())
        return 0;
    return maxEffectSlots - (int) song.mixer[(std::size_t) insert].effects.size() + (int) slotsOf (song, insert).size();
}

// Puts the chain at the START of the insert's effects (cleaning comes before everything else on the track); a chain
// already there is replaced. False (nothing changed) on the Master, on a missing insert, or without room for it.
inline bool apply (Song& song, int insert, const Options& options = {})
{
    if (insert < 1 || insert >= (int) song.mixer.size())
        return false;
    auto slots = chain (options);
    if ((int) slots.size() > freeSlots (song, insert))
        return false;
    remove (song, insert);
    auto& fx = song.mixer[(std::size_t) insert].effects;
    // each slot is added at the end and moved to the front: the other effects keep their order, and their automation
    // follows them (mixerops::moveEffectSlot)
    int at = 0;
    for (auto& slot : slots)
    {
        fx.push_back (std::move (slot));
        const auto last = (int) fx.size() - 1;
        if (last != at)
            mixerops::moveEffectSlot (song, insert, last, at);
        ++at;
    }
    return true;
}
} // namespace lynx::cleanvoice
