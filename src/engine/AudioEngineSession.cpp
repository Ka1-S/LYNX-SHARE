// Recording into the clip grid's slots (round 190bv): the message-thread side. The launch engine (core/ClipLauncher.h)
// decides when a recording starts and ends, on its own clock; this file collects what is played in between, makes the
// clip and sends it on into loop playback. docs/analise/REA_rodada190bv.md says what follows the studied program.

#include "engine/AudioEngine.h"

#include <algorithm>
#include <cmath>

#include "core/DetailChain.h"
#include "core/SessionOps.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
// A note channel a track can record: any instrument but an audio clip or an automation's own channel.
bool takesNotes (const Song& song, int channel) noexcept
{
    return channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].instrument != InstrumentKind::audioClip;
}
} // namespace

AudioEngine::GridInput AudioEngine::gridTrackInput (int track) const
{
    const auto chain = detailchain::forTrack (song, track);
    if (takesNotes (song, chain.channel))
        return GridInput::midi;
    // a track that plays through an insert with an audio input records that input
    if (chain.insert > 0 && chain.insert < (int) song.mixer.size() && chain.channel < 0
        && inputChannelsOf (song.mixer[(std::size_t) chain.insert], (int) getInputChannelNames().size()).second > 0)
        return GridInput::audio;
    return GridInput::midi;
}

bool AudioEngine::isGridTrackArmed (int track) const noexcept
{
    return std::find (gridArmed.begin(), gridArmed.end(), track) != gridArmed.end();
}

void AudioEngine::setGridTrackArmed (int track, bool armed, bool exclusive)
{
    if (track < 0 || track >= launcher::maxTracks)
        return;
    const auto insertOf = [this] (int t) { return gridTrackInput (t) == GridInput::audio ? detailchain::forTrack (song, t).insert : -1; };
    if (armed && exclusive)
        for (const auto other : std::vector<int> (gridArmed))
            if (other != track)
                setGridTrackArmed (other, false, false);
    const auto it = std::find (gridArmed.begin(), gridArmed.end(), track);
    if (armed == (it != gridArmed.end()))
        return;
    if (armed)
    {
        gridArmed.push_back (track);
        if (const auto insert = insertOf (track); insert > 0)
            setInsertArmed (insert, true);
        else if (const auto channel = detailchain::forTrack (song, track).channel; takesNotes (song, channel))
            setSelectedChannel (channel); // what is played goes to the armed track's instrument
    }
    else
    {
        endSlotRecording (track, false);
        gridArmed.erase (it);
        if (const auto insert = insertOf (track); insert > 0)
            setInsertArmed (insert, false);
    }
    sendChangeMessage();
}

double AudioEngine::slotClockNow() const noexcept
{
    if (clipLauncher == nullptr)
        return 0.0;
    return clipLauncher->clockNow.load (std::memory_order_relaxed) + playheadAdvanceTicks();
}

bool AudioEngine::recordSlot (int track, int scene)
{
    if (! isGridTrackArmed (track) || scene < 0 || scene >= launcher::maxScenes)
        return false;
    if (song.clipGrid && launcher::clipAt (*song.clipGrid, track, scene) != nullptr)
        return false;
    // one recording per track, and while it records one more slot that takes over from its end (round 190cy: Live
    // 12.4.16 ends the first take into loop playback and records the clicked slot; the launch engine decides when)
    if (std::any_of (slotTakes.begin(), slotTakes.end(), [track, scene] (const SlotTake& t) { return t.track == track && t.scene == scene; })
        || std::count_if (slotTakes.begin(), slotTakes.end(), [track] (const SlotTake& t) { return t.track == track && ! t.known; }) >= 2)
        return false;
    // the grid plays with the song, and the launch engine exists only in a project that has a grid
    if (! song.clipGrid || (int) song.clipGrid->scenes.size() <= scene)
        editSong ([scene] (Song& s) {
            auto& grid = launcher::ensureGrid (s);
            while ((int) grid.scenes.size() <= scene)
                grid.scenes.emplace_back();
        }, false);
    if (clipLauncher == nullptr)
        return false;
    // (the arrangement's record switch writes launches to the Playlist: one kind of recording at a time)
    if (transport.recording.load())
        setRecording (false);

    SlotTake take;
    take.track = track;
    take.scene = scene;
    take.audio = gridTrackInput (track) == GridInput::audio;
    const auto chain = detailchain::forTrack (song, track);
    take.channel = takesNotes (song, chain.channel) ? chain.channel : -1;
    take.insert = take.audio ? chain.insert : -1;
    if (take.audio && ! isInsertArmed (take.insert))
        setInsertArmed (take.insert, true);
    slotTakes.push_back (std::move (take));

    if (slotTakes.back().audio)
    {
        slotAudioWanted = true;
        slotAudioCapture.store (true);
    }
    else
        slotTaking.store (true);

    if (! transport.playing.load() && ! isCountingIn())
    {
        // a new start: the launcher's clock begins at the song position
        clipLauncher->clockMinusSong.store (0.0, std::memory_order_relaxed);
        clipLauncher->clockNow.store (transport.positionTicks.load(), std::memory_order_relaxed);
    }
    launcher::Command c { launcher::Command::Type::record, track, scene };
    if (slotRecordBars > 0)
        c.at = (double) slotRecordBars * (double) song.barTicks();
    launcherCommand (c);
    updateAudioRecording(); // (the files are open before the first block)
    sendChangeMessage();
    return true;
}

