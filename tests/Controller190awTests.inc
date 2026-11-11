// Round 190aw: the MIDI controller layer (core/ControllerProfile.h, core/ControllerSession.h). No hardware: the tests
// feed the bytes a device sends and check what reaches the Surface and which bytes go back.

namespace c190aw
{
using namespace lynx::controller;

// LYNX as the layer sees it, with a log of what was asked of it.
struct FakeSurface final : Surface
{
    struct Track
    {
        std::string name;
        float volume = 0.8f, pan = 0.5f;
        bool mute = false, solo = false, arm = false;
    };
    std::vector<Track> tracks;   // 0 = Master
    std::vector<Track> rack;
    std::vector<float> params;
    std::vector<bool> stepsOn = std::vector<bool> (16, false);
    int selectedTrack = 1, selected = 0;
    bool playing = false, recording = false, patternMode = true, click = false;
    std::set<std::pair<int, int>> links; // (cc, channel) linked by hand
    mutable std::vector<std::string> log;

    explicit FakeSurface (int inserts = 20, int channelCount = 4, int paramCount = 20)
    {
        tracks.resize ((std::size_t) inserts + 1);
        tracks[0].name = "Master";
        for (int i = 1; i <= inserts; ++i)
            tracks[(std::size_t) i].name = "Insert " + std::to_string (i);
        rack.resize ((std::size_t) channelCount);
        for (int i = 0; i < channelCount; ++i)
            rack[(std::size_t) i].name = "Chan " + std::to_string (i + 1);
        params.assign ((std::size_t) paramCount, 0.25f);
    }

    static std::string f (float v)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.3f", (double) v);
        return text;
    }
    bool did (const std::string& what) const { return std::find (log.begin(), log.end(), what) != log.end(); }

    void command (Command c) override
    {
        log.push_back ("command " + std::to_string ((int) c));
        if (c == Command::play) playing = ! playing;
        if (c == Command::stop) playing = false;
        if (c == Command::record) recording = ! recording;
        if (c == Command::loop) patternMode = ! patternMode;
        if (c == Command::metronome) click = ! click;
    }
    bool lit (Command c) const override
    {
        switch (c)
        {
            case Command::play: return playing;
            case Command::stop: return ! playing;
            case Command::record: return recording;
            case Command::loop: return patternMode;
            case Command::metronome: return click;
            default: return false;
        }
    }
    void jog (int steps) override { log.push_back ("jog " + std::to_string (steps)); }

    int mixerTracks() const override { return (int) tracks.size(); }
    int selectedMixerTrack() const override { return selectedTrack; }
    void selectMixerTrack (int t) override { selectedTrack = t; log.push_back ("select track " + std::to_string (t)); }
    std::string mixerName (int t) const override { return tracks[(std::size_t) t].name; }
    float mixerValue (int t, Param p) const override { return p == Param::volume ? tracks[(std::size_t) t].volume : tracks[(std::size_t) t].pan; }
    void setMixerValue (int t, Param p, float v) override
    {
        (p == Param::volume ? tracks[(std::size_t) t].volume : tracks[(std::size_t) t].pan) = v;
        log.push_back (std::string (p == Param::volume ? "volume " : "pan ") + std::to_string (t) + " " + f (v));
    }
    bool mixerSwitch (int t, Switch s) const override
    {
        const auto& k = tracks[(std::size_t) t];
        return s == Switch::mute ? k.mute : s == Switch::solo ? k.solo : k.arm;
    }
    void setMixerSwitch (int t, Switch s, bool on) override
    {
        auto& k = tracks[(std::size_t) t];
        (s == Switch::mute ? k.mute : s == Switch::solo ? k.solo : k.arm) = on;
        log.push_back (std::string (s == Switch::mute ? "mute " : s == Switch::solo ? "solo " : "arm ") + std::to_string (t) + (on ? " on" : " off"));
    }

    int channels() const override { return (int) rack.size(); }
    int selectedChannel() const override { return selected; }
    void selectChannel (int c) override { selected = c; log.push_back ("select channel " + std::to_string (c)); }
    std::string channelName (int c) const override { return rack[(std::size_t) c].name; }
    float channelValue (int c, Param p) const override { return p == Param::volume ? rack[(std::size_t) c].volume : rack[(std::size_t) c].pan; }
    void setChannelValue (int c, Param p, float v) override
    {
        (p == Param::volume ? rack[(std::size_t) c].volume : rack[(std::size_t) c].pan) = v;
        log.push_back (std::string (p == Param::volume ? "channel volume " : "channel pan ") + std::to_string (c) + " " + f (v));
    }
    bool channelMuted (int c) const override { return rack[(std::size_t) c].mute; }
    void setChannelMuted (int c, bool m) override { rack[(std::size_t) c].mute = m; log.push_back ("channel mute " + std::to_string (c) + (m ? " on" : " off")); }

    std::string pluginName() const override { return "Synth"; }
    int pluginParams() const override { return (int) params.size(); }
    std::string pluginParamName (int i) const override { return "Par " + std::to_string (i + 1); }
    float pluginParam (int i) const override { return params[(std::size_t) i]; }
    void setPluginParam (int i, float v) override { params[(std::size_t) i] = v; log.push_back ("param " + std::to_string (i) + " " + f (v)); }

    void playNote (int key, float velocity, bool on) override { log.push_back (std::string (on ? "note on " : "note off ") + std::to_string (key) + " " + f (velocity)); }
    int steps() const override { return (int) stepsOn.size(); }
    bool step (int i) const override { return stepsOn[(std::size_t) i]; }
    void setStep (int i, bool on) override { stepsOn[(std::size_t) i] = on; log.push_back ("step " + std::to_string (i) + (on ? " on" : " off")); }

