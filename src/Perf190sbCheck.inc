    // Round 190sb (0.1.32 stage Q2, the first 15 s after start-up): FL's plugin database kept between starts
    // (readFlPluginDatabase's cache file, core/FlPluginDb.h FlPluginDbCache), through the real reader on a folder made
    // here. Included in the app class of Main.cpp; --perf190sbtest <report> and --selftest "perf190sb".
    juce::String perf190sbCheck (juce::String& log)
    {
        using namespace lynx;
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-perf190sb-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        FlStudioInstall fl;
        fl.userData = root.getChildFile ("FL user data");
        const auto installed = fl.pluginDatabase();
        const auto nfo = [] (const juce::String& name, const juce::String& file, int type) {
            return "ps_name=" + name + "\r\nps_file_name_0=" + file + "\r\nps_file_vendorname_0=Vendor of " + name + "\r\nps_file_type_0=" + juce::String (type) + "\r\n";
        };
        const auto alpha = installed.getChildFile ("Generators").getChildFile ("VST3").getChildFile ("Alpha.nfo");
        const auto beta = installed.getChildFile ("Effects").getChildFile ("CLAP").getChildFile ("Beta.nfo");
        const auto gamma = installed.getChildFile ("Effects").getChildFile ("VST").getChildFile ("Gamma.nfo");
        auto ok = alpha.getParentDirectory().createDirectory() && beta.getParentDirectory().createDirectory() && gamma.getParentDirectory().createDirectory();
        ok = ok && alpha.replaceWithText (nfo ("Alpha", "C:\\VST3\\Alpha.vst3", 2)) && beta.replaceWithText (nfo ("Beta", "C:\\CLAP\\Beta.clap", 1))
             && gamma.replaceWithText (nfo ("Gamma", "C:\\VST\\Gamma_x64.dll", 1));
        const juce::Time listed (2024, 0, 2, 3, 4, 5); // (a whole second: some file systems keep no milliseconds)
        ok = ok && alpha.setLastModificationTime (listed) && beta.setLastModificationTime (listed) && gamma.setLastModificationTime (listed);
        check ("a database folder made for the check", ok);
        const auto cache = root.getChildFile ("fl-plugin-database-cache.txt");
        const auto names = [] (const FlPluginDb& db) {
            juce::StringArray s;
            for (const auto& e : db.entries())
                s.add (juce::String::fromUTF8 (e.name.c_str()) + "|" + juce::String::fromUTF8 (e.vendor.c_str()) + "|" + juce::String::fromUTF8 (e.path.c_str())
                       + "|" + juce::String ((int) e.format) + "|" + juce::String (e.role) + "|" + juce::String::fromUTF8 (e.source.c_str()));
            return s.joinIntoString ("\n");
        };

        int files = 0, filesCached = 0;
        const auto plain = readFlPluginDatabase (fl, &files);
        const auto first = readFlPluginDatabase (fl, &filesCached, cache);
        check ("the first read (no cache yet) is the plain read, and writes the cache",
               plain.entries().size() == 3 && files == 3 && filesCached == 3 && names (first) == names (plain) && cache.existsAsFile());
        const auto second = readFlPluginDatabase (fl, &filesCached, cache);
        check ("the second read (from the cache) is the same database, entry for entry", names (second) == names (plain) && filesCached == 3);

        // a file whose size and time are as listed is not opened again: new text of the same size, the old time put back
        ok = alpha.replaceWithText (nfo ("Alphb", "C:\\VST3\\Alphb.vst3", 2)) && alpha.setLastModificationTime (listed);
        const auto stale = readFlPluginDatabase (fl, nullptr, cache);
        check ("an unchanged listing is not read again (the cache's entries are used)", ok && names (stale) == names (plain));

        // a changed file (another size) is read again; one gone leaves; one new is read
        ok = alpha.replaceWithText (nfo ("Alpha Two", "C:\\VST3\\Alpha.vst3", 2)) && gamma.deleteFile()
             && installed.getChildFile ("Generators").getChildFile ("VST3").getChildFile ("Delta.nfo").replaceWithText (nfo ("Delta", "C:\\VST3\\Delta.vst3", 2));
        const auto changed = readFlPluginDatabase (fl, &filesCached, cache);
        const auto fresh = readFlPluginDatabase (fl, &files);
        check ("changed, removed and new files: the same as a plain read", ok && filesCached == 3 && files == 3 && names (changed) == names (fresh)
                                                                              && names (fresh).contains ("Alpha Two") && ! names (fresh).contains ("Gamma"));
        const auto again = readFlPluginDatabase (fl, nullptr, cache);
        check ("and the cache written after the change gives it again", names (again) == names (fresh));

        // a damaged cache file is ignored (read as before) and replaced
        ok = cache.replaceWithText ("LYNX FL plugin database cache 1\nF\tbroken");
        const auto repaired = readFlPluginDatabase (fl, nullptr, cache);
        check ("a damaged cache: the plain read, and a whole cache written again",
               ok && names (repaired) == names (fresh) && FlPluginDbCache::parse (cache.loadFileAsString().toStdString()).size() == 3);

        root.deleteRecursively();
        return result;
    }
