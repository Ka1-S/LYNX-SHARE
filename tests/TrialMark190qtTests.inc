// Round 190qt (owner's option A, 11-24): a project LYNX imported from an .flp an unregistered (trial) FL Studio saved
// (event 0x1C = 0, REA round 190gy) goes back to FL as a trial save again: 0x1C = 0 and FL's scrambled values after it,
// byte for byte what FL's saver does (bugs190gy::asTrialSave, written apart from the writer). Registered saves and
// projects made in LYNX stay registered. Import tracks and cross-project paste keep the mark; nothing clears it.

#include "core/CrossPaste.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/TrackImport.h"
#include "flp/FlpWrite.h"

namespace trial190qt
{
const char* const note = "This project came from an FL Studio trial save, so FL Studio's trial will treat it as one.";

std::vector<std::uint8_t> flFile (bool trial)
{
    flp::WriteOptions o;
    o.version = "25.2.5.5319"; // (issue #171's FL Studio 2025)
    o.build = 5319;
    const auto plain = flp::writeProject (bugs190gy::sampleProject(), o);
    return trial ? bugs190gy::asTrialSave (plain) : plain;
}

lynx::Song imported (const std::vector<std::uint8_t>& file)
{
    lynx::FlpImportOptions options;
    options.roots.factoryData = "/nonexistent-fl";
    return lynx::importFlp (flp::parseProject (file), options);
}

std::vector<std::uint8_t> exported (const lynx::Song& s, int* notes = nullptr)
{
    lynx::flpexport::Options options;
    lynx::flpexport::Report report;
    auto bytes = lynx::flpexport::exportFlp (s, options, report);
    if (notes != nullptr)
        *notes = (int) std::count (report.notes.begin(), report.notes.end(), std::string (note));
    return bytes;
}

bool scrambled (const std::vector<std::uint8_t>& file)
{
    flp::EventReader reader (file);
    flp::Event e;
    while (reader.next (e)) {}
    return reader.isScrambled();
}

int registeredValue (const std::vector<std::uint8_t>& file)
{
    flp::EventReader reader (file);
    flp::Event e;
    while (reader.next (e))
        if (e.id == 0x1C)
            return (int) e.value;
    return -1;
}
} // namespace trial190qt

TEST (trial190qt_reader_says_which_fl_saved_the_project)
{
    CHECK (flp::parseProject (trial190qt::flFile (true)).trialSaved);
    CHECK (! flp::parseProject (trial190qt::flFile (false)).trialSaved);
    CHECK (! flp::parseProject (flp::writeProject (flp::Project {})).trialSaved);
    // an FL 20 demo save (0x1C = 0, values not scrambled yet) is one too
    flp::WriteOptions old;
    old.version = "20.9.2.2963";
    old.build = 2963;
    CHECK (flp::parseProject (bugs190gy::asTrialSave (flp::writeProject (bugs190gy::sampleProject(), old), false)).trialSaved);
}

TEST (trial190qt_writer_scrambles_a_trial_save_as_fl_does)
{
    auto p = bugs190gy::sampleProject();
    const auto registered = flp::writeProject (p);
    p.trialSaved = true;
    const auto trial = flp::writeProject (p);
    // the same file FL's trial would write: 0x1C = 0 and every u8 / u16 / u32 value after it XORed
    CHECK (trial == bugs190gy::asTrialSave (registered));
    CHECK (trial != registered);
    CHECK (trial190qt::registeredValue (trial) == 0 && trial190qt::registeredValue (registered) == 1);
    CHECK (trial190qt::scrambled (trial) && ! trial190qt::scrambled (registered));
    const auto back = flp::parseProject (trial);
    CHECK (back.trialSaved);
    bugs190gy::checkSame (flp::parseProject (registered), back);
}

