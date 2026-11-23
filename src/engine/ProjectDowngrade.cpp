#include "engine/ProjectDowngrade.h"

#include "engine/ProjectFile.h"

#include "core/ApexLimiter.h"
#include "core/ChordLane.h"
#include "core/DirectWaveState.h"
#include "core/DynamicsSuite.h"
#include "core/EchoSuite.h"
#include "core/FacetEq.h"
#include "core/GeneratorPresets.h"
#include "core/Harmor.h"
#include "core/KiloheartsDsp.h"
#include "core/Effects.h"
#include "core/LimiterLaw.h"
#include "core/LowLifterFl.h"
#include "core/LuxeVerb.h"
#include "core/LynxTune.h"
#include "core/LynxVerbs.h"
#include "core/MixSuite.h"
#include "core/Ogun.h"
#include "core/SytrusEngine.h"
#include "core/NewtoneFx.h"
#include "core/PeakShapes.h"
#include "core/RackRenderer.h"
#include "core/TrackVersions.h"
#include "core/Reeverb2.h"
#include "core/VoiceShift.h"
#include "core/VoiceTools.h"
#include "core/WavetableSynth.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

// TR_NOOP marks the loss texts for translation (tools/i18n_keys.py); the UI translates them
#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::olderlynx
{
namespace
{
using Losses = std::vector<Loss>;

juce::DynamicObject* objectOf (const juce::var& v) { return v.getDynamicObject(); }
juce::Array<juce::var>* arrayOf (const juce::var& v) { return v.getArray(); }

bool drop (juce::DynamicObject& o, const char* key)
{
    if (! o.hasProperty (key))
        return false;
    o.removeProperty (key);
    return true;
}

juce::String channelName (const juce::var& c, int index)
{
    const auto name = c["name"].toString();
    return name.isNotEmpty() ? "\"" + name + "\"" : "#" + juce::String (index + 1);
}

juce::String insertName (const juce::var& mixer, int track)
{
    // (language-neutral: "#3 \"Kick\"", the Master as "#0")
    const auto name = mixer[track]["name"].toString();
    return "#" + juce::String (track) + (name.isNotEmpty() ? " \"" + name + "\"" : juce::String());
}

// "a, b, c" (at most four, then "…")
juce::String listOf (const juce::StringArray& names)
{
    juce::StringArray shown;
    for (int i = 0; i < names.size() && i < 4; ++i)
        shown.add (names[i]);
    return shown.joinIntoString (", ") + (names.size() > 4 ? juce::String::fromUTF8 (", \u2026") : juce::String());
}

void forEachChannel (const juce::var& root, const std::function<void (juce::DynamicObject&, int)>& fn)
{
    if (auto* channels = arrayOf (root["channels"]))
        for (int i = 0; i < channels->size(); ++i)
            if (auto* c = objectOf ((*channels)[i]))
                fn (*c, i);
}

// every clip array of the project: the Playlist, the stored arrangements, the clip grid's slots
void forEachClip (const juce::var& root, const std::function<void (juce::Array<juce::var>&)>& fn)
{
    auto list = [&] (const juce::var& clips) {
        if (auto* a = arrayOf (clips))
            for (auto& c : *a)
                if (auto* fields = arrayOf (c))
                    fn (*fields);
    };
    list (root["playlist"]);
    if (auto* arrangements = arrayOf (root["arrangements"]))
        for (const auto& a : *arrangements)
            list (a["clips"]);
    if (auto* slots = arrayOf (root["clipGrid"]["clips"]))
        for (const auto& s : *slots)
            list (s["clip"]);
}

// Every effect object below `v` (the mixer's slots, a Patcher's nodes, a rack's branches): `fn (effect, where)`, `where`
// the insert ("#3 \"Vox\"") of a mixer slot, else "Patcher"
void forEachEffect (const juce::var& root, const std::function<void (juce::DynamicObject&, const juce::String&)>& fn)
{
    std::function<void (const juce::var&, const juce::String&)> walk = [&] (const juce::var& v, const juce::String& where) {
        if (auto* a = arrayOf (v))
        {
            for (auto& item : *a)
                walk (item, where);
            return;
        }
        auto* o = objectOf (v);
        if (o == nullptr)
            return;
        if (o->getProperty ("kind").isString() && o->getProperty ("params").isArray())
            fn (*o, where);
        for (auto& p : o->getProperties())
            walk (p.value, "Patcher");
    };
    if (auto* mixer = arrayOf (root["mixer"]))
        for (int t = 0; t < mixer->size(); ++t)
            if (auto* fx = arrayOf ((*mixer)[t]["effects"]))
                for (auto& e : *fx)
                {
                    if (auto* o = objectOf (e); o != nullptr && o->getProperty ("kind").isString() && o->getProperty ("params").isArray())
                        fn (*o, insertName (root["mixer"], t));
                    if (auto* o = objectOf (e))
                        for (auto& p : o->getProperties())
                            walk (p.value, "Patcher");
                }
    if (auto* channels = arrayOf (root["channels"]))
        for (auto& c : *channels)
            if (auto* o = objectOf (c))
                for (auto& p : o->getProperties())
                    walk (p.value, "Patcher");
}

void addOnce (juce::StringArray& list, const juce::String& item)
{
    if (! list.contains (item))
        list.add (item);
}

void flattenEffects (juce::var& root, const juce::StringArray& kinds, const char* loss, Losses& lost);

// Rollup 0.1.30 (round 190il): what the other 0.1.30 rounds save that 0.1.28 cannot hold (round 190hq's step above
// knew only its own). Each loss is reported only when the project has it.
void toRelease0128FromRollup030 (juce::var& root, Losses& lost)
{
    auto& o = *objectOf (root);

    // round 190kk: the engines Live's Erosion (and the next lot) open on are not in 0.1.28
    flattenEffects (root, { "corrode" },
                    TR_NOOP ("Corrode (an imported Live Erosion) is not in this release: it becomes a switched-off Utility in its slot, and its automation does nothing ({0})"),
                    lost);
    flattenEffects (root, { "platter" },
                    TR_NOOP ("Platter (an imported Live Vinyl Distortion) is not in this release: it becomes a switched-off Utility in its slot, and its automation does nothing ({0})"),
                    lost);
    flattenEffects (root, { "soundboard" },
                    TR_NOOP ("Soundboard (an imported Live Corpus) is not in this release: it becomes a switched-off Utility in its slot, and its automation does nothing ({0})"),
                    lost);
    // round 190ko: Live's Spectral Time / Spectral Resonator engines
    flattenEffects (root, { "timeprism" },
                    TR_NOOP ("Timeprism (an imported Live Spectral Time) is not in this release: it becomes a switched-off Utility in its slot, and its automation does nothing ({0})"),
                    lost);
    flattenEffects (root, { "harmonia" },
                    TR_NOOP ("Harmonia (an imported Live Spectral Resonator) is not in this release: it becomes a switched-off Utility in its slot, and its automation does nothing ({0})"),
                    lost);

    // channels: Tetra (round 190hs) is not in 0.1.28, which would play its channels as its basic synth: an empty Sampler
    // (silent) keeps the channel, its notes and its routing; DirectWave states (rounds 190hz / 190ii) get their short zone
    // records completed as DirectWave reads them, since 0.1.28 skipped them (FL's version-36 programs lost their loops)
    {
        juce::StringArray tetra, helix, braid;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            const auto instrument = c.getProperty ("instrument").toString();
            if (instrument == "tetra" || instrument == "helix" || instrument == "braid") // (rollup step 3: Helix, round 190ja, as Tetra; Braid, 190ko; LYNX Model, 190ni: the 0.1.30 step)
            {
                (instrument == "tetra" ? tetra : instrument == "helix" ? helix : braid).add (channelName (juce::var (&c), i));
                c.setProperty ("instrument", "sampler");
                drop (c, "modelParams");
                drop (c, "pluginState");
                drop (c, "samplePath");
            }
            else if (instrument == "directwave")
            {
                juce::MemoryOutputStream decoded;
                if (! juce::Base64::convertFromBase64 (decoded, c.getProperty ("pluginState").toString()))
                    return;
                const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
                auto doc = dw::readDoc (std::vector<std::uint8_t> (bytes, bytes + decoded.getDataSize()));
                if (doc && dw::completeShortZoneRecords (*doc) > 0)
                {
                    const auto state = dw::writeDoc (*doc);
                    c.setProperty ("pluginState", juce::Base64::toBase64 (state.data(), state.size()));
                }
            }
        });
        if (! tetra.isEmpty())
            lost.push_back ({ TR_NOOP ("Tetra is not in this release: its channels become empty Samplers and play nothing: {0}"), listOf (tetra), true });
        if (! helix.isEmpty())
            lost.push_back ({ TR_NOOP ("Helix is not in this release: its channels become empty Samplers and play nothing: {0}"), listOf (helix), true });
        if (! braid.isEmpty())
            lost.push_back ({ TR_NOOP ("Braid is not in this release: its channels become empty Samplers and play nothing: {0}"), listOf (braid), true });
    }

    // notes: index 19 holds a note's MPE curves (round 190hy); rollup step 11: index 20 its FL note repeat (round 190ma,
    // issue #208), written with a null curves slot when the note has no curves
    {
        int curved = 0, repeated = 0;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
                if (auto* notes = arrayOf (p["notes"]))
                    for (auto& n : *notes)
                        if (auto* fields = arrayOf (n); fields != nullptr && fields->size() > 19)
                        {
                            if (! (*fields)[19].isVoid() && ! (*fields)[19].isUndefined())
                                ++curved;
                            if (fields->size() > 20 && (int) (*fields)[20] != 0)
                                ++repeated;
                            fields->removeRange (19, fields->size() - 19);
                        }
        if (curved > 0)
            lost.push_back ({ TR_NOOP ("{0} note(s) lose their MPE curves (pitch bend, slide, pressure)"), juce::String (curved), true });
        if (repeated > 0)
            lost.push_back ({ TR_NOOP ("{0} note(s) play once instead of repeating (FL's note Repeat)"), juce::String (repeated), true });
    }

    // Pattern Blocks: a third element marks a muted block (round 190ht); 0.1.28 reads only [pattern, start] and drops
    // any other block. A muted block plays nothing, so it is left out (the sound stays); the rest keep their two.
    {
        int muted = 0;
        auto blocks = [&muted] (const juce::var& list) {
            if (auto* a = arrayOf (list))
                for (int i = a->size(); --i >= 0;)
                    if (auto* b = arrayOf ((*a)[i]); b != nullptr && b->size() > 2)
                    {
                        if ((int) (*b)[2] != 0)
                        {
                            a->remove (i);
                            ++muted;
                        }
                        else
                            b->removeRange (2, b->size() - 2);
                    }
        };
        blocks (root["blocks"]);
        if (auto* arrangements = arrayOf (root["arrangements"]))
            for (const auto& a : *arrangements)
                blocks (a["blocks"]);
        if (muted > 0)
            lost.push_back ({ TR_NOOP ("{0} muted Pattern Block(s) are left out (they play nothing)"), juce::String (muted), false });
    }

    // clips: index 13 is the clip's pitch, ±48 semitones since round 190hr (0.1.28 holds ±12)
    {
        int transposed = 0;
        forEachClip (root, [&] (juce::Array<juce::var>& clip) {
            if (clip.size() > 13)
                if (const auto pitch = (double) clip[13]; std::abs (pitch) > 12.0)
                {
                    clip.set (13, juce::jlimit (-12.0, 12.0, pitch));
                    ++transposed;
                }
        });
        if (transposed > 0)
            lost.push_back ({ TR_NOOP ("{0} clip(s) transposed by more than 12 semitones play at ±12"), juce::String (transposed), true });
    }

    // clip slots (round 190hr): the Start marker inside the loop, the clip's own scale
    {
        int markers = 0, scales = 0;
        if (auto* slots = arrayOf (root["clipGrid"]["clips"]))
            for (auto& slot : *slots)
                if (auto* so = objectOf (slot))
                {
                    markers += drop (*so, "startMarker") ? 1 : 0;
                    scales += drop (*so, "clipScale") ? 1 : 0;
                }
        if (markers > 0)
            lost.push_back ({ TR_NOOP ("{0} clip slot(s) launch from their loop start (the Start marker inside the loop)"), juce::String (markers), true });
        if (scales > 0)
            lost.push_back ({ TR_NOOP ("Clip scales (Scale Mode) of {0} clip slot(s)"), juce::String (scales), false });
    }

    // effects (round 190ho): Voice Shift's Measured law revision 1, Ridge Strip's 4K Pink EQ; (round 190iq) Hush De-esser's
    // measured Allround timing (hidden parameters 0.1.28 ignores: it plays revision 0, Black, and the earlier timing)
    {
        juce::StringArray thump, pink, consoleFilters, lineLaw, hybridLoop, valhallaLoop, luxeSpace, hushTiming, hushVocal, echoVoicings, latticeTaps, luxeDucking, verbRevisions,
            omni, hushRen, gLaw;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr) // (a slot written without parameters)
                return;
            const auto kind = e.getProperty ("kind").toString();
            if (kind == "voiceShift" && params->size() > voiceshift::revision)
            {
                if ((float) (double) (*params)[voiceshift::law] >= 0.5f && (float) (double) (*params)[voiceshift::revision] >= 0.5f)
                    addOnce (thump, where);
                params->removeRange (voiceshift::revision, params->size() - voiceshift::revision);
            }
            else if (kind == "ridgeStrip" && params->size() > mixsuite::ridge::k4Pink)
            {
                if ((float) (double) (*params)[mixsuite::ridge::k4Pink] >= 0.5f)
                    addOnce (pink, where);
                // rollup step 12: round 190mm's omni law [100 …] (set only by the Waves Scheps Omni Channel translator)
                if (params->size() > mixsuite::ridge::omniLaw && (float) (double) (*params)[mixsuite::ridge::omniLaw] >= 0.5f)
                    addOnce (omni, where);
                // rollup step 13: round 190mt's G-Channel EQ law [93 …] (set only by the Waves SSL G-Channel translator)
                if (params->size() > mixsuite::ridge::gEqLaw && (float) (double) (*params)[mixsuite::ridge::gEqLaw] >= 0.5f)
                    addOnce (gLaw, where);
                // rollup step 8: round 190lb's SSL 4K G / 4K E filters (and the G's ceiling), round 190le's 32C voicing
                for (const auto hidden : { mixsuite::ridge::k4gLaw, mixsuite::ridge::k4eHpLaw, mixsuite::ridge::h32 })
                    if (params->size() > hidden && (float) (double) (*params)[hidden] >= 0.5f)
                        addOnce (consoleFilters, where);
                params->removeRange (mixsuite::ridge::k4Pink, params->size() - mixsuite::ridge::k4Pink);
            }
            else if (kind == "hushDeEsser" && params->size() > mixsuite::hush::timing)
            {
                // rollup step 12: round 190mm's Renaissance de-esser law [essLaw …] (set only by the RDeEsser translator)
                if (params->size() > mixsuite::hush::essLaw && (float) (double) (*params)[mixsuite::hush::essLaw] >= 0.5f)
                    addOnce (hushRen, where);
                // (the timing is the Allround law's: Single Vocal plays the same either way)
                if ((float) (double) (*params)[mixsuite::hush::timing] >= 0.5f && (float) (double) (*params)[mixsuite::hush::allround] >= 0.5f)
                    addOnce (hushTiming, where);
                // rollup step 5, round 190js: Single Vocal's "stands out" revision (hidden, after the timing; Allround ignores it)
                if (params->size() > mixsuite::hush::vocalRevision && (float) (double) (*params)[mixsuite::hush::vocalRevision] >= 0.5f
                    && (float) (double) (*params)[mixsuite::hush::allround] < 0.5f)
                    addOnce (hushVocal, where);
                params->removeRange (mixsuite::hush::timing, params->size() - mixsuite::hush::timing);
            }
            // rollup step 3: round 190is's Echoform voicings revision (heard only with an echo voicing), round 190it's
            // reverb revisions (the Measured law's: Law 1), Lattice Mod's tap network and LuxeVerb's Space ducking
            else if (kind == "echoform" && params->size() > echosuite::echoform::revision)
            {
                if ((float) (double) (*params)[echosuite::echoform::revision] >= 0.5f && (float) (double) (*params)[echosuite::echoform::voicing] >= 0.5f)
                    addOnce (echoVoicings, where);
                // rollup step 8: round 190kw's line law (set only by a translated FabFilter Timeless 3 state)
                if (params->size() > echosuite::echoform::lineLaw && (float) (double) (*params)[echosuite::echoform::lineLaw] >= 0.5f)
                    addOnce (lineLaw, where);
                // rollup step 9: round 190lf's H-Delay loop law (set only by a translated Waves H-Delay state; read with the
                // hybrid engine on)
                if (params->size() > echosuite::echoform::hybridLaw && (float) (double) (*params)[echosuite::echoform::hybridLaw] >= 0.5f)
                    addOnce (hybridLoop, where);
                // rollup step 10: round 190ly's ValhallaDelay loop law (set only by a translated ValhallaDelay; without a voicing)
                if (params->size() > echosuite::echoform::valhallaLaw && (float) (double) (*params)[echosuite::echoform::valhallaLaw] >= 0.5f)
                    addOnce (valhallaLoop, where);
                params->removeRange (echosuite::echoform::revision, params->size() - echosuite::echoform::revision);
            }
            else if (kind == "latticeMod" && params->size() > echosuite::lattice::revision)
            {
                if ((float) (double) (*params)[echosuite::lattice::revision] >= 0.5f)
                    addOnce (latticeTaps, where);
                params->removeRange (echosuite::lattice::revision, params->size() - echosuite::lattice::revision);
            }
            else if (kind == "luxeVerb" && params->size() > luxeVerbSpaceDucking)
            {
                if ((float) (double) (*params)[luxeVerbSpaceDucking] > 0.0f)
                    addOnce (luxeDucking, where);
                // rollup step 10: round 190kw / 190lp / 190lz's Space engine revision 1 (every new LuxeVerb; heard only with
                // the Space engine on) and its Stereo Width past 100 % go with it
                if (params->size() > luxeVerbSpaceRevision && (float) (double) (*params)[luxeVerbSpaceRevision] >= 0.5f
                    && (float) (double) (*params)[luxeVerbSpaceFirst] >= 0.5f)
                    addOnce (luxeSpace, where);
                params->removeRange (luxeVerbSpaceDucking, params->size() - luxeVerbSpaceDucking);
            }
            else
            {
                const auto verb = [&] (const char* id, int law, int revision) {
                    if (kind != id || params->size() <= revision)
                        return;
                    if ((float) (double) (*params)[revision] >= 0.5f && (float) (double) (*params)[law] >= 0.5f)
                        addOnce (verbRevisions, where);
                    params->removeRange (revision, params->size() - revision);
                };
                verb ("epochVerb", verbs::epoch::engineLaw, verbs::epoch::revision);
                verb ("atrium", verbs::atrium::engineLaw, verbs::atrium::revision);
                verb ("foilPlate", verbs::foil::engineLaw, verbs::foil::revision);
                verb ("halo", verbs::halo::engineLaw, verbs::halo::revision);
            }
        });
        if (! thump.isEmpty())
            lost.push_back ({ TR_NOOP ("Voice Shift's Measured law plays its earlier version (a low thump at sung onsets): {0}"), listOf (thump), true });
        if (! pink.isEmpty())
            lost.push_back ({ TR_NOOP ("Ridge Strip's 4K Pink EQ plays as the Black EQ: {0}"), listOf (pink), true });
        if (! consoleFilters.isEmpty())
            lost.push_back ({ TR_NOOP ("Ridge Strip's measured SSL 4K G / 4K E filters and its 32C voicing are left out: {0}"), listOf (consoleFilters), true });
        if (! lineLaw.isEmpty())
            lost.push_back ({ TR_NOOP ("Echoform's Timeless 3 line law plays as its own echo: {0}"), listOf (lineLaw), true });
        if (! valhallaLoop.isEmpty())
            lost.push_back ({ TR_NOOP ("Echoform's ValhallaDelay loop law (the ValhallaDelay stand-in) plays as its own echo: {0}"), listOf (valhallaLoop), true });
        if (! luxeSpace.isEmpty())
            lost.push_back ({ TR_NOOP ("LuxeVerb's Space engine plays its earlier version (not the Pro-R 2 laws): {0}"), listOf (luxeSpace), true });
        if (! hybridLoop.isEmpty())
            lost.push_back ({ TR_NOOP ("Echoform's H-Delay loop law (the Waves H-Delay stand-in) plays as its plain hybrid echo: {0}"), listOf (hybridLoop), true });
        if (! hushTiming.isEmpty())
            lost.push_back ({ TR_NOOP ("Hush De-esser's Allround mode plays its earlier timing: {0}"), listOf (hushTiming), true });
        if (! hushVocal.isEmpty())
            lost.push_back ({ TR_NOOP ("Hush De-esser's Single Vocal mode plays its earlier version: {0}"), listOf (hushVocal), true });
        if (! omni.isEmpty())
            lost.push_back ({ TR_NOOP ("Ridge Strip's omni law (the Waves Scheps Omni Channel stand-in) is left out: it plays Ridge's own strip: {0}"), listOf (omni), true });
        if (! gLaw.isEmpty())
            lost.push_back ({ TR_NOOP ("Ridge Strip's G-Channel EQ law (the Waves SSL G-Channel stand-in) plays Ridge's own E bands: {0}"), listOf (gLaw), true });
        if (! hushRen.isEmpty())
            lost.push_back ({ TR_NOOP ("Hush De-esser's Renaissance law (the Waves RDeEsser stand-in) plays Hush's own de-esser: {0}"), listOf (hushRen), true });
        if (! echoVoicings.isEmpty())
            lost.push_back ({ TR_NOOP ("Echoform's echo voicings play their earlier loop: {0}"), listOf (echoVoicings), true });
        if (! verbRevisions.isEmpty())
            lost.push_back ({ TR_NOOP ("Epoch Verb, Atrium, Foil Plate and Halo play the 0.1.27 version of their Measured law: {0}"), listOf (verbRevisions), true });
        if (! latticeTaps.isEmpty())
            lost.push_back ({ TR_NOOP ("Lattice Mod plays its earlier tap network: {0}"), listOf (latticeTaps), true });
        if (! luxeDucking.isEmpty())
            lost.push_back ({ TR_NOOP ("LuxeVerb's Space ducking is not in this release: {0}"), listOf (luxeDucking), true });
    }

    // rollup step 3, round 190ik (warp engines): an 11-number warpModes list (the Texture / Complex Pro law), Beats'
    // pitch-aware law (0.1.28 reads it as Measured), Complex Pro / Texture transpositions rendered offline (0.1.28
    // renders a baked pitch only for Beats: it goes to the Pitch knob, as 0.1.28 itself moves it)
    {
        juce::StringArray pitchAware, newEngines, knobPitch;
        bool warped = false;
        // (a project saved by this build has the mark; a channel without its own list then plays the pitch-aware Beats
        // and the Measured Texture / Complex Pro laws, which 0.1.28 reads as Measured Beats and its Classic engines)
        const auto marked = (bool) root.getProperty ("warpLaw190ik", false);
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            auto* sampler = objectOf (c.getProperty ("samplerSettings"));
            if (sampler == nullptr)
                return;
            const juce::var sv (sampler);
            const auto method = (int) sv.getProperty ("stretchMethod", 0); // (StretchMethod: 3 Beats, 5 Texture, 6 Complex)
            const auto stretched = (double) sv.getProperty ("stretchBeats", 0.0) > 0.0;
            const auto keepsPitch = (bool) sv.getProperty ("keepPitch", false);
            warped = warped || stretched;
            auto* list = arrayOf (sampler->getProperty ("warpModes"));
            const auto listed = list != nullptr && list->size() >= 9;
            const auto beatsLaw = listed && list->size() > 9 ? (double) (*list)[9] : 2.0;
            const auto warpLaw = listed && list->size() > 10 ? (double) (*list)[10] : 1.0;
            const auto complexPro = listed && (double) (*list)[8] != 0.0;
            if (marked && stretched && keepsPitch && method == 3 && beatsLaw >= 1.5)
                pitchAware.add (channelName (juce::var (&c), i));
            if (marked && stretched && keepsPitch && warpLaw >= 0.5 && (method == 5 || (method == 6 && complexPro)))
                newEngines.add (channelName (juce::var (&c), i));
            if (list != nullptr && list->size() > 10)
            {
                if (beatsLaw >= 1.5)
                    list->set (9, 1.0);
                list->removeRange (10, list->size() - 10);
            }
            if (const auto baked = (double) sv.getProperty ("stretchPitch", 0.0); baked != 0.0 && method != 3)
            {
                sampler->setProperty ("pitch", juce::jlimit (-24.0, 24.0, (double) sv.getProperty ("pitch", 0.0) + baked));
                sampler->removeProperty ("stretchPitch");
                knobPitch.add (channelName (juce::var (&c), i));
            }
        });
        if (! pitchAware.isEmpty())
            lost.push_back ({ TR_NOOP ("Beats' pitch-aware cuts play as the Measured law: {0}"), listOf (pitchAware), true });
        if (! newEngines.isEmpty())
            lost.push_back ({ TR_NOOP ("Texture and Complex Pro play their earlier engine (no grains or formants): {0}"), listOf (newEngines), true });
        if (! knobPitch.isEmpty())
            lost.push_back ({ TR_NOOP ("Complex Pro and Texture transpositions play through the Pitch knob: {0}"), listOf (knobPitch), true });
        // issue #187 (round 190ik): engine behaviour, nothing to drop; reported when warped samples meet tempo automation
        bool tempoAutomation = false;
        if (auto* automations = arrayOf (root["automations"]))
            for (const auto& a : *automations)
                tempoAutomation = tempoAutomation || a["target"][0].toString() == "tempo";
        if (warped && tempoAutomation)
            lost.push_back ({ TR_NOOP ("Warped samples do not follow tempo changes in this release: they keep the project tempo"), {}, true });
    }

    // rollup step 3, round 190im (R12): Live's deactivated clip slots ("inactive"); 0.1.28 plays them
    {
        int inactive = 0;
        if (auto* slots = arrayOf (root["clipGrid"]["clips"]))
            for (auto& slot : *slots)
                if (auto* so = objectOf (slot))
                    inactive += drop (*so, "inactive") ? 1 : 0;
        if (inactive > 0)
            lost.push_back ({ TR_NOOP ("{0} deactivated clip slot(s) play again"), juce::String (inactive), true });
    }

    // round 190lr (DL-14): Live's scene follow actions ("follow" on a scene); 0.1.28 has none
    {
        int follows = 0;
        if (auto* scenes = arrayOf (root["clipGrid"]["scenes"]))
            for (auto& scene : *scenes)
                if (auto* so = objectOf (scene))
                    follows += drop (*so, "follow") ? 1 : 0;
        if (follows > 0)
            lost.push_back ({ TR_NOOP ("{0} scene follow action(s) are left out"), juce::String (follows), true });
    }

    // rollup step 4, round 190jh: Facet EQ's automatic dynamic thresholds (hidden parameters past its count; 0.1.28 plays
    // those bands with their manual threshold)
    {
        juce::StringArray autoBands, dispersion, puig, wavesEq, api560;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr || e.getProperty ("kind").toString() != "facetEq" || params->size() <= facet::autoThresholdParam0)
                return;
            for (int b = 0; b < 8 && facet::autoThresholdParam0 + b < params->size(); ++b)
                if ((float) (double) (*params)[facet::autoThresholdParam0 + b] >= 0.5f)
                    addOnce (autoBands, where);
            // rollup step 9: round 190lg's automatic-threshold revision [125] goes with the bands' automatic thresholds (above);
            // round 190ln's all-pass dispersion [126 … 128] (set only by a translated Kilohearts Disperser state)
            if (params->size() > facet::dispersionCountParam && facet::dispersionSections ((float) (double) (*params)[facet::dispersionCountParam]) > 0)
                addOnce (dispersion, where);
            // rollup step 11: round 190mg's PuigTec law [129] (set only by a translated Waves PuigTec EQP1A state)
            if (params->size() > facet::puigLawParam && facet::puigLaw ((float) (double) (*params)[facet::puigLawParam]) > 0)
                addOnce (puig, where);
            // rollup step 12: round 190mm's Waves design law [130] + band codes [131 …] (set only by the Renaissance EQ /
            // Q10 / RChannel translators)
            if (params->size() > facet::wavesLawParam && (float) (double) (*params)[facet::wavesLawParam] >= 0.5f)
                addOnce (wavesEq, where);
            // rollup step 13: round 190mt's API-560 law [139] + slider gains [140 …] (set only by the Waves API-560 translator;
            // its Q10 codes 14 / 15 go with the Waves design law above)
            if (params->size() > facet::api560LawParam && (float) (double) (*params)[facet::api560LawParam] >= 0.5f)
                addOnce (api560, where);
            params->removeRange (facet::autoThresholdParam0, params->size() - facet::autoThresholdParam0);
        });
        if (! autoBands.isEmpty())
            lost.push_back ({ TR_NOOP ("Facet EQ's automatic dynamic thresholds play with the bands' manual threshold: {0}"), listOf (autoBands), true });
        if (! dispersion.isEmpty())
            lost.push_back ({ TR_NOOP ("Facet EQ's all-pass dispersion (the Kilohearts Disperser stand-in) is left out: {0}"), listOf (dispersion), true });
        if (! puig.isEmpty())
            lost.push_back ({ TR_NOOP ("Facet EQ's PuigTec law (the Waves PuigTec stand-in: its output stage and mains hum) is left out: {0}"), listOf (puig), true });
        if (! wavesEq.isEmpty())
            lost.push_back ({ TR_NOOP ("Facet EQ's Waves design law (the Renaissance EQ and Q10 stand-ins) plays Facet's own band shapes: {0}"), listOf (wavesEq), true });
        if (! api560.isEmpty())
            lost.push_back ({ TR_NOOP ("Facet EQ's API-560 law (the Waves API-560 stand-in) plays Facet's own bands: {0}"), listOf (api560), true });
    }

    // rollup step 4, round 190jl: MIDI Out channels and the plugin wrappers' Output port reach hardware MIDI outputs from
    // 0.1.30 on (0.1.28 sends them to plugins on that port only); engine behaviour, nothing to drop
    {
        juce::StringArray hardware;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            // (the wrapper's "midiOutPort" is written only when it has a port)
            if (c.getProperty ("instrument").toString() == "midiOut" || c.getProperty ("plugin").hasProperty ("midiOutPort"))
                addOnce (hardware, channelName (juce::var (&c), i));
        });
        if (! hardware.isEmpty())
            lost.push_back ({ TR_NOOP ("MIDI Out channels and plugins' MIDI output reach only plugins in this release, not hardware MIDI outputs: {0}"), listOf (hardware), true });
    }

    // rollup step 4, round 190jg: sample-accurate automation of hosted CLAP / VST3 plugins (0.1.28 applies a change once
    // per piece of up to 128 samples); engine behaviour, reported when automation drives such a plugin's parameter
    {
        const auto hosted = [] (const juce::var& plugin) {
            const auto id = plugin["id"].toString();
            return id.startsWith ("VST3-") || id.startsWith ("CLAP-");
        };
        bool timed = false;
        std::function<void (const juce::var&)> walk = [&] (const juce::var& v) {
            if (timed)
                return;
            if (auto* a = arrayOf (v))
            {
                for (const auto& item : *a)
                    walk (item);
                return;
            }
            auto* o = objectOf (v);
            if (o == nullptr)
                return;
            if (const auto& target = o->getProperty ("target"); target.isArray() && target.size() >= 4)
            {
                const auto kind = target[0].toString();
                const auto index = (int) target[1], slot = (int) target[2];
                if (kind == "channelParam")
                    timed = root["channels"][index]["instrument"].toString() == "plugin" && hosted (root["channels"][index]["plugin"]);
                else if (kind == "effectParam")
                    timed = root["mixer"][index]["effects"][slot]["kind"].toString() == "plugin" && hosted (root["mixer"][index]["effects"][slot]["plugin"]);
            }
            for (const auto& p : o->getProperties())
                walk (p.value);
        };
        walk (root["automations"]);
        walk (root["patterns"]);
        walk (root["playlistTracks"]);
        if (timed)
            lost.push_back ({ TR_NOOP ("Automation of hosted CLAP and VST3 plugins changes once per 128 samples in this release (0.1.30 places each change at its own sample)"), {}, true });
    }

    // rollup step 5, round 190jv: on FL's native generators a slide note moves in steps, once per tick of the project, as
    // in FL (0.1.28 glides straight, except Slicex, which stepped already); engine behaviour, nothing to drop
    {
        juce::StringArray stepped;
        const auto* channels = arrayOf (root["channels"]);
        if (auto* patterns = arrayOf (root["patterns"]))
            for (const auto& p : *patterns)
                if (auto* notes = arrayOf (p["notes"]))
                    for (const auto& n : *notes)
                        if (n.isArray() && n.size() >= 9 && (bool) n[8]) // [start, length, channel, key, velocity, pan, fine, muted, slide, ..]
                        {
                            const auto ch = (int) n[2];
                            if (channels == nullptr || ch < 0 || ch >= channels->size())
                                continue;
                            const auto& c = channels->getReference (ch);
                            const auto kind = genpresets::kindFromId (c["instrument"].toString().toStdString());
                            if (kind && *kind != InstrumentKind::slicex && RackRenderer::stepsSlides (*kind))
                                addOnce (stepped, channelName (c, ch));
                        }
        if (! stepped.isEmpty())
            lost.push_back ({ TR_NOOP ("Slide notes on FL's native generators glide instead of stepping once per tick: {0}"), listOf (stepped), true });
    }

    // rollup step 6, round 190kc: Live's crossfader (Crossfade Assign per insert, the Main's position and curve, its
    // automation target "crossfader", which 0.1.28 reads as no target) and the return tracks' flags (a return is an
    // insert like any other: 0.1.28 routes it the same, its sends are ordinary sends)
    {
        juce::StringArray assigned, returns;
        if (auto* mixer = arrayOf (root["mixer"]))
            for (int t = 0; t < mixer->size(); ++t)
                if (auto* m = objectOf ((*mixer)[t]))
                {
                    if (drop (*m, "crossfade"))
                        addOnce (assigned, insertName (root["mixer"], t));
                    if (m->getProperty ("liveReturn").isBool() && (bool) m->getProperty ("liveReturn"))
                        addOnce (returns, insertName (root["mixer"], t));
                    drop (*m, "liveReturn");
                    drop (*m, "returnPre");
                    drop (*m, "returnSendsOn");
                }
        drop (o, "crossfader");
        drop (o, "crossfadeCurve");
        if (! assigned.isEmpty())
            lost.push_back ({ TR_NOOP ("The crossfader (Live's Crossfade Assign) is not in this release: these inserts play at their full level, and its automation does nothing: {0}"),
                             listOf (assigned), true });
        if (! returns.isEmpty())
            lost.push_back ({ TR_NOOP ("Live return tracks show as ordinary mixer inserts: {0}"), listOf (returns), false });
    }

    // rollup step 6, round 190ke: the Simple dock's Controls tab and its own knob mapping (0.1.28 has no Controls tab)
    {
        juce::StringArray mapped;
        if (auto* mixer = arrayOf (root["mixer"]))
            for (int t = 0; t < mixer->size(); ++t)
                if (auto* m = objectOf ((*mixer)[t]); m != nullptr && drop (*m, "smartControls"))
                    addOnce (mapped, insertName (root["mixer"], t));
        if (! mapped.isEmpty())
            lost.push_back ({ TR_NOOP ("The Simple Controls tab's own knob mappings: {0}"), listOf (mapped), false });
    }

    // round 190kz: EffectSlot::engineRev (Maximus 1 = its gain smoothers in double precision; 0.1.28 runs the x87 ones,
    // which gave the same samples on every Maximus state measured): dropped, nothing to report
    forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String&) { drop (e, "engineRev"); });

    // round 190ok: ChannelDef::smartDisable (FL's generator smart disable for a hosted instrument): an older LYNX runs the
    // plugin all the time, the same sound (dropped, nothing to report)
    forEachChannel (root, [&] (juce::DynamicObject& c, int) { drop (c, "smartDisable"); });

    // rollup step 6, round natives190kj: ChannelDef::engineRev (Harmor 1 = a part with a sample in its IMG section plays
    // FL's resynthesis; 0.1.28 plays such a part's timbres) and Newtone's state version 3 (+ the key, each note's voiced
    // end and velocity): 0.1.28 reads up to version 2 and would load no notes at all, so the state is written as version
    // 2 (the notes and every edit kept; the voiced end only shaped the analysed smooth times, which are saved)
    {
        juce::StringArray resynth, newtone, natural;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            const auto rev = (int) c.getProperty ("engineRev");
            if (drop (c, "engineRev") && rev >= 1 && c.getProperty ("instrument").toString() == "harmor")
            {
                juce::MemoryOutputStream decoded;
                if (juce::Base64::convertFromBase64 (decoded, c.getProperty ("pluginState").toString()))
                {
                    const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
                    if (harmorUsesResynthesis (std::vector<std::uint8_t> (bytes, bytes + decoded.getDataSize())))
                        addOnce (resynth, channelName (juce::var (&c), i));
                }
            }
        });
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            if (e.getProperty ("kind").toString() != "newtone")
                return;
            juce::MemoryOutputStream decoded;
            if (! juce::Base64::convertFromBase64 (decoded, e.getProperty ("state").toString()))
                return;
            const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
            const std::vector<std::uint8_t> state (bytes, bytes + decoded.getDataSize());
            if (newtonefx::nativeVersionOf (state) < 3)
                return;
            if (const auto track = newtonefx::parseNative (state))
            {
                const auto older = newtonefx::writeNative (*track, 2);
                e.setProperty ("state", juce::Base64::toBase64 (older.data(), older.size()));
                addOnce (newtone, where);
                if (track->engine != 0) // (round 190li: 0.1.28 renders the notes with Newtone's first engine)
                    addOnce (natural, where);
            }
        });
        if (! resynth.isEmpty())
            lost.push_back ({ TR_NOOP ("Harmor parts with a sample in the IMG section play their timbres instead of the resynthesis: {0}"), listOf (resynth), true });
        if (! newtone.isEmpty())
            lost.push_back ({ TR_NOOP ("Newtone's detected key and note velocities are left out: {0}"), listOf (newtone), false });
        if (! natural.isEmpty())
            lost.push_back ({ TR_NOOP ("Newtone's natural pitch engine, pitch modulation and pitch drift are left out (the notes play with the first engine): {0}"), listOf (natural), true });
    }

    // rollup step 7: round 190kl's extra Peak Controller LFO shapes and round 190kt's console law of Crest's Bus style
    // (hidden parameters past the slot's count, written only when set; 0.1.28 plays FL's Shape and the Bus style as it
    // was), round 190kn's mixer lock state (0.1.28: a solo elsewhere silences the insert and Unmute All unmutes it),
    // round qafix190kq's imported take lanes (0.1.28 gives them columns in the clip grid and the Session mixer) and round
    // 190ks's "record the notes played in the plugin's window" switch (written only when off: 0.1.28 never records them)
    {
        juce::StringArray shapes, console, locked, fet, crestLaws;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr)
                return;
            const auto kind = e.getProperty ("kind").toString();
            if (kind == "peakController" && params->size() > peakshapes::extraShapeParam)
            {
                if (peakshapes::idOf ((float) (double) (*params)[peakshapes::extraShapeParam]) != 0)
                    addOnce (shapes, where);
                params->removeRange (peakshapes::extraShapeParam, params->size() - peakshapes::extraShapeParam);
            }
            else if (kind == "crestCompressor" && params->size() > dyn::crest::busLaw)
            {
                if ((float) (double) (*params)[dyn::crest::busLaw] >= 0.5f)
                    addOnce (console, where);
                // rollup step 8: round 190ky's FET law (the Waves CLA-76 stand-in), round 190kw's revision 1 (a new Crest:
                // the Pro-C 3 laws of its styles; 0.1.28 plays the fitted laws)
                const auto at = [params] (int i) { return params->size() > i ? (float) (double) (*params)[i] : 0.0f; };
                if (at (dyn::crest::fetLaw) > 0.0f)
                    addOnce (fet, where);
                else if (at (dyn::crest::revision) >= 0.5f && at (dyn::crest::busLaw) < 0.5f)
                    addOnce (crestLaws, where);
                params->removeRange (dyn::crest::busLaw, params->size() - dyn::crest::busLaw);
            }
        });
        bool soloed = false;
        if (auto* mixer = arrayOf (root["mixer"]))
            for (int t = 0; t < mixer->size(); ++t)
                if (auto* m = objectOf ((*mixer)[t]))
                {
                    soloed = soloed || (bool) m->getProperty ("solo");
                    if (drop (*m, "lockState"))
                        addOnce (locked, insertName (root["mixer"], t));
                }
        forEachChannel (root, [&] (juce::DynamicObject& c, int) {
            if (auto* plugin = objectOf (c.getProperty ("plugin")))
                drop (*plugin, "recordOwnNotes");
        });
        // rollup step 8: round 190ld's lock states of effect slots and Playlist tracks (a later solo leaves them as they
        // are; nothing they play changes)
        bool slotLocks = false;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String&) { slotLocks = drop (e, "lockState") || slotLocks; });
        const auto trackLocks = drop (o, "playlistTrackSoloLocks");
        if (slotLocks || trackLocks)
            lost.push_back ({ TR_NOOP ("Lock states of effect slots and Playlist tracks"), {}, false });
        if (auto* mixer = arrayOf (root["mixer"]))
            for (auto& m : *mixer)
                if (auto* fx = arrayOf (m["effects"]))
                    for (auto& e : *fx)
                        if (auto* plugin = objectOf (e["plugin"]))
                            drop (*plugin, "recordOwnNotes");
        if (! shapes.isEmpty())
            lost.push_back ({ TR_NOOP ("The Peak Controller's LYNX LFO shapes play its FL Shape: {0}"), listOf (shapes), true });
        if (! console.isEmpty())
            lost.push_back ({ TR_NOOP ("Crest Compressor's console law (the Waves SSLComp stand-in) plays as its Bus style: {0}"), listOf (console), true });
        if (! fet.isEmpty())
            lost.push_back ({ TR_NOOP ("Crest Compressor's FET law (the Waves CLA-76 stand-in) plays as its style: {0}"), listOf (fet), true });
        if (! crestLaws.isEmpty())
            lost.push_back ({ TR_NOOP ("Crest Compressor's styles play their earlier laws (not the reference's): {0}"), listOf (crestLaws), true });
        if (! locked.isEmpty())
            lost.push_back ({ TR_NOOP ("Mixer lock states (a solo silences these inserts, and Unmute all unmutes them): {0}"), listOf (locked), soloed });
        if (auto* lanes = arrayOf (root["playlistTakeLanes"]))
        {
            const auto count = lanes->size();
            drop (o, "playlistTakeLanes");
            if (count > 0)
                lost.push_back ({ TR_NOOP ("{0} imported take lane(s) get their own columns in the clip grid and the Session mixer"), juce::String (count), false });
        }
    }

    // the view
    {
        // rollup step 4: round 190jf's nested Playlist folders (0.1.28 opens the groups one level deep), round 190jd's
        // second display view of an effect (Fruity Limiter's CURVE; 0.1.28 shows its one view)
        if (auto* nests = arrayOf (root["playlistTrackNest"]))
        {
            const auto count = nests->size();
            drop (o, "playlistTrackNest");
            if (count > 0)
                lost.push_back ({ TR_NOOP ("{0} track(s) in nested Playlist folders open one level deep"), juce::String (count), false });
        }
        juce::StringArray views, speeds;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            // (rollup step 12: round 190mk keeps Fruity Multiband's display SPEED there too)
            const auto multiband = e.getProperty ("kind").toString() == "fruityMultiband";
            if (drop (e, "displayView"))
                addOnce (multiband ? speeds : views, where);
        });
        if (! views.isEmpty())
            lost.push_back ({ TR_NOOP ("Fruity Limiter's CURVE view (its display shows the scrolling graph): {0}"), listOf (views), false });
        if (! speeds.isEmpty())
            lost.push_back ({ TR_NOOP ("Fruity Multiband Compressor's display SPEED goes back to its default: {0}"), listOf (speeds), false });
        // rollup step 12: round 190mk's zipped Channel rack rows (FL's Zip selected)
        int zipped = 0;
        forEachChannel (root, [&] (juce::DynamicObject& c, int) {
            if (drop (c, "zipped"))
                ++zipped;
        });
        if (zipped > 0)
            lost.push_back ({ TR_NOOP ("{0} zipped channel(s) show at full height in the Channel rack"), juce::String (zipped), false });

        bool colours = false;
        if (auto* automations = arrayOf (root["automations"]))
            for (auto& a : *automations)
                if (auto* ao = objectOf (a))
                    colours = drop (*ao, "colour") || colours;
        if (colours)
            lost.push_back ({ TR_NOOP ("Automation clip colours"), {}, false });
        if (drop (o, "mixSnapshots"))
            lost.push_back ({ TR_NOOP ("Mix snapshots (the Mixer menu)"), {}, false });
    }

    // rollup step 9: hidden parameters past the slot's count, written only when set (0.1.28 plays the effect without them):
    // round 190kw / 190lg's Saturn laws of Ember (every new Ember, and a translated FabFilter Saturn 2 state), round 190lg's
    // Pro-MB band law of Prism Multiband, round 190lf's Waves stand-in laws — L2 on the Limiter, CLA-2A's cell law on Glow
    // Leveler, TrueVerb's direct sound on Reeverb 2, NS1's zero law on Quell (each set only by its translator)
    {
        juce::StringArray saturn, proMb, l2, cell, direct, zero, c4, ns1, c6;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr)
                return;
            const auto kind = e.getProperty ("kind").toString();
            const auto cut = [params, &where] (int from, juce::StringArray& list) {
                if (params->size() <= from)
                    return;
                for (int i = from; i < params->size(); ++i)
                    if ((float) (double) (*params)[i] != 0.0f)
                    {
                        addOnce (list, where);
                        break;
                    }
                params->removeRange (from, params->size() - from);
            };
            if (kind == "ember")
            {
                // (lawRate alone changes nothing without the law)
                if (params->size() > mixsuite::ember::revision && (float) (double) (*params)[mixsuite::ember::revision] >= 0.5f)
                    addOnce (saturn, where);
                if (params->size() > mixsuite::ember::revision)
                    params->removeRange (mixsuite::ember::revision, params->size() - mixsuite::ember::revision);
            }
            else if (kind == "prismMultiband")
            {
                // rollup step 11: round 190mg's C4 law [count + 2, + 3] (set only by the Waves C4 translator) has its own line
                if (params->size() > dyn::prism::c4Law && (float) (double) (*params)[dyn::prism::c4Law] > 0.0f)
                {
                    addOnce (c4, where);
                    // rollup step 12: round 190mm's two floating bands of the C6 [c6Float0 …] (on the C4 law)
                    for (int band = 0; band < 2; ++band)
                        if (const auto on = dyn::prism::c6Param (band, dyn::prism::c6On); params->size() > on && (float) (double) (*params)[on] >= 0.5f)
                            addOnce (c6, where);
                    params->removeRange (dyn::prism::c4Law, params->size() - dyn::prism::c4Law);
                }
                cut (dyn::prism::revision, proMb);
            }
            else if (kind == "limiter")
            {
                if (params->size() > limiterlaw::lawParam && limiterlaw::isL2 ((float) (double) (*params)[limiterlaw::lawParam]))
                    addOnce (l2, where);
                if (params->size() > limiterlaw::lawParam)
                    params->removeRange (limiterlaw::lawParam, params->size() - limiterlaw::lawParam);
            }
            else if (kind == "glowLeveler")
                cut (mixsuite::glow::cellLaw, cell);
            else if (kind == "reeverb2")
                cut (reeverb2DirectLaw, direct);
            else if (kind == "quellDenoiser")
            {
                // rollup step 11: round 190mg's NS1 law (after the zero law; set only by the NS1 translator) has its own line
                if (params->size() > voicetools::quell::ns1Law && (float) (double) (*params)[voicetools::quell::ns1Law] > 0.0f)
                {
                    addOnce (ns1, where);
                    params->removeRange (voicetools::quell::ns1Law, params->size() - voicetools::quell::ns1Law);
                }
                cut (voicetools::quell::zeroLaw, zero);
            }
        });
        if (! saturn.isEmpty())
            lost.push_back ({ TR_NOOP ("Ember's Saturn styles play their earlier measured characters (not the reference's): {0}"), listOf (saturn), true });
        if (! proMb.isEmpty())
            lost.push_back ({ TR_NOOP ("Prism Multiband's Pro-MB band law plays its measured detector: {0}"), listOf (proMb), true });
        if (! l2.isEmpty())
            lost.push_back ({ TR_NOOP ("The Limiter's L2 law (the Waves L2 stand-in) plays as the classic Limiter: {0}"), listOf (l2), true });
        if (! cell.isEmpty())
            lost.push_back ({ TR_NOOP ("Glow Leveler's cell law (the Waves CLA-2A stand-in) plays as its Opto voicing: {0}"), listOf (cell), true });
        if (! direct.isEmpty())
            lost.push_back ({ TR_NOOP ("Reeverb 2's measured direct sound (the Waves TrueVerb stand-in) is left out: {0}"), listOf (direct), true });
        if (! zero.isEmpty())
            lost.push_back ({ TR_NOOP ("Quell's zero law (the Waves NS1 stand-in) is left out: its lowest Amount settings play the matched engine: {0}"), listOf (zero), true });
        if (! c4.isEmpty())
            lost.push_back ({ TR_NOOP ("Prism Multiband's C4 law (the Waves C4 stand-in) plays as Prism's own engine: {0}"), listOf (c4), true });
        if (! ns1.isEmpty())
            lost.push_back ({ TR_NOOP ("Quell's NS1 law (the Waves NS1 stand-in) plays the matched engine: {0}"), listOf (ns1), true });
        if (! c6.isEmpty())
            lost.push_back ({ TR_NOOP ("Prism Multiband's floating bands (the Waves C6 stand-in) are left out: {0}"), listOf (c6), true });
    }

    // rollup step 10: round 190lp's black-box revision 1 of Apex Limiter (Pro-L 2) and Sentry Gate (Pro-G), every new one
    // (hidden, past the slot's count), and round 190ly's rate law of Spiral Echo (every new one; heard with Law = Measured
    // at rates other than 48 kHz). Round 190lp's Crest Smooth / Op-El are part of Crest's revision 1 (its line above), round
    // 190ls / 190ly's Atrium dense tank and Epoch's short-decay trim part of the reverbs' revision (the Measured law's line)
    {
        juce::StringArray apexLaws, sentryLaws, spiralRate;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr)
                return;
            const auto kind = e.getProperty ("kind").toString();
            const auto at = [params] (int i) { return i < params->size() ? (float) (double) (*params)[i] : 0.0f; };
            const auto cutFrom = [params] (int from) {
                if (params->size() > from)
                    params->removeRange (from, params->size() - from);
            };
            if (kind == "apexLimiter")
            {
                if (at (apex::pRevision) >= 0.5f)
                    addOnce (apexLaws, where);
                cutFrom (apex::pRevision);
            }
            else if (kind == "sentryGate")
            {
                if (at (dyn::sentry::revision) >= 0.5f)
                    addOnce (sentryLaws, where);
                cutFrom (dyn::sentry::revision);
            }
            else if (kind == "spiralEcho")
            {
                if (at (echosuite::spiral::rateLaw) >= 0.5f && at (echosuite::spiral::law) >= 0.5f)
                    addOnce (spiralRate, where);
                cutFrom (echosuite::spiral::rateLaw);
            }
        });
        if (! apexLaws.isEmpty())
            lost.push_back ({ TR_NOOP ("Apex Limiter's styles play their earlier fitted laws (not the reference's): {0}"), listOf (apexLaws), true });
        if (! sentryLaws.isEmpty())
            lost.push_back ({ TR_NOOP ("Sentry Gate plays its earlier measured gate (not the reference's): {0}"), listOf (sentryLaws), true });
        if (! spiralRate.isEmpty())
            lost.push_back ({ TR_NOOP ("Spiral Echo's Measured loop plays its earlier phase at sample rates other than 48 kHz: {0}"), listOf (spiralRate), true });
    }

    // rollup step 11: hidden parameters past the slot's count, written only when set (0.1.28 plays the effect without them):
    // round 190mb / 190mj's Valhalla laws — Horizon's revision (every new Horizon) and its FutureVerb law, Nebula's
    // Supermassive tone, Orbit's SpaceModulator level and voices (Epoch's rate law goes with the reverbs' revision, the
    // Measured law's line); round 190mg's Waves laws — Compressor's Renaissance law, Gemini Doubler's splice law, LYNX Tune's
    // Waves Tune Real-Time law (each set only by its translator; Crest's FET amplifier goes with the FET law's line, the
    // L2 requantizer with the L2 law's, Prism's C4 law, Quell's NS1 law and Facet's PuigTec law have their lines above)
    {
        juce::StringArray horizonLaws, nebulaTone, orbitLaws, renLaw, splice, wavesTune, voxLaw, apiLaw, channelEq, bassLaw, vitamin, rvoxType, rchGate;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr)
                return;
            const auto kind = e.getProperty ("kind").toString();
            const auto cut = [params, &where] (int from, juce::StringArray& list) {
                if (params->size() <= from)
                    return;
                for (int i = from; i < params->size(); ++i)
                    if ((float) (double) (*params)[i] != 0.0f)
                    {
                        addOnce (list, where);
                        break;
                    }
                params->removeRange (from, params->size() - from);
            };
            if (kind == "horizon")
            {
                // (revision 1 is heard only with the engine law on; reaLaw is read with it)
                const auto lawOn = params->size() > verbs::horizon::engineLaw && (float) (double) (*params)[verbs::horizon::engineLaw] >= 0.5f;
                if (lawOn)
                    cut (verbs::horizon::revision, horizonLaws);
                else if (params->size() > verbs::horizon::revision)
                    params->removeRange (verbs::horizon::revision, params->size() - verbs::horizon::revision);
            }
            else if (kind == "nebula")
                cut (verbs::nebula::toneLaw, nebulaTone);
            else if (kind == "orbit")
                cut (echosuite::orbit::smLaw, orbitLaws);
            else if (kind == "compressor")
            {
                // rollup step 12: round 190mm's Vox law [21 …], API-2500 bus law [25 …] and the Renaissance Channel's EQ
                // [48 …] (each set only by its Waves translator) have their own lines; the Renaissance law [20] keeps its
                const auto on = [params] (int i) { return params->size() > i && (float) (double) (*params)[i] >= 0.5f; };
                if (on (compressorVoxLaw))
                    addOnce (voxLaw, where);
                if (on (compressorApiLaw))
                    addOnce (apiLaw, where);
                if (on (compressorChannelLaw))
                    addOnce (channelEq, where);
                // rollup step 13: round 190mt's RVox type [38] and gate [39 …] (set only by the RChannel translator)
                if (on (compressorRVoxType))
                    addOnce (rvoxType, where);
                if (on (compressorRchGate))
                    addOnce (rchGate, where);
                if (on (compressorRenLaw) || ! (on (compressorVoxLaw) || on (compressorApiLaw) || on (compressorChannelLaw) || on (compressorRVoxType) || on (compressorRchGate)))
                    cut (compressorRenLaw, renLaw);
                else if (params->size() > compressorRenLaw)
                    params->removeRange (compressorRenLaw, params->size() - compressorRenLaw);
            }
            else if (kind == "flLowLifter")
                cut (lowlifter::bassLaw, bassLaw); // rollup step 12: round 190mm's bass law (MaxxBass / Renaissance Bass)
            else if (kind == "aether")
                cut (mixsuite::aether::vitLaw, vitamin); // rollup step 12: round 190mm's Vitamin law
            else if (kind == "geminiDoubler")
                cut (voicetools::gemini::spliceLaw, splice);
            else if (kind == "lynxTune")
                cut (lynxtune::wavesLaw, wavesTune);
        });
        if (! horizonLaws.isEmpty())
            lost.push_back ({ TR_NOOP ("Horizon plays its earlier reverb and echo modes (not the ValhallaFutureVerb laws): {0}"), listOf (horizonLaws), true });
        if (! nebulaTone.isEmpty())
            lost.push_back ({ TR_NOOP ("Nebula's classic modes play without the ValhallaSupermassive tone: {0}"), listOf (nebulaTone), true });
        if (! orbitLaws.isEmpty())
            lost.push_back ({ TR_NOOP ("Orbit plays its own voices and levels (not the ValhallaSpaceModulator laws): {0}"), listOf (orbitLaws), true });
        if (! renLaw.isEmpty())
            lost.push_back ({ TR_NOOP ("Compressor's Renaissance law (the Waves Renaissance Compressor stand-in) plays as its vintage voicing: {0}"), listOf (renLaw), true });
        if (! splice.isEmpty())
            lost.push_back ({ TR_NOOP ("Gemini Doubler's splice law (the Waves Doubler stand-in) plays its measured read head: {0}"), listOf (splice), true });
        if (! wavesTune.isEmpty())
            lost.push_back ({ TR_NOOP ("LYNX Tune's Waves Tune Real-Time law is left out (the uncorrected sound is re-synthesised): {0}"), listOf (wavesTune), true });
        if (! voxLaw.isEmpty())
            lost.push_back ({ TR_NOOP ("Compressor's Vox law (the Waves Renaissance Vox stand-in) plays as the plain Compressor: {0}"), listOf (voxLaw), true });
        if (! apiLaw.isEmpty())
            lost.push_back ({ TR_NOOP ("Compressor's bus law (the Waves API-2500 stand-in) plays as the plain Compressor: {0}"), listOf (apiLaw), true });
        if (! channelEq.isEmpty())
            lost.push_back ({ TR_NOOP ("Compressor's channel EQ (the Waves RChannel stand-in) is left out: {0}"), listOf (channelEq), true });
        if (! rvoxType.isEmpty())
            lost.push_back ({ TR_NOOP ("Compressor's RVox type (the Waves RChannel stand-in) plays the Renaissance law's own curve: {0}"), listOf (rvoxType), true });
        if (! rchGate.isEmpty())
            lost.push_back ({ TR_NOOP ("Compressor's gate (the Waves RChannel stand-in) is left out: {0}"), listOf (rchGate), true });
        if (! bassLaw.isEmpty())
            lost.push_back ({ TR_NOOP ("Low Lifter's bass law (the Waves MaxxBass and Renaissance Bass stand-ins) plays as Low Lifter: {0}"), listOf (bassLaw), true });
        if (! vitamin.isEmpty())
            lost.push_back ({ TR_NOOP ("Aether's Vitamin law (the Waves Vitamin stand-in) plays Aether's own enhancer: {0}"), listOf (vitamin), true });
    }

    // rollup step 9, round 190lq (issue #204): a channel's notes on an audio clip channel play as the channel's Sampler
    // (FL's law: the whole clip, layered, the key transposes it) in projects with the "clipNotes190lq" mark; 0.1.28 plays
    // every such note cut at its own end, at the clip's own pitch (a channel already on "clipNoteLegacy" plays that law
    // there too, but a note's end there ends every sounding note of the channel)
    {
        juce::StringArray fl;
        std::vector<bool> played;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
                if (auto* notes = arrayOf (p["notes"]))
                    for (auto& n : *notes)
                        if (auto* fields = arrayOf (n); fields != nullptr && fields->size() > 2)
                            if (const auto c = (int) (*fields)[2]; c >= 0 && c < 4096)
                            {
                                if ((int) played.size() <= c)
                                    played.resize ((std::size_t) c + 1, false);
                                played[(std::size_t) c] = true;
                            }
        const auto marked = drop (o, "clipNotes190lq");
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (c.getProperty ("instrument").toString() != "audioClip")
                return;
            auto legacy = ! marked;
            if (auto* sampler = objectOf (c.getProperty ("samplerSettings")))
                legacy = drop (*sampler, "clipNoteLegacy") || legacy;
            if (! legacy && i < (int) played.size() && played[(std::size_t) i])
                fl.add (channelName (juce::var (&c), i));
        });
        if (! fl.isEmpty())
            lost.push_back ({ TR_NOOP ("Notes on audio clip channels play cut at each note's end and at the clip's own pitch: {0}"), listOf (fl), true });
    }

    // round 190kx: audio clips edited in an ARA plugin (Melodyne...) play unedited in 0.1.28 (it hosts no ARA)
    {
        juce::StringArray edited;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (drop (c, "ara"))
                edited.add (channelName (juce::var (&c), i));
        });
        // round 190oq (issue #243): an ARA plugin on a mixer insert is a plain insert effect there
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            if (drop (e, "ara"))
                edited.add (where);
        });
        drop (o, "araDocuments");
        if (! edited.isEmpty())
            lost.push_back ({ TR_NOOP ("Audio clips edited in an ARA plugin (Melodyne...) play without those edits: {0}"), listOf (edited), false });
    }

    // round 190lk: audio clips whose notes were edited in the polyphonic note editor play unedited in 0.1.28
    {
        juce::StringArray edited;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (drop (c, "polyEdit"))
                edited.add (channelName (juce::var (&c), i));
        });
        if (! edited.isEmpty())
            lost.push_back ({ TR_NOOP ("Audio clips with notes edited in the polyphonic note editor play without those edits: {0}"), listOf (edited), false });
    }

    // rollup step 13: round 190mv's FL Current mixer track (the last insert, "current"; 0.1.28 shows it as an ordinary insert
    // that nothing routes into, so its effects no longer process the selected track) and round 190mr's window layout saved
    // with the project (0.1.28 ignores it: the layout on screen stays)
    {
        juce::StringArray current;
        if (auto* mixer = arrayOf (root["mixer"]))
            for (int t = 0; t < mixer->size(); ++t)
                if (auto* m = objectOf ((*mixer)[t]); m != nullptr && m->getProperty ("current").isBool() && (bool) m->getProperty ("current"))
                {
                    addOnce (current, insertName (root["mixer"], t));
                    drop (*m, "current");
                }
        if (! current.isEmpty())
            lost.push_back ({ TR_NOOP ("The Current mixer track shows as an ordinary insert: its effects no longer process the selected track: {0}"), listOf (current), true });
        if (drop (o, "windowLayout"))
            lost.push_back ({ TR_NOOP ("The window layout saved with the project is not restored"), {}, false });
    }

    // rollup step 14: round 190nb's Kilohearts laws on Effector (hidden [12 …], set only by the Kilohearts translator for
    // Ring Mod, Reverser, Resonator, Phase Distortion and Shaper; 0.1.28 plays the Effector algorithm the visible Type
    // names) and round 190mx's launched pattern clips playing their pattern's events (#198, Session Record writes knob
    // moves there; 0.1.28's clip grid plays a launched clip's notes only: engine behaviour, nothing to drop)
    {
        juce::StringArray khsLaws, launchedEvents;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr || e.getProperty ("kind").toString() != "effector" || params->size() <= khsdsp::lawParam)
                return;
            if (khsdsp::lawOf ((float) (double) (*params)[khsdsp::lawParam]) != khsdsp::Law::none)
                addOnce (khsLaws, where);
            params->removeRange (khsdsp::lawParam, params->size() - khsdsp::lawParam);
        });
        if (auto* slots = arrayOf (root["clipGrid"]["clips"]))
            for (const auto& s : *slots)
                if (const auto* list = arrayOf (s["clip"])) // (the slot's clip in the Playlist's clip form: a list of one)
                    for (const auto& one : *list)
                        if (const auto* c = arrayOf (one); c != nullptr && c->size() >= 4 && (c->size() < 6 || (int) (*c)[5] == 0))
                        {
                            const auto pattern = (int) (*c)[0];
                            const auto& p = root["patterns"][pattern];
                            if (const auto* lanes = arrayOf (p["events"]); lanes != nullptr && ! lanes->isEmpty())
                                addOnce (launchedEvents, p["name"].toString().isNotEmpty() ? "\"" + p["name"].toString() + "\"" : "#" + juce::String (pattern + 1));
                        }
        if (! khsLaws.isEmpty())
            lost.push_back ({ TR_NOOP ("Effector's Kilohearts laws (the Ring Mod, Reverser, Resonator, Phase Distortion and Shaper stand-ins) play Effector's own algorithm: {0}"), listOf (khsLaws), true });
        if (! launchedEvents.isEmpty())
            lost.push_back ({ TR_NOOP ("Launched pattern clips play without their pattern's events (recorded knob moves): {0}"), listOf (launchedEvents), true });
    }
}

