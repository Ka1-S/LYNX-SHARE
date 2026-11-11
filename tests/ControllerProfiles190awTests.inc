// Round 190aw, phase B: the built-in controller profiles (resources/controllers/*.lynxctl). No hardware: each test
// feeds the bytes the maker's document says the device sends and checks LYNX's side and the bytes sent back.
// (FakeSurface, Wire and feed are in Controller190awTests.inc.)

#include "core/ControllerProfilesData.h"

TEST (controller190aw_builtin_profiles_parse_and_match_their_files)
{
    using namespace c190aw;
    const auto lib = Library::builtIn();
    for (const auto& e : lib.errors)
        std::printf ("  %s\n", e.c_str());
    CHECK (lib.errors.empty());

    // src/core/ControllerProfilesData.h must be regenerated (tools/make_controllers.py) whenever a profile file changes
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator ("resources/controllers"))
        if (entry.path().extension() == ".lynxctl")
        {
            auto text = readFile (entry.path());
            std::string plain;
            for (const auto c : text)
                if (c != '\r')
                    plain.push_back (c);
            files[entry.path().stem().string()] = plain;
        }
    auto embedded = 0;
    for (const auto* f = builtInProfiles; f->id != nullptr; ++f, ++embedded)
    {
        CHECK (files.count (f->id) == 1);
        CHECK (files[f->id] == f->text);
    }
    CHECK (embedded == (int) files.size() && (int) lib.profiles.size() == embedded && embedded == 6);

    // every profile names its maker and where its map comes from, and no profile is a user profile
    for (const auto& p : lib.profiles)
    {
        CHECK (! p->name.empty() && ! p->maker.empty() && ! p->sources.empty() && ! p->user);
        CHECK (! p->bindings.empty());
    }

    // detection by port name
    auto id = [&] (const char* port) {
        const auto p = lib.detect (port);
        return p != nullptr ? p->id : std::string ("-");
    };
    CHECK (id ("Mackie Control") == "mackie-control" && id ("MCU Pro USB v3.1") == "mackie-control" && id ("Mackie Control Universal Port 1") == "mackie-control");
    CHECK (id ("Mackie Control XT") == "mackie-control-xt" && id ("MCU XT Pro") == "mackie-control-xt" && id ("Mackie Control Extender") == "mackie-control-xt");
    CHECK (id ("MIDIIN2 (Launchkey MK3 49)") == "novation-launchkey-mk3" && id ("Launchkey MK3 61 LKMK3 DAW Out") == "novation-launchkey-mk3");
    CHECK (id ("MIDIIN2 (APC Key 25 mk2)") == "akai-apc-key-25-mk2" && id ("APC Key 25 mk2 Control") == "akai-apc-key-25-mk2");
    CHECK (id ("KL Essential 49 mk3 MCU/HUI") == "arturia-keylab-essential-mk3" && id ("KeyLab Essential 61 mk3 MCU/HUI") == "arturia-keylab-essential-mk3");
    // the ports that carry the keys stay generic (on them the keys' notes must play), and so does anything unknown
    CHECK (id ("Launchkey MK3 49") == "-" && id ("Launchkey MK3 49 LKMK3 MIDI Out") == "-" && id ("APC Key 25 mk2") == "-"
           && id ("KL Essential 49 mk3 MIDI") == "-" && id ("Arturia KeyLab 49") == "-" && id ("Akai MPD218") == "-" && id ("MPK mini 3") == "-"
           && id ("USB MIDI Interface") == "-" && id ("") == "-");
    // the nanoKONTROL2 is never detected (its port name is the same in its CC mode); it is chosen by hand
    CHECK (id ("nanoKONTROL2") == "-" && id ("nanoKONTROL2 1 SLIDER/KNOB") == "-");
    CHECK (lib.resolve ("korg-nanokontrol2", "nanoKONTROL2") == lib.find ("korg-nanokontrol2") && lib.find ("korg-nanokontrol2") != nullptr);

    // the Mackie Control relatives carry the whole map, in groups of their own
    const auto mcu = lib.find ("mackie-control");
    CHECK (mcu != nullptr && mcu->group == "mackie" && mcu->layers.size() == 3 && mcu->display.cells == 8 && mcu->display.width == 7);
    for (const char* relative : { "mackie-control-xt", "arturia-keylab-essential-mk3", "korg-nanokontrol2" })
    {
        const auto p = lib.find (relative);
        CHECK (p != nullptr && p->bindings.size() == mcu->bindings.size() && p->feedback.size() == mcu->feedback.size()
               && p->display.rows.size() == mcu->display.rows.size() && p->name != mcu->name);
    }
    CHECK (lib.find ("mackie-control-xt")->group == "mackie" && lib.find ("arturia-keylab-essential-mk3")->group == "keylab-essential"
           && lib.find ("korg-nanokontrol2")->group == "nanokontrol2" && lib.find ("korg-nanokontrol2")->match.empty());
}