std::string AudioEngine::slotRecordingName (int track) const
{
    // the track's name as the grid shows it (an unnamed track: "Track N")
    auto name = track >= 0 && track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track].name : std::string();
    if (name.empty())
        name = tr ("Track {0}", { juce::String (track + 1) }).toStdString();
    return session::recordedClipName (song, name);
}

void AudioEngine::endSlotRecording (int track, bool thenPlay)
{
    const auto it = std::find_if (slotTakes.begin(), slotTakes.end(), [track] (const SlotTake& t) { return t.track == track; });
    if (it == slotTakes.end() || clipLauncher == nullptr)
        return;
    launcherCommand ({ launcher::Command::Type::recordEnd, track, thenPlay ? 1 : 0 });
    // stopped before it began (during the count-in, or the transport never started): nothing to wait for
    if (! transport.playing.load() && ! it->known)
    {
        slotTakes.erase (it);
        stop();
    }
    sendChangeMessage();
}

void AudioEngine::toggleSessionRecord (int scene)
{
    if (isSessionRecording())
    {
        for (const auto& t : std::vector<SlotTake> (slotTakes))
            if (! t.known)
                endSlotRecording (t.track, true);
        sessionOverdub = false;
        sendChangeMessage();
        return;
    }
    auto any = false;
    for (const auto track : std::vector<int> (gridArmed))
    {
        const auto playing = clipLauncher != nullptr ? clipLauncher->tracks[(std::size_t) track].playing.load (std::memory_order_relaxed) : -1;
        // a track whose clip plays keeps playing it: MIDI goes into the clip (overdub); it does not start a new clip
        if (playing >= 0 && transport.playing.load())
        {
            any = any || gridTrackInput (track) == GridInput::midi;
            continue;
        }
        if (recordSlot (track, scene))
            any = true;
    }
    if (any && transport.playing.load())
        for (const auto track : gridArmed)
            if (clipLauncher != nullptr && clipLauncher->tracks[(std::size_t) track].playing.load (std::memory_order_relaxed) >= 0
                && gridTrackInput (track) == GridInput::midi)
            {
                sessionOverdub = true;
                overdubHasUndoStep = false;
                slotTaking.store (true);
            }
    sendChangeMessage();
}

void AudioEngine::readSlotTakeMessages()
{
    if (clipLauncher == nullptr)
        return;
    launcher::RecordedTake told;
    while (clipLauncher->takes.pop (told))
    {
        const auto it = std::find_if (slotTakes.begin(), slotTakes.end(),
                                      [&] (const SlotTake& t) { return t.track == told.track && t.scene == told.scene; });
        if (it == slotTakes.end())
            continue;
        if (told.until <= told.from) // cancelled before it began
        {
            slotTakes.erase (it);
            continue;
        }
        it->known = true;
        it->from = told.from;
        it->until = it->placed ? it->until : told.until; // (a clip already made keeps its length)
        it->thenPlay = told.thenPlay;
    }
}

