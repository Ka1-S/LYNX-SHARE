// --- Round 190bb (issue #108): MPE stage 1 — a controller's per-note bend / timbre / pressure in, per-note data out ----
#include "core/Mpe.h"

namespace mpe190bb
{
using lynx::mpe::InEvent;
struct Fed
{
    int count = 0;
    InEvent e[lynx::mpe::Input::maxEvents];
};
inline Fed feed (lynx::mpe::Input& in, std::initializer_list<int> bytes)
{
    std::uint8_t b[3] = { 0, 0, 0 };
    auto n = 0;
    for (const auto v : bytes)
        b[n++] = (std::uint8_t) v;
    Fed f;
    f.count = in.feed (b, n, f.e);
    return f;
}
inline bool near (float a, float b, float tolerance = 0.01f) { return std::abs (a - b) <= tolerance; }
} // namespace mpe190bb

TEST (mpe190bb_laws_bend_timbre_pressure)
{
    using namespace lynx::mpe;
    CHECK (bendCents (8192, 48.0f) == 0.0f);
    CHECK (mpe190bb::near (bendCents (16383, 48.0f), 4800.0f) && mpe190bb::near (bendCents (0, 48.0f), -4800.0f));
    CHECK (mpe190bb::near (bendCents (16383, 2.0f), 200.0f));
    for (const auto cents : { -4800.0f, -1234.0f, -50.0f, 0.0f, 33.0f, 700.0f, 4800.0f })
        CHECK (mpe190bb::near (bendCents (bendValue (cents, 48.0f), 48.0f), cents, 0.6f)); // (14 bits over 96 semitones: 0.59 c steps)
    CHECK (bendValue (99999.0f, 48.0f) == 16383 && bendValue (-99999.0f, 48.0f) == 0 && bendValue (std::nanf (""), 48.0f) == 8192);
    // CC 74: 64 is the centre = Mod X 0.5 (a note nobody touched keeps the piano roll's default)
    CHECK (modXOfTimbre (64) == 0.5f && modXOfTimbre (0) == 0.0f && modXOfTimbre (127) == 1.0f);
    for (int cc = 0; cc < 128; ++cc)
        CHECK (timbreOfModX (modXOfTimbre (cc)) == cc);
    CHECK (timbreOfModX (0.5f) == 64);
    // pressure: none = Mod Y 0.5 (the default), full = 1; Mod Y under 0.5 sends none
    CHECK (modYOfPressure (0.0f) == 0.5f && modYOfPressure (1.0f) == 1.0f && pressureOfModY (0.5f) == 0.0f && pressureOfModY (0.2f) == 0.0f);
    CHECK (mpe190bb::near (pressureOfModY (modYOfPressure (0.37f)), 0.37f, 1.0e-6f));
    CHECK (finePitchOfCents (700.0f) == 120.0f && finePitchOfCents (-700.0f) == -120.0f && finePitchOfCents (35.0f) == 35.0f);
}

