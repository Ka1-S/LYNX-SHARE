// Round 90 (docs/analise/UI_STUDY_round90.md): Channel Rack note preview rule, FLP playlist track colours.

TEST (fb190bm_rack_complete_preview_span)
{
    using lynx::notePreviewSpan;
    const int step = 22;
    // a 2-bar pattern in a rack that shows 32 steps: step-aligned, as before
    auto s = notePreviewSpan (32 * step, 32 * step, 0, true);
    CHECK (s.x == 0 && s.width == 32 * step && ! s.fitted);
    // 4 and 8 bars in the same rack: the whole pattern in the visible width, wherever the steps are scrolled to
    for (int steps : { 64, 128, 512 })
        for (int scroll : { 0, 5 * step, (steps - 32) * step })
        {
            s = notePreviewSpan (steps * step, 32 * step, scroll, true);
            CHECK (s.fitted);
            CHECK (s.x == scroll); // (drawn at the visible left edge)
            CHECK (s.width == 32 * step);
            CHECK (s.x + s.width <= steps * step); // inside the rack
        }
    // a wider rack than the pattern: nothing to fit
    s = notePreviewSpan (64 * step, 80 * step, 0, true);
    CHECK (s.width == 64 * step && ! s.fitted);
    // the option off: step-aligned at any length (the miniature scrolls with the steps)
    s = notePreviewSpan (128 * step, 32 * step, 10 * step, false);
    CHECK (s.x == 0 && s.width == 128 * step && ! s.fitted);
    // no view yet
    s = notePreviewSpan (128 * step, 0, 0, true);
    CHECK (s.width == 128 * step && ! s.fitted);
}

TEST (uilook90_rack_note_preview_rule)
{
    lynx::Pattern p;
    p.toggleStep (0, 0);
    p.toggleStep (0, 4);
    CHECK (! lynx::showsNotePreview (p, 0)); // plain steps keep the step buttons
    CHECK (! lynx::showsNotePreview (p, 1)); // no notes at all

    auto keyed = p;
    keyed.notes[1].key = 63;
    CHECK (lynx::showsNotePreview (keyed, 0)); // another key
    CHECK (lynx::channelKeyRange (keyed, 0) == std::make_pair (60, 63));

    auto longer = p;
    longer.notes[0].length = 2 * lynx::ticksPerStep;
    CHECK (lynx::showsNotePreview (longer, 0));

    auto offGrid = p;
    offGrid.notes[0].start += 1;
    CHECK (lynx::showsNotePreview (offGrid, 0));

    auto slide = p;
    slide.notes[0].slide = true;
    CHECK (lynx::showsNotePreview (slide, 0));
    CHECK (! lynx::showsNotePreview (slide, 2));
    CHECK (lynx::channelKeyRange (slide, 2) == std::make_pair (60, 60));
}

TEST (uilook90_flp_track_colours)
{
    if (realFlpDir.empty())
        return;

    const auto path = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / "Ookay - Thief.flp";
    if (! std::filesystem::exists (path))
        return;

    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
    const auto song = lynx::importFlp (flp::parseProject (file));
    // track 1 "DRUMS" is red (0x00BBGGRR 0x0b0e77), track 23 "BASSES" blue, track 41 has FL's default grey
    CHECK (song.playlistTracks.size() >= 40);
    if (song.playlistTracks.size() >= 40)
    {
        CHECK (song.playlistTracks[0].name == "DRUMS" && song.playlistTracks[0].colour == 0xFF770E0Bu);
        CHECK (song.playlistTracks[22].name == "BASSES" && song.playlistTracks[22].colour == 0xFF16296Cu);
    }
    CHECK (song.playlistTracks.size() <= 40 || song.playlistTracks[40].colour == 0);
}

TEST (uilook90_overview_strip_gestures)
{
    using namespace lynx::gesture;
    // 1000 px strip over 100 bars of 10 units; the window shows bars 20..40
    const OverviewWindow w { 200.0, 200.0 };
    CHECK (overviewGrabAt (200.0, 1000.0, 1000.0, w) == OverviewGrab::left);
    CHECK (overviewGrabAt (404.0, 1000.0, 1000.0, w) == OverviewGrab::right);
    CHECK (overviewGrabAt (300.0, 1000.0, 1000.0, w) == OverviewGrab::move);
    CHECK (overviewGrabAt (700.0, 1000.0, 1000.0, w) == OverviewGrab::none);

    // moving keeps the length and stays inside the content
    auto m = overviewDrag (OverviewGrab::move, w, 100.0, 1000.0, 1000.0, 10.0);
    CHECK (m.start == 300.0 && m.length == 200.0);
    m = overviewDrag (OverviewGrab::move, w, 5000.0, 1000.0, 1000.0, 10.0);
    CHECK (m.start == 800.0 && m.length == 200.0);
    m = overviewDrag (OverviewGrab::move, w, -5000.0, 1000.0, 1000.0, 10.0);
    CHECK (m.start == 0.0);

    // the edges zoom: the other edge stays, the window never gets shorter than minLength
    auto l = overviewDrag (OverviewGrab::left, w, -100.0, 1000.0, 1000.0, 10.0);
    CHECK (l.start == 100.0 && l.length == 300.0);
    l = overviewDrag (OverviewGrab::left, w, 1000.0, 1000.0, 1000.0, 10.0);
    CHECK (l.start == 390.0 && l.length == 10.0);
    auto r = overviewDrag (OverviewGrab::right, w, 100.0, 1000.0, 1000.0, 10.0);
    CHECK (r.start == 200.0 && r.length == 300.0);
    r = overviewDrag (OverviewGrab::right, w, 5000.0, 1000.0, 1000.0, 10.0);
    CHECK (r.length == 800.0);

    // a click outside the window centres it there
    CHECK (overviewCentredOn (700.0, w, 1000.0).start == 600.0);
    CHECK (overviewCentredOn (990.0, w, 1000.0).start == 800.0);

    // a tiny window still has a middle to grab
    CHECK (overviewGrabAt (201.5, 1000.0, 1000.0, { 200.0, 3.0 }) == OverviewGrab::move);
}