void AudioEngine::updateSlotRecording()
{
    if (slotTakes.empty() && ! sessionOverdub && ! slotTaking.load (std::memory_order_relaxed))
    {
        if (clipLauncher != nullptr) // (nobody listening: the queue stays empty)
        {
            launcher::RecordedTake dropped;
            while (clipLauncher->takes.pop (dropped)) {}
        }
        return;
    }
    auto& runtime = *clipLauncher;
    const auto playing = transport.playing.load();
    // (read before the messages: once the launch engine has stopped, every end it had to tell has been told)
    const auto engineRunning = runtime.running.load (std::memory_order_relaxed);
    readSlotTakeMessages();

    // the transport stopped under a recording that had not begun: it is over (the queued start is cancelled at the next play)
    if (! playing && ! isCountingIn() && ! engineRunning)
        for (auto it = slotTakes.begin(); it != slotTakes.end();)
        {
            if (! it->known)
            {
                runtime.commands.push ({ launcher::Command::Type::recordEnd, it->track, 0 });
                it = slotTakes.erase (it);
            }
            else
                ++it;
        }
    if (! playing && sessionOverdub)
        sessionOverdub = false;

    // what was played since the last tick, on the launcher's clock
    const auto offset = runtime.clockMinusSong.load (std::memory_order_relaxed);
    std::vector<RecordedEvent> played;
    RecordedEvent e;
    while (slotRecordQueue.pop (e))
        played.push_back (e);
    played.insert (played.end(), uiSlotRecorded.begin(), uiSlotRecorded.end());
    uiSlotRecorded.clear();
    std::stable_sort (played.begin(), played.end(), [] (const RecordedEvent& a, const RecordedEvent& b) { return a.tick < b.tick; });
    for (auto& ev : played)
    {
        ev.tick += offset;
        for (auto& take : slotTakes)
            if (! take.audio && (take.channel < 0 || take.channel == ev.channel))
                take.events.push_back (ev);
        if (sessionOverdub)
            for (const auto track : gridArmed)
                if (runtime.tracks[(std::size_t) track].playing.load (std::memory_order_relaxed) >= 0
                    && std::none_of (slotTakes.begin(), slotTakes.end(), [track] (const SlotTake& t) { return t.track == track; }))
                    if (const auto channel = detailchain::forTrack (song, track).channel; channel < 0 || channel == ev.channel)
                        overdubEvents.push_back ({ track, ev });
    }

    const auto now = slotClockNow();
    const auto grid = recordOptions.quantise;

    // MIDI takes whose end is known: the clip is made at once (so it can play on from its end without a gap) and takes
    // the notes still played before the end as they come
    for (auto& take : slotTakes)
    {
        if (take.audio || ! take.known)
            continue;
        if (take.placed && take.placedEvents == take.events.size())
            continue;
        auto notes = session::takeNotes (take.events, take.from, take.until, grid);
        const auto length = (std::int64_t) std::llround (take.until - take.from);
        const auto track = take.track, scene = take.scene;
        if (! take.placed)
        {
            // named as Live names a clip recorded into a slot, "<track> <n>" (round 190cy; the Pattern workflow never
            // records into slots, so its pattern names are untouched)
            const auto name = slotRecordingName (track);
            editSong ([&] (Song& s) { session::placeRecordedPattern (s, track, scene, std::move (notes), length, name); });
            take.placed = true;
            if (take.thenPlay && playing)
                runtime.commands.push ({ launcher::Command::Type::launch, track, scene, take.until });
        }
        else
            editSong ([&] (Song& s) {
                if (! s.clipGrid)
                    return;
                if (const auto* clip = launcher::clipAt (*s.clipGrid, track, scene); clip != nullptr && clip->kind == ClipKind::pattern
                                                                                    && clip->pattern >= 0 && clip->pattern < (int) s.patterns.size())
                {
                    s.patterns[(std::size_t) clip->pattern].notes = std::move (notes);
                    s.patterns[(std::size_t) clip->pattern].sortNotes();
                }
            }, false);
        take.placedEvents = take.events.size();
    }
    // a MIDI take is over once its end has passed and the keys held across it were released (or a moment later)
    slotTakes.erase (std::remove_if (slotTakes.begin(), slotTakes.end(),
                                     [&] (const SlotTake& t) { return ! t.audio && t.known && t.placed && (! playing || now > t.until + ppq / 2.0); }),
                     slotTakes.end());

    // audio takes: one set of files for every slot recording audio; it closes when the last of them has ended (and the
    // input's latency has passed), then each slot gets its part
    if (slotAudioWanted)
    {
        auto open = false;
        auto last = 0.0;
        for (const auto& t : slotTakes)
            if (t.audio)
            {
                open = open || ! t.known;
                last = std::max (last, t.until);
            }
        const auto tail = (double) takeLatency * takeTicksPerSample + ppq / 8.0;
        if (! open && (! playing || now > last + tail))
        {
            slotAudioWanted = false;
            slotAudioCapture.store (false);
            updateAudioRecording(); // closes the files: finishAudioTake → placeSlotAudio
            slotTakes.erase (std::remove_if (slotTakes.begin(), slotTakes.end(), [] (const SlotTake& t) { return t.audio; }), slotTakes.end());
        }
    }

    // overdub: every finished note goes into the clip its track is playing, where the loop was when it was played
    if (! overdubEvents.empty())
    {
        std::vector<std::pair<int, RecordedEvent>> left;
        for (const auto track : std::vector<int> (gridArmed))
        {
            std::vector<RecordedEvent> mine;
            for (const auto& [t, ev] : overdubEvents)
                if (t == track)
                    mine.push_back (ev);
            if (mine.empty())
                continue;
            const auto scene = runtime.tracks[(std::size_t) track].playing.load (std::memory_order_relaxed);
            const auto* clip = song.clipGrid && scene >= 0 ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
            if (clip == nullptr || clip->kind != ClipKind::pattern || clip->pattern < 0 || clip->pattern >= (int) song.patterns.size())
                continue; // (the clip stopped: what was held is dropped)
            // the notes that have ended are written; a key still down waits for its release
            std::vector<RecordedEvent> finished;
            for (std::size_t i = 0; i < mine.size(); ++i)
            {
                if (! mine[i].on)
                    continue;
                const auto end = std::find_if (mine.begin() + (std::ptrdiff_t) i + 1, mine.end(),
                                               [&] (const RecordedEvent& o) { return o.channel == mine[i].channel && o.key == mine[i].key; });
                if (end == mine.end())
                {
                    if (sessionOverdub)
                        left.push_back ({ track, mine[i] });
                    continue;
                }
                finished.push_back (mine[i]);
                finished.push_back (*end);
            }
            if (finished.empty())
                continue;
            const auto anchor = runtime.tracks[(std::size_t) track].anchor.load (std::memory_order_relaxed);
            auto notes = session::overdubNotes (finished, anchor, clip->length, grid);
            for (auto& n : notes)
                n.start += clip->offset;
            const auto pattern = clip->pattern;
            editSong ([&] (Song& s) {
                if (pattern < (int) s.patterns.size())
                    session::mergeNotes (s.patterns[(std::size_t) pattern], notes);
            }, ! overdubHasUndoStep);
            overdubHasUndoStep = true;
        }
        overdubEvents = std::move (left);
    }

    const auto midiBusy = sessionOverdub || std::any_of (slotTakes.begin(), slotTakes.end(), [] (const SlotTake& t) { return ! t.audio; });
    if (slotTaking.exchange (midiBusy) != midiBusy || slotTakes.empty())
        sendChangeMessage();
}