    bool linkedByHand (int cc, int channel) const override { return links.count ({ cc, channel }) != 0 || links.count ({ cc, -1 }) != 0; }
};

// what was sent to a device, as hex ("90 5E 7F")
struct Wire
{
    std::vector<std::string> messages;
    Send send()
    {
        return [this] (const std::uint8_t* bytes, std::size_t size) {
            std::string text;
            for (std::size_t i = 0; i < size; ++i)
            {
                char hex[4];
                std::snprintf (hex, sizeof (hex), "%02X", bytes[i]);
                text += (i != 0 ? " " : "") + std::string (hex);
            }
            messages.push_back (text);
        };
    }
    bool has (const std::string& m) const { return std::find (messages.begin(), messages.end(), m) != messages.end(); }
    int count (const std::string& prefix) const
    {
        return (int) std::count_if (messages.begin(), messages.end(), [&] (const std::string& m) { return m.rfind (prefix, 0) == 0; });
    }
    void clear() { messages.clear(); }
};

inline bool feed (Session& s, FakeSurface& surface, Shared& shared, std::initializer_list<int> bytes)
{
    std::vector<std::uint8_t> b;
    for (const auto v : bytes)
        b.push_back ((std::uint8_t) v);
    return s.input (b.data(), b.size(), surface, shared);
}

// the text a display sysex carries after its header ("F0 00 00 66 14 12 <pos>")
inline std::string lcdText (const std::string& hexMessage, int headerBytes)
{
    std::string out;
    std::istringstream in (hexMessage);
    std::string byte;
    for (int i = 0; in >> byte; ++i)
        if (i >= headerBytes && byte != "F7")
            out.push_back ((char) std::strtol (byte.c_str(), nullptr, 16));
    return out;
}

// A motor-fader surface in the Mackie Control style and a plain knob box, written as profile files.
inline const char* motorProfile = R"(# test profile
[device]
name = Test motor surface
maker = Test
source = written for the tests
match = Test Control*, *Motor*
exclude = *XT*
group = test
strips = 8
layers = pan, plugin

[init]
send = F0 00 00 66 14 00 F7

[exit]
send = F0 00 00 66 14 08 F7

[input]
bend ch 1-8 = mixer.volume
bend ch 9 = master.volume
note 104-111 = mixer.touch
note 112 = master.touch
cc 16-23 ch 1 = mixer.pan mode=sign step=0.02 layer=pan
cc 16-23 ch 1 = plugin.param mode=sign step=0.01 layer=plugin
cc 60 ch 1 = transport.jog mode=sign
note 0-7 ch 1 = mixer.arm
note 8-15 ch 1 = mixer.solo
note 16-23 ch 1 = mixer.mute
note 24-31 ch 1 = mixer.select
note 42 ch 1 = layer pan
note 43 ch 1 = layer plugin
note 46 ch 1 = mixer.bank.left
note 47 ch 1 = mixer.bank.right
note 48 ch 1 = mixer.strip.left
note 49 ch 1 = mixer.strip.right
note 44 ch 1 = plugin.page.left
note 45 ch 1 = plugin.page.right
note 86 ch 1 = transport.loop
note 89 ch 1 = transport.metronome
note 93 ch 1 = transport.stop
note 94 ch 1 = transport.play
note 95 ch 1 = transport.record

[feedback]
mixer.volume = bend ch 1-8
master.volume = bend ch 9
mixer.pan = cc 48-55 ch 1 ring=pan layer=pan
plugin.param = cc 48-55 ch 1 ring=wrap layer=plugin
mixer.arm = note 0-7 ch 1
mixer.solo = note 8-15 ch 1
mixer.mute = note 16-23 ch 1
mixer.select = note 24-31 ch 1
layer pan = note 42 ch 1
layer plugin = note 43 ch 1
transport.loop = note 86 ch 1
transport.metronome = note 89 ch 1
transport.stop = note 93 ch 1
transport.play = note 94 ch 1
transport.record = note 95 ch 1

[display]
write = F0 00 00 66 14 12 {pos} {text} F7
width = 7
cells = 8
row 1 = mixer.name layer=pan
row 2 = mixer.pan layer=pan
row 1 = plugin.param.name layer=plugin
row 2 = plugin.param.value layer=plugin
)";

inline const char* knobProfile = R"([device]
name = Test knob box
match = Knob Box*

[input]
cc 0-7 ch 1 = mixer.volume          # sliders
cc 16-23 ch 1 = plugin.param        # knobs
cc 32-39 ch 1 = mixer.solo
cc 58 ch 1 = mixer.bank.left
cc 59 ch 1 = mixer.bank.right
cc 41 ch 1 = transport.play
cc 70 = channel.volume selected
note 36-43 ch 10 = pad.step
note 60-67 ch 10 = pad.note key=36
cc 9 ch 16 value 2 = transport.record

[feedback]
mixer.solo = cc 32-39 ch 1
transport.play = cc 41 ch 1
pad.step = note 36-43 ch 10 on=3 off=1
)";

inline std::shared_ptr<const Profile> profileOf (const char* text, const char* id)
{
    auto parsed = parseProfile (text, id);
    for (const auto& e : parsed.errors)
        std::printf ("  profile %s: %s\n", id, e.c_str());
    return std::make_shared<const Profile> (std::move (parsed.profile));
}
} // namespace c190aw

