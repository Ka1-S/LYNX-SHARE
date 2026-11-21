// Round 190oh (Online Sessions + plugin host review, P3): FL's wrapper Settings › GUI — "DPI aware (requires reloading
// the plugin)" and "Scale editor dimensions" (Fruity Wrapper's DFM VisualPanel; FL 2026 saves with each box toggled:
// E:\lynx-work\online_host_190oh\fl\b0.flp / d1.flp / s1.flp, wrapper flags 0xB4007D8C / 0xB0007D8C / 0xF4007D8C)

TEST (host190oh_wrapper_gui_flags_from_fl)
{
    lynx::ForeignPlugin f;
    f.hasWrapperOptions = true;
    f.wrapperVersion = 12;
    lynx::PluginRef ref;
    // FL's defaults: DPI aware on, Scale editor dimensions off (as a new PluginRef)
    CHECK (ref.dpiAware && ! ref.scaleEditorDimensions);
    f.wrapperFlags = 0xB4007D8Cu;
    lynx::applyFlWrapperOptions (f, ref);
    CHECK (ref.dpiAware && ! ref.scaleEditorDimensions);
    f.wrapperFlags = 0xB0007D8Cu; // "DPI aware" off
    lynx::applyFlWrapperOptions (f, ref);
    CHECK (! ref.dpiAware && ! ref.scaleEditorDimensions && ref.hostKeysFirst == false && ref.allowSmartDisable);
    f.wrapperFlags = 0xF4007D8Cu; // "Scale editor dimensions" on (DPI aware on again)
    lynx::applyFlWrapperOptions (f, ref);
    CHECK (ref.dpiAware && ref.scaleEditorDimensions);
    // FL's "defaults" flag: nothing taken from the word
    lynx::PluginRef fresh;
    f.wrapperFlags = 0x400000u;
    lynx::applyFlWrapperOptions (f, fresh);
    CHECK (fresh.dpiAware && ! fresh.scaleEditorDimensions);
}

TEST (host190oh_wrapper_gui_flags_round_trip_through_flp_export)
{
    using namespace lynx;
    ForeignPlugin p;
    p.format = ForeignPlugin::Format::vst2;
    p.name = "Verb";
    p.path = "C:/x/Verb.dll";
    p.vst2Id = 0x56657262;
    for (const bool aware : { true, false })
        for (const bool dims : { false, true })
        {
            PluginRef r;
            r.dpiAware = aware;
            r.scaleEditorDimensions = dims;
            ForeignPlugin back;
            CHECK (parseFlWrapper (flpexport::writeWrapper (p, false, &r), back) && back.hasWrapperOptions);
            CHECK (((back.wrapperFlags & 0x04000000u) != 0) == aware);
            CHECK (((back.wrapperFlags & 0x40000000u) != 0) == dims);
            PluginRef read;
            applyFlWrapperOptions (back, read);
            CHECK (read.dpiAware == aware && read.scaleEditorDimensions == dims);
        }
    // a plugin at FL's defaults writes FL 2026's default word, as before this round
    PluginRef plain;
    ForeignPlugin back;
    CHECK (parseFlWrapper (flpexport::writeWrapper (p, false, &plain), back));
    CHECK (back.wrapperFlags == 0xB4007D8Cu);
}

TEST (host190oh_wrapper_settings_gui_section)
{
    const auto& o = lynx::wrapperOptions();
    CHECK (o.size() == 18);
    if (o.size() == 18)
    {
        CHECK (std::string (o[16].section) == "GUI" && std::string (o[16].label) == "DPI aware (requires reloading the plugin)"
               && o[16].member == &lynx::PluginRef::dpiAware && o[16].recreates && ! o[16].needsFixed);
        CHECK (std::string (o[17].section) == "GUI" && std::string (o[17].label) == "Scale editor dimensions"
               && o[17].member == &lynx::PluginRef::scaleEditorDimensions && o[17].recreates);
        CHECK (std::string (o[16].hint) == "For plugins that appear too big or small");
    }
    // the two fields count in the plugin's equality (undo / the session's patches see a change)
    lynx::PluginRef a, b;
    b.dpiAware = false;
    CHECK (! (a == b));
    b = a;
    b.scaleEditorDimensions = true;
    CHECK (! (a == b));
}