TEST (controller190aw_mackie_control_profile_with_extender)
{
    using namespace c190aw;
    const auto lib = Library::builtIn();
    FakeSurface lynx (40, 12, 20);
    for (std::size_t i = 1; i < lynx.tracks.size(); ++i)
        lynx.tracks[i].name = "Trk " + std::to_string (i);
    lynx.tracks[3].solo = true;
    Hub hub;
    std::map<std::string, Wire> wires;
    Hub::DeviceSend send = [&] (const std::string& input, const std::uint8_t* bytes, std::size_t size) { wires[input].send() (bytes, size); };
    auto in = [&] (const std::string& device, std::initializer_list<int> bytes) {
        std::vector<std::uint8_t> b;
        for (const auto v : bytes)
            b.push_back ((std::uint8_t) v);
        return hub.input (device, b.data(), b.size(), lynx);
    };
    auto clear = [&] {
        for (auto& [name, w] : wires)
            w.clear();
    };
    auto lcd = [&] (const std::string& device, const char* header, int pos) {
        char head[48];
        std::snprintf (head, sizeof (head), "%s %02X ", header, pos);
        std::string found = "<none>";
        for (const auto& m : wires[device].messages)
            if (m.rfind (head, 0) == 0)
                found = lcdText (m, 7); // (the last one written)
        return found;
    };

    hub.configure ({ { "Mackie Control", lib.detect ("Mackie Control") }, { "Mackie Control XT", lib.detect ("Mackie Control XT") } }, lynx, send);
    auto& main = wires["Mackie Control"];
    auto& xt = wires["Mackie Control XT"];
    CHECK (hub.session ("Mackie Control XT") != nullptr && hub.session ("Mackie Control XT")->unitOffset() == 8 && hub.shared ("mackie")->strips == 16);

    // what the units show after opening: names on the upper row, pans on the lower; the extender with its own model byte
    CHECK (lcd ("Mackie Control", "F0 00 00 66 14 12", 0) == "Trk 1  " && lcd ("Mackie Control", "F0 00 00 66 14 12", 49) == "Trk 8  "
           && lcd ("Mackie Control", "F0 00 00 66 14 12", 56) == "C      ");
    CHECK (lcd ("Mackie Control XT", "F0 00 00 66 15 12", 0) == "Trk 9  " && lcd ("Mackie Control XT", "F0 00 00 66 15 12", 49) == "Trk 16 "
           && xt.count ("F0 00 00 66 14 12") == 0 && main.count ("F0 00 00 66 15 12") == 0);
    CHECK (main.has ("90 0A 7F") && main.has ("90 2A 7F") && main.has ("90 5D 7F") && main.has ("B0 30 16") && main.has ("E0 20 66") && main.has ("E8 20 66"));

    // strip 3 of the main unit: fader with touch, V-Pot, mute, solo, arm, select
    clear();
    CHECK (in ("Mackie Control", { 0x90, 106, 127 }) && in ("Mackie Control", { 0xE2, 0x70, 0x7F }) && in ("Mackie Control", { 0x90, 106, 0 }));
    CHECK (std::abs (lynx.tracks[3].volume - 16368.0f / 16383.0f) < 1e-4f);
    CHECK (in ("Mackie Control", { 0xB0, 18, 0x44 }) && std::abs (lynx.tracks[3].pan - 0.46f) < 1e-5f);
    CHECK (in ("Mackie Control", { 0x90, 18, 127 }) && lynx.tracks[3].mute && in ("Mackie Control", { 0x90, 10, 127 }) && ! lynx.tracks[3].solo
           && in ("Mackie Control", { 0x90, 2, 127 }) && lynx.tracks[3].arm && in ("Mackie Control", { 0x90, 26, 127 }) && lynx.selectedTrack == 3);
    hub.refresh (lynx, send);
    CHECK (main.has ("E2 60 7F") && main.has ("90 12 7F") && main.has ("90 0A 00") && main.has ("90 02 7F") && main.has ("90 1A 7F") && main.has ("90 18 00"));
    CHECK (main.count ("B0 32") == 0); // 0.46 is still nearest the ring's centre LED (sent as B0 32 16 at opening): nothing new
    CHECK (lcd ("Mackie Control", "F0 00 00 66 14 12", 56 + 14) == "L8     ");
    // the extender's strip 2 is insert 10; the Master fader
    CHECK (in ("Mackie Control XT", { 0x90, 17, 127 }) && lynx.tracks[10].mute && in ("Mackie Control", { 0xE8, 0x00, 0x00 }) && lynx.tracks[0].volume == 0.0f);

    // bank right moves both units by 16; channel right by one
    clear();
    CHECK (in ("Mackie Control", { 0x90, 47, 127 }) && hub.shared ("mackie")->mixerBank == 16);
    hub.refresh (lynx, send);
    CHECK (lcd ("Mackie Control", "F0 00 00 66 14 12", 0) == "Trk 17 " && lcd ("Mackie Control XT", "F0 00 00 66 15 12", 0) == "Trk 25 ");
    CHECK (in ("Mackie Control XT", { 0x90, 49, 127 }) && hub.shared ("mackie")->mixerBank == 17 && in ("Mackie Control", { 0x90, 48, 127 })
           && in ("Mackie Control", { 0x90, 46, 127 }) && hub.shared ("mackie")->mixerBank == 0);

    // ASSIGNMENT Plug-in: the V-Pots are the focused plugin's parameters, the cursor keys turn the pages
    clear();
    lynx.log.clear();
    CHECK (in ("Mackie Control", { 0x90, 43, 127 }) && hub.shared ("mackie")->layer == 1);
    CHECK (in ("Mackie Control", { 0xB0, 16, 0x02 }) && lynx.did ("param 0 0.270") && in ("Mackie Control XT", { 0xB0, 16, 0x02 }) && lynx.did ("param 8 0.270"));
    hub.refresh (lynx, send);
    CHECK (main.has ("90 2B 7F") && main.has ("90 2A 00") && lcd ("Mackie Control", "F0 00 00 66 14 12", 0) == "Par 1  "
           && lcd ("Mackie Control", "F0 00 00 66 14 12", 56) == "27%    " && lcd ("Mackie Control XT", "F0 00 00 66 15 12", 0) == "Par 9  ");
    CHECK (in ("Mackie Control", { 0x90, 99, 127 }) && hub.shared ("mackie")->page == 1 && in ("Mackie Control", { 0x90, 98, 127 }) && hub.shared ("mackie")->page == 0);
    // ASSIGNMENT Track: the Channel Rack's volumes; a V-Pot push selects the channel; cursor up / down too
    clear();
    lynx.log.clear();
    CHECK (in ("Mackie Control", { 0x90, 40, 127 }) && hub.shared ("mackie")->layer == 2);
    CHECK (in ("Mackie Control", { 0xB0, 17, 0x45 }) && lynx.did ("channel volume 1 0.750") && in ("Mackie Control", { 0x90, 34, 127 }) && lynx.selected == 2);
    CHECK (in ("Mackie Control", { 0x90, 97, 127 }) && lynx.selected == 3 && in ("Mackie Control", { 0x90, 96, 127 }) && lynx.selected == 2);
    hub.refresh (lynx, send);
    CHECK (lcd ("Mackie Control", "F0 00 00 66 14 12", 0) == "Chan 1 " && lcd ("Mackie Control", "F0 00 00 66 14 12", 56 + 7) == "75%    ");
    CHECK (in ("Mackie Control", { 0x90, 42, 127 }) && hub.shared ("mackie")->layer == 0);
    // in the Pan layer a V-Pot push and the cursor's left / right do nothing
    lynx.log.clear();
    CHECK (in ("Mackie Control", { 0x90, 34, 127 }) && in ("Mackie Control", { 0x90, 98, 127 }) && lynx.log.empty() && hub.shared ("mackie")->page == 0);

    // transport and jog
    lynx.log.clear();
    CHECK (in ("Mackie Control", { 0x90, 94, 127 }) && lynx.playing && in ("Mackie Control", { 0x90, 93, 127 }) && ! lynx.playing
           && in ("Mackie Control", { 0x90, 95, 127 }) && lynx.recording && in ("Mackie Control", { 0x90, 86, 127 }) && ! lynx.patternMode
           && in ("Mackie Control", { 0x90, 89, 127 }) && lynx.click);
    CHECK (in ("Mackie Control", { 0x90, 91, 127 }) && lynx.did ("command " + std::to_string ((int) Command::rewind)) && in ("Mackie Control", { 0x90, 92, 127 })
           && lynx.did ("command " + std::to_string ((int) Command::forward)) && in ("Mackie Control", { 0x90, 81, 127 })
           && lynx.did ("command " + std::to_string ((int) Command::undo)));
    CHECK (in ("Mackie Control", { 0xB0, 60, 0x01 }) && lynx.did ("jog 1") && in ("Mackie Control", { 0xB0, 60, 0x41 }) && lynx.did ("jog -1"));

    // the surface's other switches are taken and do nothing; other channels and controllers are not the surface's
    lynx.log.clear();
    const Claims claims (*lib.find ("mackie-control"));
    const std::uint8_t f1[] { 0x90, 54, 127 }, shift[] { 0x90, 70, 127 }, zoom[] { 0x90, 100, 127 }, key[] { 0x91, 60, 100 }, wheel[] { 0xB0, 1, 64 },
        pedal[] { 0xB0, 64, 127 }, bend10[] { 0xE9, 0, 64 };
    CHECK (claims.has (f1, 3) && claims.has (shift, 3) && claims.has (zoom, 3) && ! claims.has (key, 3) && ! claims.has (wheel, 3) && ! claims.has (pedal, 3)
           && ! claims.has (bend10, 3));
    CHECK (in ("Mackie Control", { 0x90, 54, 127 }) && in ("Mackie Control", { 0x90, 70, 127 }) && lynx.log.empty());
    CHECK (! in ("Mackie Control", { 0x91, 60, 100 }) && ! in ("Mackie Control", { 0xB0, 1, 64 }));

    hub.closeAll (send);
}

