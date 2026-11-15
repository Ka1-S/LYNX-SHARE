// Round 190gy: three user reports.
//  #171 (Windows, FL Studio 2025 trial): an .flp saved by an unregistered FL opened with garbage tempo, channels and
//       mixer routing, and "samples missing". FL 21+ XORs every u8/u16/u32 event value of such a file with a key that
//       depends on the value's file offset (REA round 190gy, docs/analise/REA_rodada190gy.md).
//  #165 (Linux): the Missing files window crashed when it painted a list row past the end of its list, which JUCE
//       asks for (more of them at a larger interface size). The window's own check runs in --selftest.
//  #169: Stop and Space go back to the position clicked on a ruler (also during playback), or where playback
//       started, as FL with "Remember seek time" (on in a fresh FL); pause keeps the position.

#include "flp/FlpWrite.h"

namespace bugs190gy
{
// FL's trial save as REA round 190gy found it in FLEngine (FUN_00a9ece0): from the event after 0x1C = 0 on, value byte
// j of every u8 / u16 / u32 event is XORed with (0x79 − the value's file offset + 3·j). Event IDs, the 0xAC escape,
// data sizes and data bytes stay as they are. Written here on its own (not with the reader's code) on purpose.
std::vector<std::uint8_t> asTrialSave (std::vector<std::uint8_t> file, bool scramble = true)
{
    const auto le32 = [&file] (std::size_t at) {
        return (std::uint32_t) file[at] | (std::uint32_t) file[at + 1] << 8 | (std::uint32_t) file[at + 2] << 16 | (std::uint32_t) file[at + 3] << 24;
    };
    std::size_t p = 8 + le32 (4) + 8;
    bool on = false;
    while (p < file.size())
    {
        std::uint32_t id = file[p++];
        if (id == 0xAC)
        {
            id = le32 (p);
            p += 4;
        }
        const auto cls = (id > 0xFF ? id >> 24 : id) >> 6;
        if (cls < 3)
        {
            const std::size_t size = cls == 0 ? 1 : cls == 1 ? 2 : 4;
            if (id == 0x1C)
                file[p] = 0; // "saved by an unregistered FL"
            if (on)
            {
                auto key = (std::uint8_t) (0x79u - (std::uint32_t) p);
                for (std::size_t j = 0; j < size; ++j, key = (std::uint8_t) (key + 3))
                    file[p + j] ^= key;
            }
            if (id == 0x1C && scramble)
                on = true;
            p += size;
        }
        else
        {
            std::size_t n = 0;
            for (int shift = 0;; shift += 7)
            {
                const auto b = file[p++];
                n |= (std::size_t) (b & 0x7F) << shift;
                if ((b & 0x80) == 0)
                    break;
            }
            p += n;
        }
    }
    return file;
}

flp::Project sampleProject()
{
    flp::Project p;
    p.tempo = 127.5;
    p.header.ppq = 96;
    const char* names[] = { "808 Kick", "808 Clap", "808 HiHat", "808 Snare" };
    for (int i = 0; i < 4; ++i)
    {
        flp::Channel c;
        c.index = i;
        c.type = flp::ChannelType::sampler;
        c.name = names[i];
        c.samplePath = std::string ("%FLStudioFactoryData%\\Data\\Patches\\Packs\\Legacy\\Drums\\Dance\\Basic ") + names[i] + ".wav";
        c.mixerTrack = i + 1;
        p.channels.push_back (c);
    }
    flp::Pattern pat;
    pat.index = 1;
    for (std::uint32_t n = 0; n < 9; ++n)
    {
        flp::Note note;
        note.position = n * 36;
        note.length = 24 + n;
        note.channel = (std::uint16_t) (n % 4);
        note.key = (std::uint16_t) (48 + n);
        note.velocity = (std::uint8_t) (90 + n);
        pat.notes.push_back (note);
    }
    p.patterns.push_back (pat);
    flp::Arrangement a;
    a.name = "Arrangement";
    for (std::uint32_t i = 0; i < 6; ++i)
    {
        flp::PlaylistItem item;
        item.position = i * 384 + i * 13; // (off the grid, like the report's project)
        item.length = 384;
        item.index = 1;
        a.items.push_back (item);
    }
    p.arrangements.push_back (a);
    return p;
}

void checkSame (const flp::Project& a, const flp::Project& b)
{
    CHECK (std::abs (a.tempo - b.tempo) < 1.0e-9);
    CHECK (a.channels.size() == b.channels.size());
    for (std::size_t i = 0; i < std::min (a.channels.size(), b.channels.size()); ++i)
    {
        CHECK (a.channels[i].index == b.channels[i].index);
        CHECK (a.channels[i].type == b.channels[i].type);
        CHECK (a.channels[i].name == b.channels[i].name);
        CHECK (a.channels[i].samplePath == b.channels[i].samplePath);
        CHECK (a.channels[i].mixerTrack == b.channels[i].mixerTrack);
    }
    CHECK (a.patterns.size() == b.patterns.size());
    if (! a.patterns.empty() && a.patterns.size() == b.patterns.size())
    {
        CHECK (a.patterns[0].index == b.patterns[0].index);
        CHECK (a.patterns[0].notes.size() == b.patterns[0].notes.size());
        for (std::size_t i = 0; i < std::min (a.patterns[0].notes.size(), b.patterns[0].notes.size()); ++i)
        {
            const auto& x = a.patterns[0].notes[i];
            const auto& y = b.patterns[0].notes[i];
            CHECK (x.position == y.position && x.length == y.length && x.channel == y.channel && x.key == y.key && x.velocity == y.velocity);
        }
    }
    CHECK (a.arrangements.size() == b.arrangements.size());
    if (! a.arrangements.empty() && a.arrangements.size() == b.arrangements.size())
    {
        CHECK (a.arrangements[0].items.size() == b.arrangements[0].items.size());
        for (std::size_t i = 0; i < std::min (a.arrangements[0].items.size(), b.arrangements[0].items.size()); ++i)
            CHECK (a.arrangements[0].items[i].position == b.arrangements[0].items[i].position
                   && a.arrangements[0].items[i].length == b.arrangements[0].items[i].length);
    }
    CHECK (a.mixer.size() == b.mixer.size());
}
} // namespace bugs190gy

