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
    for (const auto& note : { expressive, inactiveChance, tiny, Note {} })
    {
        const auto value = remote_notes::toValue (note);
        check (value.getDynamicObject()->getProperties().size() == 19, "Note read omitted an expression field");
        check (remote_notes::fromValue (value, 1) == note, "Note codec changed native expression values");
        // Use the socket writer's JSON precision, rather than only testing an in-memory var.
        const auto wire = json (juce::JSON::toString (value, true).toRawUTF8());
        check (remote_notes::fromValue (wire, 1) == note, "Note read/replace JSON roundtrip lost expression data");
    }

    const auto complete = remote_notes::toValue (expressive);
    for (const auto* field : { "start", "length", "channel", "key", "velocity", "pan", "muted", "finePitch",
                              "slide", "porta", "release", "modX", "modY", "colour", "group", "chance",
                              "velocityRange", "chanceGroup", "chanceOne" })
    {
        auto missing = complete.clone();
        missing.getDynamicObject()->removeProperty (field);
        refuses ([&] { remote_notes::fromValue (missing, 1); });
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