TEST (controller190aw_profile_file_parses_and_reports_errors_by_line)
{
    using namespace c190aw;
    const auto parsed = parseProfile (motorProfile, "motor");
    CHECK (parsed.ok());
    const auto& p = parsed.profile;
    CHECK (p.id == "motor" && p.name == "Test motor surface" && p.maker == "Test" && p.group == "test" && p.strips == 8);
    CHECK (p.sources.size() == 1 && p.match.size() == 2 && p.exclude.size() == 1 && p.layers.size() == 2);
    CHECK (p.init.size() == 1 && p.init[0].size() == 7 && p.init[0][5] == 0x00 && p.exit.size() == 1 && p.exit[0][5] == 0x08);
    CHECK (p.bindings.size() == 24 && p.feedback.size() == 15 && p.display.rows.size() == 4);

    // ranges: 8 pitch bends over channels, 8 notes over numbers
    const auto& faders = p.bindings[0];
    CHECK (faders.message.kind == MessageKind::bend && faders.message.channelRange && faders.message.count == 8 && faders.action == Action::mixerVolume);
    CHECK (! faders.pickup); // (a pitch-bend fader is a motor fader unless the file says pickup=on)
    CHECK (faders.message.element (MessageKind::bend, 3, 0) == 3 && faders.message.element (MessageKind::bend, 8, 0) == -1);
    const auto& pots = p.bindings[4];
    CHECK (pots.mode == ValueMode::relativeSign && pots.layer == 0 && std::abs (pots.step - 0.02f) < 1e-6f && pots.message.channel == 0);
    CHECK (pots.message.element (MessageKind::cc, 0, 18) == 2 && pots.message.element (MessageKind::cc, 1, 18) == -1
           && pots.message.element (MessageKind::note, 0, 18) == -1);
    CHECK (p.bindings[5].layer == 1 && p.bindings[5].action == Action::pluginParam);
    CHECK (p.feedback[2].encoding == Encoding::ringPan && p.feedback[2].layer == 0 && p.feedback[3].encoding == Encoding::ringWrap);
    CHECK (p.display.width == 7 && p.display.cells == 8 && p.display.write.size() == 9 && p.display.write[6] == Display::pos
           && p.display.write[7] == Display::text && p.display.write[8] == 0xF7);

    // the knob box: any channel, a value filter, 'selected', inline comments, LED values
    const auto knobs = parseProfile (knobProfile, "knobs");
    CHECK (knobs.ok());
    CHECK (knobs.profile.bindings.size() == 10 && knobs.profile.group.empty());
    CHECK (knobs.profile.bindings[0].pickup && knobs.profile.bindings[0].mode == ValueMode::absolute);
    CHECK (knobs.profile.bindings[6].message.channel == -1 && knobs.profile.bindings[6].strip == selectedStrip);
    CHECK (knobs.profile.bindings[8].key == 36 && knobs.profile.bindings[9].value == 2 && knobs.profile.bindings[9].message.channel == 15);
    CHECK (knobs.profile.feedback[2].on == 3 && knobs.profile.feedback[2].off == 1);

    // errors name their line; a file with errors is not a profile
    const auto bad = parseProfile ("[device]\nname = Bad\n[input]\ncc 200 = mixer.volume\ncc 7 = mixer.volum\nnote 1-4 ch 1-4 = mixer.mute\n"
                                   "cc 1 = transport.play 3\ncc 2 = mixer.pan mode=fast\ncc 3 = layer nowhere\n[feedback]\nmixer.mute = note 5\n[nonsense]\n",
                                   "bad");
    CHECK (! bad.ok());
    CHECK (bad.errors.size() == 8);
    auto startsWith = [&] (std::size_t i, const char* prefix) { return i < bad.errors.size() && bad.errors[i].rfind (prefix, 0) == 0; };
    CHECK (startsWith (0, "line 4:") && startsWith (1, "line 5: unknown action 'mixer.volum'") && startsWith (2, "line 6:") && startsWith (3, "line 7:")
           && startsWith (4, "line 8:") && startsWith (5, "line 9:") && startsWith (6, "line 11:") && startsWith (7, "line 12:"));
    CHECK (! parseProfile ("", "empty").ok());
    CHECK (! parseProfile ("[device]\nmaker = Nobody\n", "noname").ok());
    CHECK (! parseProfile ("[device]\nname = X\n[display]\nrow 1 = mixer.name\n", "nowrite").ok());

    // a library: the user's file replaces a built-in of the same id; a broken file is left out with its errors;
    // extends takes the parent's maps and its own name and patterns
    Library lib;
    CHECK (lib.add (motorProfile, "motor", false) && lib.add (knobProfile, "knobs", false));
    CHECK (! lib.add ("[device]\nname = Broken\n[input]\ncc x = mixer.pan\n", "broken", true) && lib.profiles.size() == 2 && lib.errors.size() == 1
           && lib.errors[0].rfind ("broken: line 4:", 0) == 0);
    CHECK (lib.add ("[device]\nname = Clone keyboard\nmatch = Clone 49*\nextends = motor\nsource = the clone's manual\n", "clone", true));
    const auto clone = lib.find ("clone");
    CHECK (clone != nullptr && clone->name == "Clone keyboard" && clone->user && clone->bindings.size() == 24 && clone->match.size() == 1
           && clone->match[0] == "Clone 49*" && clone->sources.size() == 2 && clone->group == "test");
    CHECK (! lib.add ("[device]\nname = Orphan\nextends = nothing\n", "orphan", true));
    CHECK (lib.add (std::string (knobProfile) + "\n[device]\nname = My knob box\n", "knobs", true) && lib.profiles.size() == 3
           && lib.find ("knobs")->name == "My knob box" && lib.find ("knobs")->user);
}

