// Round 190gw: FL Studio 2026 facts from the live screen session 190gu (/mnt/e/lynx-work/live190gu/FACTS.md) applied
// FL-exact: the Playlist header's side strips and FL's 48 px default row (B1), the mixer's "Insert multiple..." /
// "Add multiple..." boxes (B4), the Sampler waveform menu and the audio clip menu (B3), FLM's 3x Osc switches (B6).

#include "core/ClipOps.h"
#include "core/EditorPolish.h"
#include "core/FlClipMenu.h"
#include "core/FlpExport.h"
#include "core/LinkFormula.h"
#include "core/MixerOps.h"
#include "core/SamplerPage.h"
#include "core/Spectrogram.h"
#include "core/TrackHeaderGestures.h"

namespace flfacts190gw
{
// two Synth channels, A on insert 1 and B on insert 2, each a 4-beat note from the start (song mode)
inline lynx::Song twoSynths()
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.clear();
    s.playlist.clear();
    s.automations.clear();
    s.patterns.clear();
    s.mixer = Song::defaultMixer (3);
    s.mode = PlayMode::song;
    for (int k = 0; k < 2; ++k)
    {
        ChannelDef c;
        c.name = k == 0 ? "Amount Too - Bass" : "Amount Too - Nylon GTR";
        c.instrument = InstrumentKind::synth;
        c.mixerTrack = 1 + k;
        s.channels.push_back (c);
        Pattern p;
        p.name = "P" + std::to_string (k + 1);
        Note n;
        n.start = 0;
        n.length = 4 * ppq;
        n.key = 72;
        n.channel = k;
        p.notes.push_back (n);
        s.patterns.push_back (p);
        Clip clip;
        clip.kind = ClipKind::pattern;
        clip.pattern = k;
        clip.track = k;
        clip.start = 0;
        clip.length = 4 * ppq;
        s.playlist.push_back (clip);
    }
    return s;
}

// the RMS of the output over the second half of `blocks` blocks with only channel `alone`'s pattern clip playing
inline double levelOf (lynx::Song song, int alone, int blocks = 120)
{
    constexpr int rate = 44100, block = 256;
    for (auto& c : song.playlist)
        if (c.kind == lynx::ClipKind::pattern && c.pattern != alone)
            c.muted = true;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, block);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    double sum = 0.0;
    long count = 0;
    double tick = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        tick += block * tps;
        if (b < blocks / 2)
            continue;
        for (const auto x : l)
            sum += (double) x * x;
        count += block;
    }
    return std::sqrt (sum / (double) count);
}

inline std::pair<double, double> insertLevels (const lynx::Song& song) { return { levelOf (song, 0), levelOf (song, 1) }; }
} // namespace flfacts190gw

