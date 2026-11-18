// --- Round 190lo (UI half): the polyphonic note editor's analyses prepared ahead of time ("Prepare edited clips when a
// project opens", a LYNX extra) and the note-assignment comparison the editor follows undo with --------------------------

TEST (polyedit190lo_prepared_analysis_store)
{
    using namespace lynx;
    const auto source = pe190lk::chord ({ 60.0, 64.0, 67.0 });
    auto m = pe190lk::cMajor();
    const auto e4 = pe190lk::noteNear (m, 64.0);
    CHECK (e4 >= 0);
    if (e4 < 0)
        return;
    m.edits[e4].semitones = -1.0f;
    polyedit::clearPrepared();
    std::shared_ptr<polyphonic::Analysis> a = polyedit::toAnalysis (m);
    CHECK (polyphonic::maskBytes (*a) == 0);
    CHECK (polyedit::prepareMasks (*source, m, *a));
    CHECK (polyphonic::maskBytes (*a) > 0); // (the edited span's masks are built)
    // a render through the prepared object = a render from scratch, sample for sample
    const auto fresh = polyedit::render (*source, m);
    const auto prepared = polyedit::render (*source, m, a.get());
    CHECK (fresh != nullptr && prepared != nullptr && fresh->left == prepared->left && fresh->right == prepared->right);
    // kept and taken once, for the same notes only
    polyedit::keepPrepared ("chord.wav", m, a);
    CHECK (polyedit::preparedCount() == 1);
    auto other = m;
    other.notes[(std::size_t) e4].active = false; // (another note assignment: other masks)
    CHECK (polyedit::takePrepared ("chord.wav", other) == nullptr);
    CHECK (polyedit::preparedCount() == 0);
    polyedit::keepPrepared ("chord.wav", m, a);
    CHECK (polyedit::takePrepared ("other.wav", m) == nullptr && polyedit::preparedCount() == 1);
    CHECK (polyedit::takePrepared ("chord.wav", m) == a && polyedit::preparedCount() == 0);
    // bounded: at most 4 objects
    for (int k = 0; k < 6; ++k)
        polyedit::keepPrepared ("clip" + std::to_string (k), m, polyedit::toAnalysis (m));
    CHECK (polyedit::preparedCount() == 4);
    CHECK (polyedit::takePrepared ("clip5", m) != nullptr && polyedit::takePrepared ("clip0", m) == nullptr);
    polyedit::clearPrepared();
    CHECK (polyedit::preparedCount() == 0);
    // an unedited model needs nothing
    auto plain = pe190lk::cMajor();
    auto b = polyedit::toAnalysis (plain);
    CHECK (polyedit::prepareMasks (*source, plain, *b) && polyphonic::maskBytes (*b) == 0);
}

TEST (polyedit190lo_same_notes_ignores_assignment)
{
    using namespace lynx;
    const auto& m = pe190lk::cMajor();
    auto other = m;
    CHECK (polyedit::sameNotes (m, other));
    if (! other.notes.empty())
    {
        other.notes[0].active = ! other.notes[0].active;
        other.edits[0].gainDb = -3.0f;
        CHECK (polyedit::sameNotes (m, other)); // (an assignment and an edit: still the same analysis)
        other.notes[0].f0[0] += 1.0f;
        CHECK (! polyedit::sameNotes (m, other));
    }
    other = m;
    other.notes.pop_back();
    CHECK (! polyedit::sameNotes (m, other));
}