TEST (controller190aw_detects_by_port_name_and_follows_the_setting)
{
    using namespace c190aw;
    Library lib;
    lib.add (motorProfile, "motor", false);
    lib.add (knobProfile, "knobs", false);
    lib.add ("[device]\nname = Motor XT\nmatch = *Control XT*\nextends = motor\n", "motor-xt", false);

    CHECK (matchScore ({ "Test Control*" }, "test control usb") > 0 && matchScore ({ "Test Control*" }, "My Test Control") == 0);
    CHECK (matchScore ({ "*Motor*", "Big Motor Desk" }, "Big Motor Desk") > matchScore ({ "*Motor*" }, "Big Motor Desk"));
    CHECK (matchScore ({}, "anything") == 0 && matchScore ({ "*" }, "anything") > 0);

    CHECK (lib.detect ("Test Control") == lib.find ("motor"));
    CHECK (lib.detect ("Test Control XT") == lib.find ("motor-xt")); // (excluded by the first, matched by the second)
    CHECK (lib.detect ("KNOB BOX 2") == lib.find ("knobs"));
    CHECK (lib.detect ("Some Piano") == nullptr);

    // the setting: "" detects, "generic" turns the profile off, an id forces it (also on a port that does not match)
    CHECK (lib.resolve ("", "Test Control") == lib.find ("motor"));
    CHECK (lib.resolve (genericChoice, "Test Control") == nullptr);
    CHECK (lib.resolve ("knobs", "Some Piano") == lib.find ("knobs"));
    CHECK (lib.resolve ("gone", "Test Control") == nullptr);

    // a user profile wins a tie against a built-in one
    lib.add ("[device]\nname = Mine\nmatch = Knob Box*\n[input]\ncc 1 = transport.play\n", "mine", true);
    CHECK (lib.detect ("Knob Box") == lib.find ("mine"));

    // the output port of an input port
    const std::vector<std::string> outs { "Launch 49", "MIDIOUT2 (Launch 49)", "Desk DAW In", "Box MIDI In", "Thing Out", "Other" };
    CHECK (pairedOutput ("Launch 49", outs) == "Launch 49");
    CHECK (pairedOutput ("MIDIIN2 (Launch 49)", outs) == "MIDIOUT2 (Launch 49)");
    CHECK (pairedOutput ("Desk DAW Out", outs) == "Desk DAW In");
    CHECK (pairedOutput ("Box MIDI Out", outs) == "Box MIDI In");
    CHECK (pairedOutput ("Thing In", outs) == "Thing Out");
    CHECK (pairedOutput ("Nothing", outs).empty() && pairedOutput ("", outs).empty());
}