TEST (flfacts190gw_header_strips_are_fls)
{
    using namespace lynx::trackhdr;
    // B1: the left 10 px flip the size; the right 9 px column selects; the rest is the header's plate
    CHECK (lockStripWidth == 10 && selectionStripWidth == 9);
    CHECK (zoneAt (0, 96) == HeaderZone::lockStrip && zoneAt (9, 96) == HeaderZone::lockStrip && zoneAt (10, 96) == HeaderZone::plate);
    CHECK (zoneAt (86, 96) == HeaderZone::plate && zoneAt (87, 96) == HeaderZone::selectionStrip && zoneAt (95, 96) == HeaderZone::selectionStrip);
    // click = alone, Shift+click and right-click toggle (no menu), double click = only <-> all, Ctrl = the header's Ctrl
    CHECK (stripPress (false, false, false, false) == StripAction::only);
    CHECK (stripPress (false, true, false, false) == StripAction::toggle && stripPress (false, false, true, false) == StripAction::toggle);
    CHECK (stripPress (false, false, true, true) == StripAction::toggle && stripPress (true, true, true, false) == StripAction::toggle);
    CHECK (stripPress (false, false, false, true) == StripAction::onlyOrAll && stripPress (true, false, false, false) == StripAction::ctrlPress);
    // FL's live sequence (fl-right-strip-*.png): click 1 alone, Shift+click 2 adds, Shift+click 1 removes, right-click 5 adds
    std::vector<int> sel { 4 };
    applySelection (sel, 0, SelMode::only);
    applySelection (sel, 1, SelMode::toggle);
    CHECK ((sel == std::vector<int> { 0, 1 }));
    applySelection (sel, 0, SelMode::toggle);
    applySelection (sel, 4, SelMode::toggle);
    CHECK ((sel == std::vector<int> { 1, 4 }));
    // mode 5: alone -> every track -> alone
    applySelection (sel, 2, SelMode::only);
    applyOnlyOrAll (sel, 2, 6);
    CHECK ((sel == std::vector<int> { 0, 1, 2, 3, 4, 5 }));
    applyOnlyOrAll (sel, 2, 6);
    CHECK ((sel == std::vector<int> { 2 }));
    // the left strip's flip: a row never locked becomes the smallest (16 px), a second click gives its size back
    CHECK (flippedLock (-1) == 1 && flippedLock (1) == -1 && flippedLock (80) == -80);
}

TEST (flfacts190gw_fl_default_row_is_48)
{
    using namespace lynx;
    // B1: FL's Default template row is 48 px; new songs take it, the size factor works on it
    CHECK (Song::flDefaultRowHeight == 48 && Song::legacyRowHeight == 38);
    CHECK (Song().playlistRowHeight == 48 && Song::makeDefault().playlistRowHeight == 48);
    CHECK (polish::heightForFlSize (1.0, 48) == 48 && polish::heightForFlSize (0.0, 48) == 16 && polish::heightForFlSize (2.0, 48) == 80);
    CHECK (polish::heightForFlSize (0.5, 48) == 32 && polish::heightForFlSize (1.5, 48) == 64 && polish::heightForFlSize (1.0 / 3.0, 48) == 26);
    CHECK (std::abs (polish::flSizeForHeight (80, 48) - 2.0f) < 1.0e-6f && std::abs (polish::flSizeForHeight (48, 48) - 1.0f) < 1.0e-6f);
    // a project kept on LYNX's old 38 px row exports its sizes on that row (its look is kept: 60 px = 200 %)
    auto song = Song::makeDefault();
    song.playlistRowHeight = Song::legacyRowHeight;
    song.playlistTracks.resize (2);
    song.playlistTracks[1].height = 60;
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    float size1 = -1.0f;
    for (const auto& a : project.arrangements)
        for (const auto& t : a.tracks)
            if (t.index == 1)
                size1 = t.heightScale;
    CHECK (std::abs (size1 - 2.0f) < 1.0e-6f);
    // ...and FL opens it at 200 % of its own 48 px row
    const auto back = importFlp (project);
    CHECK (back.playlistRowHeight == 48 && back.playlistTracks.size() >= 2 && back.playlistTracks[1].height == 80);
}

