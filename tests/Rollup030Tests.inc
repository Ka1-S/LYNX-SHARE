// Rollup 0.1.30 (round 190il): what two of the merged branches do together.
// - formats190ii found that FL's shipped version-36 DirectWave programs store the sample format record (0x1F7) in 36
//   bytes (no bit depth), and that DirectWave reads every record as read (min (size, record)) over a record of zeros
//   (the bit depth 32). It completed the records in its own importer only; dwave190hz's editor ("Open program") and old
//   .flp projects went through readZone, which skipped the short record and with it the loop. readZone now reads short
//   records the way DirectWave does, for every path.

#include <cstring>

#include "core/DirectWave.h"
#include "core/DirectWaveState.h"
#include "core/SamplerImport.h"

namespace rollup030
{
struct DwBytes
{
    std::vector<std::uint8_t> b;
    void u8 (unsigned v) { b.push_back ((std::uint8_t) v); }
    void u32 (std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); }
    void u64 (std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); }
    void f32 (float f) { std::uint32_t v; std::memcpy (&v, &f, 4); u32 (v); }
    void chunk (std::uint32_t id, const std::vector<std::uint8_t>& body)
    {
        u32 (id);
        u64 (body.size());
        b.insert (b.end(), body.begin(), body.end());
    }
};

// a sample format record (0x1F7) of `size` bytes: 1000 frames, mono, 44.1 kHz, the loop, offset 0, bits 24
inline std::vector<std::uint8_t> formatRecord (std::size_t size, int loopMode, int loopA, int loopB)
{
    DwBytes f;
    f.u64 (1000); f.u32 (1); f.u32 (16); f.f32 (44100.0f);
    f.u32 ((std::uint32_t) loopMode); f.u32 ((std::uint32_t) loopA); f.u32 ((std::uint32_t) loopB); f.u32 (0); f.u32 (24);
    f.b.resize (size);
    return f.b;
}

// one zone: keys 44…46, root 45, the sample format record as given
inline std::vector<std::uint8_t> zoneBody (const std::vector<std::uint8_t>& format)
{
    DwBytes header;
    header.u8 (45); header.u8 (44); header.u8 (46); header.u8 (0); header.u8 (127);
    header.u8 (0); header.u8 (0); header.u8 (204); header.u8 (0);
    header.f32 (1.0f); header.f32 (0.5f); header.u32 (0); header.u32 (512);
    DwBytes zone;
    zone.chunk (0x1F4, header.b);
    zone.chunk (0x1F5, { 'z' });
    const std::string path = "%FLStudioData%\\Patches\\Packs\\Piano\\Piano 2\\a.wav";
    zone.chunk (0x1F6, std::vector<std::uint8_t> (path.begin(), path.end()));
    zone.chunk (0x1F7, format);
    zone.chunk (4, {});
    return zone.b;
}

// a DirectWave state of `version` (as an .flp or a program file after its magic holds it): one program, one zone
inline std::vector<std::uint8_t> stateWith (std::uint32_t version, const std::vector<std::uint8_t>& zone)
{
    DwBytes p64;
    p64.u32 (0); p64.u8 (0); p64.u8 (0); p64.f32 (1.0f); p64.f32 (0.0f);
    for (int i = 0; i < 20; ++i)
        p64.u8 (0);
    DwBytes prog;
    prog.chunk (0x64, p64.b);
    prog.chunk (0x66, { 'P', 'i', 'a', 'n', 'o' });
    prog.chunk (3, zone);
    prog.chunk (2, {});
    DwBytes s;
    s.u32 (version);
    s.chunk (6, std::vector<std::uint8_t> (16, 0));
    s.chunk (1, prog.b);
    return s.b;
}
} // namespace rollup030

