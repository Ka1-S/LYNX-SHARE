// Round 190ig (design190ia plan, slice 3, FM-22): an automation clip has a colour, as FL draws it in its Automation Clip
// channel's colour. The .flp's channel colour event (0x80) is read on import and written back on export; a clip with no
// colour (older projects, files without the event) stays 0 and is drawn in FL's default channel colour.
#include "core/FlpExport.h"
#include "flp/FlpWrite.h"

TEST (design190ig_automation_colour_flp_round_trip)
{
    using namespace lynx;
    for (const std::uint32_t colour : { 0xce2925u, 0u }) // (Thief's TEMPO channel; none)
    {
        flp::Project p;
        flp::Channel c;
        c.index = 0;
        c.type = flp::ChannelType::automationClip;
        c.name = "TEMPO";
        c.colour = colour;
        c.hasLevels = true;
        c.pan = 0;
        c.volume = 12800;
        c.automation.push_back ({ 0.0, 0.2, 0.0f, 0 });
        c.automation.push_back ({ 4.0, 0.8, 0.0f, 0 });
        p.channels.push_back (c);
        const auto read = flp::parseProject (flp::writeProject (p));
        auto song = importFlp (read, fx171e::importOptions());
        CHECK (song.automations.size() == 1);
        if (song.automations.size() != 1)
            continue;
        CHECK (song.automations[0].colour == (colour != 0 ? 0xFF000000u | colour : 0u));

        flpexport::Options options;
        flpexport::Report report;
        const auto again = flp::parseProject (flpexport::exportFlp (song, options, report));
        const flp::Channel* out = nullptr;
        for (const auto& ch : again.channels)
            if (ch.type == flp::ChannelType::automationClip)
                out = &ch;
        CHECK (out != nullptr && out->colour == colour);
        const auto song2 = importFlp (again, fx171e::importOptions());
        CHECK (song2.automations.size() == 1 && song2.automations[0].colour == song.automations[0].colour);
    }
}

TEST (design190ig_automation_colour_default_is_none)
{
    lynx::Automation a;
    CHECK (a.colour == 0u); // (an older project's clip, and a new one before a colour is chosen)
}
