// --- Owner decision D5 (round als190ka-export): AIFF and M4A (AAC) export next to WAV / MP3 / OGG / FLAC
// (core/ExportOptions.h; the encoders are engine/Exporter.cpp and engine/AacEncoder.h, checked by --selftest's
// export_formats) -------------------------------------------------------------------------------------------------

namespace export190ka
{
inline double levelRms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) std::max<std::size_t> (1, to - from));
}

inline std::vector<float> testSine (double hz, double rate, std::size_t n, double amplitude = 0.5)
{
    std::vector<float> x (n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = (float) (amplitude * std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / rate));
    return x;
}
} // namespace export190ka

TEST (export190ka_formats_names_extensions_and_order)
{
    using namespace lynx::exportopts;
    CHECK (formatCount == 6 && (int) Format::aiff == 4 && (int) Format::aac == 5); // (appended: saved numbers keep their meaning)
    CHECK (std::string (extension (Format::aiff)) == ".aif" && std::string (extension (Format::aac)) == ".m4a");
    CHECK (std::string (extension (Format::wav)) == ".wav" && std::string (extension (Format::flac)) == ".flac");
    CHECK (std::string (formatName (Format::aiff)) == "AIFF" && std::string (formatName (Format::aac)) == "M4A"
           && std::string (formatName (Format::wav)) == "WAV" && std::string (formatName (Format::ogg)) == "OGG");
    CHECK ((aacBitrates() == std::vector<int> { 96, 128, 160, 192 }));

    Options o;
    o.format = Format::aac;
    o.moreFormats = (1u << (int) Format::aiff) | (1u << (int) Format::wav) | (1u << (int) Format::flac);
    const auto f = formatsOf (o);
    CHECK (f.size() == 4 && f[0] == Format::aac && f[1] == Format::wav && f[2] == Format::flac && f[3] == Format::aiff);
    // the main format is never "more"; the six bits are kept, nothing above them
    auto self = o;
    self.moreFormats |= 1u << (int) Format::aac;
    CHECK (clamped (self).moreFormats == o.moreFormats);
    self.moreFormats = 0xFFu;
    CHECK (clamped (self).moreFormats == 0x1Fu);

    // bits and dither: AIFF is integer 16 / 24 (dithered as WAV / FLAC), M4A encodes 16-bit with no dither
    Options a;
    a.format = Format::aiff;
    a.aiffBits = 16;
    CHECK (fileBits (a) == 16 && usesDither (a));
    a.aiffBits = 24;
    CHECK (fileBits (a) == 24 && usesDither (a));
    a.dither = DitherMode::none;
    CHECK (! usesDither (a));
    Options m;
    m.format = Format::aac;
    CHECK (fileBits (m) == 16 && ! usesDither (m));
}

TEST (export190ka_settings_round_trip_and_old_files)
{
    using namespace lynx::exportopts;
    Options o;
    o.format = Format::aiff;
    o.aiffBits = 16;
    o.aacKbps = 128;
    o.moreFormats = (1u << (int) Format::aac) | (1u << (int) Format::mp3);
    CHECK (fromText (toText (o)) == o);
    o.format = Format::aac;
    o.moreFormats = 1u << (int) Format::aiff;
    CHECK (fromText (toText (o)) == o);

    // a file saved before D5 (no aiffBits / aacKbps; every old "more" bit) loads exactly as before, with the defaults
    const std::string old = "format=3\nwavBits=16\nflacBits=16\nflacLevel=7\nmp3Kbps=192\noggKbps=160\nsampleRate=48000\nmoreFormats=7\n";
    const auto back = fromText (old);
    CHECK (back.format == Format::flac && back.wavBits == 16 && back.flacBits == 16 && back.flacLevel == 7 && back.mp3Kbps == 192);
    CHECK (back.moreFormats == 7u && back.aiffBits == 24 && back.aacKbps == 192);
    CHECK (fromText ("format=1\nmoreFormats=15\n").moreFormats == 13u); // (MP3 is the main one)

    // bad values: nearest bitrate, AIFF has no 32-bit, unknown formats and bits fall back
    const auto bad = fromText ("format=6\naiffBits=32\naacKbps=150\nmoreFormats=999\n");
    CHECK (bad.format == Format::wav && bad.aiffBits == 24 && bad.aacKbps == 160 && bad.moreFormats == 0x3Eu);
    CHECK (fromText ("aacKbps=20\n").aacKbps == 96 && fromText ("aacKbps=320\n").aacKbps == 192 && fromText ("format=5\n").format == Format::aac);

    // presets and "same as last time" carry the new formats
    Preset p { "M4A demo", o };
    CHECK (presetsFromText (presetsToText ({ p })) == std::vector<Preset> { p });
    LastExport last { o, "/music/exports/Song.m4a" };
    CHECK (lastExportFromText (lastExportToText (last)) == last);
    CHECK (sameAsLastFile (last, "Other").find ("Other.m4a") != std::string::npos);
    last.options.format = Format::aiff;
    CHECK (sameAsLastFile (last, "Other").find ("Other.aif") != std::string::npos);
}

