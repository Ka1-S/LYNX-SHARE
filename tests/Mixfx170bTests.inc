// Round 170b (Busy Works' beat video, macOS): FL's mixer multi-selection routing, FL's effect slot places (move up /
// down, the wheel, into empty slots), the trackpad's wheel steps over a slot, and the plugin menus' categories.

#include "core/LowLatency.h"
#include "core/MixerOps.h"
#include "core/PluginCatalog.h"
#include "core/ValueMath.h"

namespace
{
lynx::Song mixfx170bSong (int inserts)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (inserts);
    return song;
}

lynx::EffectSlot mixfx170bFx (lynx::EffectKind kind)
{
    lynx::EffectSlot s;
    s.kind = kind;
    return s;
}
} // namespace

TEST (mixfx170b_route_arrow_acts_on_every_selected_track)
{
    namespace mo = lynx::mixerops;
    auto song = mixfx170bSong (5);
    // FL's plain click on the arrow under insert 4 with inserts 1, 2, 3 selected: all three route to 4
    CHECK (mo::switchSelectedRoute (song, { 1, 2, 3 }, 4, true) == 3);
    for (int t : { 1, 2, 3 })
        CHECK (song.routes (t, 4));
    CHECK (song.routes (1, 0)); // (a plain route keeps the Master)
    // the same click again switches them all off
    CHECK (mo::switchSelectedRoute (song, { 1, 2, 3 }, 4, false) == 3);
    for (int t : { 1, 2, 3 })
        CHECK (! song.routes (t, 4));
    // the target itself in the selection is skipped, a cycle is skipped
    song.setRoute (4, 2, true);
    CHECK (mo::switchSelectedRoute (song, { 1, 2, 4 }, 4, true) == 1); // 2 -> 4 would close 4 -> 2 -> 4
    CHECK (song.routes (1, 4) && ! song.routes (2, 4));
    // Ctrl+click = "Route to this track only" for every selected track: each loses its other routes (Master too)
    auto only = mixfx170bSong (5);
    CHECK (mo::routeSelectedTo (only, { 1, 2, 3 }, 5, mo::RouteKind::routeOnly) == 3);
    for (int t : { 1, 2, 3 })
    {
        CHECK (only.routes (t, 5));
        CHECK (! only.routes (t, 0));
    }
}

TEST (mixfx170b_effect_rows_follow_fl_slot_places)
{
    namespace mo = lynx::mixerops;
    using K = lynx::EffectKind;
    auto song = mixfx170bSong (2);
    auto& fx = song.mixer[1].effects;
    fx.push_back (mixfx170bFx (K::compressor));
    // an old project: packed rows
    CHECK ((mo::effectRows (fx) == std::vector<int> { 0 }));
    // Busy Works' case: the only effect, the compressor in slot 1, moves down into the empty slot 2
    CHECK (mo::moveEffectBy (song, 1, 0, 1) == 0);
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 1 }));
    CHECK (mo::effectAtRow (song.mixer[1].effects, 0) == -1 && mo::effectAtRow (song.mixer[1].effects, 1) == 0);
    // a new effect picked on the empty slot 1 lands there, before the compressor in the chain
    CHECK (mo::insertEffectAtRow (song, 1, mixfx170bFx (K::eq), 0) == 0);
    CHECK (song.mixer[1].effects[0].kind == K::eq && song.mixer[1].effects[1].kind == K::compressor);
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 0, 1 }));
    CHECK (song.mixer[1].effects[0].row == -1 && song.mixer[1].effects[1].row == -1); // (packed again: saved as before)
    // a taken slot refuses an insert
    CHECK (mo::insertEffectAtRow (song, 1, mixfx170bFx (K::delay), 1) == -1);
    // FL wraps: slot 1 up = slot 10
    CHECK (mo::moveEffectBy (song, 1, 0, -1) == 1);
    CHECK (song.mixer[1].effects[0].kind == K::compressor && song.mixer[1].effects[1].kind == K::eq);
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 1, 9 }));
    // and back down from slot 10 to slot 1
    CHECK (mo::moveEffectBy (song, 1, 1, 1) == 0);
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 0, 1 }));
}