TEST (mpe190bb_input_decodes_a_member_channels_notes)
{
    using namespace lynx::mpe;
    using mpe190bb::feed;
    Input in;
    // off: every message is an ordinary one
    CHECK (feed (in, { 0x91, 60, 100 }).e[0].type == InEvent::Type::plain);
    in.setZone ({ true, 15, 48.0f });

    // channel 2: the bend and the timbre come before the note on (as the specification asks), then the note
    CHECK (feed (in, { 0xe1, 0x00, 0x50 }).count == 0); // 10240: +2048 of 8191 → +1200.1 cents; nothing sounds yet
    CHECK (feed (in, { 0xb1, 74, 127 }).count == 0);
    auto on = feed (in, { 0x91, 60, 100 });
    CHECK (on.count == 1 && on.e[0].type == InEvent::Type::noteOn && on.e[0].key == 60 && on.e[0].channel == 1);
    CHECK (mpe190bb::near (on.e[0].velocity, 100.0f / 127.0f) && mpe190bb::near (on.e[0].cents, 1200.15f, 0.1f));
    CHECK (on.e[0].timbre == 1.0f && on.e[0].pressure == 0.0f);

    // channel 3: another note with nothing sent before it: centre values
    on = feed (in, { 0x92, 64, 80 });
    CHECK (on.count == 1 && on.e[0].type == InEvent::Type::noteOn && on.e[0].cents == 0.0f && on.e[0].timbre == 0.5f);

    // channel 2's pressure and bend move its note only
    auto x = feed (in, { 0xd1, 96 });
    CHECK (x.count == 1 && x.e[0].type == InEvent::Type::expr && x.e[0].expr == Expr::pressure && x.e[0].key == 60
           && mpe190bb::near (x.e[0].value, 96.0f / 127.0f));
    x = feed (in, { 0xd1, 40 }); // (the pressure falls again: the peak stays for the note's Mod Y)
    x = feed (in, { 0xe1, 0x00, 0x30 }); // 6144: −2048 / 8192 → −1200 cents
    CHECK (x.count == 1 && x.e[0].expr == Expr::pitch && x.e[0].key == 60 && mpe190bb::near (x.e[0].value, -1200.0f, 0.1f));
    x = feed (in, { 0xb2, 74, 0 });
    CHECK (x.count == 1 && x.e[0].expr == Expr::timbre && x.e[0].key == 64 && x.e[0].value == 0.0f);

    // the master channel and other controllers are ordinary messages
    CHECK (feed (in, { 0xe0, 0x00, 0x60 }).e[0].type == InEvent::Type::plain);
    CHECK (feed (in, { 0x90, 48, 90 }).e[0].type == InEvent::Type::plain);
    CHECK (feed (in, { 0xb1, 1, 50 }).e[0].type == InEvent::Type::plain);
    CHECK (feed (in, { 0xb1, 64, 127 }).e[0].type == InEvent::Type::plain);

    // the note off carries what the note started with and its peak pressure
    auto off = feed (in, { 0x81, 60, 64 });
    CHECK (off.count == 1 && off.e[0].type == InEvent::Type::noteOff && off.e[0].key == 60);
    CHECK (mpe190bb::near (off.e[0].cents, 1200.15f, 0.1f) && off.e[0].timbre == 1.0f && mpe190bb::near (off.e[0].pressure, 96.0f / 127.0f)
           && mpe190bb::near (off.e[0].velocity, 64.0f / 127.0f));
    off = feed (in, { 0x92, 64, 0 }); // (a note on with velocity 0 is a note off)
    CHECK (off.count == 1 && off.e[0].type == InEvent::Type::noteOff && off.e[0].key == 64 && off.e[0].timbre == 0.5f);
    CHECK (feed (in, { 0xd1, 100 }).count == 0); // nothing sounds on the channel any more

    // two notes on one channel share its bend; polyphonic key pressure is one key's
    feed (in, { 0xe4, 0x00, 0x40 });
    feed (in, { 0x94, 50, 100 });
    feed (in, { 0x94, 53, 100 });
    x = feed (in, { 0xe4, 0x7f, 0x7f });
    CHECK (x.count == 2 && x.e[0].key == 50 && x.e[1].key == 53 && mpe190bb::near (x.e[1].value, 4800.0f, 0.1f));
    x = feed (in, { 0xa4, 53, 127 });
    CHECK (x.count == 1 && x.e[0].key == 53 && x.e[0].expr == Expr::pressure && x.e[0].value == 1.0f);
    CHECK (feed (in, { 0x84, 50, 0 }).e[0].pressure == 0.0f && feed (in, { 0x84, 53, 0 }).e[0].pressure == 1.0f);
}