// --- 0.1.29 / 0.1.30 → 0.1.28 ------------------------------------------------------------------------------------------
void toRelease0128 (juce::var& root, Losses& lost)
{
    auto& o = *objectOf (root);
    // (reported only when the project has such automation: the mark is on in every new project)
    bool levelAutomation = false;
    if (auto* automations = arrayOf (root["automations"]))
        for (const auto& a : *automations)
            if (const auto kind = a["target"][0].toString(); kind == "mixerVolume" || kind == "mixerPan" || kind == "mixerSend" || kind == "vcaLevel")
                levelAutomation = true;
    if (drop (o, "mixerLevelPdc190hq") && levelAutomation)
        lost.push_back ({ TR_NOOP ("Mixer › Plugin delay compensation › \"Compensate fader and route automations too\" is not in this release: that automation plays a little early behind plugins with latency"), {}, true });

    int markers = 0;
    if (auto* automations = arrayOf (root["automations"]))
        for (auto& a : *automations)
            if (auto* ao = objectOf (a); ao != nullptr && drop (*ao, "tempoMarkers"))
                if (auto* points = arrayOf (a["points"]))
                    markers += juce::jmax (0, points->size() - 1);
    if (markers > 0)
        lost.push_back ({ TR_NOOP ("{0} tempo marker(s) on the ruler become a plain tempo automation clip (the tempo plays the same)"), juce::String (markers), false });

    // round 190hq's side-chain compensation is engine behaviour: nothing to drop, but an older release lines up less
    bool sidechain = false;
    if (auto* mixer = arrayOf (root["mixer"]))
        for (const auto& t : *mixer)
            if (auto* fx = arrayOf (t["effects"]))
                for (const auto& e : *fx)
                    sidechain = sidechain || (int) e.getProperty ("sidechain", -1) > 0 || (int) e.getProperty ("sidechain2", -1) > 0;
    if (sidechain)
        lost.push_back ({ TR_NOOP ("Side-chains are not lined up with plugin delay compensation in this release: behind plugins with latency, ducking may be a few milliseconds off"), {}, true });

    toRelease0128FromRollup030 (root, lost);
}