TEST (mixfx170b_moving_onto_an_effect_swaps_and_automations_follow)
{
    namespace mo = lynx::mixerops;
    using K = lynx::EffectKind;
    auto song = mixfx170bSong (2);
    for (auto k : { K::eq, K::delay, K::reverb })
        song.mixer[1].effects.push_back (mixfx170bFx (k));
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::effectParam, 1, 0, 3 }; // the EQ's parameter 3
    song.automations = { a };
    // the EQ dragged onto the reverb's slot 3: the two swap places (FL), the delay stays in slot 2
    CHECK (mo::moveEffectToRow (song, 1, 0, 2));
    CHECK (song.mixer[1].effects[0].kind == K::reverb && song.mixer[1].effects[1].kind == K::delay && song.mixer[1].effects[2].kind == K::eq);
    CHECK (song.automations[0].target.slot == 2);
    // the EQ to the empty slot 6: the others keep their slots
    CHECK (mo::moveEffectToRow (song, 1, 2, 5));
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 0, 1, 5 }));
    CHECK (song.automations[0].target.slot == 2);
    // deleting the delay leaves slot 2 empty (FL); the EQ stays in slot 6
    mo::removeEffectKeepRows (song, 1, 1);
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 0, 5 }));
    CHECK (song.automations[0].target.slot == 1);
    // the index-based move (Browser, tests) keeps the rows in use
    CHECK (mo::moveEffectSlot (song, 1, 1, 0));
    CHECK (song.mixer[1].effects[0].kind == K::eq);
    CHECK ((mo::effectRows (song.mixer[1].effects) == std::vector<int> { 0, 5 }));
    // nothing moves onto its own slot or past the 10 slots
    CHECK (! mo::moveEffectToRow (song, 1, 0, 0));
    CHECK (! mo::moveEffectToRow (song, 1, 0, lynx::maxEffectSlots));
}

TEST (mixfx170b_effect_rows_stay_in_the_ten_slots)
{
    namespace mo = lynx::mixerops;
    std::vector<lynx::EffectSlot> fx (3);
    fx[0].row = 4;
    fx[1].row = 2; // (out of order in a file: it follows the previous one)
    fx[2].row = -1;
    CHECK ((mo::effectRows (fx) == std::vector<int> { 4, 5, 6 }));
    fx[0].row = lynx::maxEffectSlots - 1;
    CHECK ((mo::effectRows (fx) == std::vector<int> { lynx::maxEffectSlots - 3, lynx::maxEffectSlots - 2, lynx::maxEffectSlots - 1 })); // (pulled up to fit)
    std::vector<lynx::EffectSlot> full (lynx::maxEffectSlots);
    const auto rows = mo::effectRows (full);
    for (int i = 0; i < lynx::maxEffectSlots; ++i)
        CHECK (rows[(std::size_t) i] == i);
}

TEST (mixfx170b_trackpad_wheel_steps_add_up_and_ignore_inertia)
{
    using lynx::controls::slotWheelSteps;
    double acc = 0.0;
    // a mouse wheel: one notch, one slot, whatever its size
    CHECK (slotWheelSteps (acc, 0.23, false, false) == 1);
    CHECK (slotWheelSteps (acc, -0.05, false, false) == -1);
    // a trackpad: many small deltas make one step, its glide nothing
    int steps = 0;
    for (int i = 0; i < 10; ++i)
        steps += slotWheelSteps (acc, 0.04, true, false);
    CHECK (steps == 1);
    for (int i = 0; i < 50; ++i)
        steps += slotWheelSteps (acc, 0.2, true, true);
    CHECK (steps == 1);
    // a change of direction starts again
    CHECK (slotWheelSteps (acc, -0.2, true, false) == 0);
    CHECK (slotWheelSteps (acc, -0.2, true, false) == -1);
}

