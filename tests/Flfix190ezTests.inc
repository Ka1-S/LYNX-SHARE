// Round 190ez: what the live FL session 190ew found different (part B): Edison's region trigger notes in FL's "tlst"
// chunk, Gross Beat's Options menu items (option bytes 4…8), Pitcher's MIDI port line, the Piano roll randomizer's
// half-lane law. Evidence: /mnt/e/lynx-work/live190ew/ANSWERS.md (EDI-2, GB-2, PIT-1, PR-1) and
// /mnt/e/lynx-work/flfix190ez/s/rea-gb.md (REA of Gross Beat_x64.dll).

namespace flfix190ez
{
inline void put32 (std::vector<std::uint8_t>& o, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        o.push_back ((std::uint8_t) (v >> (8 * i)));
}

inline void tag (std::vector<std::uint8_t>& o, const char* t) { o.insert (o.end(), t, t + 4); }

// the body of a RIFF chunk of a WAV, or empty
inline std::vector<std::uint8_t> chunk (const std::vector<std::uint8_t>& wav, const char* id)
{
    for (std::size_t p = 12; p + 8 <= wav.size();)
    {
        const auto len = (std::size_t) wav[p + 4] | (std::size_t) wav[p + 5] << 8 | (std::size_t) wav[p + 6] << 16 | (std::size_t) wav[p + 7] << 24;
        if (std::memcmp (wav.data() + p, id, 4) == 0)
            return { wav.begin() + (std::ptrdiff_t) p + 8, wav.begin() + (std::ptrdiff_t) std::min (wav.size(), p + 8 + len) };
        p += 8 + len + (len & 1);
    }
    return {};
}

// FL's tlst body for one region (cue id 1) on D3 = note 38: the 28 bytes of the WAV Edison wrote in 190ew (EDI-2)
inline std::vector<std::uint8_t> flTlst()
{
    return { 1, 0, 0, 0, 'c', 'u', 'e', ' ', 0, 0, 0, 0, 1, 0, 0, 0, 0xff, 0x26, 0x90, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
}

// a 16-bit mono WAV with one region (cue id 1 at 0, ltxt length 100, labl "rega") and a trigger chunk `id` / `body`
inline std::vector<std::uint8_t> wavWith (const char* id, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> o;
    tag (o, "RIFF");
    put32 (o, 0);
    tag (o, "WAVE");
    tag (o, "fmt ");
    put32 (o, 16);
    o.insert (o.end(), { 1, 0, 1, 0 });
    put32 (o, 44100);
    put32 (o, 88200);
    o.insert (o.end(), { 2, 0, 16, 0 });
    tag (o, "data");
    put32 (o, 400);
    o.insert (o.end(), 400, 0);
    tag (o, "cue ");
    put32 (o, 28);
    put32 (o, 1);
    put32 (o, 1);
    put32 (o, 0);
    tag (o, "data");
    put32 (o, 0);
    put32 (o, 0);
    put32 (o, 0);
    tag (o, "LIST");
    put32 (o, 4 + 8 + 9 + 1 + 8 + 20);
    tag (o, "adtl");
    tag (o, "labl");
    put32 (o, 9);
    put32 (o, 1);
    o.insert (o.end(), { 'r', 'e', 'g', 'a', 0, 0 }); // (odd size: one pad byte)
    tag (o, "ltxt");
    put32 (o, 20);
    put32 (o, 1);
    put32 (o, 100);
    tag (o, "beat");
    o.insert (o.end(), 8, 0);
    tag (o, id);
    put32 (o, (std::uint32_t) body.size());
    o.insert (o.end(), body.begin(), body.end());
    const auto size = (std::uint32_t) (o.size() - 8);
    for (int i = 0; i < 4; ++i)
        o[(std::size_t) (4 + i)] = (std::uint8_t) (size >> (8 * i));
    return o;
}

// a Gross Beat slot playing a factory time slot that jumps (so the click reduction is heard)
inline lynx::EffectSlot gbSlot (std::uint8_t clickReduction)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    auto st = lynx::grossbeat::editableState (slot.state);
    st.options[8] = clickReduction;
    slot.state = lynx::grossbeat::withState (slot.state, st);
    const auto names = lynx::grossbeat::slotNames (slot.state, false);
    int stutter = 1;
    for (int i = 0; i < (int) names.size(); ++i)
        if (names[(std::size_t) i].find ("Stutter") != std::string::npos || names[(std::size_t) i].find ("stutter") != std::string::npos)
        {
            stutter = i;
            break;
        }
    slot.params[0] = lynx::grossbeat::paramFromRaw (0, stutter);
    return slot;
}
} // namespace flfix190ez