// --- 0.1.28 → 0.1.27 -----------------------------------------------------------------------------------------------------
// A slot 0.1.27 does not know (it would drop it, moving every later slot): a switched-off Utility in its place.
bool flattenUnknownEffect (juce::var& e, const juce::StringArray& kinds)
{
    auto* o = objectOf (e);
    if (o == nullptr || ! kinds.contains (e["kind"].toString()))
        return false;
    auto replacement = std::make_unique<juce::DynamicObject>();
    replacement->setProperty ("kind", "utility");
    replacement->setProperty ("on", false);
    for (const auto* keep : { "row", "sidechain", "sidechain2" })
        if (o->hasProperty (keep))
            replacement->setProperty (keep, o->getProperty (keep));
    e = juce::var (replacement.release());
    return true;
}

// nested chains (a Patcher's nodes, a rack's branches): every effect object below `v`
void flattenNested (juce::var& v, int& count, const juce::StringArray& kinds)
{
    if (auto* a = arrayOf (v))
    {
        for (auto& item : *a)
            flattenNested (item, count, kinds);
        return;
    }
    auto* o = objectOf (v);
    if (o == nullptr)
        return;
    for (auto& p : o->getProperties())
    {
        auto value = p.value;
        if (value.isObject() && kinds.contains (value["kind"].toString()) && (p.name.toString() == "effect" || p.name.toString() == "head"))
        {
            if (flattenUnknownEffect (value, kinds))
            {
                o->setProperty (p.name, value);
                ++count;
            }
            continue;
        }
        if (auto* list = arrayOf (value); list != nullptr && (p.name.toString() == "chain" || p.name.toString() == "effects"))
            for (auto& item : *list)
                if (flattenUnknownEffect (item, kinds))
                    ++count;
        flattenNested (value, count, kinds);
    }
}

