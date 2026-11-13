// Issue #152 (0.1.25, Windows 11): (1) "the count-in doesn't keep its change"; (3) "I pick a physical input of my
// interface on insert X and LYNX records the signal of another insert". (2), the VST3 controls that follow a MIDI
// controller in the sound but not in the plugin's window, is a JUCE host fix (cmake/LynxPatchJuce.cmake fix 25).
#include "core/CountIn.h"

TEST (iss152_count_in_toggle_keeps_its_length)
{
    using namespace lynx::countin;
    // Ctrl+P / the button: a 2-bar count-in goes off and comes back at 2 bars (it came back at 1 bar)
    CHECK (toggled (2, 2) == 0);
    CHECK (toggled (0, 2) == 2);
    CHECK (toggled (0, 4) == 4);
    CHECK (toggled (1, 1) == 0);
    // a length out of range: FL's 1 bar .. LYNX's 4
    CHECK (toggled (0, 0) == 1 && toggled (0, 9) == 4);
    // a count-in that is on sets the length; off keeps it
    CHECK (lengthFor (2, 1) == 2 && lengthFor (0, 4) == 4 && lengthFor (0, -3) == 1);
}

TEST (iss152_count_in_settings_round_trip_in_any_order)
{
    using namespace lynx::countin;
    // what MainComponent writes: the switch and the length; read back in either order
    for (const bool lengthFirst : { false, true })
        for (const int saved : { 0, 1, 2, 4 })
        {
            const int savedLength = saved > 0 ? saved : 2; // off with 2 bars kept
            int bars = 0, length = 1;                       // the defaults at start-up
            const std::pair<std::string_view, int> lines[] { { onKey, saved > 0 ? 1 : 0 }, { lengthKey, savedLength } };
            for (int k = 0; k < 2; ++k)
            {
                const auto& [key, value] = lines[lengthFirst ? 1 - k : k];
                CHECK (readSetting (key, value, bars, length));
            }
            CHECK (bars == saved);
            CHECK (length == savedLength);
        }
    // a new install starts as FL's factory form (on, 1 bar); an older settings file without the keys keeps LYNX's off
    CHECK (startingBars (false) == 1 && startingBars (true) == 0);
    int bars = 3, length = 3;
    CHECK (! readSetting ("recordPreRoll", 2, bars, length) && bars == 3 && length == 3); // another key: untouched
    CHECK (std::string (onKey) != std::string (lengthKey));
}

namespace
{
// insert 1 plays physical input 3 and sends to insert 2; insert 2 has physical input 1 - 2 armed: the reporter's
// "records the signal of another insert"
struct Iss152Inputs
{
    lynx::Song song = lynx::Song::makeDefault();
    lynx::RackRenderer renderer;
    lynx::LiveInput input;
    std::vector<float> in1 = std::vector<float> (256, 0.25f), in2 = std::vector<float> (256, 0.25f), in3 = std::vector<float> (256, 0.5f),
                       l = std::vector<float> (256), r = std::vector<float> (256);

    Iss152Inputs()
    {
        song.channels.clear();
        song.playlist.clear();
        song.mixer = lynx::Song::defaultMixer (4);
        auto& other = song.mixer[1];
        other.inputChannel = 2;
        other.inputStereo = false;
        other.monitor = lynx::InputMonitor::on;
        other.sends.push_back ({ 2, 1.0f });
        auto& x = song.mixer[2];
        x.inputChannel = 0; // "In 1 - In 2"
        x.monitor = lynx::InputMonitor::automatic;
        renderer.prepare (48000.0, 512);
        renderer.setLiveInput (&input);
        input.channels[0] = in1.data();
        input.channels[1] = in2.data();
        input.channels[2] = in3.data();
        input.numChannels = 3;
        input.armed[2].store (true);
        input.recording.store (true);
        input.taps[0].point.store ((int) lynx::RecordSource::postEq);
        input.taps[0].track.store (2);
        input.tapCount.store (1);
    }

    float postEqTake()
    {
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        for (int block = 0; block < 12; ++block)
        {
            for (auto& d : input.taps[0].data)
                std::fill (d.begin(), d.begin() + 256, 0.0f);
            renderer.process (state.get(), true, 0.0, 256, l.data(), r.data());
        }
        return input.taps[0].data[0][200];
    }
};
} // namespace

TEST (iss152_post_eq_take_holds_other_inserts_audio)
{
    // the cause: FL's default "Record" position is Post EQ, which takes everything reaching insert X — here insert 1's
    // input 3 too (0.25 + 0.5), not only X's own input
    Iss152Inputs t;
    CHECK (t.song.mixer[2].recordSource == lynx::RecordSource::postEq);
    const auto take = t.postEqTake();
    CHECK (std::abs (take - 0.75f) < 0.05f);
}

TEST (iss152_choosing_an_input_asks_for_external_input_only)
{
    using lynx::RecordSource;
    // FL asks when an input is chosen on an insert that records more than it; "External input only" records the
    // device input alone (the engine pushes the input's channels, no tap), through the same insert on playback
    CHECK (lynx::asksExternalInputOnly (2, 0, RecordSource::postEq));
    CHECK (lynx::asksExternalInputOnly (2, 3, RecordSource::externalAndMixer));
    CHECK (lynx::asksExternalInputOnly (2, 0, RecordSource::postTrack));
    CHECK (! lynx::asksExternalInputOnly (2, 0, RecordSource::externalInput)); // already
    CHECK (! lynx::asksExternalInputOnly (2, -1, RecordSource::postEq));       // "(none)"
    CHECK (! lynx::asksExternalInputOnly (0, 0, RecordSource::postEq));        // the Master: no record arm
}

TEST (iss152_simple_recording_tracks_record_their_input_alone)
{
    // the Simple workflow's voice / guitar / bass tracks: their input alone, on their own insert
    using namespace simple184a;
    for (const auto kind : { simpletracks::Kind::voice, simpletracks::Kind::guitar, simpletracks::Kind::bass })
    {
        auto s = freshSong();
        simpletracks::Request q;
        q.kind = kind;
        q.input = 0;
        const auto r = simpletracks::addTrack (s, q);
        CHECK (r.ok());
        CHECK (s.mixer[(std::size_t) r.insert].inputChannel == 0);
        CHECK (s.mixer[(std::size_t) r.insert].recordSource == lynx::RecordSource::externalInput);
    }
    // no input chosen: FL's default stays
    auto s = freshSong();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::voice;
    q.input = -1;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok() && s.mixer[(std::size_t) r.insert].recordSource == lynx::RecordSource::postEq);
}