// ---- Edison: FL's "tlst" chunk ------------------------------------------------------------------------------------
TEST (edison190ez_trigger_notes_are_written_as_fls_tlst)
{
    using namespace lynx::audioedit;
    Document doc;
    doc.audio.sampleRate = 44100.0;
    doc.audio.channels = { std::vector<float> (4000, 0.1f) };
    doc.markers.push_back ({ 0, 1000, "rega", "", 0.0, 38 });
    const auto bytes = encodeWav (doc, WavFormat::int16);
    CHECK (flfix190ez::chunk (bytes, "tlst") == flfix190ez::flTlst()); // byte for byte what FL wrote
    CHECK (flfix190ez::chunk (bytes, "lxkn").empty());
    // only the regions with a note get an entry, keyed by their cue id
    doc.markers.push_back ({ 1000, 2000, "b", "", 0.0, -1 });
    doc.markers.push_back ({ 2000, 3000, "c", "", 0.0, 127 });
    const auto two = flfix190ez::chunk (encodeWav (doc, WavFormat::int16), "tlst");
    CHECK (two.size() == 4 + 2 * 24 && two[0] == 2 && two[4 + 24 + 8] == 3 && two[4 + 24 + 13] == 127 && two[4 + 13] == 38);
}

TEST (edison190ez_reads_fls_tlst_and_027s_lxkn)
{
    using namespace lynx::audioedit;
    {
        const auto wav = flfix190ez::wavWith ("tlst", flfix190ez::flTlst());
        Document doc;
        CHECK (decodeWav (wav.data(), wav.size(), doc));
        CHECK (doc.markers.size() == 1);
        if (doc.markers.size() == 1)
            CHECK (doc.markers[0].keyNum == 38 && doc.markers[0].name == "rega" && doc.markers[0].start == 0 && doc.markers[0].end == 100);
    }
    {
        // a file saved by a 0.1.27 build: LYNX's own chunk (cue id, key)
        std::vector<std::uint8_t> lxkn;
        flfix190ez::put32 (lxkn, 1);
        flfix190ez::put32 (lxkn, 36);
        const auto wav = flfix190ez::wavWith ("lxkn", lxkn);
        Document doc;
        CHECK (decodeWav (wav.data(), wav.size(), doc));
        CHECK (doc.markers.size() == 1 && doc.markers[0].keyNum == 36);
        // saved again: FL's chunk
        CHECK (flfix190ez::chunk (encodeWav (doc, WavFormat::int16), "tlst").size() == 28);
    }
    {
        // a damaged count never reads past the chunk
        auto bad = flfix190ez::flTlst();
        bad[0] = 0xff;
        const auto wav = flfix190ez::wavWith ("tlst", bad);
        Document doc;
        CHECK (decodeWav (wav.data(), wav.size(), doc) && doc.markers.size() == 1 && doc.markers[0].keyNum == 38);
    }
}

// FL's own WAV (not in the repo: it holds FL's sample). LYNX_EDISON_FL_WAV=<path to lynx190ewtlst.wav>
TEST (edison190ez_fl_wav_round_trip)
{
    const char* path = std::getenv ("LYNX_EDISON_FL_WAV");
    if (path == nullptr)
        return;
    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> fl ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    using namespace lynx::audioedit;
    Document doc;
    CHECK (decodeWav (fl.data(), fl.size(), doc));
    CHECK (doc.markers.size() == 1);
    if (doc.markers.size() != 1)
        return;
    CHECK (doc.markers[0].name == "rega" && doc.markers[0].keyNum == 38 && doc.markers[0].start == 0 && doc.markers[0].end == 8820);
    const auto back = encodeWav (doc, WavFormat::int16);
    CHECK (flfix190ez::chunk (back, "tlst") == flfix190ez::chunk (fl, "tlst"));
    Document again;
    CHECK (decodeWav (back.data(), back.size(), again) && again.markers.size() == 1 && again.markers[0].keyNum == 38
           && again.markers[0].end == 8820 && again.audio.frames() == doc.audio.frames());
    std::printf ("edison190ez: FL WAV %zu bytes, region \"%s\" %lld..%lld key %d; tlst round trip identical\n", fl.size(),
                 doc.markers[0].name.c_str(), (long long) doc.markers[0].start, (long long) doc.markers[0].end, doc.markers[0].keyNum);
}

// ---- Gross Beat: the Options menu's option bytes ------------------------------------------------------------------
TEST (gb190ez_new_instance_options_are_fls)
{
    // FL 2026's new instance (constructor 0x83fcf0; live shot GB-2): Click reduction Low, attack compensation and DC
    // off, HQ resampling and the scratching clock on
    const auto st = lynx::grossbeat::emptyState();
    CHECK (st.options[8] == 1 && st.options[5] == 0 && st.options[4] == 0 && st.options[6] == 1 && st.options[7] == 1);
    // the bytes travel in the state
    auto edited = st;
    edited.options = { 0, 0, 0, 0, 1, 1, 0, 0, 2 };
    const auto container = lynx::grossbeat::withState (lynx::makeEffectSlot (lynx::EffectKind::grossBeat).state, edited);
    CHECK (lynx::grossbeat::editableState (container).options == edited.options);
}