TEST (controller190aw_launchkey_mk3_daw_mode_profile)
{
    using namespace c190aw;
    const auto lib = Library::builtIn();
    FakeSurface lynx;
    lynx.rack[0].name = "Kick";
    Shared shared;
    Session s (lib.find ("novation-launchkey-mk3"), 0);
    Wire wire;
    s.open (lynx, shared, wire.send());

    // DAW mode on first, then the display (16 characters a row) and the selected fader button's light
    CHECK (wire.messages.size() >= 3 && wire.messages[0] == "9F 0C 7F");
    CHECK (wire.has ("F0 00 20 29 02 0F 04 00 4B 69 63 6B 20 20 20 20 20 20 20 20 20 20 20 20 F7")); // "Kick"
    CHECK (wire.has ("F0 00 20 29 02 0F 04 01 50 61 6E 20 20 20 20 20 20 20 20 20 20 20 20 20 F7")); // "Pan"
    CHECK (wire.has ("BF 25 7F") && wire.has ("BF 26 00") && wire.has ("90 60 00"));

    // pots (channel 16) are pans at first, with soft takeover; the keyboard's pot mode report changes what they turn
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xBF, 21, 10 }) && lynx.log.empty());
    CHECK (feed (s, lynx, shared, { 0xBF, 21, 64 }) && lynx.did ("pan 1 0.504"));
    CHECK (feed (s, lynx, shared, { 0xBF, 9, 1 }) && shared.layer == 1);
    CHECK (feed (s, lynx, shared, { 0xBF, 22, 101 }) && lynx.did ("volume 2 0.795"));
    CHECK (feed (s, lynx, shared, { 0xBF, 9, 2 }) && shared.layer == 2);
    CHECK (feed (s, lynx, shared, { 0xBF, 28, 32 }) && lynx.did ("param 7 0.252"));
    CHECK (feed (s, lynx, shared, { 0xBF, 9, 4 }) && shared.layer == 2); // (Send A: no layer of LYNX's, the last one stays)
    CHECK (feed (s, lynx, shared, { 0xBF, 9, 3 }) && shared.layer == 0);

    // faders, master fader, fader buttons, track and scene buttons
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xBF, 53 + 4, 102 }) && lynx.did ("volume 5 0.803") && feed (s, lynx, shared, { 0xBF, 61, 101 }) && lynx.did ("volume 0 0.795"));
    CHECK (feed (s, lynx, shared, { 0xBF, 37 + 2, 127 }) && lynx.selectedTrack == 3 && feed (s, lynx, shared, { 0xBF, 37 + 2, 0 }));
    CHECK (feed (s, lynx, shared, { 0xBF, 103, 127 }) && shared.mixerBank == 8 && feed (s, lynx, shared, { 0xBF, 102, 127 }) && shared.mixerBank == 0);
    CHECK (feed (s, lynx, shared, { 0xBF, 107, 127 }) && lynx.selected == 1 && feed (s, lynx, shared, { 0xBF, 106, 127 }) && lynx.selected == 0);

    // transport (channel 16), the two pad-column buttons (channel 1)
    CHECK (feed (s, lynx, shared, { 0xBF, 115, 127 }) && lynx.playing && feed (s, lynx, shared, { 0xBF, 115, 0 }) && lynx.playing
           && feed (s, lynx, shared, { 0xBF, 116, 127 }) && ! lynx.playing && feed (s, lynx, shared, { 0xBF, 117, 127 }) && lynx.recording
           && feed (s, lynx, shared, { 0xBF, 118, 127 }) && ! lynx.patternMode && feed (s, lynx, shared, { 0xBF, 76, 127 }) && lynx.click);
    CHECK (feed (s, lynx, shared, { 0xBF, 77, 127 }) && lynx.did ("command " + std::to_string ((int) Command::undo)));
    CHECK (feed (s, lynx, shared, { 0xB0, 104, 127 }) && lynx.did ("command " + std::to_string ((int) Command::nextPattern))
           && feed (s, lynx, shared, { 0xB0, 105, 127 }) && lynx.did ("command " + std::to_string ((int) Command::previousPattern)));

    // Session pads: the 16 steps, lit red
    wire.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 96, 100 }) && lynx.stepsOn[0] && feed (s, lynx, shared, { 0x90, 96, 0 }) && lynx.stepsOn[0]);
    CHECK (feed (s, lynx, shared, { 0x90, 119, 100 }) && lynx.stepsOn[15] && feed (s, lynx, shared, { 0x90, 112, 1 }) && lynx.stepsOn[8]);
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 60 05") && wire.has ("90 77 05") && wire.has ("90 70 05") && wire.has ("BF 27 7F") && wire.has ("BF 25 00"));
    CHECK (feed (s, lynx, shared, { 0x90, 96, 100 }) && ! lynx.stepsOn[0]);
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 60 00"));

    // the DAW port's other buttons are taken and do nothing; a key or wheel (first port) is not the profile's
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xBF, 74, 127 }) && feed (s, lynx, shared, { 0xBF, 52, 127 }) && feed (s, lynx, shared, { 0xB0, 108, 127 })
           && feed (s, lynx, shared, { 0xBF, 3, 2 }) && lynx.log.empty());
    CHECK (! feed (s, lynx, shared, { 0x90, 60, 100 }) && ! feed (s, lynx, shared, { 0xB0, 1, 64 }) && ! feed (s, lynx, shared, { 0xE0, 0, 64 })
           && ! feed (s, lynx, shared, { 0x99, 36, 100 }));

    // closing: lights off, the display blank, DAW mode off last
    wire.clear();
    s.close (wire.send());
    CHECK (wire.has ("90 77 00") && wire.has ("BF 27 00") && wire.count ("F0 00 20 29 02 0F 04") == 2 && wire.messages.back() == "9F 0C 00");
}

