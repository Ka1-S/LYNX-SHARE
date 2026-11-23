// Round 190od: GitHub issues #236 (slide notes move velocity and pan), #238 (the colour of tracks without one), #239
// (automatic track colours and icons; FL's "clips use note colors"), #240 (a new project from a template starts its own clock).
#include "core/AutoLook.h"
#include "core/HostSlide.h"
#include "core/NewClipColours.h"
#include "core/RackRenderer.h"
#include <cmath>

namespace issues190od
{
constexpr double rate = 48000.0;
constexpr double tick = 250.0; // one FL tick (PPQ 96) at 120 BPM, 48 kHz

// a channel of `kind` playing a sine (a Sampler: the sample at its root key) with a C5 for two beats at velocity
// `noteVel`, pan 0, and a slide note on the same key at FL tick 48, `flTicks` long, at `slideVel` / `slidePan`; the
// project's slide law `props`; left and right of 1.2 s rendered in blocks of `block`
struct Stereo
{
    std::vector<float> l, r;
};
Stereo render (lynx::InstrumentKind kind, bool props, float noteVel, float slideVel, float slidePan, int flTicks, int block, bool withSlide = true)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.slideNoteProps = props;
    ChannelDef c;
    c.instrument = kind;
    c.name = "Sine";
    c.samplePath = "sine";
    s.channels = { c };
    Pattern p;
    Note n;
    n.key = 60;
    n.start = 0;
    n.length = 2 * ppq;
    n.velocity = noteVel;
    Note slide;
    slide.key = 60;
    slide.start = 48 * ppq / 96;
    slide.length = flTicks * ppq / 96;
    slide.slide = true;
    slide.velocity = slideVel;
    slide.pan = slidePan;
    p.notes = { n };
    if (withSlide)
        p.notes.push_back (slide);
    s.patterns = { p };
    Clip clip;
    clip.pattern = 0;
    clip.length = 4 * ppq;
    s.playlist = { clip };
    auto data = std::make_shared<SampleData>();
    data->sampleRate = rate;
    data->left.resize ((std::size_t) (3.0 * rate));
    for (std::size_t i = 0; i < data->left.size(); ++i)
        data->left[i] = 0.25f * (float) std::sin (6.283185307179586 * 440.0 * (double) i / rate);
    data->right = data->left;
    const std::shared_ptr<const SampleData> shared = data;
    auto state = RackState::build (s, nullptr, rate, [shared] (const std::string&) -> std::shared_ptr<const SampleData> { return shared; });
    RackRenderer rr;
    rr.prepare (rate, block);
    const auto total = (int) (1.2 * rate);
    Stereo out { std::vector<float> ((std::size_t) total), std::vector<float> ((std::size_t) total) };
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += block)
    {
        const auto len = std::min (block, total - at);
        rr.process (state.get(), true, (double) at * tps, len, out.l.data() + at, out.r.data() + at, nullptr);
    }
    return out;
}

double rms (const std::vector<float>& x, double from, double to)
{
    double sum = 0.0;
    int count = 0;
    for (auto i = (std::size_t) from; i < (std::size_t) to && i < x.size(); ++i, ++count)
        sum += (double) x[i] * (double) x[i];
    return count > 0 ? std::sqrt (sum / count) : 0.0;
}
} // namespace issues190od