TEST (controller190aw_motor_faders_vpots_leds_and_lcd_round_trip)
{
    using namespace c190aw;
    FakeSurface lynx;
    lynx.tracks[2].mute = true;
    lynx.tracks[3].pan = 0.0f;
    lynx.selectedTrack = 4;
    Shared shared;
    Session s (profileOf (motorProfile, "motor"), 0);
    Wire wire;

    // opening: the init message, then every fader, ring, LED and display cell
    s.open (lynx, shared, wire.send());
    CHECK (! wire.messages.empty() && wire.messages[0] == "F0 00 00 66 14 00 F7");
    // insert 1 at 0.8: 0.8 × 1023 = 818 → << 4 = 13088 = 0x3320 → LSB 0x20, MSB 0x66
    CHECK (wire.has ("E0 20 66") && wire.has ("E7 20 66") && wire.has ("E8 20 66"));
    CHECK (wire.has ("90 11 7F") && wire.has ("90 10 00")); // mute LED of strip 2 on, strip 1 off
    CHECK (wire.has ("90 1B 7F") && wire.has ("90 18 00")); // select LED of strip 4
    CHECK (wire.has ("B0 30 16") && wire.has ("B0 32 11")); // rings: centre (0x10 | 6), strip 3 hard left (0x10 | 1)
    CHECK (wire.has ("90 2A 7F") && wire.has ("90 2B 00")); // the pan layer's LED
    CHECK (wire.has ("90 5D 7F") && wire.has ("90 5E 00") && wire.has ("90 56 7F")); // stopped, not playing, pattern mode
    CHECK (wire.count ("F0 00 00 66 14 12") == 16);
    auto lcd = [&] (int pos) {
        char head[40];
        std::snprintf (head, sizeof (head), "F0 00 00 66 14 12 %02X ", pos);
        for (const auto& m : wire.messages)
            if (m.rfind (head, 0) == 0)
                return lcdText (m, 7);
        return std::string ("<none>");
    };
    CHECK (lcd (0) == "Insert " && lcd (7) == "Insert " && lcd (56) == "C      " && lcd (56 + 14) == "L100   ");

    // nothing changed: nothing is sent
    wire.clear();
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.messages.empty());

    // a fader moves on the device (touch, move, release): LYNX follows, sends nothing while it is held and the
    // position once it is let go
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 104, 127 }));
    CHECK (feed (s, lynx, shared, { 0xE0, 0x00, 0x40 })); // 8192 / 16383
    CHECK (lynx.did ("volume 1 0.500") && std::abs (lynx.tracks[1].volume - 0.50003f) < 1e-3f);
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.messages.empty());
    CHECK (feed (s, lynx, shared, { 0x90, 104, 0 }));
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.messages.size() == 1 && wire.messages[0] == "E0 00 40"); // 0.50003 × 1023 = 512 → 8192

    // a fader moved in LYNX (mouse, automation): the motor follows; the master too
    wire.clear();
    lynx.tracks[5].volume = 1.0f;
    lynx.tracks[0].volume = 0.0f;
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.messages.size() == 2 && wire.has ("E4 70 7F") && wire.has ("E8 00 00"));
    CHECK (feed (s, lynx, shared, { 0xE8, 0x7F, 0x7F }) && lynx.did ("volume 0 1.000"));

    // V-Pot: 3 ticks clockwise, then 1 counter-clockwise (bit 6), on strip 2: the pan moves by its step, the ring follows
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 17, 0x03 }));
    CHECK (std::abs (lynx.tracks[2].pan - 0.56f) < 1e-5f);
    CHECK (feed (s, lynx, shared, { 0xB0, 17, 0x41 }));
    CHECK (std::abs (lynx.tracks[2].pan - 0.54f) < 1e-5f);
    lynx.tracks[2].pan = 1.0f;
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("B0 31 1B")); // hard right: 0x10 | 11
    CHECK (wire.count ("F0 00 00 66 14 12 3F") == 1 && lcdText (wire.messages.back(), 7) == "R100   "); // row 2, cell 2: 56 + 7

    // buttons: press toggles, release does nothing; the LED shows LYNX's state, also when LYNX changed it
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 16, 127 }) && feed (s, lynx, shared, { 0x90, 16, 0 }) && feed (s, lynx, shared, { 0x80, 16, 64 }));
    CHECK (lynx.log.size() == 1 && lynx.did ("mute 1 on"));
    CHECK (feed (s, lynx, shared, { 0x90, 8 + 2, 127 }) && lynx.did ("solo 3 on"));
    CHECK (feed (s, lynx, shared, { 0x90, 0 + 7, 127 }) && lynx.did ("arm 8 on"));
    CHECK (feed (s, lynx, shared, { 0x90, 24 + 5, 127 }) && lynx.did ("select track 6"));
    lynx.tracks[2].mute = false; // (unmuted with the mouse)
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 10 7F") && wire.has ("90 11 00") && wire.has ("90 0A 7F") && wire.has ("90 07 7F") && wire.has ("90 1D 7F") && wire.has ("90 1B 00"));
    CHECK (wire.messages.size() == 6);

    // transport
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 94, 127 }) && feed (s, lynx, shared, { 0x90, 95, 127 }) && feed (s, lynx, shared, { 0x90, 89, 127 })
           && feed (s, lynx, shared, { 0x90, 86, 127 }));
    CHECK (lynx.playing && lynx.recording && lynx.click && ! lynx.patternMode);
    CHECK (feed (s, lynx, shared, { 0xB0, 60, 0x01 }) && feed (s, lynx, shared, { 0xB0, 60, 0x42 }) && lynx.did ("jog 1") && lynx.did ("jog -2"));
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 5E 7F") && wire.has ("90 5D 00") && wire.has ("90 5F 7F") && wire.has ("90 59 7F") && wire.has ("90 56 00") && wire.messages.size() == 5);
    CHECK (feed (s, lynx, shared, { 0x90, 93, 127 }) && ! lynx.playing);

    // what the profile does not name is not taken (it plays as before)
    CHECK (! feed (s, lynx, shared, { 0x90, 60, 100 }) && ! feed (s, lynx, shared, { 0xB0, 1, 64 }) && ! feed (s, lynx, shared, { 0xD0, 50 })
           && ! feed (s, lynx, shared, { 0xEA, 0, 64 }));
    const Claims claims (s.profile());
    const std::uint8_t note60[] { 0x90, 60, 100 }, play[] { 0x90, 94, 127 }, playOff[] { 0x80, 94, 0 }, bend1[] { 0xE0, 0, 64 }, bend11[] { 0xEA, 0, 64 },
        pot[] { 0xB0, 16, 1 }, potOther[] { 0xB1, 16, 1 }, pressure[] { 0xD0, 9 };
    CHECK (! claims.has (note60, 3) && claims.has (play, 3) && claims.has (playOff, 3) && claims.has (bend1, 3) && ! claims.has (bend11, 3)
           && claims.has (pot, 3) && ! claims.has (potOther, 3) && ! claims.has (pressure, 2) && ! claims.has (nullptr, 0));

    // closing: LEDs and rings off, the display blank, faders left where they are, then the exit message
    wire.clear();
    s.close (wire.send());
    CHECK (wire.has ("90 10 00") && wire.has ("90 5F 00") && wire.has ("B0 31 00") && wire.count ("E") == 0);
    CHECK (wire.count ("F0 00 00 66 14 12") == 16 && wire.messages.back() == "F0 00 00 66 14 08 F7");
    wire.clear();
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.messages.empty());
}

