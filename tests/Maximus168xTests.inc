// Round 168x (issue #40, Carlrune20, CachyOS): "I cannot create new points or move points in the In/Out volume graph
// editor" of Maximus. The graph was a picture: it never took the mouse and nothing could write a curve back. Now it
// takes FL's envelope-editor gestures and every edit is written into the band's envelope in FL's own layout.
// Evidence: docs/analise/REA_rodada168x.md §1. Included from tests.cpp (after maximusTestState).

namespace
{
std::vector<std::uint8_t> mx168Slot()
{
    using namespace lynx;
    std::array<std::array<int, 13>, 4> bands {};
    for (auto& b : bands)
        b = { 0, 1000, 1000, 0, 0, 1000, 1115, 0, 2, 5000, 3, 2882, 0 };
    std::array<std::vector<std::array<double, 4>>, 8> envs;
    for (auto& e : envs)
        e = { { 0.0, 0.0, 0.0, 0 }, { 0.5, 0.5, 0.0, 0 }, { 0.5, 0.5, 0.0, 0 } }; // FL's default: identity up to 0 dB, then flat
    return maximusfx::pack ({ hlfx::MaximusBuild::shared, -1, maximusTestState ({ 19800, 1, 48065, 1, 1115, 1000, 32768 }, bands, envs, 0) });
}

// the RMS of a sine at `amp` after `slot`'s Maximus (the last second of two)
double mx168Rms (const std::vector<std::uint8_t>& slot, float amp)
{
    auto fx = lynx::maximusfx::make (slot);
    fx->prepare (44100.0);
    std::vector<float> l (88200), r (88200);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = amp * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) i / 44100.0);
    lynx::EffectParams params {};
    for (std::size_t at = 0; at < l.size(); at += 512)
        fx->process (l.data() + at, r.data() + at, (int) std::min<std::size_t> (512, l.size() - at), params, 120.0);
    double sum = 0.0;
    for (std::size_t i = 44100; i < l.size(); ++i)
        sum += (double) l[i] * l[i];
    return std::sqrt (sum / 44100.0);
}
} // namespace

TEST (mx168_the_in_out_graph_takes_the_mouse)
{
    // the root cause: the plugin window hit-tested only the displays it listed, and "maxcurve" was not one of them
    CHECK (lynx::plugui::displayTakesMouse ("maxcurve"));
    CHECK (lynx::plugui::displayTakesMouse ("waveshaper") && lynx::plugui::displayTakesMouse ("eq"));
    CHECK (! lynx::plugui::displayTakesMouse ("maxbands") && ! lynx::plugui::displayTakesMouse (nullptr));
    bool placed = false;
    for (const auto& item : lynx::plugui::layoutFor (lynx::EffectKind::maximus).items)
        placed = placed || (item.display != nullptr && std::strcmp (item.display, "maxcurve") == 0);
    CHECK (placed);
}

TEST (mx168_curve_edits_are_written_in_fl_layout)
{
    using namespace lynx;
    const auto slot = mx168Slot();
    for (int band = 0; band < 4; ++band)
    {
        const auto pts = maximusfx::curveForEditing (slot, band);
        CHECK (pts.size() == 3 && pts[0].x == 0.0 && pts[1].x == 0.5 && pts[2].x == 1.0 && pts[2].y == 0.5f);
        CHECK (maximusfx::withCurve (slot, band, pts) == slot); // nothing moved: the same bytes
    }

    // FL's "add point" (right-click): a point under the identity at −12 dB in → −6 dB out raises the quiet part
    auto pts = maximusfx::curveForEditing (slot, 3);
    const auto at = maximusfx::addCurvePoint (pts, 0.25, 0.32f);
    CHECK (at == 1 && pts.size() == 4 && pts[1].x == 0.25);
    const auto edited = maximusfx::withCurve (slot, 3, pts);
    CHECK (edited != slot);
    const auto back = maximusfx::curveForEditing (edited, 3);
    CHECK (back.size() == 4 && std::abs (back[1].x - 0.25) < 1e-12 && back[1].y == 0.32f && back[3].x == 1.0);
    CHECK (maximusfx::curveForEditing (edited, 0).size() == 3); // the other bands' curves untouched
    CHECK (maximusfx::withCurve (edited, 3, back) == edited);    // write → read → write is bit-identical
    const auto blob = maximusfx::unpack (edited);
    CHECK (blob && hlfx::makeMaximusPreset (blob->state) != nullptr); // the engine plays the edited state
    CHECK (maximusfx::stateParam (edited, 0).value_or (-1) == 19800);  // the parameters are kept

    // and the sound follows: a quiet sine comes out louder through the master band's raised curve
    const auto before = mx168Rms (slot, 0.05f), after = mx168Rms (edited, 0.05f);
    CHECK (before > 0.0 && after > before * 1.3);

    // "move point" (left-drag): can't pass its neighbours; the ends stay at x = 0 and x = 1
    CHECK (maximusfx::moveCurvePoint (pts, 1, 0.9, 0.4f) == 1 && pts[1].x == 0.5 && pts[1].y == 0.4f);
    maximusfx::moveCurvePoint (pts, 0, 0.3, 0.1f);
    CHECK (pts[0].x == 0.0 && pts[0].y == 0.1f);
    maximusfx::moveCurvePoint (pts, 3, 0.2, 0.7f);
    CHECK (pts[3].x == 1.0 && pts[3].y == 0.7f);

    // "delete point": FL refuses the first one; LYNX keeps the last too
    CHECK (! maximusfx::removeCurvePoint (pts, 0) && ! maximusfx::removeCurvePoint (pts, 3));
    CHECK (maximusfx::removeCurvePoint (pts, 1) && pts.size() == 3);

    // "move tension": the bent segment is what the engine plays (FL's single curve)
    auto bent = maximusfx::curveForEditing (slot, 3);
    bent[1].tension = 0.4f;
    const auto straight = maximusfx::curveShape (maximusfx::curveForEditing (slot, 3), 8);
    const auto curved = maximusfx::curveShape (bent, 8);
    CHECK (straight.size() == 9 && curved.size() == 9 && std::abs (straight[4] - 0.5f) < 1e-6f && std::abs (curved[2] - straight[2]) > 0.01f);
    CHECK (std::abs (curved[0]) < 1e-6f && std::abs (curved[4] - 0.5f) < 1e-6f); // the points themselves don't move
    const auto bentState = maximusfx::withCurve (slot, 3, bent);
    CHECK (maximusfx::curveForEditing (bentState, 3)[1].tension == 0.4f);
    CHECK (maximusfx::curveLevelText (0.5) == "+0.0 dB" && maximusfx::curveLevelText (0.0) == "-inf dB");
}

TEST (mx168_fl_presets_write_back_unchanged)
{
    // every Maximus preset FL ships: read for editing and written back gives the very same state
    if (realFlpDir.empty())
        return;
    const auto dir = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / "Maximus";
    if (! std::filesystem::exists (dir))
        return;
    int n = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir))
    {
        if (entry.path().extension() != ".fst")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto state = lynx::hlfx::maximusStateFromFst (bytes);
        if (! state)
            continue;
        const auto slot = lynx::maximusfx::pack ({ lynx::hlfx::MaximusBuild::shared, -1, *state });
        for (int band = 0; band < 4; ++band)
        {
            const auto pts = lynx::maximusfx::curveForEditing (slot, band);
            if (pts.empty())
                continue;
            ++n;
            CHECK (lynx::maximusfx::withCurve (slot, band, pts) == slot);
        }
    }
    CHECK (n > 20);
}
