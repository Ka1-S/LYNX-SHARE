// Round 190nz (issue #232 stage 2): the cross-window clipboard's text and documents (src/engine/CrossClipboard.h) read
// back exactly, inline and through the side file, and a pasted fragment merges as core/CrossPaste.h says.
#include "engine/CrossClipboard.h"
#include "engine/ProjectFile.h"
#include "core/CrossPaste.h"
#include "core/Groove.h"
#include <cstdio>
#include <stdexcept>

using namespace lynx;
namespace
{
int checks = 0, failures = 0;
#define EXPECT(condition)                                                                    \
    do                                                                                       \
    {                                                                                        \
        ++checks;                                                                            \
        if (! (condition))                                                                   \
        {                                                                                    \
            ++failures;                                                                      \
            std::fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
        }                                                                                    \
    } while (false)

juce::File sideFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-crossclip-tests");
}

crossclipboard::Payload roundTrip (const crossclipboard::Payload& p, std::size_t limit = crossclipboard::inlineLimit)
{
    const auto text = crossclipboard::clipboardText (p, sideFolder(), limit);
    EXPECT (text.isNotEmpty());
    const auto envelope = crossclipboard::envelopeOf (text);
    EXPECT (envelope.has_value());
    if (! envelope)
        return {};
    EXPECT (envelope->kind == p.kind && envelope->stamp == p.stamp && envelope->pid == p.pid);
    auto back = crossclipboard::open (*envelope);
    EXPECT (back.has_value());
    return back ? *back : crossclipboard::Payload {};
}

crossclipboard::Payload payload (crossclipboard::Kind kind)
{
    crossclipboard::Payload p;
    p.kind = kind;
    p.stamp = juce::Uuid().toString();
    p.pid = 4242;
    p.from = juce::String::fromUTF8 ("Álbum – faixa 2");
    return p;
}

void notes()
{
    auto p = payload (crossclipboard::Kind::notes);
    Note a;
    a.start = 96;
    a.length = 48;
    a.channel = 5; // (the channel does not travel: the paste puts the notes on the channel being edited)
    a.key = 67;
    a.velocity = 0.61f;
    a.pan = -0.25f;
    a.finePitch = 33.0f;
    a.slide = true;
    a.release = 0.2f;
    a.modX = 0.3f;
    a.modY = 0.9f;
    a.colour = 7;
    a.group = 2;
    a.repeat = 3;
    a.chance = 0.5f;
    a.velocityRange = 0.25f;
    a.chanceGroup = 1;
    a.chanceOne = true;
    NoteCurves curves;
    curves.lanes[0] = { { 0.0f, 0.5f }, { 0.5f, 0.75f } };
    a.curves = curves;
    Note b;
    b.start = 0;
    b.key = 48;
    b.muted = true;
    p.notes = { a, b };
    const auto back = roundTrip (p);
    EXPECT (back.from == p.from);
    EXPECT (back.notes.size() == 2u);
    if (back.notes.size() == 2u)
    {
        // sorted by start; every field but the channel kept
        auto expectA = a;
        expectA.channel = 0;
        auto expectB = b;
        expectB.channel = 0;
        EXPECT (back.notes[0].start == 0 && back.notes[0].key == 48 && back.notes[0].muted && back.notes[0].channel == 0);
        EXPECT (back.notes[1].key == 67 && back.notes[1].curves == expectA.curves);
        EXPECT (std::abs (back.notes[1].velocity - a.velocity) < 1.0e-4f && back.notes[1].chanceOne && back.notes[1].repeat == 3);
        EXPECT (back.notes[1].colour == 7 && back.notes[1].group == 2 && back.notes[1].slide);
    }
}

Song source()
{
    auto song = Song::makeDefault();
    ChannelDef lead;
    lead.name = "Lead";
    lead.instrument = InstrumentKind::plugin;
    lead.plugin.id = "VST3-Lead";
    lead.plugin.name = "Lead";
    lead.plugin.state = juce::Base64::toBase64 ("lead state", 10).toStdString();
    song.channels.push_back (lead);
    ChannelDef vox;
    vox.name = "Vox";
    vox.instrument = InstrumentKind::audioClip;
    vox.samplePath = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox take.wav").getFullPathName().toStdString();
    song.channels.push_back (vox);
    auto& pattern = song.patterns[0];
    pattern.notes.push_back ({ 0, 96, 5, 72, 0.5f });
    pattern.sortNotes();
    Automation a;
    a.name = "Lead volume";
    a.target = { AutomationTargetKind::channelVolume, 5, 0, 0 };
    a.points = { { 0, 0.1f, 0.2f, 0 }, { 192, 0.8f, 0.0f, 0 } };
    song.automations.push_back (a);
    Groove g;
    g.name = "Shuffle";
    g.timing = 0.7f;
    const auto id = groove::add (song.grooves, g);
    song.clipEnvelopes.push_back ({});
    song.clipEnvelopes.back().pan = { { 0, 0.2f, 0.0f, 0 } };
    song.playlist.clear();
    Clip c;
    c.pattern = 0;
    c.track = 0;
    c.start = 384;
    c.length = 384;
    c.groove = id;
    song.playlist.push_back (c);
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 6;
    audio.track = 1;
    audio.start = 0;
    audio.length = 192;
    audio.gain = 0.5f;
    audio.envelope = 0;
    song.playlist.push_back (audio);
    Clip automation;
    automation.kind = ClipKind::automation;
    automation.automation = 0;
    automation.track = 2;
    automation.length = 192;
    song.playlist.push_back (automation);
    return song;
}