TEST (controller190aw_bank_switching_layers_and_pages)
{
    using namespace c190aw;
    FakeSurface lynx (20, 4, 20);
    lynx.tracks[9].mute = true;
    lynx.tracks[9].name = "Voz L\xC3\xAD" "der \xE2\x99\xAA"; // "Voz Líder ♪": an accent and a sign no display has
    Shared shared;
    Session s (profileOf (motorProfile, "motor"), 0);
    Wire wire;
    s.open (lynx, shared, wire.send());

    // bank right: the strips are inserts 9…16; LEDs and names follow
    wire.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 47, 127 }) && shared.mixerBank == 8);
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 10 7F")); // strip 1 = insert 9, muted
    CHECK (wire.count ("F0 00 00 66 14 12 00") == 1);
    for (const auto& m : wire.messages)
        if (m.rfind ("F0 00 00 66 14 12 00", 0) == 0)
            CHECK (lcdText (m, 7) == "Voz Li ");
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 16 + 1, 127 }) && lynx.did ("mute 10 on"));
    CHECK (feed (s, lynx, shared, { 0xE2, 0x7F, 0x7F }) && lynx.did ("volume 11 1.000"));

    // bank right again: inserts 17…20, the other strips are empty (off, blank, and do nothing)
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 47, 127 }) && shared.mixerBank == 16);
    CHECK (feed (s, lynx, shared, { 0x90, 47, 127 }) && shared.mixerBank == 16); // (no further bank)
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("B0 34 00") && wire.has ("E4 00 00")); // strip 5: ring off, fader down
    CHECK (feed (s, lynx, shared, { 0x90, 16 + 4, 127 }) && feed (s, lynx, shared, { 0xE4, 0, 64 }) && lynx.log.empty());
    CHECK (feed (s, lynx, shared, { 0x90, 16 + 3, 127 }) && lynx.did ("mute 20 on"));

    // one strip left, bank left twice, and no further
    CHECK (feed (s, lynx, shared, { 0x90, 48, 127 }) && shared.mixerBank == 15);
    CHECK (feed (s, lynx, shared, { 0x90, 46, 127 }) && shared.mixerBank == 7 && feed (s, lynx, shared, { 0x90, 46, 127 }) && shared.mixerBank == 0);
    CHECK (feed (s, lynx, shared, { 0x90, 49, 127 }) && shared.mixerBank == 1 && feed (s, lynx, shared, { 0x90, 48, 127 })
           && feed (s, lynx, shared, { 0x90, 48, 127 }) && shared.mixerBank == 0);

    // the plug-in layer: the V-Pots turn the focused plugin's parameters, the rings and the display show them
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0x90, 43, 127 }) && shared.layer == 1);
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("90 2B 7F") && wire.has ("90 2A 00"));
    CHECK (wire.has ("B0 30 24")); // 0.25: wrap mode 0x20 | (1 + round (2.5) = 4)
    CHECK (wire.count ("F0 00 00 66 14 12") == 16);
    CHECK (feed (s, lynx, shared, { 0xB0, 18, 0x05 }) && lynx.did ("param 2 0.300") && lynx.tracks[3].pan == 0.5f);
    // next page: parameters 9…16, then 17…20, and no fourth page
    CHECK (feed (s, lynx, shared, { 0x90, 45, 127 }) && shared.page == 1);
    CHECK (feed (s, lynx, shared, { 0xB0, 16, 0x41 }) && lynx.did ("param 8 0.240"));
    CHECK (feed (s, lynx, shared, { 0x90, 45, 127 }) && shared.page == 2 && feed (s, lynx, shared, { 0x90, 45, 127 }) && shared.page == 2);
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 16 + 5, 0x01 }) && lynx.log.empty()); // (parameter 22 does not exist)
    wire.clear();
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("B0 35 00"));
    CHECK (feed (s, lynx, shared, { 0x90, 44, 127 }) && feed (s, lynx, shared, { 0x90, 44, 127 }) && feed (s, lynx, shared, { 0x90, 44, 127 }) && shared.page == 0);
    // back to pan
    CHECK (feed (s, lynx, shared, { 0x90, 42, 127 }) && shared.layer == 0);
    CHECK (feed (s, lynx, shared, { 0xB0, 18, 0x05 }) && std::abs (lynx.tracks[3].pan - 0.6f) < 1e-5f);

    CHECK (displayAscii ("A\xC3\xA7\xC3\xA3o \xC3\xA9 \xC3\xB1", 8) == "Acao e n" && displayAscii ("abc", 5) == "abc  " && displayAscii ("\xE6\x97\xA5\xE6\x9C\xAC", 3) == "?? " && displayAscii ("x", 0).empty());
    CHECK (panText (0.5f) == "C" && panText (0.0f) == "L100" && panText (0.75f) == "R50" && volumeText (0.8f) == "80%");
}

