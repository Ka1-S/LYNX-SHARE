// Round 190mx (issue #184, the reporter's GarageBand for iOS project): the signature sequence of GarageBand for iOS 2.3+
// starts with the 32-byte settings event its regions have (0x32, 0x88 @+0x17) and holds the signatures as 48-byte events
// (0x30); the reader took the settings event for the header and got 7/1 (his file came out right only because
// Output/metadata.plist overrides it). Synthetic bytes only (round 190ic's helpers).

TEST (band190mx_ios23_signature_after_the_settings_event)
{
    using namespace lynx;
    using band190ic::Bytes;
    auto bytes = band190ic::song();
    // a second signature sequence in the iOS 2.3+ layout (read after the song's own, so it wins): 6/8, then 7/8 at bar 3
    Bytes settings (32, 0);
    settings[0] = 0x32;
    settings[0x0C] = 7;
    settings[0x17] = 0x88;
    Bytes first (48, 0), second (48, 0);
    first[0] = second[0] = 0x30;
    band190ic::put32 (first, 4, 38400);
    first[0x0B] = 3;
    first[0x0C] = 6;
    band190ic::put32 (second, 4, 38400 + 2 * 3 * 480);
    second[0x0B] = 3;
    second[0x0C] = 7;
    band190ic::Song extra;
    extra.add ("qSvE", 1, 0, band190ic::cat ({ settings, first, second, band190ic::tail() }));
    bytes.insert (bytes.end(), extra.records[0].begin(), extra.records[0].end());
    band190ic::put32 (bytes, 0x10, (std::uint32_t) (bytes.size() - 24));
    std::string error;
    const auto p = band::parseSong (bytes, &error);
    CHECK (p.has_value());
    if (! p)
        return;
    CHECK (p->numerator == 6 && p->denominator == 8);
    CHECK (p->meters.size() == 1 && p->meters[0].first == 2 * 3 * 480 && p->meters[0].second.first == 7 && p->meters[0].second.second == 8);
}
