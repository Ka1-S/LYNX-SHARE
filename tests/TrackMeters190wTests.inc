// Round 190w: FL 2026's Playlist menu › View › Track headers ([DFM] EEShowAudioTrackControlsAction Checked=true,
// EEShowAudioTrackMetersAction Checked=true, EEShowInstrumentTrackMetersAction unchecked; [SHOT] flgap190w): which
// mixer track a header's level meter shows (core trackmodes::meteredInsert), and the MeterBank's second reader.

TEST (r190w_track_header_levels_follow_fl_defaults)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.playlistTracks.resize (4);
    const trackmodes::HeaderOptions fl; // FL's defaults: audio tracks on, instrument tracks off
    CHECK (fl.audioControls && fl.audioLevels && ! fl.instrumentLevels);

    CHECK (trackmodes::meteredInsert (song, 0, fl) < 0); // unassigned: no meter
    CHECK (trackmodes::meteredInsert (song, -1, fl) < 0 && trackmodes::meteredInsert (song, 99, fl) < 0);

    CHECK (trackmodes::makeAudioTrack (song, 1));
    const auto audioInsert = song.playlistTracks[1].mixerTrack;
    CHECK (audioInsert > 0 && trackmodes::meteredInsert (song, 1, fl) == audioInsert);

    CHECK (trackmodes::makeInstrumentTrack (song, 2, 0));
    CHECK (trackmodes::meteredInsert (song, 2, fl) < 0); // off by default
    auto all = fl;
    all.instrumentLevels = true;
    CHECK (trackmodes::meteredInsert (song, 2, all) == song.channels[0].mixerTrack); // the channel's own route
    song.channels[0].mixerTrack = 0; // routed to the Master afterwards: the Master's level
    CHECK (trackmodes::meteredInsert (song, 2, all) == 0);

    auto none = fl;
    none.audioLevels = false;
    CHECK (trackmodes::meteredInsert (song, 1, none) < 0);
    CHECK (trackmodes::meteredInsert (song, 1, all) == audioInsert);
}

TEST (r190w_meter_bank_view_peaks_do_not_steal_the_mixers)
{
    using namespace lynx;
    MeterBank bank;
    bank.accumulate (3, 0.5f, 0.25f);
    CHECK (bank.takePeak (3, 0) == 0.5f);         // the Mixer reads its peak
    CHECK (bank.takeViewPeak (3, 0) == 0.5f);     // the Playlist headers still get theirs
    CHECK (bank.takeViewPeak (3, 1) == 0.25f);
    CHECK (bank.takeViewPeak (3, 0) == 0.0f);     // read and reset
    CHECK (bank.takePeak (3, 1) == 0.25f);
    CHECK (bank.takeViewPeak (-1, 0) == 0.0f && bank.takeViewPeak (maxMixerTracks, 0) == 0.0f);
}