// automation that drove slot (track, slot)'s parameters or mix now drives nothing ("none")
void detachTargets (juce::var& v, int track, int slot)
{
    auto isSlotTarget = [&] (const juce::var& t) {
        if (! t.isArray() || t.size() < 4)
            return false;
        const auto kind = t[0].toString();
        return (kind == "effectParam" || kind == "effectMix") && (int) t[1] == track && (int) t[2] == slot;
    };
    if (auto* a = arrayOf (v))
    {
        for (auto& item : *a)
            detachTargets (item, track, slot);
        return;
    }
    auto* o = objectOf (v);
    if (o == nullptr)
        return;
    for (auto& p : o->getProperties())
    {
        auto value = p.value;
        if (p.name.toString() == "target" && isSlotTarget (value))
            if (auto* t = arrayOf (value))
                t->set (0, "none");
        if (p.name.toString() == "links")
            if (auto* links = arrayOf (value))
                for (auto& l : *links)
                    if (isSlotTarget (l))
                        if (auto* t = arrayOf (l))
                            t->set (0, "none");
        detachTargets (value, track, slot);
    }
}

// Every slot of `kinds` (mixer inserts, Patchers, racks) becomes a switched-off Utility; their automation is detached and one
// loss line names where they were
void flattenEffects (juce::var& root, const juce::StringArray& kinds, const char* loss, Losses& lost)
{
    auto& o = *objectOf (root);
    juce::StringArray where;
    if (auto* mixer = arrayOf (root["mixer"]))
        for (int t = 0; t < mixer->size(); ++t)
            if (auto* fx = arrayOf ((*mixer)[t]["effects"]))
                for (int s = 0; s < fx->size(); ++s)
                    if (flattenUnknownEffect (fx->getReference (s), kinds))
                    {
                        where.add (insertName (o.getProperty ("mixer"), t) + juce::String::fromUTF8 (" \u203a ") + juce::String (s + 1));
                        detachTargets (root, t, s);
                    }
    int nested = 0;
    if (auto* mixer = arrayOf (root["mixer"]))
        for (auto& t : *mixer)
            if (auto* fx = arrayOf (t["effects"]))
                for (auto& e : *fx)
                    flattenNested (e, nested, kinds);
    if (auto* channels = arrayOf (root["channels"]))
        for (auto& c : *channels)
            flattenNested (c, nested, kinds);
    if (nested > 0)
        where.add (juce::String (nested) + juce::String::fromUTF8 (" \u00d7 Patcher"));
    if (! where.isEmpty())
        lost.push_back ({ loss, listOf (where), true });
}