TEST (flfacts190gw_mixer_insert_multiple_range)
{
    using namespace lynx;
    // B4: FL's rule is 502 tracks (Master + 500 inserts + Current): the box takes 1 .. 500 - inserts. With 123 inserts
    // (the live session's project) that is FL's "Please enter a number between 1 and 377." for 600
    Song song;
    song.mixer = Song::defaultMixer (123); // (Master + 123 inserts)
    CHECK (mixerops::insertCount (song) == 123 && mixerops::roomForInserts (song) == 377);
    CHECK (mixerops::checkInsertMultiple (song, 600, false) == mixerops::InsertCheck::outOfRange);
    CHECK (mixerops::checkInsertMultiple (song, 0, false) == mixerops::InsertCheck::outOfRange);
    CHECK (mixerops::checkInsertMultiple (song, 378, false) == mixerops::InsertCheck::outOfRange);
    CHECK (mixerops::checkInsertMultiple (song, 377, false) == mixerops::InsertCheck::ok);
    CHECK (mixerops::checkInsertMultiple (song, 2, false) == mixerops::InsertCheck::ok);
    // the "+" menu's Add multiple...: only more than the room is refused ("You can add a maximum of 377 tracks.")
    CHECK (mixerops::checkInsertMultiple (song, 600, true) == mixerops::InsertCheck::tooMany);
    CHECK (mixerops::checkInsertMultiple (song, 0, true) == mixerops::InsertCheck::ok);
    // 2 inserted before track 9: "Insert 9", "Insert 10", the old track 9 is now 11
    song.mixer[9].name = "Percussion";
    CHECK (mixerops::insertTracks (song, 9, 2) == 2);
    CHECK (song.mixer[9].name == "Insert 9" && song.mixer[10].name == "Insert 10" && song.mixer[11].name == "Percussion");
    // FL 2026's 125-insert default: room 375 (167a's "You can add a maximum of 375 tracks.")
    song.mixer = Song::defaultMixer (125);
    CHECK (mixerops::roomForInserts (song) == 375);
}

TEST (flfacts190gw_open_in_new_channel_takes_the_sample_name)
{
    using namespace lynx;
    Song song;
    song.channels.clear();
    CHECK (samplerpage::openInNewChannelName (song, "Basic 808 Clap") == "Basic 808 Clap");
    ChannelDef c;
    c.name = "Basic 808 Clap";
    song.channels.push_back (c);
    // FL: "Basic 808 Clap #2" then "#3" next to the Sampler channel "Basic 808 Clap"
    CHECK (samplerpage::openInNewChannelName (song, "Basic 808 Clap") == "Basic 808 Clap #2");
    c.name = "Basic 808 Clap #2";
    song.channels.push_back (c);
    CHECK (samplerpage::openInNewChannelName (song, "Basic 808 Clap") == "Basic 808 Clap #3");
}

TEST (flfacts190gw_sampler_views_and_spectrogram)
{
    using namespace lynx;
    // B3: both view switches are in the menu now, global, off by default
    std::vector<std::string> have;
    for (const auto& e : samplerpage::waveMenu())
        if (e.text != nullptr && e.have)
            have.push_back (e.text);
    CHECK (std::find (have.begin(), have.end(), "Spectrum view") != have.end());
    CHECK (std::find (have.begin(), have.end(), "Multichannel waveform view") != have.end());
    CHECK (std::find (have.begin(), have.end(), "Edit in pitch corrector") != have.end());
    CHECK (std::find (have.begin(), have.end(), "Edit in time warper") != have.end());
    CHECK (std::find (have.begin(), have.end(), "Open in new Fruity Slicer 2 channel") == have.end()); // (no Slicer 2)
    CHECK (! samplerpage::waveViews().spectrum && ! samplerpage::waveViews().multichannel);

    // the spectrogram: a 1 kHz sine at 44.1 kHz is loudest in bin round(1000 / 44100 * 1024) = 23 of every column
    std::vector<float> sine (44100);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = (float) (0.5 * std::sin (2.0 * 3.14159265358979 * 1000.0 * (double) i / 44100.0));
    const auto cells = spectrogram::compute (sine.data(), nullptr, (std::int64_t) sine.size(), 0.0, (double) sine.size(), 8);
    CHECK (cells.columns == 8 && cells.bins == 512 && (int) cells.level.size() == 8 * 512);
    for (int c = 1; c < 7; ++c)
    {
        const auto* col = cells.level.data() + (std::size_t) c * 512;
        const auto peak = (int) (std::max_element (col, col + 512) - col);
        CHECK (peak == 23 && col[peak] > 0.99f && col[200] < 0.2f);
    }
    // silence: every cell at the floor; the heat colours run black -> yellow
    std::vector<float> quiet (4096, 0.0f);
    const auto none = spectrogram::compute (quiet.data(), quiet.data(), 4096, 0.0, 4096.0, 4);
    CHECK (std::all_of (none.level.begin(), none.level.end(), [] (float v) { return v == 0.0f; }));
    CHECK (spectrogram::colourOf (0.0f) == 0xFF000000u && spectrogram::colourOf (1.0f) == 0xFFFCEC4Au);
    const auto mid = spectrogram::colourOf (0.68f); // crimson
    CHECK (((mid >> 16) & 0xFF) > 150 && ((mid >> 8) & 0xFF) < 60);
}