// The files of a closed audio take become the clips of the slots that recorded: each slot's part of its insert's file.
bool AudioEngine::placeSlotAudio (const std::vector<std::pair<juce::File, int>>& files, double ticksPerSample, int latency)
{
    readSlotTakeMessages();
    const auto startClock = takeStartClock.exchange (-1.0);
    const auto latencyTicks = (double) latency * ticksPerSample;
    struct Placed
    {
        int track, scene, insert;
        std::string path, name;
        std::int64_t length, offset;
        float fraction;
        double until;
        bool thenPlay;
    };
    std::vector<Placed> placed;
    for (const auto& take : slotTakes)
    {
        if (! take.audio || ! take.known || take.until <= take.from)
            continue;
        const auto file = std::find_if (files.begin(), files.end(), [&] (const auto& f) { return f.second == take.insert; });
        if (file == files.end())
            continue;
        // what came in at clock time T was heard one latency earlier: T is `latency` further into the file
        const auto exact = std::max (0.0, take.from - startClock + latencyTicks);
        const auto whole = std::floor (exact + 0.5);
        placed.push_back ({ take.track, take.scene, take.insert, file->first.getFullPathName().toStdString(),
                            file->first.getFileNameWithoutExtension().toStdString(), (std::int64_t) std::llround (take.until - take.from),
                            (std::int64_t) whole, (float) (exact - whole), take.until, take.thenPlay });
    }
    if (placed.empty())
        return false;
    editSong ([&placed] (Song& s) {
        for (const auto& p : placed)
            session::placeRecordedAudio (s, p.track, p.scene, p.path, p.name, p.insert, p.length, p.offset, p.fraction);
    });
    if (transport.playing.load() && clipLauncher != nullptr)
        for (const auto& p : placed)
            if (p.thenPlay)
                clipLauncher->commands.push ({ launcher::Command::Type::launch, p.track, p.scene, p.until });
    return true;
}
} // namespace lynx