void toRelease0127 (juce::var& root, Losses& lost)
{
    auto& o = *objectOf (root);

    // the effect 0.1.28 added (Anvil Bus, round 190fk)
    flattenEffects (root, { "anvilBus" }, TR_NOOP ("Anvil Bus is not in this release: it becomes a switched-off Utility in its slot, and its automation does nothing ({0})"), lost);

    // channels
    {
        juce::StringArray cut, pads, warp, frozen, beats, soundfonts;
        bool scales = false;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            const auto name = channelName (juce::var (&c), i);
            if (drop (c, "cutOthersOnly") | drop (c, "cutAtNoteEnd"))
                cut.add (name);
            scales = drop (c, "editorScale") || scales;
            if (drop (c, "simplePad"))
                pads.add (name);
            if (drop (c, "trackFreeze"))
                frozen.add (name);
            if (auto* sampler = objectOf (c.getProperty ("samplerSettings")))
            {
                if (drop (*sampler, "warpModes"))
                    warp.add (name);
                if ((int) sampler->getProperty ("stretchMethod") == (int) StretchMethod::beats)
                    beats.add (name);
            }
            if (c.getProperty ("instrument").toString() == "soundfont")
                soundfonts.add (name);
        });
        if (! cut.isEmpty())
            lost.push_back ({ TR_NOOP ("Cut group modes (\"Cut others only\", \"Cut at the note's end\"): {0}"), listOf (cut), true });
        if (! pads.isEmpty())
            lost.push_back ({ TR_NOOP ("Simple workflow pad settings (mode, tone, release, group): {0}"), listOf (pads), true });
        if (! warp.isEmpty())
            lost.push_back ({ TR_NOOP ("Warp mode settings (Preserve, loop mode, grain sizes) go back to their defaults: {0}"), listOf (warp), true });
        if (! frozen.isEmpty())
            lost.push_back ({ TR_NOOP ("Freeze Track: these channels play live again (the same sound, more CPU): {0}"), listOf (frozen), false });
        if (scales)
            lost.push_back ({ TR_NOOP ("Plugin window sizes (Scale)"), {}, false });
        if (drop (o, "beatsLaw190gx") && ! beats.isEmpty())
            lost.push_back ({ TR_NOOP ("Beats warping uses this release's older (Classic) law: {0}"), listOf (beats), true });
        if (drop (o, "sfModBase190he") && ! soundfonts.isEmpty())
            lost.push_back ({ TR_NOOP ("Soundfont modulation knobs centre at this release's older value: {0}"), listOf (soundfonts), true });
    }

    // clips: index 24 (a Playlist clip's groove, round 190fm) and 25 (its envelopes, round 190gi) are new
    {
        int grooves = 0, envelopes = 0;
        forEachClip (root, [&] (juce::Array<juce::var>& clip) {
            if (clip.size() > 24 && (juce::int64) clip[24] != 0)
                ++grooves;
            if (clip.size() > 25 && (int) clip[25] >= 0)
                ++envelopes;
            if (clip.size() > 24)
                clip.removeRange (24, clip.size() - 24);
        });
        if (drop (o, "clipEnvelopes") && envelopes > 0)
            lost.push_back ({ TR_NOOP ("{0} audio clip envelope(s): those clips play at their own level and pan"), juce::String (envelopes), true });
        if (grooves > 0)
            lost.push_back ({ TR_NOOP ("{0} Playlist clip groove(s): those clips play straight"), juce::String (grooves), true });
    }

    // automation clips with several target links (round 190gw): 0.1.27 drives the first one, 1:1
    {
        juce::StringArray linked;
        if (auto* automations = arrayOf (root["automations"]))
            for (int i = 0; i < automations->size(); ++i)
                if (auto* a = objectOf ((*automations)[i]))
                {
                    const auto links = a->getProperty ("links");
                    const auto formula = links.isArray() && links.size() > 0 && links[0].isArray() && links[0].size() >= 5 ? links[0][4].toString() : juce::String();
                    if (drop (*a, "links") && (links.size() > 1 || formula.isNotEmpty()))
                        linked.add ("\"" + a->getProperty ("name").toString() + "\"");
                }
        if (! linked.isEmpty())
            lost.push_back ({ TR_NOOP ("Automation clips with several targets or a formula drive only their first target, 1:1: {0}"), listOf (linked), true });
    }

    // the song
    if (o.hasProperty ("beatUnit"))
    {
        const auto unit = (int) o.getProperty ("beatUnit");
        drop (o, "beatUnit");
        lost.push_back ({ TR_NOOP ("The time signature's beat unit: n/{0} becomes n/4"), juce::String (unit), true });
    }
    if (drop (o, "linkTracksToMixer"))
        lost.push_back ({ TR_NOOP ("\"Link Playlist tracks to mixer tracks\" (a setting)"), {}, false });
    if (drop (o, "midiGenerate"))
        lost.push_back ({ TR_NOOP ("MIDI generator settings"), {}, false });
    if (drop (o, "playlistRowHeight"))
        lost.push_back ({ TR_NOOP ("The Playlist's row height"), {}, false });
}

// --- 0.1.31 → 0.1.30 ------------------------------------------------------------------------------------------------------
// Round 190nu (core/KeyRegions.h): the Piano roll's key markers keep their keys in "markerKeys" beside each pattern's
// marker list. 0.1.30 keeps such markers as named Key/Scale markers with no effect: their keys go, the names stay.
// Rollup 0.1.31 step 3 calls it from toRelease0130 below.
void keyMarkersFor0130 (juce::var& root, Losses& lost)
{
    int keyMarkers = 0;
    if (auto* patterns = arrayOf (root["patterns"]))
        for (auto& p : *patterns)
            if (auto* po = objectOf (p))
            {
                if (auto* keys = arrayOf (po->getProperty ("markerKeys")))
                    keyMarkers += keys->size();
                drop (*po, "markerKeys");
            }
    if (keyMarkers > 0)
        lost.push_back ({ TR_NOOP ("{0} key marker(s) in the Piano roll keep their names but not their keys (the rows show the Piano roll's own scale)"),
                          juce::String (keyMarkers), false });
}

