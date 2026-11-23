// Round 190ig (round 190ia CL-6): Live 12.4's own clip / track colours for .als imports in the Clip-launch layout.
#include "core/LayoutStyle.h"

TEST (als190ig_live_colour_table)
{
    using namespace lynx;
    // the colours round 190ia measured on screen in Live's "Patience" demo set, by their indices in the set
    const std::pair<int, std::uint32_t> measured[] = { { 2, 0xffcc9927 }, { 3, 0xfff7f47c }, { 25, 0xffb677c6 }, { 32, 0xffd2e498 },
                                                       { 33, 0xffbad074 }, { 34, 0xff9bc48d }, { 35, 0xffd4fde1 }, { 36, 0xffcdf1f8 },
                                                       { 41, 0xffa9a9a9 }, { 42, 0xffc6928b }, { 50, 0xff85a5c2 }, { 52, 0xffa595b5 },
                                                       { 55, 0xff7b7b7b } };
    for (const auto& [index, colour] : measured)
        CHECK (alsLiveColour (index) == colour);
    CHECK (alsLiveColour (-1) == 0 && alsLiveColour (70) == 0);
    // every row of the 14 x 5 chooser ends in its grey
    CHECK (alsLiveColour (13) == 0xffffffff && alsLiveColour (27) == 0xffd0d0d0 && alsLiveColour (69) == 0xff3c3c3c);
    for (int i = 0; i < 70; ++i)
    {
        CHECK (alsLiveColour (i) != 0);
        CHECK (alsColourIndex (alsLiveColour (i)) == i); // (the export gives Live's index back)
        CHECK (alsColourIndex (alsColour (i)) == i);     // (and still the approximation's)
    }
}

TEST (als190ig_import_colours_follow_the_layout)
{
    using namespace lynx;
    const auto before = layoutstyle::current();
    const auto colourOf = [] (const Song& s, const std::string& name) {
        for (const auto& m : s.mixer)
            if (m.name == name)
                return m.colour;
        return 0u;
    };
    const auto bytes = als175a::bytesOf (als175a::richSet());
    layoutstyle::setCurrent (layoutstyle::Style::pattern);
    const auto pattern = importAls (bytes, als175a::options (nullptr));
    layoutstyle::setCurrent (layoutstyle::Style::clipLaunch);
    const auto live = importAls (bytes, als175a::options (nullptr));
    layoutstyle::setCurrent (before);
    CHECK (pattern.has_value() && live.has_value());
    if (! pattern || ! live)
        return;
    CHECK (colourOf (*pattern, "Keys") == alsLiveColour (3)); // (round 190oo CS O-1: Live's colours in every layout)
    CHECK (colourOf (*live, "Keys") == alsLiveColour (3)); // (Clip-launch layout: Live's #F7F47C)
    CHECK (colourOf (*live, "Keys") == 0xfff7f47cu);
}