// #236 (FL manual, Piano roll › Slide notes: "During slides all note properties move toward those set in the slide
// note"; FL 2026's renders, REA_rodada190od): a slide note at a lower velocity fades the sounding note out to the level
// that velocity plays at, in n + 1 steps on FL's ticks; a higher one fades it in
TEST (issues190od_slide_note_fades_the_velocity_in_steps)
{
    using namespace issues190od;
    using lynx::InstrumentKind;
    for (const auto block : { 480, 64 })
    {
        const auto loud = render (InstrumentKind::sampler, true, 1.0f, 1.0f, 0.0f, 2, block, false);
        const auto quiet = render (InstrumentKind::sampler, true, 0.25f, 0.25f, 0.0f, 2, block, false);
        const auto fade = render (InstrumentKind::sampler, true, 1.0f, 0.25f, 0.0f, 2, block);
        const auto ref = rms (loud.l, 30 * tick, 46 * tick), low = rms (quiet.l, 60 * tick, 90 * tick);
        CHECK (ref > 0.01 && low > 0.0 && low < ref * 0.7);
        // before the slide: the note's own level; after it: the slide note's velocity's level
        CHECK (std::abs (rms (fade.l, 30 * tick, 46 * tick) / ref - 1.0) < 0.01);
        CHECK (std::abs (rms (fade.l, 60 * tick, 90 * tick) / low - 1.0) < 0.02);
        // the two ticks of the slide: steps between them, falling (step 1 of 3 at tick 48, 2 of 3 at tick 49)
        const auto t48 = rms (fade.l, 48 * tick + 20, 49 * tick - 20), t49 = rms (fade.l, 49 * tick + 20, 50 * tick - 20);
        CHECK (t48 < ref * 0.999 && t48 > t49 && t49 > low * 1.001);
        // the same project on the law before (projects saved before 0.1.31): the level stays
        const auto legacy = render (InstrumentKind::sampler, false, 1.0f, 0.25f, 0.0f, 2, block);
        CHECK (std::abs (rms (legacy.l, 60 * tick, 90 * tick) / ref - 1.0) < 0.01);
        // fade in: from the quiet note up to the loud level
        const auto in = render (InstrumentKind::sampler, true, 0.25f, 1.0f, 0.0f, 2, block);
        CHECK (std::abs (rms (in.l, 60 * tick, 90 * tick) / ref - 1.0) < 0.02);
    }
}

// #236: a slide note's pan moves the sounding note's pan (FL: all note properties); the same velocity and pan as the
// note's change nothing (bit for bit what the slide did before)
TEST (issues190od_slide_note_moves_the_pan_and_equal_props_change_nothing)
{
    using namespace issues190od;
    using lynx::InstrumentKind;
    const auto left = render (InstrumentKind::sampler, true, 0.8f, 0.8f, -1.0f, 4, 256);
    const auto l = rms (left.l, 60 * tick, 90 * tick), r = rms (left.r, 60 * tick, 90 * tick);
    CHECK (l > 0.01 && r < l * 0.05);
    CHECK (std::abs (rms (left.l, 30 * tick, 46 * tick) - rms (left.r, 30 * tick, 46 * tick)) < 1.0e-4); // (centred before)
    const auto same = render (InstrumentKind::sampler, true, 0.8f, 0.8f, 0.0f, 4, 256);
    const auto before = render (InstrumentKind::sampler, false, 0.8f, 0.8f, 0.0f, 4, 256);
    CHECK (same.l == before.l && same.r == before.r);
}

// #236: LYNX's own engines that glide (LYNX Synth) take the same steps: the rack splits their render calls for them
TEST (issues190od_slide_note_velocity_on_a_gliding_engine)
{
    using namespace issues190od;
    using lynx::InstrumentKind;
    const auto loud = render (InstrumentKind::synth, true, 1.0f, 1.0f, 0.0f, 8, 480, false);
    const auto fade = render (InstrumentKind::synth, true, 1.0f, 0.2f, 0.0f, 8, 480);
    const auto ref = rms (loud.l, 60 * tick, 90 * tick), after = rms (fade.l, 60 * tick, 90 * tick);
    CHECK (ref > 0.001 && after < ref * 0.6);
}

// #236: new projects take FL's law (the project file's key: the app's --issuesshot190od checks it)
TEST (issues190od_new_projects_take_the_slide_note_props_law)
{
    using namespace lynx;
    CHECK (makeNewProject().slideNoteProps);
    CHECK (! Song {}.slideNoteProps);
}

// #240: a template's own date and working time stay behind; the new project is dated when it starts
TEST (issues190od_template_clock_is_cleared)
{
    using namespace lynx;
    ProjectInfo info;
    info.title = "My template";
    info.created = 1.7e9;
    info.workSeconds = 1693.0;
    clearProjectClock (info);
    CHECK (info.created == 0.0 && info.workSeconds == 0.0 && info.title == "My template");
}