void clips()
{
    const auto song = source();
    auto p = payload (crossclipboard::Kind::clips);
    p.fragment = crosspaste::clipsFragment (song, song.playlist);
    const auto back = roundTrip (p);
    const auto& f = back.fragment;
    EXPECT (f.channels.size() == p.fragment.channels.size());
    EXPECT (f.playlist.size() == 3u);
    for (std::size_t i = 0; i < juce::jmin (f.channels.size(), p.fragment.channels.size()); ++i)
    {
        EXPECT (f.channels[i].name == p.fragment.channels[i].name);
        EXPECT (f.channels[i].plugin.state == p.fragment.channels[i].plugin.state);
        EXPECT (f.channels[i].samplePath == p.fragment.channels[i].samplePath);
        EXPECT (f.channels[i].instrument == p.fragment.channels[i].instrument);
    }
    if (f.playlist.size() == 3u)
    {
        EXPECT (f.playlist[0].pattern >= 0 && f.patterns[(std::size_t) f.playlist[0].pattern].notes == p.fragment.patterns[0].notes);
        EXPECT (f.playlist[1].gain == 0.5f && f.playlist[1].envelope >= 0);
        EXPECT (f.automations.size() == 1u && f.automations[0].points == p.fragment.automations[0].points);
        EXPECT (f.grooves.grooves.size() == 1u && f.grooves.grooves[0].name == "Shuffle");
    }

    // pasted into another project: Kick..Synth are its own, Lead and Vox come in
    auto other = Song::makeDefault();
    const auto pasted = crosspaste::importClips (other, f);
    EXPECT (pasted.size() == 3u);
    EXPECT (other.channels.size() == 7u && other.channels[5].name == "Lead" && other.channels[6].name == "Vox");
    if (pasted.size() == 3u)
    {
        EXPECT (other.automations[(std::size_t) pasted[2].automation].target.index == 5);
        EXPECT (pasted[1].channel == 6);
        EXPECT (pasted[0].groove != 0);
        // the same paste again (a second Ctrl+V): nothing new in the project
        const auto patterns = other.patterns.size();
        crosspaste::importClips (other, f);
        EXPECT (other.patterns.size() == patterns && other.channels.size() == 7u);
    }
    // a fragment saved through the project's own code never brings a phantom pattern along
    Clip onlyAudio = song.playlist[1];
    auto q = payload (crossclipboard::Kind::clips);
    q.fragment = crosspaste::clipsFragment (song, { onlyAudio });
    const auto audioBack = roundTrip (q);
    auto third = Song::makeDefault();
    const auto patterns = third.patterns.size();
    EXPECT (crosspaste::importClips (third, audioBack.fragment).size() == 1u);
    EXPECT (third.patterns.size() == patterns && third.channels.size() == 6u);
}

void channelsThroughTheSideFile()
{
    auto song = source();
    // a plugin state larger than the inline limit: the clipboard holds a path, the file holds the document
    std::string big (200 * 1024, 'A');
    song.channels[5].plugin.state = big;
    auto p = payload (crossclipboard::Kind::channels);
    p.fragment = crosspaste::channelsFragment (song, { 5, 6 });
    const auto text = crossclipboard::clipboardText (p, sideFolder());
    EXPECT (text.length() < 1000);
    const auto envelope = crossclipboard::envelopeOf (text);
    EXPECT (envelope.has_value() && envelope->file.existsAsFile() && envelope->document.isEmpty());
    const auto back = envelope ? crossclipboard::open (*envelope) : std::nullopt;
    EXPECT (back.has_value());
    if (back)
    {
        EXPECT (back->fragment.channels.size() == 2u);
        EXPECT (back->fragment.channels.size() == 2u && back->fragment.channels[0].plugin.state == big);
        auto other = Song::makeDefault();
        const auto added = crosspaste::importChannels (other, back->fragment, 2);
        EXPECT (added == (std::vector<int> { 2, 3 }) && other.channels[2].name == "Lead" && other.channels[3].name == "Vox");
    }
    // the file gone (another window pruned it, the temp folder was emptied): nothing to paste, no crash
    if (envelope)
    {
        envelope->file.deleteFile();
        EXPECT (! crossclipboard::open (*envelope).has_value());
    }
    // only the newest side files stay
    for (int i = 0; i < 7; ++i)
    {
        auto q = payload (crossclipboard::Kind::channels);
        q.fragment = p.fragment;
        crossclipboard::clipboardText (q, sideFolder());
        juce::Thread::sleep (15);
    }
    EXPECT (sideFolder().findChildFiles (juce::File::findFiles, false, "*.json").size() <= 4);
    EXPECT (sideFolder().findChildFiles (juce::File::findFiles, false, "*.part").isEmpty());
}