TEST (gb190ez_click_reduction_change_while_running_equals_a_fresh_load)
{
    // Options › Click reduction › High taken by a running instance (acceptState → Engine::update) sounds exactly like an
    // instance loaded with High; a state sent again unchanged changes nothing (old projects sound the same)
    const auto low = flfix190ez::gbSlot (1), high = flfix190ez::gbSlot (2);
    const auto plainLow = fled190ee::render (low, {});
    const auto plainHigh = fled190ee::render (high, {});
    const auto lowThenHigh = fled190ee::render (low, [&high] (lynx::Effect& fx, int block) {
        if (block == 0)
            fx.acceptState (high.state);
    });
    const auto lowAgain = fled190ee::render (low, [&low] (lynx::Effect& fx, int block) {
        if (block == 3)
            fx.acceptState (low.state);
    });
    CHECK (fled190ee::diff (plainLow, plainHigh) > 1.0e-4); // the setting is heard with this slot
    CHECK (fled190ee::diff (lowThenHigh, plainHigh) == 0.0);
    CHECK (fled190ee::diff (lowAgain, plainLow) == 0.0);
}

// ---- Pitcher: FL's "MIDI Input Port Number" line at the bottom of its panel ---------------------------------------
TEST (pitcher190ez_port_line_in_the_panel)
{
    const auto& l = lynx::plugui::layoutFor (lynx::EffectKind::pitchCorrect);
    bool found = false;
    for (const auto& item : l.items)
        if (item.type == lynx::plugui::Item::display && item.display != nullptr && std::strcmp (item.display, "pitchport") == 0)
            found = item.y + item.h <= l.height && item.y >= 330.0f;
    CHECK (found && lynx::plugui::displayTakesMouse ("pitchport"));
}

// ---- Piano roll randomizer: at 100 % a level moves at most half its lane (live FL check 190ew PR-1, 190cq) ------
TEST (prr190ez_randomizer_moves_half_the_lane)
{
    using namespace lynx::notetools;
    lynx::Pattern p;
    for (int i = 0; i < 64; ++i)
    {
        lynx::Note n;
        n.key = 60;
        n.length = 24;
        n.start = i * 24;
        n.velocity = 0.78f;
        n.release = 0.0f;
        n.modX = 0.5f;
        n.modY = 0.5f;
        p.notes.push_back (n);
    }
    std::vector<int> all;
    for (int i = 0; i < 64; ++i)
        all.push_back (i);
    RandomiseOptions o;
    o.velocity = 1.0f;
    o.pan = 1.0f;
    o.modX = 1.0f;
    o.modY = 1.0f;
    o.release = 1.0f;
    o.seed = 5;
    auto both = p;
    randomise (both, all, o);
    float lowVel = 1.0f, maxX = 0.0f, minX = 1.0f, maxRel = 0.0f, maxPan = 0.0f;
    int panEnds = 0, xEnds = 0;
    for (const auto& n : both.notes)
    {
        lowVel = std::min (lowVel, n.velocity);
        maxX = std::max (maxX, std::max (n.modX, n.modY));
        minX = std::min (minX, std::min (n.modX, n.modY));
        maxRel = std::max (maxRel, n.release);
        maxPan = std::max (maxPan, std::abs (n.pan));
        panEnds += std::abs (n.pan) >= 1.0f;
        xEnds += n.modX <= 0.0f || n.modX >= 1.0f;
    }
    CHECK (lowVel >= 0.78f - 0.5f - 1e-6f && lowVel < 0.5f); // FL: from 78 % the lowest seen was ~30 %
    CHECK (maxRel <= 0.5f + 1e-6f && maxRel > 0.3f);          // Release from 0: 0…0.5
    CHECK (maxX <= 1.0f && minX >= 0.0f && maxX > 0.85f && minX < 0.15f); // from the centre towards both ends
    CHECK (maxPan > 0.85f && panEnds == 0 && xEnds == 0);    // no pile-up at the ends (LYNX's old law clamped there)
    // Bipolar off: only up (unchanged)
    o.bipolar = false;
    auto up = p;
    randomise (up, all, o);
    bool onlyUp = true;
    for (const auto& n : up.notes)
        onlyUp = onlyUp && n.velocity >= 0.78f && n.modX >= 0.5f && n.modY >= 0.5f && n.pan >= 0.0f && n.release <= 0.5f + 1e-6f;
    CHECK (onlyUp);
}