TEST (mixfx170b_plugin_menus_group_by_fl_category_lynx_first)
{
    namespace pc = lynx::plugincatalog;
    // FL Studio 2026's Plugin database folders
    CHECK (pc::flCategory ("Fruity Compressor", false) == "Dynamics");
    CHECK (pc::flCategory ("Fruity Limiter", false) == "Dynamics");
    CHECK (pc::flCategory ("Reeverb 2", false) == "Delay reverb"); // (LYNX's name, without "Fruity ")
    CHECK (pc::flCategory ("Peak Controller", false) == "Controller");
    CHECK (pc::flCategory ("Wave Candy", false) == "Visual");
    CHECK (pc::flCategory ("EQUO", false) == "Filter");
    CHECK (pc::flCategory ("Sytrus", true) == "Synth classic");
    CHECK (pc::flCategory ("Fruity DrumSynth Live", true) == "Drum");
    CHECK (pc::flCategory ("Fruity Compressor", true).empty()); // (not a generator)
    // LYNX's own generic Compressor is not FL's Fruity Compressor
    CHECK (pc::flCategory ("Compressor", false, false).empty());
    CHECK (pc::flCategory ("Compressor", false, true) == "Dynamics");

    pc::Folders user;
    pc::addFolder (user, false, "My chain");
    pc::moveToFolder (user, false, "lynx:fx:Fruity Limiter", "My chain");
    const std::vector<pc::BuiltinPlugin> builtins {
        { "lynx:fx:Compressor", "Compressor", false, false },
        { "lynx:fx:Halo", "Halo", false, false },
        { "lynx:fx:Fruity Compressor", "Fruity Compressor", true, true },
        { "lynx:fx:Fruity Limiter", "Fruity Limiter", true, true },
        { "lynx:fx:Reeverb 2", "Reeverb 2", true, true },
        { "lynx:fx:Oddity", "Oddity", true, true },
    };
    const auto folders = pc::withDefaultCategories (user, false, builtins);
    CHECK (folders.of.at ("lynx:fx:Compressor") == pc::lynxCategory);
    CHECK (folders.of.at ("lynx:fx:Halo") == pc::lynxCategory);
    CHECK (folders.of.at ("lynx:fx:Fruity Compressor") == "Dynamics");
    CHECK (folders.of.at ("lynx:fx:Fruity Limiter") == "My chain"); // the user's folder wins
    CHECK (folders.of.at ("lynx:fx:Reeverb 2") == "Delay reverb");
    CHECK (folders.of.at ("lynx:fx:Oddity") == "Misc"); // an FL plugin FL files nowhere
    std::vector<std::string> keys;
    for (const auto& b : builtins)
        keys.push_back (b.key);
    keys.push_back ("VST3-Some Installed Plugin"); // (no folder: not in the categories)
    const auto tree = pc::menuTree (folders, false, keys);
    std::vector<std::string> names;
    for (const auto& f : tree)
        names.push_back (f.name);
    // LYNX SHARE first, then A-Z (FL's categories and the user's folders together)
    CHECK ((names == std::vector<std::string> { pc::lynxCategory, "Delay reverb", "Dynamics", "Misc", "My chain" }));
    CHECK (tree[0].items.size() == 2);
    CHECK (pc::allItems (tree).size() == builtins.size());
}