void stepsMixerAndPoints()
{
    auto p = payload (crossclipboard::Kind::steps);
    ChannelStepsClipboard::Slot a;
    a.copied = true;
    a.notes = { { 0, 24, 0, 60, 0.7f }, { 96, 24, 0, 60, 0.9f } };
    a.loops = true;
    a.loopSetting = ChannelLoop::nextBeat;
    ChannelStepsClipboard::Slot gap; // (a channel that was not selected)
    ChannelStepsClipboard::Slot empty;
    empty.copied = true; // (a selected channel without notes: a paste clears that channel)
    p.steps.slots = { a, gap, empty };
    const auto steps = roundTrip (p).steps;
    EXPECT (steps.slots.size() == 3u);
    if (steps.slots.size() == 3u)
    {
        EXPECT (steps.slots[0].copied && steps.slots[0].notes.size() == 2u && steps.slots[0].loops && steps.slots[0].loopSetting == ChannelLoop::nextBeat);
        EXPECT (! steps.slots[1].copied && steps.slots[1].notes.empty());
        EXPECT (steps.slots[2].copied && steps.slots[2].notes.empty() && ! steps.slots[2].loops);
    }

    auto m = payload (crossclipboard::Kind::mixerTracks);
    MixerTrack t;
    t.name = "Vocal bus";
    t.colour = 0xff3080c0;
    t.volume = 0.62f;
    t.pan = -0.2f;
    EffectSlot eq;
    eq.kind = EffectKind::limiter;
    eq.params = { 0.5f, 0.25f, 0.75f };
    t.effects.push_back (eq);
    t.sends.push_back ({ 3, 0.5f }); // (routing belongs to the project: it does not travel)
    m.mixerTracks = { t, t };
    const auto tracks = roundTrip (m).mixerTracks;
    EXPECT (tracks.size() == 2u);
    if (! tracks.empty())
    {
        EXPECT (tracks[0].name == "Vocal bus" && tracks[0].colour == 0xff3080c0u);
        EXPECT (std::abs (tracks[0].volume - 0.62f) < 1.0e-4f && std::abs (tracks[0].pan + 0.2f) < 1.0e-4f);
        EXPECT (tracks[0].effects.size() == 1u && tracks[0].effects[0].kind == EffectKind::limiter && tracks[0].effects[0].params.size() >= 3u);
        if (! tracks[0].effects.empty() && tracks[0].effects[0].params.size() >= 3u) // (the effect's own parameter count; the three set kept)
            EXPECT (std::abs (tracks[0].effects[0].params[0] - 0.5f) < 1.0e-4f && std::abs (tracks[0].effects[0].params[2] - 0.75f) < 1.0e-4f);
        EXPECT (tracks[0].sends.empty());
    }

    auto pts = payload (crossclipboard::Kind::points);
    pts.points.points = { { 0, 0.25f, 0.5f, 2 }, { 96, 1.0f, -0.3f, 0 } };
    pts.points.span = 96;
    const auto points = roundTrip (pts).points;
    EXPECT (points.span == 96 && points.points.size() == 2u);
    if (points.points.size() == 2u)
        EXPECT (points.points[0].mode == 2 && std::abs (points.points[1].tension + 0.3f) < 1.0e-5f && points.points[1].tick == 96);
}

void notOurs()
{
    EXPECT (! crossclipboard::envelopeOf ("hello").has_value());
    EXPECT (! crossclipboard::envelopeOf ("").has_value());
    EXPECT (! crossclipboard::envelopeOf ("LYNX SHARE clipboard 9\nnotes x 1\n{}").has_value()); // (a newer format)
    EXPECT (! crossclipboard::envelopeOf ("LYNX SHARE clipboard 1\nsomething x 1\n{}").has_value());
    // an envelope whose document is not the one it names (half written, edited by hand)
    auto p = payload (crossclipboard::Kind::notes);
    p.notes = { Note {} };
    auto text = crossclipboard::clipboardText (p, sideFolder());
    auto envelope = crossclipboard::envelopeOf (text);
    EXPECT (envelope.has_value());
    if (envelope)
    {
        envelope->stamp = "other";
        EXPECT (! crossclipboard::open (*envelope).has_value());
        envelope->stamp = p.stamp;
        envelope->document = envelope->document.substring (0, envelope->document.length() / 2);
        EXPECT (! crossclipboard::open (*envelope).has_value());
    }
    EXPECT (! crossclipboard::fromJson ("{\"lynxClipboard\":2,\"kind\":\"notes\"}").has_value());
    EXPECT (crossclipboard::kindFromName ("mixerTracks") == crossclipboard::Kind::mixerTracks);
}
} // namespace

int main()
{
    sideFolder().deleteRecursively();
    notes();
    clips();
    channelsThroughTheSideFile();
    stepsMixerAndPoints();
    notOurs();
    sideFolder().deleteRecursively();
    std::printf ("cross-window clipboard tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
