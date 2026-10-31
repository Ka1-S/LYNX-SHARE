// Round 167a (left167): leftovers from 0.1.21 (docs/analise/REA_rodada167a.md).

#include "core/EffectPresets.h"
#include "core/FruityBasicFx.h"

namespace left167
{
lynx::Song sendSong (int target)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (200);
    auto slot = makeEffectSlot (EffectKind::fruitySend);
    flbasic::sendToParams ({ 0, 256, 256, target }, slot.params.data());
    s.mixer[1].effects.push_back (slot);
    return s;
}
} // namespace left167

TEST (left167_fruity_send_files_keep_the_legacy_span_when_every_target_fits)
{
    using namespace lynx;
    // the slot alone: 0..127 when its target fits (older LYNX versions read the same insert), else today's span
    CHECK (flbasic::saveSendSpanOf (left167::sendSong (12), 1, 0) == flbasic::legacySendSpan);
    CHECK (flbasic::saveSendSpanOf (left167::sendSong (127), 1, 0) == flbasic::legacySendSpan);
    CHECK (flbasic::saveSendSpanOf (left167::sendSong (128), 1, 0) == maxMixerTracks - 1);
    // an automation clip or an event lane of "Send to" that reaches past 127 needs today's span too
    auto s = left167::sendSong (12);
    const AutomationTarget sendTo { AutomationTargetKind::effectParam, 1, 0, 3 };
    Automation a;
    a.target = sendTo;
    a.points.push_back ({ 0, flbasic::sendTrackValue (5), 0.0f });
    a.points.push_back ({ 96, flbasic::sendTrackValue (100), 0.0f });
    s.automations.push_back (a);
    CHECK (flbasic::saveSendSpanOf (s, 1, 0) == flbasic::legacySendSpan);
    s.patterns.emplace_back();
    PatternEvents lane;
    lane.target = sendTo;
    lane.events.push_back ({ 0, flbasic::sendTrackValue (190) });
    s.patterns.back().events.push_back (lane);
    CHECK (flbasic::saveSendSpanOf (s, 1, 0) == maxMixerTracks - 1);
    CHECK (flbasic::sendToTargetOf (s, sendTo) == &s.mixer[1].effects[0]);
    CHECK (flbasic::sendToTargetOf (s, { AutomationTargetKind::effectParam, 1, 0, 2 }) == nullptr); // its Dry knob
    // every insert survives the trip through the file's 4 decimals in either span
    for (int track = 0; track <= 127; ++track)
    {
        const auto v = flbasic::sendTrackValue (track);
        const auto saved = (float) (std::lround (flbasic::sendTrackValueInSpan (v, 127) * 10000.0) / 10000.0);
        CHECK (flbasic::sendTrack (flbasic::rescaleSendTrackValue (saved, 127)) == track);
        CHECK (std::lround (saved * 127.0f) == track); // what LYNX 0.1.20 and older read
    }
}

TEST (left167_fruity_send_presets_carry_their_span)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::fruitySend);
    flbasic::sendToParams ({ 0, 256, 256, 9 }, slot.params.data());
    const auto text = presets::toText (EffectKind::fruitySend, "Bus 9", slot.params);
    CHECK (text.find ("Send to span = 127") != std::string::npos);
    const auto back = presets::fromText (EffectKind::fruitySend, text);
    CHECK (back && flbasic::sendFromParams (back->params.data()).track == 9);
    // a preset saved before round 166d (no span line): "Send to" over 0..127
    const auto old = presets::fromText (EffectKind::fruitySend, "LYNX preset 1\nengine = Fruity Send\nname = Old\nSend to = "
                                                                       + std::to_string (40.0 / 127.0) + "\n");
    CHECK (old && flbasic::sendFromParams (old->params.data()).track == 40);
    // past insert 127: today's span
    flbasic::sendToParams ({ 0, 256, 256, 333 }, slot.params.data());
    const auto far = presets::fromText (EffectKind::fruitySend, presets::toText (EffectKind::fruitySend, "Far", slot.params));
    CHECK (far && flbasic::sendFromParams (far->params.data()).track == 333);
}

// FL's saver writes the Playlist's "Automatic crossfades" (this project) as byte event 0x2C [DECOMP, REA_rodada167a §3];
// files without it (before FL 21) open with it off. The demo projects carry 0 or 1.
TEST (left167_flp_import_reads_the_automatic_crossfades_flag)
{
    for (const int flag : { -1, 0, 1 })
    {
        FlpBuilder b;
        b.u8 (0x9C); // tempo
        b.u32 (120000);
        if (flag >= 0)
        {
            b.u8 (0x2C);
            b.u8 ((std::uint8_t) flag);
        }
        const auto project = flp::parseProject (b.bytes);
        CHECK (project.autoCrossfades == (flag == 1));
        const auto song = lynx::importFlp (project, {});
        CHECK (song.autoCrossfades == (flag == 1));
    }
    if (! realFlpDir.empty()) // FL's own: "Basic with limiter" (FL 2026) on, "Mehdi Ebrahiminejad - Never Out" off
        for (const auto& [name, on] : { std::pair<const char*, bool> { "Templates/Minimal/Basic with limiter/Basic with limiter.flp", true },
                                        { "Demo projects/Demo songs/Mehdi Ebrahiminejad - Never Out.flp", false } })
        {
            const auto path = realFlpDir + "/" + name;
            std::ifstream in (path, std::ios::binary);
            if (! in)
            {
                std::printf ("  (missing %s)\n", path.c_str());
                continue;
            }
            std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
            CHECK (flp::parseProject (bytes).autoCrossfades == on);
        }
}