// Rollup 0.1.31 (round 190no): what the 0.1.31 rounds save that 0.1.30 cannot hold, each loss reported only when the
// project has it. A copy for an older release runs this step first, so the later steps never see these. Round 190nc's
// Macro Map writes ordinary macro mappings (targets 0.1.30 has): nothing to change for it.
// Round 190pj (issue #241): JSFX scripts (hosted plugins of the "JSFX" format, which 0.1.30 and older do not have; their
// slot would stay "plugin not installed" there). Every JSFX slot becomes empty: a mixer insert (or a Patcher / rack node)
// becomes a switched-off Utility in its place (the later slots keep theirs; its automation is detached), a generator
// channel an empty Sampler that plays nothing. One line says so. Called from toRelease0130 below.
void jsfxFor0130 (juce::var& root, Losses& lost)
{
    auto& o = *objectOf (root);
    const auto isJsfx = [] (const juce::var& plugin) { return plugin["id"].toString().startsWith ("JSFX-"); };
    juce::StringArray where;
    const auto empty = [] (juce::DynamicObject& e) {
        juce::Array<juce::var> none;
        e.setProperty ("kind", "utility");
        e.setProperty ("on", false);
        e.setProperty ("params", none);
        for (const auto* gone : { "plugin", "mix", "engineRev", "displayView", "abOther", "abB", "ara" })
            drop (e, gone);
    };
    if (auto* mixer = arrayOf (root["mixer"]))
        for (int t = 0; t < mixer->size(); ++t)
            if (auto* fx = arrayOf ((*mixer)[t]["effects"]))
                for (int s = 0; s < fx->size(); ++s)
                    if (auto* e = objectOf ((*fx)[s]); e != nullptr && e->getProperty ("kind").toString() == "plugin" && isJsfx (e->getProperty ("plugin")))
                    {
                        addOnce (where, insertName (o.getProperty ("mixer"), t) + juce::String::fromUTF8 (" \u203a ") + juce::String (s + 1));
                        empty (*e);
                        detachTargets (root, t, s);
                    }
    // inside Patchers and racks (the mixer's own slots are done above)
    forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& in) {
        if (e.getProperty ("kind").toString() == "plugin" && isJsfx (e.getProperty ("plugin")))
        {
            addOnce (where, in);
            empty (e);
        }
    });
    forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
        if (c.getProperty ("instrument").toString() != "plugin" || ! isJsfx (c.getProperty ("plugin")))
            return;
        addOnce (where, channelName (juce::var (&c), i));
        c.setProperty ("instrument", "sampler");
        drop (c, "plugin");
        drop (c, "pluginState");
        drop (c, "samplePath");
    });
    if (! where.isEmpty())
        lost.push_back ({ TR_NOOP ("JSFX scripts are not in this release: their slots become empty (a switched-off Utility on an insert, an empty Sampler on a channel): {0}"),
                          listOf (where), true });
}