TEST (controller190aw_apc_key_25_mk2_profile)
{
    using namespace c190aw;
    const auto lib = Library::builtIn();
    FakeSurface lynx;
    Shared shared;
    Session s (lib.find ("akai-apc-key-25-mk2"), 0);
    Wire wire;
    s.open (lynx, shared, wire.send());

    // the Introduction message first; the Volume layer's light and the selected track's
    CHECK (! wire.messages.empty() && wire.messages[0] == "F0 47 7F 4E 60 00 04 00 01 00 00 F7");
    CHECK (wire.has ("90 52 01") && wire.has ("90 53 00") && wire.has ("90 40 01") && wire.has ("90 41 00") && wire.has ("96 20 00"));

    // knobs are relative (1… up, 127… down): volume, then pan, then the focused plugin's parameters
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 48, 0x02 }) && lynx.did ("volume 1 0.820") && feed (s, lynx, shared, { 0xB0, 48, 0x7D }) && lynx.did ("volume 1 0.790"));
    CHECK (feed (s, lynx, shared, { 0x90, 83, 127 }) && shared.layer == 1 && feed (s, lynx, shared, { 0xB0, 55, 0x01 }) && lynx.did ("pan 8 0.510"));
    CHECK (feed (s, lynx, shared, { 0x90, 84, 127 }) && shared.layer == 2 && feed (s, lynx, shared, { 0xB0, 49, 0x7F }) && lynx.did ("param 1 0.240"));
    wire.clear();
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 54 01") && wire.has ("90 52 00") && wire.messages.size() == 2);

    // pads: the two top rows are steps (lit red at full brightness), the lower rows play notes from key 36
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 32, 127 }) && lynx.stepsOn[0] && feed (s, lynx, shared, { 0x80, 32, 0 }) && feed (s, lynx, shared, { 0x90, 31, 127 }) && lynx.stepsOn[15]);
    CHECK (feed (s, lynx, shared, { 0x90, 0, 127 }) && lynx.did ("note on 36 1.000") && feed (s, lynx, shared, { 0x80, 0, 0 }) && lynx.did ("note off 36 0.000")
           && feed (s, lynx, shared, { 0x90, 23, 64 }) && lynx.did ("note on 59 0.504"));
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("96 20 05") && wire.has ("96 1F 05") && wire.messages.size() == 2);

    // track buttons select, Scene Launch 4 / 5 move the bank, the transport buttons, Shift does nothing
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 64 + 5, 127 }) && lynx.selectedTrack == 6 && feed (s, lynx, shared, { 0x90, 86, 127 }) && shared.mixerBank == 8
           && feed (s, lynx, shared, { 0x90, 64, 127 }) && lynx.selectedTrack == 9 && feed (s, lynx, shared, { 0x90, 85, 127 }) && shared.mixerBank == 0);
    CHECK (feed (s, lynx, shared, { 0x90, 91, 127 }) && lynx.playing && feed (s, lynx, shared, { 0x90, 81, 127 }) && ! lynx.playing
           && feed (s, lynx, shared, { 0x90, 93, 127 }) && lynx.recording);
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 98, 127 }) && lynx.log.empty());
    // (a pad number outside the matrix and the sustain pedal are not the profile's)
    CHECK (! feed (s, lynx, shared, { 0x90, 45, 127 }) && ! feed (s, lynx, shared, { 0xB0, 64, 127 }));
}