TEST (rollup030_directwave_short_format_record_keeps_its_loop)
{
    using namespace rollup030;
    // FL's version-36 record: 36 bytes, no bit depth → the loop is read, bits = 32 (DirectWave's record starts with 32)
    {
        const auto body = zoneBody (formatRecord (36, 2, 300, 800));
        const auto z = lynx::dw::parseZone (body.data(), body.size());
        CHECK (z.loopMode == 2 && z.loopA == 300 && z.loopB == 800 && z.frames == 1000 && z.rate == 44100.0f);
        CHECK (z.bits == 32 && z.offset == 0 && z.root == 45 && z.lowKey == 44 && z.highKey == 46);
    }
    // the full 40-byte record keeps its own bit depth
    {
        const auto body = zoneBody (formatRecord (40, 1, 10, 900));
        const auto z = lynx::dw::parseZone (body.data(), body.size());
        CHECK (z.loopMode == 1 && z.loopA == 10 && z.loopB == 900 && z.bits == 24);
    }
    // shorter still: what is there is read, the rest is DirectWave's zeros (no loop end, offset 0), bits 32
    {
        const auto body = zoneBody (formatRecord (28, 2, 300, 800));
        const auto z = lynx::dw::parseZone (body.data(), body.size());
        CHECK (z.loopMode == 2 && z.loopA == 300 && z.loopB == 0 && z.offset == 0 && z.bits == 32 && z.frames == 1000);
    }
}

TEST (rollup030_directwave_v36_state_loops_on_every_path)
{
    using namespace rollup030;
    const auto state = stateWith (36, zoneBody (formatRecord (36, 2, 300, 800)));
    const auto loopKept = [] (const lynx::dw::Zone& z) { return z.loopMode == 2 && z.loopA == 300 && z.loopB == 800 && z.bits == 32; };

    // an old .flp project's DirectWave state, as stored (no importer in between)
    const auto st = lynx::dw::parseState (state);
    CHECK (st && lynx::dw::playedProgram (*st) != nullptr);
    if (st && lynx::dw::playedProgram (*st) != nullptr)
        CHECK (loopKept (lynx::dw::playedProgram (*st)->zones.at (0)));

    // dwave190hz's editor, "Open program" on a version-36 .dwp
    auto file = std::vector<std::uint8_t> { 'D', 'w', 'P', 'r' };
    file.insert (file.end(), state.begin(), state.end());
    const auto opened = lynx::dw::readProgramFile (file);
    CHECK (opened && ! opened->zones.empty());
    if (opened && ! opened->zones.empty())
        CHECK (loopKept (opened->zones[0]));

    // formats190ii's importer: the state it keeps is the file's own, and it reads the same
    const auto imported = lynx::msimport::stateOfFile (file);
    CHECK (imported && *imported == state);
    if (imported)
        if (const auto again = lynx::dw::parseState (*imported); again && lynx::dw::playedProgram (*again) != nullptr)
            CHECK (loopKept (lynx::dw::playedProgram (*again)->zones.at (0)));
}

TEST (rollup030_directwave_copy_for_an_older_release_completes_short_records)
{
    // File › "Save a copy for an older LYNX" (0.1.28, whose readZone skipped a short record): the document's short zone
    // records get their full size as DirectWave reads them; nothing else changes
    using namespace rollup030;
    const auto state = stateWith (36, zoneBody (formatRecord (36, 2, 300, 800)));
    auto doc = lynx::dw::readDoc (state);
    CHECK (doc.has_value());
    if (! doc)
        return;
    CHECK (lynx::dw::completeShortZoneRecords (*doc) == 1);
    CHECK (lynx::dw::completeShortZoneRecords (*doc) == 0);
    const auto written = lynx::dw::writeDoc (*doc);
    CHECK (written.size() == state.size() + 4);
    const auto st = lynx::dw::parseState (written);
    CHECK (st && lynx::dw::playedProgram (*st) != nullptr);
    if (st && lynx::dw::playedProgram (*st) != nullptr)
    {
        const auto& z = lynx::dw::playedProgram (*st)->zones.at (0);
        CHECK (z.loopMode == 2 && z.loopA == 300 && z.loopB == 800 && z.bits == 32 && z.root == 45);
    }
    // a state with full records is written back byte for byte
    const auto full = stateWith (38, zoneBody (formatRecord (40, 1, 10, 900)));
    auto fullDoc = lynx::dw::readDoc (full);
    CHECK (fullDoc && lynx::dw::completeShortZoneRecords (*fullDoc) == 0 && lynx::dw::writeDoc (*fullDoc) == full);
}