TEST (controller190aw_extender_takes_the_next_strips_and_shares_the_bank)
{
    using namespace c190aw;
    FakeSurface lynx (40);
    for (std::size_t i = 1; i < lynx.tracks.size(); ++i)
        lynx.tracks[i].name = "T" + std::to_string (i);
    Library lib;
    lib.add (motorProfile, "motor", false);
    lib.add ("[device]\nname = Motor XT\nmatch = *Control XT*\nextends = motor\n", "motor-xt", false);
    lib.add (knobProfile, "knobs", false);

    Hub hub;
    std::map<std::string, Wire> wires;
    Hub::DeviceSend send = [&] (const std::string& input, const std::uint8_t* bytes, std::size_t size) { wires[input].send() (bytes, size); };
    auto feedHub = [&] (const std::string& device, std::initializer_list<int> bytes) {
        std::vector<std::uint8_t> b;
        for (const auto v : bytes)
            b.push_back ((std::uint8_t) v);
        return hub.input (device, b.data(), b.size(), lynx);
    };

    hub.configure ({ { "Test Control", lib.detect ("Test Control") }, { "Test Control XT", lib.detect ("Test Control XT") }, { "Piano", lib.detect ("Piano") },
                     { "Knob Box", lib.detect ("Knob Box") } },
                   lynx, send);
    CHECK (hub.has ("Test Control") && hub.has ("Test Control XT") && hub.has ("Knob Box") && ! hub.has ("Piano"));
    CHECK (hub.session ("Test Control")->unitOffset() == 0 && hub.session ("Test Control XT")->unitOffset() == 8);
    CHECK (hub.shared ("test") != nullptr && hub.shared ("test")->strips == 16);
    CHECK (wires["Test Control"].has ("F0 00 00 66 14 00 F7") && wires["Test Control XT"].has ("F0 00 00 66 14 00 F7") && wires["Piano"].messages.empty());

    // a device without a profile (generic) is not taken
    CHECK (! feedHub ("Piano", { 0x90, 94, 127 }) && ! feedHub ("Piano", { 0xB0, 16, 1 }) && lynx.log.empty());

    // the extender's strip 1 is insert 9
    CHECK (feedHub ("Test Control XT", { 0x90, 16, 127 }) && lynx.did ("mute 9 on"));
    CHECK (feedHub ("Test Control", { 0x90, 16, 127 }) && lynx.did ("mute 1 on"));
    // bank right on the main unit moves both by 16
    for (auto& [name, w] : wires)
        w.clear();
    lynx.log.clear();
    CHECK (feedHub ("Test Control", { 0x90, 47, 127 }) && hub.shared ("test")->mixerBank == 16);
    CHECK (feedHub ("Test Control XT", { 0x90, 16 + 7, 127 }) && lynx.did ("mute 32 on"));
    hub.refresh (lynx, send);
    CHECK (wires["Test Control XT"].has ("90 17 7F") && wires["Test Control"].count ("F0 00 00 66 14 12") == 8);
    // the knob box has its own bank
    CHECK (feedHub ("Knob Box", { 0xB0, 32, 127 }) && lynx.did ("solo 1 on"));

    // the extender is unplugged: it gets its closing messages, the main unit keeps its bank
    for (auto& [name, w] : wires)
        w.clear();
    hub.configure ({ { "Test Control", lib.find ("motor") }, { "Knob Box", lib.find ("knobs") } }, lynx, send);
    CHECK (! hub.has ("Test Control XT") && wires["Test Control XT"].messages.back() == "F0 00 00 66 14 08 F7" && wires["Test Control"].messages.empty());
    CHECK (hub.shared ("test")->mixerBank == 16 && hub.shared ("test")->strips == 8);
    // the setting changes to "Generic controller": the session goes
    hub.configure ({ { "Knob Box", lib.find ("knobs") } }, lynx, send);
    CHECK (! hub.has ("Test Control") && wires["Test Control"].messages.back() == "F0 00 00 66 14 08 F7" && hub.shared ("test") == nullptr);
    hub.closeAll (send);
    CHECK (! hub.has ("Knob Box"));
}

TEST (controller190aw_soft_takeover_for_knobs_and_sliders_without_motors)
{
    using namespace c190aw;
    FakeSurface lynx;
    Shared shared;
    Session s (profileOf (knobProfile, "knobs"), 0);
    Wire wire;
    s.open (lynx, shared, wire.send());

    // slider 1 sits at 20 while insert 1 is at 0.8 (102): it moves nothing until it reaches the value
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 20 }) && feed (s, lynx, shared, { 0xB0, 0, 60 }) && feed (s, lynx, shared, { 0xB0, 0, 90 }));
    CHECK (lynx.log.empty() && lynx.tracks[1].volume == 0.8f);
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 104 })); // crossed 0.8: caught
    CHECK (lynx.did ("volume 1 0.819"));
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 30 }) && std::abs (lynx.tracks[1].volume - 30.0f / 127.0f) < 1e-5f); // and it stays caught

    // the value moved in LYNX: the slider has to reach it again
    lynx.tracks[1].volume = 0.9f;
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 31 }) && lynx.log.empty());
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 115 }) && lynx.did ("volume 1 0.906"));

    // bank right: slider 1 is now insert 9 (0.8) and must be caught again; a slider that lands on the value is caught at once
    CHECK (feed (s, lynx, shared, { 0xB0, 59, 127 }) && shared.mixerBank == 8);
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 120 }) && lynx.log.empty() && lynx.tracks[9].volume == 0.8f);
    CHECK (feed (s, lynx, shared, { 0xB0, 0, 101 }) && lynx.did ("volume 9 0.795"));
    CHECK (feed (s, lynx, shared, { 0xB0, 1, 102 }) && lynx.did ("volume 10 0.803")); // 102 / 127 is within a step of 0.8

    // knobs on the focused plugin's parameters (0.25 = 32): the same rule
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 16, 100 }) && lynx.log.empty());
    CHECK (feed (s, lynx, shared, { 0xB0, 16, 31 }) && lynx.did ("param 0 0.244"));
    // the selected channel's volume, from any MIDI channel
    lynx.selected = 2;
    CHECK (feed (s, lynx, shared, { 0xB5, 70, 102 }) && lynx.did ("channel volume 2 0.803"));

    // CC buttons with LEDs, pads as steps and as notes, a value filter
    wire.clear();
    lynx.log.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 41, 127 }) && feed (s, lynx, shared, { 0xB0, 41, 0 }) && lynx.playing);
    CHECK (feed (s, lynx, shared, { 0x99, 36 + 4, 90 }) && feed (s, lynx, shared, { 0x89, 36 + 4, 0 }) && lynx.did ("step 4 on") && lynx.stepsOn[4]);
    CHECK (feed (s, lynx, shared, { 0x99, 62, 127 }) && lynx.did ("note on 38 1.000") && feed (s, lynx, shared, { 0x89, 62, 0 }) && lynx.did ("note off 38 0.000"));
    CHECK (feed (s, lynx, shared, { 0xBF, 9, 1 }) && ! lynx.recording && feed (s, lynx, shared, { 0xBF, 9, 2 }) && lynx.recording);
    s.refresh (lynx, shared, wire.send());
    CHECK (wire.has ("B0 29 7F") && wire.has ("99 28 03") && wire.messages.size() == 2);
}

