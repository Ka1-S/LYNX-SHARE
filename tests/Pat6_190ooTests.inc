// Round 190oo (design review 2), PAT-6: FL keeps the Playlist's vertical zoom per arrangement in event 0xF2 (28 B: u32
// index, f64 scroll, f64 zoom, i32 vertical scroll, i32 vertical zoom). The row is 16 + 2·v px at 100 % UI scale: FL 2026's
// new project v = 16 → 48 px, Ookay - Thief v = 5 → 26 px. The importer opens the Playlist with that row; a file without the
// record keeps FL's 48 px.

namespace pat6_190oo
{
// one arrangement (0x63 = `index`) and, when `zoom` >= 0, its 0xF2 view record
inline std::vector<std::uint8_t> arrangementFile (int index, int zoom, float trackSize = 1.0f)
{
    FlpBuilder b;
    b.u8 (0x9C); // tempo
    b.u32 (140000);
    b.u8 (0x63); // arrangement
    b.u16 ((std::uint16_t) index);
    {
        // track 2 (0xEE, FL 20+'s 70 B record): its size factor at +13
        std::vector<std::uint8_t> t (70, 0);
        const std::uint32_t one = 2;
        std::memcpy (&t[0], &one, 4);
        const std::uint32_t colour = 0x565148;
        std::memcpy (&t[4], &colour, 4);
        t[12] = 1;
        std::memcpy (&t[13], &trackSize, 4);
        const std::int32_t unlocked = -1;
        std::memcpy (&t[17], &unlocked, 4);
        t[61] = 1;
        b.data (0xEE, t);
    }
    if (zoom >= 0)
    {
        // Ookay - Thief's record with the zoom swapped in: 00000000 0000000000000000 00000040588b3840 05020000 <zoom>
        std::vector<std::uint8_t> v { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0x58, 0x8b, 0x38, 0x40, 0x05, 0x02, 0, 0 };
        const auto i = (std::uint32_t) index;
        std::memcpy (&v[0], &i, 4);
        const auto z = (std::uint32_t) zoom;
        v.insert (v.end(), { (std::uint8_t) z, (std::uint8_t) (z >> 8), (std::uint8_t) (z >> 16), (std::uint8_t) (z >> 24) });
        b.data (0xF2, v);
    }
    return b.bytes;
}
} // namespace pat6_190oo

TEST (pat6_190oo_row_height_from_fl_vertical_zoom)
{
    using namespace lynx;
    // the mapping: FL's new project and Thief, LYNX's limits, and back
    CHECK (polish::flRowHeightForZoom (16) == 48 && polish::flRowHeightForZoom (5) == 26 && polish::flRowHeightForZoom (0) == 16);
    CHECK (polish::flRowHeightForZoom (-3) == 16 && polish::flRowHeightForZoom (100000) == polish::maxTrackHeight);
    CHECK (polish::flZoomForRowHeight (48) == 16 && polish::flZoomForRowHeight (26) == 5 && polish::flZoomForRowHeight (38) == 11);

    // the parser keeps the record on its arrangement
    const auto thief = flp::parseProject (pat6_190oo::arrangementFile (0, 5));
    CHECK (thief.arrangements.size() == 1 && thief.arrangements[0].rowZoom == 5);
    CHECK (flp::parseProject (pat6_190oo::arrangementFile (0, -1)).arrangements[0].rowZoom == -1);

    // the importer opens the Playlist with FL's row: Thief 26 px, a new FL project 48 px, no record 48 px
    CHECK (importFlp (thief).playlistRowHeight == 26);
    CHECK (importFlp (flp::parseProject (pat6_190oo::arrangementFile (0, 16))).playlistRowHeight == 48);
    CHECK (importFlp (flp::parseProject (pat6_190oo::arrangementFile (0, -1))).playlistRowHeight == Song::flDefaultRowHeight);

    // a sized track works on that row, as FL's layout does (16 + (base − 16) × size): 200 % of 26 px = 36 px
    const auto sized = importFlp (flp::parseProject (pat6_190oo::arrangementFile (0, 5, 2.0f)));
    CHECK (sized.playlistRowHeight == 26 && sized.playlistTracks.size() >= 2 && sized.playlistTracks[1].height == 36);

    // a record for another arrangement index does not apply
    {
        auto bytes = pat6_190oo::arrangementFile (0, -1);
        FlpBuilder tail;
        tail.bytes.clear();
        std::vector<std::uint8_t> v (28, 0);
        v[0] = 3;
        v[24] = 5;
        tail.data (0xF2, v);
        bytes.insert (bytes.end(), tail.bytes.begin(), tail.bytes.end());
        CHECK (flp::parseProject (bytes).arrangements[0].rowZoom == -1);
    }

    // LYNX's export writes the song's row as FL's zoom (new songs: 16, FL 2026's own), and it comes back
    {
        auto song = Song::makeDefault();
        flpexport::Options options;
        flpexport::Report report;
        auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
        CHECK (! project.arrangements.empty() && project.arrangements[0].rowZoom == 16);
        song.playlistRowHeight = 26;
        project = flp::parseProject (flpexport::exportFlp (song, options, report));
        CHECK (! project.arrangements.empty() && project.arrangements[0].rowZoom == 5);
        CHECK (importFlp (project).playlistRowHeight == 26);
    }

    // FL's own files: Thief 26 px, the Empty template (File › New) 48 px
    if (! realFlpDir.empty())
        for (const auto& [name, row] : { std::pair<const char*, int> { "Demo projects/Demo songs/Ookay - Thief.flp", 26 },
                                         { "Templates/Minimal/Empty/Empty.flp", 48 } })
        {
            const auto path = realFlpDir + "/" + name;
            std::ifstream in (path, std::ios::binary);
            if (! in)
            {
                std::printf ("  (missing %s)\n", path.c_str());
                continue;
            }
            const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
            CHECK (importFlp (flp::parseProject (bytes)).playlistRowHeight == row);
        }
}