// #239 (LYNX extra, core/AutoLook.h): name rules colour and give an icon to what gets a matching name
TEST (issues190od_auto_look_rules_follow_names)
{
    using namespace lynx;
    autolook::Rules rules;
    rules.on = true;
    rules.list.push_back ({ "kick", autolook::Match::contains, 0xff3a6fd8, 7, autolook::everything });
    rules.list.push_back ({ "REC", autolook::Match::startsWith, 0, 33, autolook::everything });
    rules.list.push_back ({ "Bass", autolook::Match::is, 0xffc04040, -1, autolook::channels });
    CHECK (autolook::matches (rules.list[0], "  Big KICK 2 ") && ! autolook::matches (rules.list[0], "Kic"));
    CHECK (autolook::matches (rules.list[1], "rec vox") && ! autolook::matches (rules.list[1], "Vox REC"));
    CHECK (autolook::matches (rules.list[2], "bass") && ! autolook::matches (rules.list[2], "Bass 2"));

    auto song = Song::makeDefault();
    song.playlistTracks.resize (6);
    auto before = autolook::names (song, rules);
    song.playlistTracks[2].name = "Kick";
    song.mixer[4].name = "REC 1";
    CHECK (autolook::follow (song, before, rules) == 2);
    CHECK (song.playlistTracks[2].colour == 0xff3a6fd8 && song.playlistTracks[2].icon == 7);
    CHECK (song.mixer[4].icon == 33 && song.mixer[4].colour == 0u);

    // the user's own colour afterwards stays; a track inserted above shifts the names without renaming anything
    song.playlistTracks[2].colour = 0xff20a050;
    before = autolook::names (song, rules);
    song.playlistTracks.insert (song.playlistTracks.begin(), PlaylistTrack {});
    CHECK (autolook::follow (song, before, rules) == 0 && song.playlistTracks[3].colour == 0xff20a050);

    // a channel rule; not for a track of that name; rules off: nothing
    if (song.channels.size() > 1)
    {
        before = autolook::names (song, rules);
        song.channels[1].name = "Bass";
        song.playlistTracks[4].name = "Bass";
        autolook::follow (song, before, rules);
        CHECK (song.channels[1].colour == 0xffc04040 && song.playlistTracks[4].colour == 0u);
    }
    rules.on = false;
    before = autolook::names (song, rules);
    song.playlistTracks[5].name = "Kick 2";
    CHECK (autolook::follow (song, before, rules) == 0 && song.playlistTracks[5].colour == 0u);
    // Apply to this project: every match at once
    CHECK (autolook::applyAll (song, rules) >= 1 && song.playlistTracks[5].colour == 0xff3a6fd8);
    // the settings file
    rules.on = true;
    CHECK (autolook::fromText (autolook::toText (rules)) == rules);
    CHECK (autolook::fromText ("not a rules file").list.empty());
}

// FL's Theme › Options › "Audio and automation clips use note colors" (round 190od, core/NewClipColours.h): with the
// option on, what an edit adds without a colour (audio clip channels, automation clips) takes a note colour; off: nothing
TEST (issues190od_new_clips_use_note_colours)
{
    using namespace lynx;
    std::array<std::uint32_t, newclipcolours::count> notes {};
    for (int i = 0; i < newclipcolours::count; ++i)
        notes[(std::size_t) i] = 0xff100000u + (std::uint32_t) i;
    auto song = Song::makeDefault();
    const auto channels = song.channels.size(), automations = song.automations.size();
    ChannelDef clip;
    clip.instrument = InstrumentKind::audioClip;
    song.channels.push_back (clip);
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    song.channels.push_back (synth);
    song.automations.push_back ({});
    newclipcolours::set (false, notes);
    CHECK (newclipcolours::colourNew (song, channels, automations) == 0 && song.channels[channels].colour == 0u);
    newclipcolours::set (true, notes);
    CHECK (newclipcolours::colourNew (song, channels, automations) == 2);
    const auto isNote = [&notes] (std::uint32_t c) { return std::find (notes.begin(), notes.end(), c) != notes.end(); };
    CHECK (isNote (song.channels[channels].colour) && song.channels[channels + 1].colour == 0u && isNote (song.automations[automations].colour));
    // a colour already there stays; what was there before the edit stays
    song.channels[channels].colour = 0xff123456;
    CHECK (newclipcolours::colourNew (song, channels, automations + 1) == 0 && song.channels[channels].colour == 0xff123456u);
    newclipcolours::set (false, notes);
}