TEST (mpe190bb_input_follows_the_zone_configuration)
{
    using namespace lynx::mpe;
    using mpe190bb::feed;
    Input in;
    in.setZone ({ true, 15, 48.0f });
    // the per-note bend range (RPN 0 on a member channel): 24 semitones
    CHECK (feed (in, { 0xb1, 101, 0 }).count == 0 && feed (in, { 0xb1, 100, 0 }).count == 0 && feed (in, { 0xb1, 6, 24 }).count == 0
           && feed (in, { 0xb1, 38, 0 }).count == 0);
    CHECK (in.currentZone().bendRange == 24.0f);
    feed (in, { 0xe3, 0x7f, 0x7f });
    CHECK (mpe190bb::near (feed (in, { 0x93, 60, 100 }).e[0].cents, 2400.0f, 0.1f));
    // the MPE Configuration Message on the master channel: 3 member channels (2 … 4)
    feed (in, { 0xb0, 101, 0 });
    CHECK (feed (in, { 0xb0, 100, 6 }).count == 0 && feed (in, { 0xb0, 6, 3 }).count == 0);
    CHECK (in.currentZone().members == 3);
    CHECK (feed (in, { 0x93, 62, 100 }).e[0].type == InEvent::Type::noteOn);
    CHECK (feed (in, { 0x94, 62, 100 }).e[0].type == InEvent::Type::plain); // channel 5 is outside the zone now
    CHECK (feed (in, { 0xe4, 0, 0 }).e[0].type == InEvent::Type::plain);
    // the settings clamp
    in.setZone ({ true, 40, 500.0f });
    CHECK (in.currentZone().members == 15 && in.currentZone().bendRange == 96.0f);
    // malformed input never reads past what it was given
    std::uint8_t one[1] = { 0x91 };
    InEvent e[Input::maxEvents];
    CHECK (in.feed (one, 1, e) == 1 && e[0].type == InEvent::Type::plain);
    std::uint8_t sys[3] = { 0xf8, 0, 0 };
    CHECK (in.feed (sys, 1, e) == 1 && e[0].type == InEvent::Type::plain);
}

TEST (mpe190bb_allocator_gives_each_note_its_channel)
{
    using namespace lynx::mpe;
    Allocator a;
    std::set<int> used;
    for (int k = 0; k < 15; ++k)
        used.insert (a.noteOn (40 + k));
    CHECK (used.size() == 15 && *used.begin() == 1 && *used.rbegin() == 15); // never the master channel (0)
    const auto shared = a.noteOn (70); // a 16th note shares a channel
    CHECK (shared >= 1 && shared <= 15 && a.sounding (shared) == 2);
    CHECK (a.noteOff (70) == shared && a.sounding (shared) == 1);
    // a note off goes to the channel its note on took; the channel released longest ago is taken first
    const auto c45 = a.channelOf (45), c41 = a.channelOf (41);
    CHECK (a.noteOff (45) == c45 && a.noteOff (41) == c41 && a.channelOf (45) == -1 && a.noteOff (45) == -1);
    CHECK (a.noteOn (80) == c45 && a.noteOn (81) == c41);
    // the same key twice: two channels, released oldest first
    a.reset();
    const auto first = a.noteOn (60), second = a.noteOn (60);
    CHECK (first != second && a.channelOf (60) == second && a.noteOff (60) == first && a.noteOff (60) == second);
    a.setMembers (2);
    a.reset();
    CHECK (a.noteOn (1) == 1 && a.noteOn (2) == 2 && a.noteOn (3) == 1);
}

TEST (mpe190bb_member_channel_messages_as_note_expressions)
{
    using namespace lynx::mpe;
    NoteExpression x;
    const std::uint8_t bend[3] = { 0xe2, 0x7f, 0x7f }, timbre[3] = { 0xb3, 74, 127 }, pressure[2] = { 0xd4, 64 };
    CHECK (toNoteExpression (bend, 3, 48.0f, x) && x.expr == Expr::pitch && x.channel == 2 && std::abs (x.value - 48.0) < 1.0e-3);
    CHECK (toNoteExpression (timbre, 3, 48.0f, x) && x.expr == Expr::timbre && x.channel == 3 && x.value == 1.0);
    CHECK (toNoteExpression (pressure, 2, 48.0f, x) && x.expr == Expr::pressure && x.channel == 4 && std::abs (x.value - 64.0 / 127.0) < 1.0e-9);
    // the master channel's and everything else stay MIDI
    const std::uint8_t master[3] = { 0xe0, 0, 0x40 }, other[3] = { 0xb2, 1, 5 }, note[3] = { 0x92, 60, 100 };
    CHECK (! toNoteExpression (master, 3, 48.0f, x) && ! toNoteExpression (other, 3, 48.0f, x) && ! toNoteExpression (note, 3, 48.0f, x));
    // the zone's setup: the configuration message, then the bend range on each member channel
    std::vector<std::array<int, 3>> sent;
    zoneSetup (15, 48, [&sent] (int s, int a, int b) { sent.push_back ({ s, a, b }); });
    CHECK (sent.size() == 3 + 15 * 4);
    CHECK ((sent[0] == std::array<int, 3> { 0xb0, 101, 0 }) && (sent[1] == std::array<int, 3> { 0xb0, 100, 6 }) && (sent[2] == std::array<int, 3> { 0xb0, 6, 15 }));
    CHECK ((sent[5] == std::array<int, 3> { 0xb1, 6, 48 }) && (sent.back() == std::array<int, 3> { 0xbf, 38, 0 }));
    // … which an MPE input reads back
    Input in;
    in.setZone ({ true, 4, 12.0f });
    mpe190bb::Fed f;
    for (const auto& m : sent)
        f = mpe190bb::feed (in, { m[0], m[1], m[2] });
    CHECK (in.currentZone().members == 15 && in.currentZone().bendRange == 48.0f);
}