TEST (trial190qt_a_trial_save_goes_back_to_fl_as_one)
{
    using namespace lynx;
    const auto song = trial190qt::imported (trial190qt::flFile (true));
    CHECK (song.flTrialSaved);
    int notes = 0;
    const auto out = trial190qt::exported (song, &notes);
    CHECK (notes == 1);
    CHECK (trial190qt::registeredValue (out) == 0 && trial190qt::scrambled (out));
    const auto p = flp::parseProject (out);
    CHECK (p.trialSaved);
    CHECK (std::abs (p.tempo - 127.5) < 1.0e-9); // (FL's scrambling undone: the values read as written)
    // apart from the mark, the same file the registered import gives
    auto registered = song;
    registered.flTrialSaved = false;
    const auto plain = trial190qt::exported (registered);
    CHECK (out == bugs190gy::asTrialSave (plain));
    // and LYNX reads its own trial export back as the same project, still marked
    const auto again = trial190qt::imported (out);
    CHECK (again.flTrialSaved && again.channels.size() == song.channels.size());
    for (std::size_t i = 0; i < std::min (again.channels.size(), song.channels.size()); ++i)
        CHECK (again.channels[i].name == song.channels[i].name && again.channels[i].mixerTrack == song.channels[i].mixerTrack
               && again.channels[i].samplePath == song.channels[i].samplePath);
    CHECK (std::abs (again.bpm - song.bpm) < 1.0e-9);
}

TEST (trial190qt_registered_and_lynx_projects_stay_registered)
{
    using namespace lynx;
    const auto fromRegistered = trial190qt::imported (trial190qt::flFile (false));
    CHECK (! fromRegistered.flTrialSaved);
    int notes = -1;
    auto out = trial190qt::exported (fromRegistered, &notes);
    CHECK (notes == 0 && trial190qt::registeredValue (out) == 1 && ! trial190qt::scrambled (out) && ! flp::parseProject (out).trialSaved);

    const auto own = Song::makeDefault();
    CHECK (! own.flTrialSaved);
    out = trial190qt::exported (own, &notes);
    CHECK (notes == 0 && trial190qt::registeredValue (out) == 1 && ! trial190qt::scrambled (out));
}

TEST (trial190qt_import_tracks_and_paste_keep_the_mark)
{
    using namespace lynx;
    auto trialSource = tracks190pe::song();
    trialSource.flTrialSaved = true;
    const auto registeredSource = tracks190pe::song();

    // import tracks: the trial origin comes along, with one report line
    auto dest = tracks190pe::song();
    auto r = trackimport::importTracks (dest, trialSource, { 1 }, {});
    CHECK (r.ok && dest.flTrialSaved);
    CHECK (std::count_if (r.notes.begin(), r.notes.end(), [] (const std::string& n) { return n.find ("FL Studio trial save") != std::string::npos; }) == 1);
    // never the reverse: a registered project's tracks leave the mark as it is
    r = trackimport::importTracks (dest, registeredSource, { 0 }, {});
    CHECK (r.ok && dest.flTrialSaved);
    auto plain = tracks190pe::song();
    r = trackimport::importTracks (plain, registeredSource, { 0 }, {});
    CHECK (r.ok && ! plain.flTrialSaved);
    // nothing imported: nothing marked
    auto none = tracks190pe::song();
    r = trackimport::importTracks (none, trialSource, {}, {});
    CHECK (! r.ok && ! none.flTrialSaved);

    // paste clips / channels from another project (the fragment carries the mark)
    auto clipsDest = tracks190pe::song();
    const auto clips = crosspaste::clipsFragment (trialSource, trialSource.playlist);
    CHECK (clips.flTrialSaved);
    CHECK (! crosspaste::importClips (clipsDest, clips).empty() && clipsDest.flTrialSaved);
    auto channelsDest = tracks190pe::song();
    const auto channels = crosspaste::channelsFragment (trialSource, { 0 });
    CHECK (channels.flTrialSaved);
    CHECK (! crosspaste::importChannels (channelsDest, channels, 0).empty() && channelsDest.flTrialSaved);
    auto stays = trialSource;
    CHECK (! crosspaste::importChannels (stays, crosspaste::channelsFragment (registeredSource, { 0 }), 0).empty() && stays.flTrialSaved);
    auto stillRegistered = tracks190pe::song();
    CHECK (! crosspaste::importClips (stillRegistered, crosspaste::clipsFragment (registeredSource, registeredSource.playlist)).empty()
           && ! stillRegistered.flTrialSaved);

    // and the merged project's export is a trial save
    CHECK (trial190qt::registeredValue (trial190qt::exported (dest)) == 0);
    CHECK (trial190qt::registeredValue (trial190qt::exported (plain)) == 1);
}