// More mixer slots (PR #260, round 190qf; FL 2026.2: 10 slots, up to 25 with the "+" tab): an insert shows FL's 10 rows,
// grows row by row up to maxEffectSlots, always shows the rows holding effects, and shrinks only over an empty last row;
// Move up / down wraps over the rows shown; FL's "Number of effect slots on new mixer tracks"
TEST (more_mixer_slots_rows_grow_and_shrink)
{
    namespace mo = lynx::mixerops;
    CHECK (lynx::maxEffectSlots == 25 && lynx::flEffectSlots == 10); // (FL 2026.2's cap)
    lynx::Song song;
    song.mixer.resize (2);
    auto& t = song.mixer[1];
    CHECK (mo::effectRowCount (t) == lynx::flEffectSlots);
    CHECK (mo::addEffectRow (song, 1) && mo::effectRowCount (t) == lynx::flEffectSlots + 1 && t.slotRows == 11);
    CHECK (mo::addEffectRow (song, 1) && mo::effectRowCount (t) == lynx::flEffectSlots + 2);
    // an effect in the last row keeps it
    CHECK (mo::insertEffectAtRow (song, 1, lynx::EffectSlot {}, lynx::flEffectSlots + 1) == 0);
    CHECK (! mo::removeEffectRow (song, 1));
    // Move down from the last row wraps to the first, Move up from it to the last row shown
    CHECK (mo::moveEffectBy (song, 1, 0, +1) == 0 && mo::effectRows (t.effects)[0] == 0);
    CHECK (mo::moveEffectBy (song, 1, 0, -1) == 0 && mo::effectRows (t.effects)[0] == lynx::flEffectSlots + 1);
    mo::removeEffectKeepRows (song, 1, 0);
    CHECK (mo::removeEffectRow (song, 1) && mo::effectRowCount (t) == lynx::flEffectSlots + 1);
    CHECK (mo::removeEffectRow (song, 1) && mo::effectRowCount (t) == lynx::flEffectSlots && t.slotRows == 0); // (10 = the default, 0)
    // LYNX's own Remove slot goes below 10 over empty rows, never below 1
    CHECK (mo::removeEffectRow (song, 1) && mo::effectRowCount (t) == 9 && t.slotRows == 9);
    while (mo::removeEffectRow (song, 1)) {}
    CHECK (mo::effectRowCount (t) == 1 && t.slotRows == 1);
    CHECK (mo::addEffectRow (song, 1) && mo::effectRowCount (t) == 2);
    t.slotRows = 0;
    // an effect further down than the rows asked for shows its row; the cap holds
    CHECK (mo::insertEffectAtRow (song, 1, lynx::EffectSlot {}, 20) >= 0 && mo::effectRowCount (t) == 21);
    while (mo::addEffectRow (song, 1)) {}
    CHECK (mo::effectRowCount (t) == lynx::maxEffectSlots && t.slotRows == lynx::maxEffectSlots);
    CHECK (mo::insertEffectAtRow (song, 1, lynx::EffectSlot {}, lynx::maxEffectSlots) < 0);
    CHECK (mo::insertEffectAtRow (song, 1, lynx::EffectSlot {}, lynx::maxEffectSlots - 1) >= 0);
    CHECK (mo::effectRows (t.effects).back() == lynx::maxEffectSlots - 1);
    // the low-latency mask holds every slot
    CHECK (lynx::lowlatency::bypassed ({ lynx::lowlatency::SlotMask { 1u } << (lynx::maxEffectSlots - 1) }, 0, lynx::maxEffectSlots - 1));
    CHECK (! lynx::lowlatency::bypassed ({ ~lynx::lowlatency::SlotMask { 0 } }, 0, lynx::maxEffectSlots));

    // FL 2026.2's "Number of effect slots on new mixer tracks": the inserts the user adds get it, 10 = nothing saved
    const auto keep = mo::newTrackSlots();
    mo::newTrackSlots() = 16;
    CHECK (mo::insertTracks (song, 2, 2) == 2 && song.mixer[2].slotRows == 16 && mo::effectRowCount (song.mixer[3]) == 16);
    mo::newTrackSlots() = 5;
    CHECK (mo::insertTracks (song, 4, 1) == 1 && mo::effectRowCount (song.mixer[4]) == 5);
    mo::newTrackSlots() = 10;
    CHECK (mo::insertTracks (song, 5, 1) == 1 && song.mixer[5].slotRows == 0);
    mo::newTrackSlots() = 99;
    CHECK (mo::insertTracks (song, 6, 1) == 1 && song.mixer[6].slotRows == lynx::maxEffectSlots);
    mo::newTrackSlots() = keep;
}
