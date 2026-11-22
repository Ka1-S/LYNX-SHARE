    // --selftest autolinks190ov (round 190ov): FL automation clips linked to several controls in the .lynx file and in the
    // copy for 0.1.30. A clip with two target links (the second through a control scale, FL's import writes one where LYNX
    // counts a control differently) and a plain clip of the first link's control: the file keeps the scale exactly (a sixth
    // element of the link); the 0.1.30 copy drops it with one line naming the clip (0.1.30 plays links 1:1 and lets a link
    // win over the other clips of its control); a project with LYNX's own "Crossfade with" links only gets no such line.
    // Included in the app class of Main.cpp.
    juce::String runAutoLinks190ovCheck()
    {
        auto song = lynx::Song::makeDefault();
        if (song.mixer.size() < 3)
            song.mixer.resize (3);
        song.automations.clear();
        const lynx::AutomationTarget one { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 }, two { lynx::AutomationTargetKind::mixerVolume, 2, 0, 0 };
        lynx::Automation linked;
        linked.name = "Sends up";
        linked.links = { { one, "", {} }, { two, "", { 0.125f, 0.3333333f, 0.875f } } };
        linked.target = one;
        linked.points = { { 0, 0.25f }, { lynx::ppq, 0.75f } };
        lynx::Automation plain;
        plain.name = "Fader 1";
        plain.target = one;
        plain.points = { { 0, 0.5f } };
        song.automations = { linked, plain };
        for (int a = 0; a < 2; ++a)
        {
            lynx::Clip c;
            c.kind = lynx::ClipKind::automation;
            c.automation = a;
            c.track = 4 + a;
            c.start = a * 2 * lynx::ppq;
            c.length = lynx::ppq;
            song.playlist.push_back (c);
        }
        const auto json = lynx::ProjectFile::toJson (song);
        juce::String error;
        const auto back = lynx::ProjectFile::fromJson (json, error);
        if (! back || back->automations.size() != 2 || ! (back->automations[0].links == song.automations[0].links)
            || back->automations[0].target != one || ! back->automations[1].links.empty())
            return "ERROR the .lynx file (" + error + ")";

        const auto copy = lynx::olderlynx::forRelease (json, "0.1.30");
        int lines = 0;
        for (const auto& l : copy.lost)
            if (juce::String (l.text).startsWith ("Automation clips linked to several controls") && l.detail.contains ("\"Sends up\"") && l.sound)
                ++lines;
        const auto reopened = copy.ok ? lynx::ProjectFile::fromJson (copy.json, error) : std::nullopt;
        if (! copy.ok || lines != 1 || ! reopened || reopened->automations.size() != 2 || reopened->automations[0].links.size() != 2
            || ! reopened->automations[0].links[1].scale.empty() || ! (reopened->automations[0].links[1].target == two))
            return "ERROR the copy for 0.1.30 (" + juce::String (lines) + " lines)";

        // LYNX's own links (no scale, no other clip of their controls): nothing to report
        auto own = song;
        own.automations[0].links[1].scale.clear();
        own.automations.pop_back();
        own.playlist.pop_back();
        for (const auto& l : lynx::olderlynx::forRelease (lynx::ProjectFile::toJson (own), "0.1.30").lost)
            if (juce::String (l.text).startsWith ("Automation clips linked to several controls"))
                return "ERROR a line for LYNX's own links";
        return "OK";
    }