TEST (flfacts190gw_clip_menu_items)
{
    using namespace lynx;
    // B3: Pitch-correct / Time-warp sample (LYNX's Newtone / Newtime), Crossfade with (enabled when there is a clip to
    // crossfade with); Deverb stays out (FL 2026 only offers a download there)
    const auto all = flclipmenu::shown (false, false);
    const auto has = [&all] (const char* text) {
        return std::any_of (all.begin(), all.end(), [text] (const flclipmenu::Entry& e) { return e.text == text; });
    };
    CHECK (has ("Pitch-correct sample") && has ("Time-warp sample") && has ("Crossfade with") && ! has ("Deverb"));

    // Newtone / Newtime on the Master: a new one in the first empty slot, the sample loaded; the same slot next time
    auto song = Song::makeDefault();
    song.mixer[0].effects.clear();
    const auto tone = mixerops::openSampleInMasterEditor (song, EffectKind::newtone, "/x/Amount Too - Bass.wav");
    CHECK (tone == 0 && song.mixer[0].effects.size() == 1 && song.mixer[0].effects[0].kind == EffectKind::newtone
           && song.mixer[0].effects[0].samplePath == "/x/Amount Too - Bass.wav");
    song.mixer[0].effects[0].state = { 1, 2, 3 };
    CHECK (mixerops::openSampleInMasterEditor (song, EffectKind::newtone, "/x/other.wav") == 0 && song.mixer[0].effects.size() == 1
           && song.mixer[0].effects[0].samplePath == "/x/other.wav" && song.mixer[0].effects[0].state.empty());
    const auto time = mixerops::openSampleInMasterEditor (song, EffectKind::newtime, "/x/a.wav");
    CHECK (time == 1 && song.mixer[0].effects[1].kind == EffectKind::newtime);
    CHECK (mixerops::openSampleInMasterEditor (song, EffectKind::reverb, "/x/a.wav") == -1);
    song.mixer[0].effects.clear();
    for (int i = 0; i < maxEffectSlots; ++i)
        song.mixer[0].effects.push_back (makeEffectSlot (EffectKind::reverb));
    CHECK (mixerops::openSampleInMasterEditor (song, EffectKind::newtone, "/x/a.wav") == -1); // (the Master is full)
}