void toRelease0130 (juce::var& root, Losses& lost)
{
    auto& o = *objectOf (root);

    jsfxFor0130 (root, lost); // (round 190pj)

    // round 190ni: LYNX Model. 0.1.30 would play an unknown instrument as its basic synth: an empty Sampler (silent) keeps
    // the channel, its notes and its routing, as Tetra / Helix / Braid in the copy for 0.1.28
    {
        juce::StringArray lynxModel;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (c.getProperty ("instrument").toString() != "lynxmodel")
                return;
            lynxModel.add (channelName (juce::var (&c), i));
            c.setProperty ("instrument", "sampler");
            drop (c, "modelParams");
            drop (c, "pluginState");
            drop (c, "samplePath");
        });
        if (! lynxModel.isEmpty())
            lost.push_back ({ TR_NOOP ("LYNX Model is not in this release: its channels become empty Samplers and play nothing: {0}"), listOf (lynxModel), true });
    }

    // round 190ov: FL automation clips linked to several controls (target links on an FL import). 0.1.30 plays target links
    // 1:1 (no sixth element: a control's own scale, set where LYNX counts the control differently from FL), and a link's
    // control always follows the linked clip, even where another clip of that control plays. The scales go (the copy opened
    // again in a newer LYNX plays what 0.1.30 plays); a line for the clips with a scale or a control shared with another
    // clip in the Playlist
    {
        std::set<int> withClips;
        forEachClip (root, [&] (juce::Array<juce::var>& c) {
            if (c.size() >= 7 && (int) c[5] == 2)
                withClips.insert ((int) c[6]);
        });
        const auto key = [] (const juce::var& t) {
            return t.isArray() && t.size() >= 4 ? t[0].toString() + "/" + t[1].toString() + "/" + t[2].toString() + "/" + t[3].toString() : juce::String();
        };
        std::map<juce::String, int> drivers; // control → how many automations with clips drive it
        if (auto* automations = arrayOf (root["automations"]))
            for (int i = 0; i < automations->size(); ++i)
                if (withClips.count (i) != 0)
                {
                    const auto& a = (*automations)[i];
                    if (auto* links = arrayOf (a["links"]); links != nullptr && ! links->isEmpty())
                    {
                        std::set<juce::String> own;
                        for (const auto& l : *links)
                            own.insert (key (l));
                        for (const auto& k : own)
                            ++drivers[k];
                    }
                    else if (! a.hasProperty ("source"))
                        ++drivers[key (a["target"])];
                }
        juce::StringArray changed;
        if (auto* automations = arrayOf (root["automations"]))
            for (int i = 0; i < automations->size(); ++i)
                if (auto* links = arrayOf ((*automations)[i]["links"]))
                {
                    bool scaled = false, shared = false;
                    for (auto& l : *links)
                        if (auto* fields = arrayOf (l))
                        {
                            if (fields->size() > 5)
                            {
                                fields->resize (5);
                                scaled = true;
                            }
                            shared = shared || (withClips.count (i) != 0 && drivers[key (l)] > 1);
                        }
                    if ((scaled && withClips.count (i) != 0) || shared)
                        changed.add ("\"" + (*automations)[i].getProperty ("name", {}).toString() + "\"");
                }
        if (! changed.isEmpty())
            lost.push_back ({ TR_NOOP ("Automation clips linked to several controls move them 1:1 and win over the other clips of those controls: {0}"), listOf (changed), true });
    }

    // round 190nh: the Kilohearts part-2 laws on Effector (hidden [12] codes 41 … 48; round 190oc: 49, the Nonlinear
    // Filter; set only by the Kilohearts translator). 0.1.30 reads [12] in eighths (round 190nb's five laws, codes 8 … 40,
    // which stay), so codes 41 … 43 would play its Shaper law on these values: [12 …] are cut and Effector plays the
    // algorithm its visible Type names (the Nonlinear Filter: Filter)
    {
        juce::StringArray laws, nonlinear;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr || e.getProperty ("kind").toString() != "effector" || params->size() <= khsdsp::lawParam)
                return;
            const auto law = khsdsp::lawOf ((float) (double) (*params)[khsdsp::lawParam]);
            if (khsdsp::lawCode (law) < 41)
                return;
            addOnce (law == khsdsp::Law::nonlinearFilter ? nonlinear : laws, where);
            params->removeRange (khsdsp::lawParam, params->size() - khsdsp::lawParam);
        });
        if (! laws.isEmpty())
            lost.push_back ({ TR_NOOP ("Effector's newer Kilohearts laws (the Comb Filter, Chorus, Trance Gate, Phaser, Distortion, Bitcrush, Frequency Shifter and Ensemble stand-ins) play Effector's own algorithm: {0}"),
                              listOf (laws), true });
        if (! nonlinear.isEmpty())
            lost.push_back ({ TR_NOOP ("Effector's Kilohearts Nonlinear Filter law (the kHs Nonlinear Filter stand-in) plays Effector's own Filter: {0}"),
                              listOf (nonlinear), true });
    }

    // round 190oc: Helix round 4 (a fourth state block after 0.1.30's, revision 4): unison on the granular and spectral
    // oscillators and reverse grains. 0.1.30 reads the first wt::ext4Base values (revision 3 at most); the block is cut and
    // the revision taken back to 3, so the channel plays as there (one voice, forwards). A loss only when the block plays
    // something (a unison of more than one voice, or reverse grains); its automation then names values 0.1.30 has not
    {
        juce::StringArray helix4;
        const auto& d = wt::defaults();
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (c.getProperty ("instrument").toString() != "helix")
                return;
            auto values = juce::StringArray::fromTokens (c.getProperty ("modelParams").toString(), " ", "");
            values.removeEmptyStrings();
            if (values.size() <= wt::ext4Base)
                return;
            const auto at = [&] (int k) { return k < values.size() ? values[k].getFloatValue() : (float) d[(std::size_t) k]; };
            const auto revision = wt::extBase + wt::ext::revision;
            if (at (revision) >= 3.5f
                && (std::lround (at (wt::ext4Base + wt::ext4::grainUnison)) > 1 || std::lround (at (wt::ext4Base + wt::ext4::specUnison)) > 1
                    || at (wt::ext4Base + wt::ext4::grainReverse) > 0.0f))
                addOnce (helix4, channelName (juce::var (&c), i));
            values.removeRange (wt::ext4Base, values.size() - wt::ext4Base);
            if (at (revision) > (float) wt::revision190mt)
                values.set (revision, juce::String (wt::revision190mt));
            c.setProperty ("modelParams", values.joinIntoString (" "));
        });
        if (! helix4.isEmpty())
            lost.push_back ({ TR_NOOP ("Helix's unison on the granular and spectral oscillators and its reverse grains play one voice, forwards (their automation does nothing): {0}"),
                              listOf (helix4), true });
    }

    // round 190pr: an export of the song follows its Start and Marker skip markers ("exportMarkers", FL 2026's renders);
    // 0.1.30 exports the song straight through. The key goes; a line only when an arrangement has such a marker (markers:
    // [tick, name, numerator, denominator, action]; action 2 skip, 5 Start after bar 1)
    if (drop (o, "exportMarkers"))
    {
        bool followed = false;
        if (auto* arrangements = arrayOf (root["arrangements"]))
            for (const auto& a : *arrangements)
                if (auto* markers = arrayOf (a["markers"]))
                    for (const auto& m : *markers)
                        if (m.isArray() && m.size() >= 5)
                            followed = followed || (int) m[4] == 2 || ((int) m[4] == 5 && (juce::int64) m[0] > 0);
        if (followed)
            lost.push_back ({ TR_NOOP ("The export plays the song straight through in this release (from bar 1, past the skip markers)"), {}, true });
    }

    // round 190od (issue #236): FL's slide of the note properties ("slideNoteProps": a slide note moves the sounding notes'
    // velocity, pan and Mod X / Y to its own, as it moves their pitch). 0.1.30 slides the pitch only. The key goes; a loss
    // line only when it plays: a slide note whose velocity, pan or Mod X / Y differs from another note of its channel in
    // the same pattern (slide notes like the notes they slide sound the same in 0.1.30). Rollup 0.1.31 step 4
    if (drop (o, "slideNoteProps"))
    {
        int moving = 0, chords = 0;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
            {
                auto* notes = arrayOf (p["notes"]);
                if (notes == nullptr)
                    continue;
                // a note array: [start, length, channel, key, velocity (, pan, fine pitch, muted, slide, porta, release, mod X, mod Y ...)]
                struct Props { int channel; double velocity, pan, modX, modY; bool slide; };
                std::vector<Props> props;
                for (const auto& n : *notes)
                    if (n.isArray() && n.size() >= 5)
                        props.push_back ({ (int) n[2], (double) n[4], n.size() >= 8 ? (double) n[5] : 0.0,
                                           n.size() >= 13 ? (double) n[11] : 0.5, n.size() >= 13 ? (double) n[12] : 0.5,
                                           n.size() >= 9 && (bool) n[8] });
                for (const auto& s : props)
                    if (s.slide
                        && std::any_of (props.begin(), props.end(), [&s] (const Props& n) {
                               return ! n.slide && n.channel == s.channel
                                      && (n.velocity != s.velocity || n.pan != s.pan || n.modX != s.modX || n.modY != s.modY);
                           }))
                        ++moving;
                // round 190pi: a slide note over a chord (two keys or more of its channel sounding at its start) or over
                // notes of another colour moves only its colour's held notes, by one interval; 0.1.30 slides every one of
                // them to the slide note's key
                struct Span { double start, end; int channel, key, colour; bool slide; };
                std::vector<Span> spans;
                for (const auto& n : *notes)
                    if (n.isArray() && n.size() >= 5)
                        spans.push_back ({ (double) n[0], (double) n[0] + (double) n[1], (int) n[2], (int) n[3],
                                           n.size() >= 14 ? (int) n[13] : 0, n.size() >= 9 && (bool) n[8] });
                for (const auto& s : spans)
                {
                    if (! s.slide)
                        continue;
                    int first = -1;
                    for (const auto& n : spans)
                        if (! n.slide && n.channel == s.channel && n.start <= s.start && n.end > s.start)
                        {
                            if (n.colour != s.colour || (first >= 0 && n.key != first))
                            {
                                ++chords;
                                break;
                            }
                            first = n.key;
                        }
                }
            }
        if (moving > 0)
            lost.push_back ({ TR_NOOP ("{0} slide note(s) move only the pitch: the sounding notes keep their velocity, pan and Mod X / Y (FL's slide of the note properties)"),
                              juce::String (moving), true });
        if (chords > 0)
            lost.push_back ({ TR_NOOP ("{0} slide note(s) over chords or notes of other colours take every held note to the slide note's key (FL moves only its colour's notes, all by one interval)"),
                              juce::String (chords), true });
        // round 190pi: a Slide arpeggio's voice takes the first held note's velocity and expression; 0.1.30 the note it starts on
        int slideArps = 0;
        forEachChannel (root, [&] (juce::DynamicObject& c, int) {
            if (const auto& fl = c.getProperty ("arpFl"); fl.isObject() && (bool) fl.getProperty ("slide", false))
                ++slideArps;
        });
        if (slideArps > 0)
            lost.push_back ({ TR_NOOP ("{0} channel(s) with a Slide arpeggio start it at the velocity of the note it plays first, not of the first note held"),
                              juce::String (slideArps), true });
    }

    // round 190oa (DF-21): the toolbar's main volume is FL's monitor volume, on what the audio device plays only (never in
    // an export). 0.1.30 has no such value: the key goes; the mix and every export are the same, so a view line, only when
    // it isn't 0 dB. Rollup 0.1.31 step 4
    if (const auto main = o.getProperty ("mainVolume"); drop (o, "mainVolume") && std::abs ((double) main - (double) unityFader) > 1.0e-6)
        lost.push_back ({ TR_NOOP ("The main (monitor) volume of the toolbar; exports and the mix are the same"), {}, false });

    // round 190oa (DP-8): Gross Beat's ninth parameter, the scratch dial's "Time offset (scratching)" (0 = no offset). 0.1.30
    // knows eight: [8 …] are cut; a loss only for slots where it is set (it springs back to 0 when the dial is let go, so
    // that is a saved or automated offset). Rollup 0.1.31 step 4
    {
        juce::StringArray scratched;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr || e.getProperty ("kind").toString() != "grossBeat" || params->size() <= 8)
                return;
            if ((double) (*params)[8] != 0.0)
                addOnce (scratched, where);
            params->removeRange (8, params->size() - 8);
        });
        if (! scratched.isEmpty())
            lost.push_back ({ TR_NOOP ("Gross Beat's scratch offset (Time offset (scratching)) plays without the offset: {0}"), listOf (scratched), true });
    }

    // round 190om: SimSynth's velocity law (ChannelDef::engineRev 2). 0.1.30 plays every SimSynth note at full level
    {
        juce::StringArray simsynth;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (c.getProperty ("instrument").toString() == "simsynth" && (int) c.getProperty ("engineRev") >= 2)
            {
                c.setProperty ("engineRev", 1);
                addOnce (simsynth, channelName (juce::var (&c), i));
            }
        });
        if (! simsynth.isEmpty())
            lost.push_back ({ TR_NOOP ("SimSynth plays every note at full level in this release (the note velocity is left out): {0}"), listOf (simsynth), true });
    }

    // round 190nw: the tracker editor's view settings (rows per beat, columns, track groups). A view: the notes are the
    // patterns' own, so 0.1.30 plays and shows them in its Piano roll as they are
    if (drop (o, "tracker"))
        lost.push_back ({ TR_NOOP ("The tracker's view settings (rows per beat, columns, track groups) are left out; the notes are unchanged"), {}, false });

    // round 190ne (core/TrackClips.h): track clips and "Clips own their notes". Nothing they play changes: the clips are
    // patterns; 0.1.30 lists them with the others and its pencil places the track's pattern, as FL does
    {
        int trackClips = 0;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
                if (auto* po = objectOf (p))
                    trackClips += drop (*po, "trackClip") ? 1 : 0;
        const auto ownTracks = drop (o, "playlistOwnClips");
        if (trackClips > 0 || ownTracks)
            lost.push_back ({ TR_NOOP ("{0} track clip(s) show as ordinary patterns, and instrument tracks lose \"Clips own their notes\""),
                              juce::String (trackClips), false });
    }

    // round 190nc (DL-110): Live's Solo / Cue: the Main strip's Solo/Cue switch, Cue Out and Cue Volume (the clip grid's
    // "cue") and the tracks' Cue switches. A monitoring path: the Main mix is the same without them
    {
        bool cue = false;
        if (auto* grid = objectOf (root["clipGrid"]))
            cue = drop (*grid, "cue");
        if (auto* mixer = arrayOf (root["mixer"]))
            for (auto& t : *mixer)
                if (auto* track = objectOf (t))
                    cue = drop (*track, "cue") || cue;
        if (cue)
            lost.push_back ({ TR_NOOP ("Live's Solo/Cue settings (Cue mode, Cue Out, Cue Volume and the tracks' Cue switches); the Main mix plays the same"), {}, false });
    }

    // round 190nu (chord2_190nu): the Piano roll's key markers
    keyMarkersFor0130 (root, lost);

    // round 190oz: FL's step notes on generators, held (a pattern's "heldNotes"); 0.1.30 plays them one step long
    {
        int held = 0;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
                if (auto* po = objectOf (p))
                {
                    if (auto* h = arrayOf (po->getProperty ("heldNotes")))
                        held += h->size();
                    drop (*po, "heldNotes");
                }
        if (held > 0)
            lost.push_back ({ TR_NOOP ("{0} FL step note(s) on generators end after one step (FL lets them sound on)"), juce::String (held), true });
    }

    // round 190ol (P1): the Sections lane. 0.1.30 has no sections: each becomes a plain Playlist marker at its start, named
    // after it (unless a plain marker of that name is already there), so the song's parts stay visible and "Sections from
    // markers" brings them back. Nothing plays differently: a view line, only when the project has sections
    {
        int converted = 0;
        if (auto* arrangements = arrayOf (root["arrangements"]))
            for (auto& a : *arrangements)
            {
                auto* ao = objectOf (a);
                if (ao == nullptr)
                    continue;
                const auto list = ao->getProperty ("sections");
                if (! drop (*ao, "sections") || ! list.isArray())
                    continue;
                juce::Array<juce::var> markers;
                if (auto* old = arrayOf (ao->getProperty ("markers")))
                    markers = *old;
                for (const auto& sec : *list.getArray())
                {
                    if (! sec.isArray() || sec.size() < 3)
                        continue;
                    const auto tick = (juce::int64) sec[0];
                    const auto name = sec[2].toString();
                    const auto there = std::any_of (markers.begin(), markers.end(), [&] (const juce::var& m) {
                        return m.isArray() && m.size() >= 4 && (juce::int64) m[0] == tick && m[1].toString() == name && (int) m[2] == 0
                               && (m.size() < 5 || (int) m[4] == 0);
                    });
                    if (! there)
                        markers.add (juce::Array<juce::var> { tick, name, 0, 4 });
                    ++converted;
                }
                std::stable_sort (markers.begin(), markers.end(), [] (const juce::var& x, const juce::var& y) { return (juce::int64) x[0] < (juce::int64) y[0]; });
                ao->setProperty ("markers", markers);
            }
        if (converted > 0)
            lost.push_back ({ TR_NOOP ("{0} song section(s) of the Sections lane become plain Playlist markers at their starts"), juce::String (converted), false });
    }

    // round 190pf (P13): the Chord lane. 0.1.30 has none: each chord becomes a plain Playlist marker at its start, named
    // after it ("Am7"), as the .flp export writes them; the channels that follow the lane play their own notes there
    {
        int converted = 0;
        if (auto* arrangements = arrayOf (root["arrangements"]))
            for (auto& a : *arrangements)
            {
                auto* ao = objectOf (a);
                if (ao == nullptr)
                    continue;
                const auto list = ao->getProperty ("chords");
                if (! drop (*ao, "chords") || ! list.isArray())
                    continue;
                juce::Array<juce::var> markers;
                if (auto* old = arrayOf (ao->getProperty ("markers")))
                    markers = *old;
                for (const auto& v : *list.getArray())
                {
                    if (! v.isArray() || v.size() < 4)
                        continue;
                    SongChord c;
                    c.root = (int) v[2];
                    c.quality = juce::jlimit (0, 6, (int) v[3]);
                    if (v.size() >= 5)
                        if (auto* ext = v[4].getArray())
                            for (const auto& e : *ext)
                                c.extensions.push_back ((int) e);
                    c.bass = v.size() >= 6 ? (int) v[5] : -1;
                    const auto tick = (juce::int64) v[0];
                    const auto name = juce::String (chordlane::name (c));
                    const auto there = std::any_of (markers.begin(), markers.end(), [&] (const juce::var& m) {
                        return m.isArray() && m.size() >= 4 && (juce::int64) m[0] == tick && m[1].toString() == name && (int) m[2] == 0;
                    });
                    if (! there)
                        markers.add (juce::Array<juce::var> { tick, name, 0, 4 });
                    ++converted;
                }
                std::stable_sort (markers.begin(), markers.end(), [] (const juce::var& x, const juce::var& y) { return (juce::int64) x[0] < (juce::int64) y[0]; });
                ao->setProperty ("markers", markers);
            }
        if (converted > 0)
            lost.push_back ({ TR_NOOP ("{0} chord(s) of the Chord lane become plain Playlist markers at their starts"), juce::String (converted), false });
        int following = 0;
        if (auto* channels = arrayOf (root["channels"]))
            for (auto& ch : *channels)
                if (auto* co = objectOf (ch); co != nullptr && drop (*co, "followChords"))
                    ++following;
        if (following > 0)
            lost.push_back ({ TR_NOOP ("{0} channel(s) followed the Chord lane; they play their patterns' own notes"), juce::String (following), true });
    }

    // round 190oh (P3): FL's wrapper Settings › GUI, "DPI aware" (saved only when off) and "Scale editor dimensions" (saved
    // only when on), on every plugin reference (a channel's plugin, an insert slot's plugin and what it stands in for).
    // 0.1.30 opens every editor DPI aware at the size it reports: the keys go; a window only, so a view line naming the
    // plugins, only when one of them was set. Rollup 0.1.31 step 5
    {
        juce::StringArray editors;
        std::function<void (const juce::var&)> walk = [&] (const juce::var& v) {
            if (auto* a = arrayOf (v))
            {
                for (auto& item : *a)
                    walk (item);
                return;
            }
            auto* obj = objectOf (v);
            if (obj == nullptr)
                return;
            const auto name = obj->getProperty ("name").toString();
            const auto dpi = drop (*obj, "dpiAware");
            const auto dims = drop (*obj, "scaleEditorDimensions");
            if (dpi || dims)
                addOnce (editors, name.isNotEmpty() ? name : juce::String ("?"));
            for (auto& p : obj->getProperties())
                walk (p.value);
        };
        walk (juce::var (&o));
        if (! editors.isEmpty())
            lost.push_back ({ TR_NOOP ("FL's wrapper GUI settings (DPI aware off, Scale editor dimensions) are left out: these plugin windows open with the default ones: {0}"),
                              listOf (editors), false });
    }

    // round 190ot (research190oe P5): the Simple layout's "Duck under..." is a Fruity Limiter whose one-knob goal is 9
    // (goals::duck), keyed by the trigger through a level-0 send. 0.1.30 knows goals 1 … 8 only: the slot plays exactly
    // the same (ordinary parameters, side input and send) but its card shows the Limiter's Mix instead of the Amount
    // knob. The goal stays in the copy (opened again here it is a duck again); a view line, only when there is one.
    // The shared Echo bus is an ordinary insert with Echoform and ordinary sends: nothing to change
    {
        int ducks = 0;
        forEachEffect (root, [&ducks] (juce::DynamicObject& e, const juce::String&) {
            if (e.getProperty ("kind").toString() == "fruityLimiter" && (int) e.getProperty ("goal") == 9)
                ++ducks;
        });
        if (ducks > 0)
            lost.push_back ({ TR_NOOP ("{0} \"Duck under...\" effect(s) of the Simple layout show as an ordinary Fruity Limiter with its side input (the sound is the same)"),
                              juce::String (ducks), false });
    }

    // round 190ok (D3): EffectSlot::engineRev 2 = LYNX's own devices (Ember, Prism, Crest, Echoform...) compute their
    // per-sample tanh / exp / log / pow / sin with core/FastMath.h. 0.1.30 knows revisions 0 and 1 (Maximus: ">= 1"), so it
    // plays a slot at 2 as at 1, with the C library's maths (190ok's nulls: at most -135 dBFS apart). The copy says 1, so
    // the copy opened again in a newer LYNX plays what 0.1.30 plays, bit for bit; nothing audible to report.
    // Round 190ok (D2): ChannelDef::smartDisable (FL's generator smart disable of a hosted instrument): 0.1.30 runs the
    // plugin all the time, the same sound: dropped without a line (as 190ok's own 0.1.28 step). Rollup 0.1.31 step 5
    forEachEffect (root, [] (juce::DynamicObject& e, const juce::String&) {
        if ((int) e.getProperty ("engineRev") > 1)
            e.setProperty ("engineRev", 1);
    });
    forEachChannel (root, [] (juce::DynamicObject& c, int) { drop (c, "smartDisable"); });

    // round gap190og (coverage live4:mapping-0028): key and MIDI mappings of Live's Arrangement Overdub, Loop switch,
    // Punch-In and Punch-Out (RemoteAction values after launchSelectedClip). 0.1.30 skips an action it does not know when
    // it reads the project, so they go here with a line instead of vanishing silently. Rollup 0.1.31 step 5
    if (auto* mappings = arrayOf (root["remoteMappings"]))
    {
        int newer = 0;
        for (int i = mappings->size(); --i >= 0;)
            if ((int) (*mappings)[i].getProperty ("action", -1) > (int) RemoteAction::launchSelectedClip)
            {
                mappings->remove (i);
                ++newer;
            }
        if (mappings->isEmpty())
            drop (o, "remoteMappings");
        if (newer > 0)
            lost.push_back ({ TR_NOOP ("{0} key / MIDI mapping(s) of Overdub, the Loop switch, Punch-in or Punch-out are left out"), juce::String (newer), false });
    }

    // round 190oo (PAT-7, core/RackAutomation.h): the automation clips' rows in the Channel rack. 0.1.30's rack has no
    // automation rows and the clips play the same: dropped without a loss line
    if (auto* automations = arrayOf (root["automations"]))
        for (auto& a : *automations)
            if (auto* ao = objectOf (a))
                drop (*ao, "rackSlot");
    // round 190op (P4): hidden Playlist tracks (a view: they show again, nothing they play changes)
    if (auto* hidden = arrayOf (root["playlistTracksHidden"]))
    {
        const auto count = hidden->size();
        drop (o, "playlistTracksHidden");
        lost.push_back ({ TR_NOOP ("{0} hidden Playlist track(s) show again"), juce::String (count), false });
    }

    // round 190op (P4): deactivated channels and inserts. 0.1.30 has no such state: they come back muted, so the mix sounds
    // the same, and their plugins load again
    {
        juce::StringArray units;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (drop (c, "deactivated"))
            {
                units.add (channelName (juce::var (&c), i));
                c.setProperty ("muted", true);
            }
        });
        if (auto* mixer = arrayOf (root["mixer"]))
            for (int t = 0; t < mixer->size(); ++t)
                if (auto* m = objectOf ((*mixer)[t]); m != nullptr && drop (*m, "deactivated"))
                {
                    units.add (insertName (root["mixer"], t));
                    m->setProperty ("muted", true);
                }
        if (! units.isEmpty())
            lost.push_back ({ TR_NOOP ("Deactivated channels and inserts load their plugins again and come back muted: {0}"), listOf (units), false });
    }

    // round 190ow (DE-123, core/RackAutomation.h): what the automation clip rows play
    {
        // the steps / notes on the rows: 0.1.30 starts no clip from a pattern
        int triggers = 0;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
                if (auto* po = objectOf (p))
                {
                    if (auto* notes = arrayOf (po->getProperty ("autoNotes")))
                        triggers += notes->size();
                    drop (*po, "autoNotes");
                }
        if (triggers > 0)
            lost.push_back ({ TR_NOOP ("{0} step(s) or note(s) on automation clip rows start no clip (the clips in the Playlist play as before)"),
                              juce::String (triggers), true });

        // the MIN / MAX knobs: written into the points, so the clips play the same values (an LFO keeps its own range)
        // and a muted clip: no target, so it drives nothing, as here
        juce::StringArray muted;
        bool lfoRange = false;
        if (auto* automations = arrayOf (root["automations"]))
            for (auto& a : *automations)
                if (auto* ao = objectOf (a))
                {
                    if (const auto range = ao->getProperty ("range"); range.isArray() && range.size() >= 4)
                    {
                        const auto rMin = (double) range[0], rMax = (double) range[1], pMin = (double) range[2], pMax = (double) range[3];
                        if (rMin != pMin || rMax != pMax)
                        {
                            if (auto* points = arrayOf (ao->getProperty ("points")))
                                for (auto& pt : *points)
                                    if (auto* fields = arrayOf (pt); fields != nullptr && fields->size() >= 2)
                                    {
                                        const auto span = pMax - pMin;
                                        const auto x = std::abs (span) > 1.0e-9 ? ((double) (*fields)[1] - pMin) / span : 0.0;
                                        fields->set (1, juce::roundToInt (juce::jlimit (0.0, 1.0, rMin + (rMax - rMin) * x) * 10000.0) / 10000.0);
                                    }
                            if (auto* lfo = arrayOf (ao->getProperty ("lfo")); lfo != nullptr && ! lfo->isEmpty() && (bool) (*lfo)[0])
                                lfoRange = true;
                        }
                        drop (*ao, "range");
                    }
                    if (drop (*ao, "muted"))
                    {
                        muted.add (ao->getProperty ("name").toString());
                        ao->setProperty ("target", juce::Array<juce::var> { "none", 0, 0, 0 });
                    }
                }
        if (lfoRange)
            lost.push_back ({ TR_NOOP ("An automation clip's LFO keeps the clip's first range (its MIN / MAX knobs are written into the points)"), {}, true });
        if (! muted.isEmpty())
            lost.push_back ({ TR_NOOP ("Muted automation clips lose their target, so they stay silent: {0}"), listOf (muted), false });
    }

    // round 190ox (modulators stage 2): 0.1.30 has LFO modulators on LYNX's own controls only. The other kinds (they sit
    // in "modulators190ox", which 0.1.30 does not read) go with a line naming them; an LFO's routes to a hosted
    // plugin's parameters (which 0.1.30 would refuse) and the stage 2 fields go too; an automation of a Macro's knob
    // stays as an unlinked clip
    {
        juce::StringArray kinds, hosted;
        if (auto* more = arrayOf (root["modulators190ox"]))
            for (const auto& m : *more)
            {
                const auto kind = (int) m.getProperty ("kind", 0);
                const auto kindText = juce::String (Modulator::kindName ((ModKind) juce::jlimit (0, modKindCount - 1, kind)));
                addOnce (kinds, m["name"].toString() + " (" + kindText + ")");
            }
        drop (o, "modulators190ox");
        if (auto* mods = arrayOf (root["modulators"]))
            for (auto& m : *mods)
                if (auto* mo = objectOf (m))
                {
                    drop (*mo, "at");
                    drop (*mo, "host");
                    if (auto* routes = arrayOf (mo->getProperty ("routes")))
                        for (int i = routes->size(); --i >= 0;)
                            if ((*routes)[i].size() >= 6) // (a hosted plugin's parameter: its base as the sixth value)
                            {
                                routes->remove (i);
                                addOnce (hosted, mo->getProperty ("name").toString());
                            }
                }
        int macroClips = 0;
        if (auto* automations = arrayOf (root["automations"]))
            for (auto& a : *automations)
                if (auto* ao = objectOf (a))
                    if (const auto t = ao->getProperty ("target"); t.isArray() && t.size() >= 1 && t[0].toString() == "modulatorMacro")
                    {
                        ao->setProperty ("target", juce::Array<juce::var> { "none", 0, 0, 0 });
                        ++macroClips;
                    }
        if (! kinds.isEmpty())
            lost.push_back ({ TR_NOOP ("These modulators are not in this release (it has LFO modulators only): their targets keep their own values: {0}"),
                              listOf (kinds), true });
        if (! hosted.isEmpty())
            lost.push_back ({ TR_NOOP ("Modulation of hosted plugins' parameters is not in this release: these modulators move LYNX's own controls only: {0}"),
                              listOf (hosted), true });
        if (macroClips > 0)
            lost.push_back ({ TR_NOOP ("{0} automation clip(s) of a Macro modulator's knob stay as unlinked clips"), juce::String (macroClips), true });
    }

    // rollup 0.1.31 step 6 (round 190oq, issue #243): an ARA plugin on a mixer insert. 0.1.30 hosts ARA on an audio clip
    // channel only and does not read the slot's "ara": the plugin there is an ordinary insert effect and the routed clips
    // play without its edits. The key goes with a line (the 0.1.28 step then finds nothing left to drop)
    {
        juce::StringArray inserts;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            if (drop (e, "ara"))
                addOnce (inserts, where);
        });
        if (! inserts.isEmpty())
            lost.push_back ({ TR_NOOP ("ARA plugins on mixer inserts play as ordinary insert effects in this release (the audio clips routed there play without their edits): {0}"),
                              listOf (inserts), true });
    }

    // rollup 0.1.31 step 6 (round 190om, decisions 4 and 5): what 0.1.30 plays differently although nothing is dropped.
    // (a) A control that event lanes of several patterns drive: 0.1.30 binds it to the last pattern's lane only (the other
    // patterns' events do nothing there); this release plays the lane of the pattern whose clip started last, as FL.
    // (b) Notes' own curves (MPE, a module's slides) in Playlist clips: 0.1.30 sends them in Pattern mode only, so in Song
    // mode those notes play their key. Lines only; the copy keeps every event and curve.
    {
        std::map<juce::String, juce::StringArray> lanePatterns; // a lane's target (as JSON) -> the patterns with a lane on it
        juce::StringArray curvedInClips;
        std::set<int> placed;
        if (auto* clips = arrayOf (root["playlist"]))
            for (const auto& c : *clips)
                if (c.isArray() && c.size() >= 6 && (int) c[5] == 0)
                    placed.insert ((int) c[0]);
        if (auto* patterns = arrayOf (root["patterns"]))
            for (int pi = 0; pi < patterns->size(); ++pi)
            {
                const auto& p = (*patterns)[pi];
                const auto name = p["name"].toString().isNotEmpty() ? p["name"].toString() : "Pattern " + juce::String (pi + 1);
                if (auto* lanes = arrayOf (p["events"]))
                    for (const auto& l : *lanes)
                        if (auto* events = arrayOf (l["events"]); events != nullptr && ! events->isEmpty())
                            addOnce (lanePatterns[juce::JSON::toString (l["target"], true)], name);
                if (placed.count (pi) != 0)
                    if (auto* notes = arrayOf (p["notes"]))
                        if (std::any_of (notes->begin(), notes->end(), [] (const juce::var& n) {
                                return n.isArray() && n.size() > 19 && ! n[19].isVoid() && ! n[19].isUndefined();
                            }))
                            addOnce (curvedInClips, name);
            }
        juce::StringArray shared;
        for (const auto& [target, names] : lanePatterns)
            if (names.size() > 1)
                for (const auto& n : names)
                    addOnce (shared, n);
        if (! shared.isEmpty())
            lost.push_back ({ TR_NOOP ("Controls that several patterns' event lanes drive follow only the last pattern's lane in this release: {0}"),
                              listOf (shared), true });
        if (! curvedInClips.isEmpty())
            lost.push_back ({ TR_NOOP ("In Song mode these patterns' notes play without their own pitch / timbre / pressure curves in this release: {0}"),
                              listOf (curvedInClips), true });
    }
    // round 190pa (P21): the automation write mode. 0.1.30 records automation FL's way (Read): a line naming the mode
    if (const auto mode = root["automationWriteMode"].toString(); mode.isNotEmpty())
    {
        drop (o, "automationWriteMode");
        if (mode != "read")
            lost.push_back ({ TR_NOOP ("The automation write mode ({0}) goes back to FL's Record automation (Read)"), mode.substring (0, 1).toUpperCase() + mode.substring (1), false });
    }
    // round 190pa (P14): the range on chosen tracks (a selection: nothing plays differently)
    if (drop (o, "trackRange"))
        lost.push_back ({ TR_NOOP ("The range selected on some Playlist tracks is left out"), juce::String(), false });

    // rollup 0.1.31 step 7 (round 190pi): (a) Ogun / Autogun with FL's link velocity (ChannelDef::engineRev 2, a patch that
    // leaves it on): 0.1.30 plays every note of them at the channel's level; written at revision 1, so the copy plays the
    // same in newer LYNX. (b) Sytrus' main page (master level, LFO, pitch, Mod X / Y) takes automation from 0.1.31 on: in
    // 0.1.30 such a clip moves nothing. Lines; the data stays
    {
        juce::StringArray oguns, sytrusClips;
        std::set<int> sytrusChannels;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            const auto kind = c.getProperty ("instrument").toString();
            if (kind == "sytrus")
                sytrusChannels.insert (i);
            if ((kind != "ogun" && kind != "autogun") || (int) c.getProperty ("engineRev") < 2)
                return;
            c.setProperty ("engineRev", 1);
            juce::MemoryOutputStream decoded;
            if (juce::Base64::convertFromBase64 (decoded, c.getProperty ("pluginState").toString()))
            {
                const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
                if (ogunLinksVelocity (std::vector<std::uint8_t> (bytes, bytes + decoded.getDataSize()), kind == "autogun"))
                    addOnce (oguns, channelName (juce::var (&c), i));
            }
        });
        if (auto* automations = arrayOf (root["automations"]))
            for (const auto& a : *automations)
                if (const auto t = a["target"]; t.isArray() && t.size() >= 4 && t[0].toString() == "channelParam"  // ([kind, index,
                                                && sytrusChannels.count ((int) t[1]) != 0 && sytrus::automatable ((int) t[3])) // slot, param]; round 190pr: was t[2])
                    addOnce (sytrusClips, a["name"].toString());
        if (! oguns.isEmpty())
            lost.push_back ({ TR_NOOP ("Ogun and Autogun play every note at the channel's level in this release (FL's link velocity is left out): {0}"),
                              listOf (oguns), true });
        if (! sytrusClips.isEmpty())
            lost.push_back ({ TR_NOOP ("Automation of Sytrus' main knobs (master level, LFO, pitch, Mod X / Y) does nothing in this release: {0}"),
                              listOf (sytrusClips), true });
    }

    // round 190pr: Harmor's filter 1 / 2 frequencies and Mod X / Y / Z follow their automation from ChannelDef::engineRev 3
    // on; 0.1.30 ignores it. Written at revision 2, so the copy plays the same in newer LYNX. Lines; the data stays
    {
        juce::StringArray harmorClips;
        std::set<int> harmorChannels;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (c.getProperty ("instrument").toString() != "harmor" || (int) c.getProperty ("engineRev") < 3)
                return;
            c.setProperty ("engineRev", 2);
            harmorChannels.insert (i);
        });
        const auto drives = [&] (const juce::var& t) {
            return t.isArray() && t.size() >= 4 && t[0].toString() == "channelParam" && harmorChannels.count ((int) t[1]) != 0
                   && harmor::automatable ((int) t[3]); // ([kind, index, slot, param])
        };
        if (auto* automations = arrayOf (root["automations"]))
            for (const auto& a : *automations)
                if (drives (a["target"]))
                    addOnce (harmorClips, a["name"].toString());
        if (auto* patterns = arrayOf (root["patterns"]))
            for (const auto& p : *patterns)
                if (auto* lanes = arrayOf (p["events"]))
                    for (const auto& l : *lanes)
                        if (drives (l["target"]))
                            addOnce (harmorClips, p["name"].toString());
        if (! harmorClips.isEmpty())
            lost.push_back ({ TR_NOOP ("Automation of Harmor's filter frequencies and Mod X / Y / Z does nothing in this release: {0}"),
                              listOf (harmorClips), true });
    }

    // round 190pr: Sytrus hands the notes' Mod X / Y to its voices (FL's Final.FCut / FRes) from ChannelDef::engineRev 3 on;
    // 0.1.30 plays every Sytrus note at the default. Written at revision 2; a line naming the Sytrus channels with notes
    // off the default (notes: [start, length, channel, key, velocity, pan, fine, muted, slide, porta, release, mod X, mod Y…])
    {
        juce::StringArray sytrusNotes;
        std::map<int, juce::String> sytrusChannels;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (c.getProperty ("instrument").toString() != "sytrus" || (int) c.getProperty ("engineRev") < 3)
                return;
            c.setProperty ("engineRev", 2);
            sytrusChannels[i] = channelName (juce::var (&c), i);
        });
        if (! sytrusChannels.empty())
            if (auto* patterns = arrayOf (root["patterns"]))
                for (const auto& p : *patterns)
                    if (auto* notes = arrayOf (p["notes"]))
                        for (const auto& n : *notes)
                            if (n.isArray() && n.size() >= 13)
                                if (const auto found = sytrusChannels.find ((int) n[2]); found != sytrusChannels.end()
                                    && (std::abs ((double) n[11] - 0.5) > 1.0e-3 || std::abs ((double) n[12] - 0.5) > 1.0e-3))
                                    addOnce (sytrusNotes, found->second);
        if (! sytrusNotes.isEmpty())
            lost.push_back ({ TR_NOOP ("Sytrus plays every note at the default Mod X / Y in this release: {0}"), listOf (sytrusNotes), true });
    }

    // rollup 0.1.31 step 8: every other channel at engine revision 3 (FL imports and new channels since round 190pr) is
    // written at 2. Revision 3 changes only Harmor and Sytrus (both written at 2 above) and revision 2 only SimSynth and
    // Ogun / Autogun (written at 1 above), so the copy plays the same here and in newer LYNX, and a channel 0.1.30 later
    // turns into a Harmor or Sytrus cannot carry revision 3's laws back into a newer LYNX. 0.1.28 drops the key.
    forEachChannel (root, [&] (juce::DynamicObject& c, int) {
        if (c.hasProperty ("engineRev") && (int) c.getProperty ("engineRev") > 2)
            c.setProperty ("engineRev", 2);
    });
}