TEST (mpe190bb_recorded_notes_keep_their_expression)
{
    auto song = lynx::Song::makeDefault();
    const auto loop = song.patterns[0].effectiveLength (lynx::Meter (4, 4));
    lynx::NoteRecorder r;
    r.begin (loop);
    // an MPE note: the "on" brings its bend and timbre, the "off" its peak pressure
    r.add (song, { 100.0, 0, 60, 0.5f, true, true, 35.0f, 0.9f, 0.5f });
    r.add (song, { 580.0, 0, 60, 0.0f, false, true, 35.0f, 0.9f, 0.8f });
    // a note bent past the piano roll's fine pitch range, held until the take ends
    r.add (song, { 600.0, 0, 62, 0.5f, true, true, 700.0f, 0.5f, 0.5f });
    // an ordinary note
    r.add (song, { 700.0, 0, 64, 0.7f, true });
    r.add (song, { 900.0, 0, 64, 0.0f, false });
    r.end (song, 1000.0);
    const auto notes = r.takeFinished();
    CHECK (notes.size() == 3);
    const lynx::Note plain;
    for (const auto& n : notes)
    {
        if (n.key == 60)
            CHECK (n.finePitch == 35.0f && n.modX == 0.9f && n.modY == 0.8f && n.start == 100 && n.length == 480);
        if (n.key == 62)
            CHECK (n.finePitch == 120.0f && n.modX == 0.5f && n.modY == 0.5f);
        if (n.key == 64) // as before: the defaults
            CHECK (n.finePitch == plain.finePitch && n.modX == plain.modX && n.modY == plain.modY && n.velocity == 0.7f);
        CHECK (n.pan == plain.pan && ! n.slide && n.release == plain.release);
    }
}

TEST (mpe190bb_device_setting_round_trip)
{
    using namespace lynx::prefs;
    MidiPrefs p;
    const auto before = p.toText();
    CHECK (! p.input ("Seaboard").mpe && p.input ("Seaboard").mpeMembers == 15 && p.input ("Seaboard").mpeBendRange == 48);
    auto d = p.input ("Seaboard | RISE=49");
    d.mpe = true;
    d.mpeMembers = 10;
    d.mpeBendRange = 24;
    p.setInput (d);
    const auto text = p.toText();
    CHECK (text.find ("inputmpe=10|24|Seaboard | RISE=49\n") != std::string::npos);
    const auto back = MidiPrefs::fromText (text);
    CHECK (back == p && back.input ("Seaboard | RISE=49").mpe && back.input ("Seaboard | RISE=49").mpeMembers == 10);
    // an older settings file (no such line) and a device without MPE: as before
    CHECK (! MidiPrefs::fromText ("input=1|3|-1|Keys\n").input ("Keys").mpe);
    CHECK (MidiPrefs::fromText ("input=1|3|-1|Keys\n").toText() == MidiPrefs::fromText ("input=1|3|-1|Keys\n").toText()
           && MidiPrefs::fromText ("input=1|3|-1|Keys\n").toText().find ("inputmpe") == std::string::npos);
    CHECK (MidiPrefs::fromText (before).toText() == before && before.find ("inputmpe") == std::string::npos);
    // the line alone (no "input" line before it) and junk values
    const auto alone = MidiPrefs::fromText ("inputmpe=99|0|Pad\ninputmpe=junk\n");
    CHECK (alone.input ("Pad").mpe && alone.input ("Pad").enabled && alone.input ("Pad").mpeMembers == 15 && alone.input ("Pad").mpeBendRange == 1);
    // the wrapper's switch: off by default, saved with the plugin
    lynx::PluginRef ref, other;
    CHECK (! ref.mpe);
    other.mpe = true;
    CHECK (! (ref == other));
}