// Rollup 0.1.30 step 3 (round 190jk): Helix (round 190ja) follows FL's host pitch features as round 190ix made Tetra do: a
// slide note moves the held voices (issue #14), a Porta channel starts a note at the last one's pitch and glides (round
// 168l), a Mono channel moves its voice legato. The FL-Data audits check every engine; these run without FL's Data (CI).
namespace rollup030
{
inline lynx::ChannelDef helixChannel()
{
    lynx::ChannelDef c;
    c.name = "Helix";
    c.instrument = lynx::InstrumentKind::helix;
    return c;
}
} // namespace rollup030

TEST (rollup030_helix_follows_slide_notes)
{
    using namespace lynx;
    const auto c = rollup030::helixChannel();
    Note held;
    held.key = 60;
    held.length = ppq * 8;
    held.velocity = 0.8f;
    Note slide;
    slide.key = 67;
    slide.start = ppq;
    slide.length = ppq / 4;
    slide.slide = true;
    auto fifth = held;
    fifth.key = 67;
    const auto base = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.5), 1.0);
    const auto ref = issue14::pitchAt (issue14::renderPattern (c, { fifth }, 0.0f, 1.5), 1.0);
    const auto out = issue14::renderPattern (c, { held, slide }, 0.0f, 1.5);
    CHECK (base > 200.0 && std::abs (issue14::cents (base, ref) - 700.0) < 10.0);
    CHECK (std::abs (issue14::cents (ref, issue14::pitchAt (out, 1.0))) < 10.0);
    CHECK (std::abs (issue14::cents (base, issue14::pitchAt (out, 0.3))) < 10.0); // (before the slide note)
    const auto mid = issue14::cents (base, issue14::pitchAt (out, 0.53));
    CHECK (mid > 100.0 && mid < 650.0);
    // the main pitch still moves a slid voice
    const auto master = issue14::pitchAt (issue14::renderPattern (c, { held, slide }, 100.0f, 1.5), 1.0);
    CHECK (std::abs (issue14::cents (ref, master) - 100.0) < 10.0);
}

TEST (rollup030_helix_follows_channel_porta_and_mono)
{
    using namespace lynx;
    auto inst = makeInstrument (InstrumentKind::helix);
    CHECK (inst != nullptr && inst->followsChannelPorta() && inst->followsSlides());
    if (inst == nullptr)
        return;
    inst->prepare (48000.0);
    inst->configure (rollup030::helixChannel(), 120.0);
    HostSlide::pendingGlide() = { -700.0f, 1000.0f, false };
    inst->setNextNoteExpression (0.0f, -700.0f, false);
    inst->noteOn (60, 0.8f);
    CHECK (HostSlide::pendingGlide().taken);
    HostSlide::pendingGlide() = {};

    const auto c = rollup030::helixChannel();
    Note low;
    low.key = 60;
    low.velocity = 0.8f;
    auto high = low;
    high.key = 67;
    auto held = low;
    held.key = 67;
    held.length = ppq * 8;
    const auto ref = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.6), 1.0);
    auto porta = c;
    setChannelPortamento (porta, true, 1.0f);
    low.length = ppq / 2;
    high.start = ppq;
    high.length = ppq * 3;
    const auto glide = issue14::renderPattern (porta, { low, high }, 0.0f, 1.6);
    const auto mid = issue14::cents (ref, issue14::pitchAt (glide, 0.6)), end = issue14::cents (ref, issue14::pitchAt (glide, 1.2));
    CHECK (mid > -620.0 && mid < -250.0);
    CHECK (std::abs (end) < 10.0);
    auto mono = c;
    setChannelMono (mono, true);
    low.length = ppq * 2;
    high.length = ppq * 2;
    const auto legato = issue14::renderPattern (mono, { low, high }, 0.0f, 1.6);
    CHECK (std::abs (issue14::cents (ref, issue14::pitchAt (legato, 0.3)) + 700.0) < 10.0);
    CHECK (std::abs (issue14::cents (ref, issue14::pitchAt (legato, 1.1))) < 10.0);
}