struct Step
{
    const char* release;                    // the release this step's output is for
    void (*apply) (juce::var&, Losses&);
};
// newest first: a copy for release R runs every step down to R's
constexpr Step steps[] = { { "0.1.30", toRelease0130 }, { "0.1.28", toRelease0128 }, { "0.1.27", toRelease0127 } };
} // namespace

juce::StringArray releases()
{
    juce::StringArray r;
    for (const auto& s : steps)
        r.add (s.release);
    return r;
}

Copy forRelease (const juce::String& projectJson, const juce::String& release)
{
    Copy copy;
    if (! releases().contains (release))
    {
        copy.error = "no such release: " + release;
        return copy;
    }
    auto root = juce::JSON::parse (projectJson);
    if (objectOf (root) == nullptr)
    {
        copy.error = "not a project";
        return copy;
    }
    const auto from = root["savedBy"].toString();
    // round 190pe (P20): track versions are newer than every release a copy is for. Each track keeps the version it
    // plays (the copy sounds the same); the others are left out, with the automation curves only their lanes used
    if (root.hasProperty ("playlistTrackVersions"))
    {
        juce::String error;
        if (auto song = ProjectFile::fromJson (projectJson, error))
        {
            if (const auto dropped = trackversions::keepActiveOnly (*song); dropped > 0)
                copy.lost.push_back ({ TR_NOOP ("{0} track version(s) that are not active are left out: each track keeps the version it plays"),
                                       juce::String (dropped), false });
            auto again = juce::JSON::parse (ProjectFile::toJson (*song, false));
            // (what the song does not hold, e.g. the window layout, stays as it was)
            for (const auto& p : objectOf (root)->getProperties())
                if (p.name.toString() != "playlistTrackVersions" && ! objectOf (again)->hasProperty (p.name))
                    objectOf (again)->setProperty (p.name, p.value);
            root = again;
        }
        else
            drop (*objectOf (root), "playlistTrackVersions");
    }
    for (const auto& s : steps)
    {
        s.apply (root, copy.lost);
        if (release == s.release)
            break;
    }
    // stamped as that release's own file (no "saved by a newer version" notice there); where it came from, for us
    objectOf (root)->setProperty ("savedBy", release);
    if (from.isNotEmpty())
        objectOf (root)->setProperty ("copiedFrom", from);
    copy.json = juce::JSON::toString (root, true);
    copy.ok = true;
    return copy;
}

Copy forRelease (const Song& song, const juce::String& release)
{
    return forRelease (ProjectFile::toJson (song), release);
}

juce::String copyName (const juce::String& projectName, const juce::String& release)
{
    return (projectName.isNotEmpty() ? projectName : juce::String ("Untitled")) + " (for LYNX " + release + ")";
}
} // namespace lynx::olderlynx