TEST (export190ka_size_estimate_and_fl_mask)
{
    using namespace lynx::exportopts;
    Options o;
    o.sampleRate = 48000.0;
    o.format = Format::aiff;
    o.aiffBits = 24;
    CHECK (estimatedBytes (o, 10.0) == (std::int64_t) (10.0 * 48000.0 * 2.0 * 3.0) + 54);
    o.channels = Channels::monoMerged;
    o.aiffBits = 16;
    CHECK (estimatedBytes (o, 10.0) == (std::int64_t) (10.0 * 48000.0 * 2.0) + 54);
    Options m;
    m.format = Format::aac;
    m.aacKbps = 128;
    CHECK (estimatedBytes (m, 60.0) == 960000);
    auto both = m;
    both.moreFormats = 1u << (int) Format::aiff;
    o.channels = Channels::stereo;
    o.aiffBits = 24;
    o.sampleRate = 44100.0;
    CHECK (estimatedBytes (both, 60.0) == 960000 + (std::int64_t) (60.0 * 44100.0 * 2.0 * 3.0) + 54);

    // FL's LastExportedFormats mask: M4A is bit 9 (FL's M4ABox); AIFF has no box there
    CHECK (flFormatBit (Format::aac) == 0x200u && flFormatBit (Format::aiff) == 0u && flFormatBit (Format::flac) == 0x40u);
    Options w;
    CHECK (withFlLastExported (w, 0x200).format == Format::aac && withFlLastExported (w, 0x200).moreFormats == 0);
    const auto wm = withFlLastExported (w, 0x8 | 0x200);
    CHECK (wm.format == Format::wav && wm.moreFormats == 1u << (int) Format::aac && flLastExportedMask (wm) == 0x208u);
    auto aiffToo = w;
    aiffToo.moreFormats = 1u << (int) Format::aiff;
    CHECK (flLastExportedMask (aiffToo) == 0x8u);
    CHECK (withFlLastExported (w, 0x80).format == Format::wav); // (MIDI only: WAV for the audio window)
}

TEST (export190ka_aac_rates)
{
    using namespace lynx::exportopts;
    CHECK (aacDecimation (44100.0) == 1 && aacSampleRate (44100.0) == 44100.0);
    CHECK (aacDecimation (48000.0) == 1 && aacSampleRate (48000.0) == 48000.0);
    CHECK (aacDecimation (88200.0) == 2 && aacSampleRate (88200.0) == 44100.0);
    CHECK (aacDecimation (96000.0) == 2 && aacSampleRate (96000.0) == 48000.0);
    CHECK (aacDecimation (176400.0) == 4 && aacSampleRate (176400.0) == 44100.0);
    CHECK (aacDecimation (192000.0) == 4 && aacSampleRate (192000.0) == 48000.0);
    CHECK (aacDecimation (22050.0) == 0 && aacSampleRate (32000.0) == 0.0);
}

TEST (export190ka_decimate_keeps_the_band_and_its_timing)
{
    using namespace lynx::exportopts;
    using namespace export190ka;
    // 1 kHz at 96 kHz halved: the same sine at 48 kHz, in time (no delay), to the 16-bit noise floor
    {
        const auto in = testSine (1000.0, 96000.0, 96000);
        const auto out = decimate (in, 96000.0, 2);
        const auto want = testSine (1000.0, 48000.0, 48000);
        CHECK (out.size() == 48000);
        double worst = 0.0;
        for (std::size_t i = 2000; i < 46000; ++i) // (away from the edges, where the filter runs into the zeros)
            worst = std::max (worst, (double) std::abs (out[i] - want[i]));
        CHECK (worst < 2.0e-5);
    }
    // the same for 192 → 48 kHz (two stages) and odd lengths
    {
        const auto in = testSine (440.0, 192000.0, 192001);
        const auto out = decimate (in, 192000.0, 4);
        const auto want = testSine (440.0, 48000.0, 48001);
        CHECK (out.size() == 48001);
        double worst = 0.0;
        for (std::size_t i = 2000; i < 46000; ++i)
            worst = std::max (worst, (double) std::abs (out[i] - want[i]));
        CHECK (worst < 2.0e-5);
    }
    // flat to 19 kHz at 88.2 → 44.1 kHz (within 0.05 dB)
    {
        const auto in = testSine (19000.0, 88200.0, 88200);
        const auto out = decimate (in, 88200.0, 2);
        const auto level = levelRms (out, 4000, 40000) / levelRms (in, 8000, 80000);
        CHECK (std::abs (20.0 * std::log10 (level)) < 0.05);
    }
    // what would fold into the kept band is gone: 30 kHz at 96 kHz (→ an 18 kHz alias), at 192 kHz 80 kHz (→ 16 kHz after
    // the first halving: its stop band) and 60 kHz (→ 36 kHz, then the second halving's), 23 kHz at 88.2 kHz (above
    // the new Nyquist)
    {
        const auto a = decimate (testSine (30000.0, 96000.0, 96000), 96000.0, 2);
        const auto b = decimate (testSine (80000.0, 192000.0, 192000), 192000.0, 4);
        const auto b2 = decimate (testSine (60000.0, 192000.0, 192000), 192000.0, 4);
        const auto c = decimate (testSine (23000.0, 88200.0, 88200), 88200.0, 2);
        CHECK (20.0 * std::log10 (levelRms (a, 4000, 44000) / 0.3536) < -85.0);
        CHECK (20.0 * std::log10 (levelRms (b, 4000, 44000) / 0.3536) < -85.0);
        CHECK (20.0 * std::log10 (levelRms (b2, 4000, 44000) / 0.3536) < -85.0);
        CHECK (20.0 * std::log10 (levelRms (c, 4000, 40000) / 0.3536) < -85.0);
    }
    // factor 1 (and nonsense) leaves the samples as they are
    const std::vector<float> x { 0.1f, -0.2f, 0.3f };
    CHECK (decimate (x, 48000.0, 1) == x && decimate (x, 48000.0, 3) == x);
    CHECK (decimate ({}, 96000.0, 2).empty());
}
