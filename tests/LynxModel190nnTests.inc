// Round 190nn: LYNX Model quality round 4 (tubular bells and kalimba). The S-shaped whole-note onset (LPM header byte 14
// = 2): (1 - e^(-t / rise))^2, a quiet first moment and then the rise, as a pluck or a strike starts in the recordings.

TEST (lynxmodel190nn_s_onset)
{
    using namespace lynxmodel190nm;
    const std::vector<M> modes { { 1.0f, 0, 0x4000 }, { 2.76f, 0, 0x3c00 }, { 5.4f, 0, 0x3800 } };
    const auto one = table (modes, 1, 2.0f), two = table (modes, 2, 2.0f), bad = table (modes, 3, 2.0f);
    const auto t1 = lm::parseTable (one.data(), one.size()), t2 = lm::parseTable (two.data(), two.size()),
               t3 = lm::parseTable (bad.data(), bad.size());
    CHECK (t1.has_value() && t2.has_value() && t3.has_value());
    if (! t1 || ! t2 || ! t3)
        return;
    CHECK (t1->onset == 1 && t2->onset == 2 && t3->onset == 0); // (an unknown law reads as none)
    const auto a = render (*t1), b = render (*t2);
    // the first 0.5 ms: the S-shaped onset far quieter than the exponential one (its gain rises with t^2, not t)
    double e0a = 0.0, e0b = 0.0, lateDiff = 0.0, late = 0.0;
    for (int i = 0; i < 24; ++i)
    {
        e0a += (double) a[(std::size_t) (2 * i)] * a[(std::size_t) (2 * i)];
        e0b += (double) b[(std::size_t) (2 * i)] * b[(std::size_t) (2 * i)];
    }
    CHECK (e0a > 0.0 && e0b < e0a * 0.25);
    // after 40 ms (20 time constants) both play the same note
    for (int i = 1920; i < 4800; ++i)
    {
        lateDiff = std::max (lateDiff, (double) std::abs (a[(std::size_t) (2 * i)] - b[(std::size_t) (2 * i)]));
        late = std::max (late, (double) std::abs (a[(std::size_t) (2 * i)]));
    }
    CHECK (late > 0.0 && lateDiff < late * 1.0e-5);
    // no lanes go to build-up twins
    lm::Engine e;
    e.prepare (48000.0);
    e.setTable (&*t2);
    e.note (60, 1.0f);
    CHECK (e.modesOfVoice (0) == 3);
}

TEST (lynxmodel190nn_tables)
{
    // what round 190nn ships (HANDOFF 190nn): the kalimba refitted with the S-shaped onset and the lines under a high
    // tine (the lower tines and the body, from 60 Hz up; absolute: kept in Hz); the other five keep 190nm's tables
    const auto& kal = lynx::lm::builtInTable (lynx::lm::kalimba);
    CHECK (kal.onset == 2 && kal.fade == 1);
    int low = 0;
    for (const auto& a : kal.anchors)
        for (int k = 0; k < a.count; ++k)
        {
            const auto& m = kal.modes[(std::size_t) (a.first + k)];
            low += m.family == lynx::lm::absoluteFamily && m.ratio < 0.3f && a.key >= 79.0f ? 1 : 0;
        }
    CHECK (low >= 4);
    for (int m = 0; m < lynx::lm::kalimba; ++m)
        CHECK (lynx::lm::builtInTable (m).onset != 2);
}