TEST (flfacts190gw_crossfade_with_makes_fls_clip)
{
    using namespace lynx;
    // B3: A (bars 1-5) and B (bars 3-7) on different channels; a third clip of A's channel is not offered
    Song song;
    song.channels.resize (2);
    song.channels[0].name = "Amount Too - Bass";
    song.channels[1].name = "Amount Too - Nylon GTR";
    const auto bar = 4 * (std::int64_t) ppq;
    const auto audio = [&song] (int channel, int track, std::int64_t start, std::int64_t length) {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = channel;
        c.track = track;
        c.start = start;
        c.length = length;
        song.playlist.push_back (c);
    };
    audio (0, 0, 0, 4 * bar);
    audio (1, 1, 2 * bar, 4 * bar);
    audio (0, 2, 3 * bar, bar);
    audio (1, 3, 8 * bar, bar); // (no overlap)
    CHECK ((clipops::crossfadeCandidates (song, 0) == std::vector<int> { 1 }));
    CHECK ((clipops::crossfadeCandidates (song, 1) == std::vector<int> { 0, 2 }));
    CHECK (clipops::crossfadeCandidates (song, 3).empty());
    CHECK (clipops::crossfadeWith (song, 0, 3) == -1 && clipops::crossfadeWith (song, 0, 2) == -1);

    const auto made = clipops::crossfadeWith (song, 0, 1);
    CHECK (made == 0 && song.automations.size() == 1);
    const auto& a = song.automations[0];
    CHECK (a.name == "Amount Too - Bass to Amount Too - Nylon GTR crossfade");
    CHECK (a.links.size() == 2 && a.target == a.links[0].target);
    CHECK ((a.links[0].target == AutomationTarget { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume }));
    CHECK ((a.links[1].target == AutomationTarget { AutomationTargetKind::channelAdjust, 1, 0, leveladjust::volume }));
    CHECK (a.links[0].formula == "0.5*Tension(Input,10)" && a.links[1].formula == "0.5-0.5*Tension(Input,-10)");
    CHECK (a.points.size() == 2 && a.points[0].value == 1.0f && a.points[1].value == 0.0f && a.points[1].tick == 2 * bar);
    const auto& clip = song.playlist.back();
    CHECK (clip.kind == ClipKind::automation && clip.automation == 0 && clip.track == 0 && clip.start == 2 * bar && clip.length == 2 * bar);
    // FL's two formulas: at the ramp's top A plays at 100 % (0.5) and B is silent; at its end the other way round
    const auto out = linkformula::Formula::compile (a.links[0].formula), in = linkformula::Formula::compile (a.links[1].formula);
    CHECK (std::abs (out.map (1.0f) - 0.5f) < 1e-6f && std::abs (in.map (1.0f)) < 1e-6f);
    CHECK (std::abs (out.map (0.0f)) < 1e-6f && std::abs (in.map (0.0f) - 0.5f) < 1e-6f);
    // a channel removed before both moves the links with it (the targets follow the channels)
    song.channels.insert (song.channels.begin(), ChannelDef {});
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::audio)
            ++c.channel;
    for (auto& l : song.automations[0].links)
        ++l.target.index;
    song.removeChannel (0);
    CHECK (song.automations[0].links[0].target.index == 0 && song.automations[0].links[1].target.index == 1);
}

TEST (flfacts190gw_crossfade_links_play)
{
    using namespace lynx;
    // the engine drives both links of one clip: at a held value v, A's Volume multiplier is 2 * f_A(v), B's 2 * f_B(v)
    const auto base = flfacts190gw::twoSynths();
    const auto [plainA, plainB] = flfacts190gw::insertLevels (base);
    CHECK (plainA > 1e-3 && plainB > 1e-3);
    for (const auto v : { 1.0f, 0.7f, 0.3f, 0.0f })
    {
        auto song = base;
        Automation a;
        a.name = "A to B crossfade";
        a.links = { { { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume }, clipops::crossfadeOutFormula },
                    { { AutomationTargetKind::channelAdjust, 1, 0, leveladjust::volume }, clipops::crossfadeInFormula } };
        a.target = a.links[0].target;
        a.points = { { 0, v, 0.0f }, { 4 * ppq, v, 0.0f } };
        song.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = 0;
        c.track = 2;
        c.start = 0;
        c.length = 4 * ppq;
        song.playlist.push_back (c);
        const auto [levelA, levelB] = flfacts190gw::insertLevels (song);
        const auto gainA = levelAdjustGain (2.0f * (float) (0.5 * linkformula::tension (v, 10.0)));
        const auto gainB = levelAdjustGain (2.0f * (float) (0.5 - 0.5 * linkformula::tension (v, -10.0)));
        CHECK (std::abs (levelA - plainA * gainA) <= 0.01 * plainA + 1e-6);
        CHECK (std::abs (levelB - plainB * gainB) <= 0.01 * plainB + 1e-6);
    }
}