TEST (issue171_a_project_saved_by_a_trial_fl_reads_as_the_registered_save)
{
    flp::WriteOptions o;
    o.version = "25.2.5.5319"; // the report's FL Studio 2025
    o.build = 5319;
    const auto plain = flp::writeProject (bugs190gy::sampleProject(), o);
    const auto trial = bugs190gy::asTrialSave (plain);
    CHECK (trial != plain);
    const auto a = flp::parseProject (plain);
    const auto b = flp::parseProject (trial);
    CHECK (std::abs (b.tempo - 127.5) < 1.0e-9); // (the report's 130 BPM read as 252572 BPM before the fix)
    bugs190gy::checkSame (a, b);

    // the reader says so, and only from 0x1C = 0 on
    flp::EventReader reader (trial);
    flp::Event e;
    bool sawRegistered = false, scrambledBefore = false;
    while (reader.next (e))
    {
        if (e.id == 0x1C)
        {
            sawRegistered = true;
            CHECK (e.value == 0);
        }
        else if (! sawRegistered)
            scrambledBefore = scrambledBefore || reader.isScrambled();
    }
    CHECK (sawRegistered);
    CHECK (! scrambledBefore);
    CHECK (reader.isScrambled());
    flp::EventReader plainReader (plain);
    while (plainReader.next (e)) {}
    CHECK (! plainReader.isScrambled());
}

TEST (issue171_older_unregistered_saves_are_not_unscrambled)
{
    // FL only reads the values back XORed from 20.99.1 on (FUN_00f46420: version >= 20,099,001); an FL 20 demo save
    // with 0x1C = 0 holds plain values
    flp::WriteOptions o;
    o.version = "20.9.2.2963";
    o.build = 2963;
    const auto plain = flp::writeProject (bugs190gy::sampleProject(), o);
    const auto unregistered = bugs190gy::asTrialSave (plain, false);
    bugs190gy::checkSame (flp::parseProject (plain), flp::parseProject (unregistered));

    // and the first version that has it
    o.version = "20.99.1.1";
    const auto newer = flp::writeProject (bugs190gy::sampleProject(), o);
    bugs190gy::checkSame (flp::parseProject (newer), flp::parseProject (bugs190gy::asTrialSave (newer)));
}

TEST (issue171_trial_save_imports_with_its_samples_and_mixer_routes)
{
    using namespace lynx;
    flp::WriteOptions o;
    o.version = "25.2.5.5319";
    o.build = 5319;
    const auto trial = bugs190gy::asTrialSave (flp::writeProject (bugs190gy::sampleProject(), o));
    FlpImportOptions options;
    FlpImportReport report;
    options.report = &report;
    options.roots.factoryData = "/nonexistent-fl"; // (no FL here: the sample paths themselves must come through)
    const auto song = importFlp (flp::parseProject (trial), options);
    CHECK (song.channels.size() == 4);
    CHECK (std::abs (song.bpm - 127.5) < 1.0e-6);
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        CHECK (song.channels[i].mixerTrack == (int) i + 1);
}

TEST (issue165_missing_files_rows_past_the_end_are_nothing)
{
    using lynx::listRowEntry;
    const std::vector<int> list { 7 };
    CHECK (listRowEntry (list, 0) != nullptr && *listRowEntry (list, 0) == 7);
    CHECK (listRowEntry (list, 1) == nullptr); // (the row JUCE painted below the last one: the report's crash)
    CHECK (listRowEntry (list, 5) == nullptr);
    CHECK (listRowEntry (list, -1) == nullptr);
    CHECK (listRowEntry (std::vector<int> {}, 0) == nullptr);
}

TEST (issue169_stop_and_space_go_back_to_the_seek_mark)
{
    using lynx::transportedit::stopReturnTarget;
    // "Remember seek time" on (FL's default): the ruler click, also one made while playing...
    CHECK (stopReturnTarget (false, true, true, 4 * 384, 0) == 4 * 384);
    CHECK (stopReturnTarget (false, true, false, 4 * 384, 0) == 4 * 384); // the Stop button too (it went to 1:1:1)
    // ...else where playback started
    CHECK (stopReturnTarget (false, true, false, -1, 1152) == 1152);
    CHECK (stopReturnTarget (false, true, true, -1, 1152) == 1152);
    // Stop while stopped: the very start
    CHECK (stopReturnTarget (true, true, false, 4 * 384, 1152) == 0);
    // option off: Space back to where playback started, the Stop button to the start (LYNX's earlier rule)
    CHECK (stopReturnTarget (false, false, true, 4 * 384, 1152) == 1152);
    CHECK (stopReturnTarget (false, false, false, 4 * 384, 1152) == 0);
    CHECK (stopReturnTarget (false, true, true, -1, -5) == 0);
}
