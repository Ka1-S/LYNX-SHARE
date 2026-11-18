#include "engine/ProjectDowngrade.h"

#include "engine/ProjectFile.h"

#include "core/DirectWaveState.h"
#include "core/EchoSuite.h"
#include "core/FacetEq.h"
#include "core/GeneratorPresets.h"
#include "core/Harmor.h"
#include "core/LuxeVerb.h"
#include "core/LynxVerbs.h"
#include "core/MixSuite.h"
#include "core/NewtoneFx.h"
#include "core/RackRenderer.h"
#include "core/VoiceShift.h"

#include <cmath>
#include <functional>

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

    // channels: Tetra (round 190hs) is not in 0.1.28, which would play its channels as its basic synth: an empty Sampler
    // (silent) keeps the channel, its notes and its routing; DirectWave states (rounds 190hz / 190ii) get their short zone
    // records completed as DirectWave reads them, since 0.1.28 skipped them (FL's version-36 programs lost their loops)
    {
        juce::StringArray tetra, helix;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            const auto instrument = c.getProperty ("instrument").toString();
            if (instrument == "tetra" || instrument == "helix") // (rollup step 3: Helix, round 190ja, as Tetra)
            {
                (instrument == "tetra" ? tetra : helix).add (channelName (juce::var (&c), i));
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
    }

    // notes: index 19 holds a note's MPE curves (round 190hy)
    {
        int curved = 0;
        if (auto* patterns = arrayOf (root["patterns"]))
            for (auto& p : *patterns)
                if (auto* notes = arrayOf (p["notes"]))
                    for (auto& n : *notes)
                        if (auto* fields = arrayOf (n); fields != nullptr && fields->size() > 19)
                        {
                            ++curved;
                            fields->removeRange (19, fields->size() - 19);
                        }
        if (curved > 0)
            lost.push_back ({ TR_NOOP ("{0} note(s) lose their MPE curves (pitch bend, slide, pressure)"), juce::String (curved), true });
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
        juce::StringArray thump, pink, hushTiming, hushVocal, echoVoicings, latticeTaps, luxeDucking, verbRevisions;
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
                params->removeRange (mixsuite::ridge::k4Pink, params->size() - mixsuite::ridge::k4Pink);
            }
            else if (kind == "hushDeEsser" && params->size() > mixsuite::hush::timing)
            {
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
        if (! hushTiming.isEmpty())
            lost.push_back ({ TR_NOOP ("Hush De-esser's Allround mode plays its earlier timing: {0}"), listOf (hushTiming), true });
        if (! hushVocal.isEmpty())
            lost.push_back ({ TR_NOOP ("Hush De-esser's Single Vocal mode plays its earlier version: {0}"), listOf (hushVocal), true });
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

    // rollup step 4, round 190jh: Facet EQ's automatic dynamic thresholds (hidden parameters past its count; 0.1.28 plays
    // those bands with their manual threshold)
    {
        juce::StringArray autoBands;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            auto* params = arrayOf (e.getProperty ("params"));
            if (params == nullptr || e.getProperty ("kind").toString() != "facetEq" || params->size() <= facet::autoThresholdParam0)
                return;
            for (int b = 0; b < 8 && facet::autoThresholdParam0 + b < params->size(); ++b)
                if ((float) (double) (*params)[facet::autoThresholdParam0 + b] >= 0.5f)
                    addOnce (autoBands, where);
            params->removeRange (facet::autoThresholdParam0, params->size() - facet::autoThresholdParam0);
        });
        if (! autoBands.isEmpty())
            lost.push_back ({ TR_NOOP ("Facet EQ's automatic dynamic thresholds play with the bands' manual threshold: {0}"), listOf (autoBands), true });
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

    // rollup step 6, round natives190kj: ChannelDef::engineRev (Harmor 1 = a part with a sample in its IMG section plays
    // FL's resynthesis; 0.1.28 plays such a part's timbres) and Newtone's state version 3 (+ the key, each note's voiced
    // end and velocity): 0.1.28 reads up to version 2 and would load no notes at all, so the state is written as version
    // 2 (the notes and every edit kept; the voiced end only shaped the analysed smooth times, which are saved)
    {
        juce::StringArray resynth, newtone;
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
            }
        });
        if (! resynth.isEmpty())
            lost.push_back ({ TR_NOOP ("Harmor parts with a sample in the IMG section play their timbres instead of the resynthesis: {0}"), listOf (resynth), true });
        if (! newtone.isEmpty())
            lost.push_back ({ TR_NOOP ("Newtone's detected key and note velocities are left out: {0}"), listOf (newtone), false });
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
        juce::StringArray views;
        forEachEffect (root, [&] (juce::DynamicObject& e, const juce::String& where) {
            if (drop (e, "displayView"))
                addOnce (views, where);
        });
        if (! views.isEmpty())
            lost.push_back ({ TR_NOOP ("Fruity Limiter's CURVE view (its display shows the scrolling graph): {0}"), listOf (views), false });

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

    // round 190kx: audio clips edited in an ARA plugin (Melodyne...) play unedited in 0.1.28 (it hosts no ARA)
    {
        juce::StringArray edited;
        forEachChannel (root, [&] (juce::DynamicObject& c, int i) {
            if (drop (c, "ara"))
                edited.add (channelName (juce::var (&c), i));
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

struct Step
{
    const char* release;                    // the release this step's output is for
    void (*apply) (juce::var&, Losses&);
};
// newest first: a copy for release R runs every step down to R's
constexpr Step steps[] = { { "0.1.28", toRelease0128 }, { "0.1.27", toRelease0127 } };
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
