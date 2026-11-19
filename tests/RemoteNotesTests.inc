// Exercised through the same codec used by pattern.notes / pattern.replace_notes.
void remoteNotesRoundtrip()
{
    auto expressive = fixture().patterns[0].notes[0];
    expressive.muted = true;
    expressive.slide = true;
    expressive.porta = true;
    expressive.colour = 13;
    expressive.group = 23;
    auto inactiveChance = expressive;
    inactiveChance.chance = 1.0f;
    inactiveChance.velocityRange = 0.0f;
    inactiveChance.chanceGroup = 0;
    inactiveChance.chanceOne = true;
    auto tiny = expressive;
    tiny.velocity = std::numeric_limits<float>::denorm_min();
    tiny.pan = -1.23456788e-20f;
    tiny.chance = 1.23456788e-12f;
    tiny.velocityRange = -1.23456788e-12f;
    auto curved = expressive; // (issue #108: the note's MPE curves, editable through the same codec)
    {
        NoteCurves c;
        c.lane (NoteCurveLane::pitch) = { { 0.0f, -50.0f }, { 120.5f, 1200.0f }, { 480.0f, -4800.0f } };
        c.lane (NoteCurveLane::timbre) = { { 10.0f, 0.25f } };
        c.lane (NoteCurveLane::pressure) = { { 0.0f, 0.0f }, { 240.0f, 1.0f } };
        curved.curves = c;
    }
    auto repeated = expressive; // (issue #208: FL's note repeat, an optional field)
    repeated.repeat = 12;
    for (const auto& note : { expressive, inactiveChance, tiny, Note {}, curved, repeated })
    {
        const auto value = remote_notes::toValue (note);
        check (value.getDynamicObject()->getProperties().size() == 21, "Note read omitted an expression field");
        check (remote_notes::fromValue (value, 1) == note, "Note codec changed native expression values");
        // Use the socket writer's JSON precision, rather than only testing an in-memory var.
        const auto wire = json (juce::JSON::toString (value, true).toRawUTF8());
        check (remote_notes::fromValue (wire, 1) == note, "Note read/replace JSON roundtrip lost expression data");
    }

    const auto complete = remote_notes::toValue (expressive);
    for (const auto* field : { "start", "length", "channel", "key", "velocity", "pan", "muted", "finePitch",
                              "slide", "porta", "release", "modX", "modY", "colour", "group", "chance",
                              "velocityRange", "chanceGroup", "chanceOne", "expression" })
    {
        auto missing = complete.clone();
        missing.getDynamicObject()->removeProperty (field);
        refuses ([&] { remote_notes::fromValue (missing, 1); });
    }
    // a client written before "repeat" may leave it out: the note has no repeat
    {
        auto older = remote_notes::toValue (repeated).clone();
        older.getDynamicObject()->removeProperty ("repeat");
        check (remote_notes::fromValue (older, 1).repeat == 0, "A note without the optional repeat field kept a repeat");
        auto tooFast = remote_notes::toValue (repeated).clone();
        tooFast.getDynamicObject()->setProperty ("repeat", 15);
        refuses ([&] { remote_notes::fromValue (tooFast, 1); });
    }
    auto unknown = complete.clone();
    unknown.getDynamicObject()->setProperty ("futureExpression", 1);
    refuses ([&] { remote_notes::fromValue (unknown, 1); });
    refuses ([&] { remote_notes::fromValue (complete, 0); });
    refuses ([&] { remote_notes::fromValue (juce::Array<juce::var> {}, 1); });

    const auto rejects = [&] (const char* field, const juce::var& value) {
        auto invalid = complete.clone();
        invalid.getDynamicObject()->setProperty (field, value);
        refuses ([&] { remote_notes::fromValue (invalid, 1); });
    };
    for (const auto* field : { "chance", "velocityRange" })
    {
        rejects (field, "0.5");
        rejects (field, false);
        rejects (field, std::numeric_limits<double>::quiet_NaN());
        rejects (field, std::numeric_limits<double>::infinity());
        rejects (field, -std::numeric_limits<double>::infinity());
    }
    {
        // expression: every lane, [tick, value] pairs in range with rising ticks
        const auto curves = [&] (const char* lane, const juce::var& points) {
            auto e = remote_notes::toValue (curved)["expression"].clone();
            e.getDynamicObject()->setProperty (lane, points);
            return e;
        };
        const auto pair = [] (double t, double v) { return juce::var (juce::Array<juce::var> { t, v }); };
        rejects ("expression", 1);
        rejects ("expression", juce::Array<juce::var> {});
        rejects ("expression", curves ("pitch", "0"));
        rejects ("expression", curves ("pitch", juce::Array<juce::var> { pair (0, 4800.5) }));
        rejects ("expression", curves ("timbre", juce::Array<juce::var> { pair (0, 1.5) }));
        rejects ("expression", curves ("pressure", juce::Array<juce::var> { pair (-1, 0.5) }));
        rejects ("expression", curves ("pressure", juce::Array<juce::var> { pair (10, 0.5), pair (10, 0.6) }));
        rejects ("expression", curves ("pressure", juce::Array<juce::var> { juce::Array<juce::var> { 1.0 } }));
        auto extraLane = remote_notes::toValue (curved)["expression"].clone();
        extraLane.getDynamicObject()->setProperty ("slide", juce::Array<juce::var> {});
        rejects ("expression", extraLane);
        auto noLane = remote_notes::toValue (curved)["expression"].clone();
        noLane.getDynamicObject()->removeProperty ("timbre");
        rejects ("expression", noLane);
        const auto parsed = remote_notes::fromValue (remote_notes::toValue (curved), 1);
        check (parsed.curves == curved.curves && parsed.curves->lane (NoteCurveLane::pitch).size() == 3, "Note curves were not retained");
    }
    rejects ("chance", -0.001);
    rejects ("chance", 1.001);
    rejects ("velocityRange", -1.001);
    rejects ("velocityRange", 1.001);
    rejects ("chanceGroup", -1);
    rejects ("chanceGroup", 1.5);
    rejects ("chanceGroup", "7");
    rejects ("chanceGroup", true);
    rejects ("chanceGroup", (juce::int64) std::numeric_limits<int>::max() + 1);
    rejects ("chanceOne", 1);
    rejects ("chanceOne", "true");
    rejects ("length", 0);
    rejects ("start", 1000000000);
    rejects ("channel", 1);

    for (const auto probability : { 0.0, 1.0 })
        for (const auto range : { -1.0, 1.0 })
            for (const auto group : { 0, std::numeric_limits<int>::max() })
                for (const auto one : { false, true })
                {
                    auto edge = complete.clone();
                    auto* object = edge.getDynamicObject();
                    object->setProperty ("chance", probability);
                    object->setProperty ("velocityRange", range);
                    object->setProperty ("chanceGroup", group);
                    object->setProperty ("chanceOne", one);
                    const auto parsed = remote_notes::fromValue (edge, 1);
                    check (parsed.chance == (float) probability && parsed.velocityRange == (float) range
                           && parsed.chanceGroup == group && parsed.chanceOne == one,
                           "Note probability/range/group boundary was not retained");
                }
}
