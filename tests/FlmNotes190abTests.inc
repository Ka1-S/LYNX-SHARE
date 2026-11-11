// Round 190ab (#88): the .flm note bytes beyond the velocity — pan, drum pitch, repeat, slide — as FLM plays them.

TEST (flm190ab_note_pan_pitch_repeat_slide)
{
    using namespace lynx;
    namespace t = flm182c;
    t::Bytes f;
    t::tag (f, "10LF");
    using E = t::Ev;
    const auto bytes = t::cat ({ f, t::head ("Notes", 120.0), t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::module (0, 2, {}, "Default/Default", 0, 127, 44, true) }),
        t::rack (flm::kindNormal, {}, { t::sample ("Kick", "@R@", "Drum Samples\\Kick\\Kick 1.wav") }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Lead", 1, { t::lane (0, "Lead", { t::clip (0, 4, 4, 0, 1, {
            E { 0, 2, 60 },                             // the held note
            E { 1, 0.5, 67, 204, 127, 0x7FFF, 0, 1 },   // a slide inside it
            E { 3, 0.5, 64, 204, 127, 0x7FFF, 0, 1 },   // a slide with nothing held: FLM plays nothing
            E { 2.25, 0.5, 62, 204, 255, 0x4000, 2 },   // hard right, 2 repeats, a pitch word note lanes ignore
            E { 3.5, 0.25, 65, 204, 0, 0x7FFF, -1 } }) }) }), // hard left; a negative repeat is none
        t::channel (flm::kindNormal, "Drums", 2, { t::lane (3, "Drums", { t::clip (0, 4, 4, 0, 1, {
            E { 0.25, 0.1249, 0, 255, 57, 0x7FFF, 1 },  // an odd step with 1 repeat, panned left
            E { 0.5, 0.1249, 0, 255, 127, 0x7FFF, 1 },  // an even step with 1 repeat
            E { 1, 0.1249, 0, 255, 127, 65534 },        // an octave up
            E { 1.5, 0.1249, 0, 255, 127, 0 },          // an octave down
            E { 2, 0.1249, 0, 255, 127, 35000 },        // 0.818 semitones up
            E { 3, 0.1249, 0, 255, 127, 0x7FFF, 0, 1 } }) }, -1, -1, 0.263) }) }); // drum lanes do not slide
    std::string error;
    const auto parsed = flm::parse ({ bytes.data(), bytes.size() }, &error);
    CHECK (parsed.has_value());
    if (parsed)
    {
        const flm::Lane* drums = nullptr;
        for (const auto& c : parsed->channels)
            for (const auto& l : c.lanes)
                if (l.kind() == flm::LaneKind::drums)
                    drums = &l;
        CHECK (drums != nullptr && drums->swing == 0.263 && drums->clips.size() == 1 && drums->clips[0].events[0].repeat == 1
               && drums->clips[0].events[0].pan == 57 && drums->clips[0].events[5].slide == 1);
    }
    FlpImportReport report;
    const auto song = importFlm (bytes, t::options (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    CHECK (song->panTriangular); // FLM pans by balance
    const auto at = [] (int flmTick) { return (std::int64_t) std::llround ((double) flmTick / 128.0 * ppq); };
    const auto notesOf = [&] (const char* name) {
        std::vector<Note> out;
        const auto* c = t::channelNamed (*song, name);
        const auto index = c == nullptr ? -1 : (int) (c - song->channels.data());
        for (const auto& p : song->patterns)
            for (const auto& n : p.notes)
                if (n.channel == index)
                    out.push_back (n);
        std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return out;
    };
    const auto lead = notesOf ("Lead");
    CHECK (lead.size() == 6);
    if (lead.size() == 6)
    {
        CHECK (lead[0].key == 60 && ! lead[0].slide && lead[0].pan == 0.0f && lead[0].finePitch == 0.0f);
        CHECK (lead[1].key == 67 && lead[1].slide && lead[1].start == at (128) && lead[1].length == at (64));
        // 2 repeats on an odd step, no swing: thirds of a 1/16, in FLM's own ticks (truncated): 288, 298, 309
        CHECK (lead[2].key == 62 && lead[2].start == at (288) && lead[2].pan == 1.0f && lead[2].finePitch == 0.0f && ! lead[2].slide);
        CHECK (lead[3].key == 62 && lead[3].start == at (298) && lead[3].pan == 1.0f && lead[3].velocity == lead[2].velocity);
        CHECK (lead[4].key == 62 && lead[4].start == at (309));
        // one note-off for all of them, at the note's end
        CHECK (lead[3].start + lead[3].length == lead[2].start + lead[2].length && lead[4].start + lead[4].length == lead[2].start + lead[2].length);
        CHECK (lead[5].key == 65 && lead[5].pan == -1.0f);
    }
    const auto drums = notesOf ("Kick");
    CHECK (drums.size() == 8);
    if (drums.size() == 8)
    {
        // odd step: the 1/16 is 1/4 − 0.263 / 8 of a beat, the hit half-way: tick (int) (32 + 13.896) = 45
        CHECK (drums[0].start == at (32) && drums[1].start == at (45) && std::abs (drums[0].pan - (114.0f - 255.0f) / 255.0f) < 1e-6f
               && drums[1].pan == drums[0].pan && drums[1].length == drums[0].length && drums[0].key == 60);
        // even step: 1/4 + 0.263 / 8, half-way: tick (int) (64 + 18.104) = 82
        CHECK (drums[2].start == at (64) && drums[3].start == at (82) && drums[2].pan == 0.0f);
        CHECK (drums[4].key == 72 && drums[4].finePitch == 0.0f && drums[5].key == 48 && drums[5].finePitch == 0.0f);
        CHECK (drums[6].key == 61 && std::abs (drums[6].finePitch - (float) (100.0 * (12.0 * (2.0 * 35000.0 / 65534.0 - 1.0) - 1.0))) < 1e-3f);
        CHECK (drums[7].start == at (384) && ! drums[7].slide && drums[7].key == 60);
    }
    CHECK (report.converted["FL Studio Mobile: slide note"] == 1 && report.converted["FL Studio Mobile: note repeat (extra hits)"] == 4
           && report.converted["FL Studio Mobile: drum note pitch"] == 3 && report.converted["FL Studio Mobile: note pan"] == 3
           && report.unsupported["FL Studio Mobile: slide note without a held note (FLM plays nothing)"] == 1);
}