// Rollup 0.1.30 step 4 (round 190jt): round 190jf's FL group mute (Alt+click a member's LED) on Playlist tracks linked
// to the mixer (round 190ib), whose LED is the insert's mute as well: the clicked track's lit state flips, and a group
// switched back on unmutes its linked inserts too, so every LED of the group goes out together.
TEST (rollup030_group_mute_on_linked_tracks)
{
    using namespace lynx;
    Song s;
    s.mixer.resize (6);
    s.playlistTracks.resize (4);
    for (int t = 1; t <= 2; ++t)
        s.playlistTracks[(std::size_t) t].grouped = true; // a group: track 0 and its members 1, 2; track 3 alone
    const std::vector<int> inserts { 1, 2, -1, 3 };     // track 2 repeats track 1 (unassigned, round 190ib's "→ 1")
    s.mixer[2].muted = true;                            // track 1's LED is lit by its insert
    CHECK (noteedit::muteGroup (s, 1, inserts));        // lit → the group goes off: the Playlist tracks unmute ...
    CHECK (! s.playlistTracks[0].muted && ! s.playlistTracks[1].muted && ! s.playlistTracks[2].muted);
    CHECK (! s.mixer[1].muted && ! s.mixer[2].muted);   // ... and so do their linked inserts
    CHECK (noteedit::muteGroup (s, 2, inserts));        // nothing lit → the whole group mutes (FL: the Playlist tracks)
    CHECK (s.playlistTracks[0].muted && s.playlistTracks[1].muted && s.playlistTracks[2].muted && ! s.playlistTracks[3].muted);
    CHECK (! s.mixer[3].muted);
    CHECK (noteedit::muteGroup (s, 0, inserts));        // lit again → all go out
    CHECK (! s.playlistTracks[0].muted && ! s.playlistTracks[1].muted && ! s.playlistTracks[2].muted);
    // FL's groups (no link): as round 190jf
    s.mixer[1].muted = true;
    CHECK (noteedit::muteGroup (s, 0));
    CHECK (s.playlistTracks[0].muted && s.mixer[1].muted);
}

// Rollup 0.1.30 step 6 (round 190km): the Loop Browser (gaps190kf) lists LYNX's own beats and sounds (190ke's tagged
// content, core/FactoryLoops.h): a beat is a pattern loop at its genre's tempo with the genre keyword, a sound a MIDI
// loop's instrument; effect chains are not listed; the row's path is the tag store's item and finds its item again.
#include "core/FactoryLoops.h"

TEST (rollup030_loop_browser_lists_lynx_content)
{
    using namespace lynx;
    const auto all = factoryloops::loops();
    int beats = 0, sounds = 0, trapBeats = 0, trapWord = 0, bassSounds = 0;
    for (const auto& info : all)
    {
        const auto* item = factoryloops::itemOf (info.path);
        CHECK (item != nullptr);
        if (item == nullptr)
            continue;
        CHECK (item->kind != factorycontent::Kind::chain);
        CHECK (info.name == item->name);
        if (item->kind == factorycontent::Kind::beat)
        {
            ++beats;
            trapWord += info.keywords.count ("trap") != 0 ? 1 : 0; // (the Trap keyword also takes "drill")
            CHECK (info.type == loopkeys::pattern);
            CHECK (info.tempo >= 60.0 && info.tempo <= 200.0);
            CHECK (info.scale == loopkeys::Scale::neither);
            CHECK (info.keywords.count ("all drums") == 1); // (190ke's "Drums" / "Beat" tags)
            if (item->genre == "trap")
            {
                ++trapBeats;
                CHECK (info.keywords.count ("trap") == 1);
                CHECK (std::abs (info.tempo - 140.0) < 0.5); // (Loop Starter's suggested Trap tempo)
            }
        }
        else
        {
            ++sounds;
            CHECK (info.type == loopkeys::midi);
            if (info.keywords.count ("bass") != 0)
                ++bassSounds;
        }
    }
    CHECK (beats > 0 && sounds > 0 && trapBeats > 0 && bassSounds > 0);
    CHECK ((int) all.size() == beats + sounds);
    // the Loop Browser's own filter: Trap + Loop Type pattern gives exactly the trap beats; a plain file path is not LYNX's
    loopkeys::Filter f;
    f.keywords = { "trap" };
    f.types = loopkeys::pattern;
    int passed = 0;
    for (const auto& info : all)
        passed += loopkeys::matches (info, f, false) ? 1 : 0;
    CHECK (passed == trapWord && trapWord >= trapBeats);
    CHECK (factoryloops::itemOf ("/home/user/Samples/Trap beat 1.wav") == nullptr);
    CHECK (factoryloops::itemOf (collections::pluginItem ("some.vst3")) == nullptr);
}