TEST (controller190aw_links_made_by_hand_come_first)
{
    using namespace c190aw;
    FakeSurface lynx;
    Shared shared;
    Session s (profileOf (knobProfile, "knobs"), 0);
    Wire wire;
    s.open (lynx, shared, wire.send());

    // CC 16 on channel 1 is linked by hand (Link to MIDI controller): the profile leaves it to the link
    lynx.links.insert ({ 16, 0 });
    lynx.log.clear();
    CHECK (! feed (s, lynx, shared, { 0xB0, 16, 32 }) && lynx.log.empty());
    // the next knob still belongs to the profile
    CHECK (feed (s, lynx, shared, { 0xB0, 17, 32 }) && lynx.did ("param 1 0.252"));
    // a link on any channel
    lynx.links.insert ({ 41, -1 });
    CHECK (! feed (s, lynx, shared, { 0xB0, 41, 127 }) && ! lynx.playing);
    // the link is removed: the profile has the control again
    lynx.links.clear();
    CHECK (feed (s, lynx, shared, { 0xB0, 41, 127 }) && lynx.playing);
}

TEST (controller190aw_old_settings_and_generic_devices_behave_as_before)
{
    using namespace c190aw;
    using namespace lynx::prefs;

    // a settings file written before controller types existed: every device detects, nothing else changes
    const std::string old = "velocity=soft\npickup=1\nfootPedal=1\ninput=1|2|4|Keystation 49\ninput=0|0|-1|Pads | MPD=218\nlink=7|-1|4|0|0|0\n";
    const auto p = MidiPrefs::fromText (old);
    CHECK (p.inputs.size() == 2 && p.input ("Keystation 49").controller.empty() && p.input ("Keystation 49").midiChannel == 2
           && p.input ("Keystation 49").target == 4 && ! p.input ("Pads | MPD=218").enabled && p.globalLinks.size() == 1 && p.pickup);
    CHECK (p.toText() == old); // (and it is written back byte for byte)
    CHECK (MidiPrefs::fromText (MidiPrefs {}.toText()) == MidiPrefs {});

    // the controller type is kept per device, also for a device with no other setting and a name with '|' and '='
    auto q = p;
    auto in = q.input ("Keystation 49");
    in.controller = "mackie-control";
    q.setInput (in);
    MidiInputPrefs box;
    box.name = "Box | A=B";
    box.controller = genericChoice;
    q.setInput (box);
    CHECK (q.inputs.size() == 3);
    const auto back = MidiPrefs::fromText (q.toText());
    CHECK (back == q && back.input ("Keystation 49").controller == "mackie-control" && back.input ("Keystation 49").target == 4
           && back.input ("Box | A=B").controller == genericChoice && back.input ("Box | A=B").enabled && back.input ("Other").controller.empty());
    // back to "detect": the device leaves the list again
    box.controller.clear();
    q.setInput (box);
    CHECK (q.inputs.size() == 2 && q.toText().find ("Box") == std::string::npos);
    // (the lines in either order)
    CHECK (MidiPrefs::fromText ("controller=knobs|Dev\ninput=1|3|-1|Dev\n").input ("Dev").controller == "knobs"
           && MidiPrefs::fromText ("controller=knobs|Dev\ninput=1|3|-1|Dev\n").input ("Dev").midiChannel == 3);

    // LYNX ships no profile that takes a plain keyboard: with the built-in library a device nobody knows is generic
    const auto builtIn = Library::builtIn();
    CHECK (builtIn.errors.empty());
    CHECK (builtIn.resolve ("", "Keystation 49") == nullptr && builtIn.resolve ("", "USB MIDI Interface") == nullptr && builtIn.resolve ("", "") == nullptr);
    CHECK (builtIn.resolve (genericChoice, "Mackie Control") == nullptr);

    // the MIDI thread's test: a generic device's messages are never taken; a profile takes its own controls only, and
    // not the controllers a MIDI link uses (those keep moving their parameter exactly as before)
    const Claims none;
    const std::uint8_t cc7[] { 0xB0, 7, 100 }, cc16[] { 0xB0, 16, 100 }, cc70[] { 0xB3, 70, 1 }, note[] { 0x90, 60, 100 }, bend[] { 0xE0, 0, 64 };
    CHECK (! none.takes (cc7, 3, 0, 0) && ! none.takes (note, 3, 0, 0) && ! none.takes (bend, 3, 0, 0));
    const Claims knobs (*profileOf (knobProfile, "knobs"));
    CHECK (knobs.takes (cc7, 3, 0, 0) && knobs.takes (cc16, 3, 0, 0) && knobs.takes (cc70, 3, 0, 0) && ! knobs.takes (note, 3, 0, 0) && ! knobs.takes (bend, 3, 0, 0));
    CHECK (! knobs.takes (cc7, 3, std::uint64_t { 1 } << 7, 0) && knobs.takes (cc16, 3, std::uint64_t { 1 } << 7, 0));
    CHECK (! knobs.takes (cc70, 3, 0, std::uint64_t { 1 } << (70 - 64)) && knobs.takes (cc70, 3, std::uint64_t { 1 } << 6, 0));

    // a Hub without devices, or with generic ones only, takes nothing and sends nothing
    FakeSurface lynx;
    Hub hub;
    auto sentAny = false;
    Hub::DeviceSend send = [&] (const std::string&, const std::uint8_t*, std::size_t) { sentAny = true; };
    hub.configure ({ { "Keystation 49", nullptr }, { "Pads", builtIn.resolve ("", "Pads") } }, lynx, send);
    hub.refresh (lynx, send);
    CHECK (! hub.has ("Keystation 49") && ! hub.input ("Keystation 49", cc7, 3, lynx) && ! hub.input ("Pads", note, 3, lynx) && lynx.log.empty() && ! sentAny);
}
