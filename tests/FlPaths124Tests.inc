// Issue #124: an FL Studio project opened with "Samples missing" although its kicks were on the PC. FL 2026 (26.1.7,
// [EXEC] round 185b: six sampler channels pointed at samples in different folders, saved with Ctrl+S) writes
//   %FLStudioUserData%\Audio\LYNXTEST185\k_user.wav     (Documents\Image-Line\FL Studio\...)
//   %FLStudioDownloadsData%\LYNXTEST185\k_dl.wav        (Documents\Image-Line\Downloads\...: FL Cloud sounds)
//   %ILSharedData%LYNXTEST185\k_shared.wav              (Documents\Image-Line\...: no separator after the variable)
//   E:\lynx-work\issues185b\fl1\proj\Samples\k_rel.wav  (anywhere else, next to the project too: the full path)
//   %FLStudioFactoryData%\Data\Patches\Packs\Drums\Kicks\808 Kick.wav
// LYNX resolved only the factory / user data / data variables; the Downloads and Shared data ones were never found.

namespace flpaths124
{
inline lynx::FlPathRoots roots()
{
    lynx::FlPathRoots r;
    r.factoryData = "C:\\Program Files\\Image-Line\\FL Studio 2026";
    r.data = r.factoryData + "\\Data";
    r.sharedData = "C:\\Users\\me\\OneDrive\\Documents\\Image-Line";
    r.userData = r.sharedData + "\\FL Studio";
    r.downloadsData = r.sharedData + "\\Downloads";
    r.home = "C:\\Users\\me";
    return r;
}
} // namespace flpaths124

TEST (flpaths124_fl_2026_sample_variables_resolve)
{
    const auto r = flpaths124::roots();
    CHECK (lynx::resolveFlPath ("%FLStudioDownloadsData%\\FL CLOUD Sounds\\Kicks\\Kick 01.wav", r)
           == "C:\\Users\\me\\OneDrive\\Documents\\Image-Line\\Downloads\\FL CLOUD Sounds\\Kicks\\Kick 01.wav");
    CHECK (lynx::resolveFlPath ("%ILSharedData%My kicks\\k_shared.wav", r) == "C:\\Users\\me\\OneDrive\\Documents\\Image-Line\\My kicks\\k_shared.wav");
    CHECK (lynx::resolveFlPath ("%FLStudioUserData%\\Audio\\k_user.wav", r) == "C:\\Users\\me\\OneDrive\\Documents\\Image-Line\\FL Studio\\Audio\\k_user.wav");
    // a root saved with its trailing separator: still one separator
    auto slash = r;
    slash.sharedData += "\\";
    CHECK (lynx::resolveFlPath ("%ILSharedData%My kicks\\k.wav", slash) == "C:\\Users\\me\\OneDrive\\Documents\\Image-Line\\My kicks\\k.wav");
    CHECK (lynx::resolveFlPath ("%ILSharedData%\\My kicks\\k.wav", slash) == "C:\\Users\\me\\OneDrive\\Documents\\Image-Line\\My kicks\\k.wav");
    // no FL Shared data on this PC: unresolved (the Missing files window then offers to locate it)
    CHECK (lynx::resolveFlPath ("%FLStudioDownloadsData%\\k.wav", {}).empty());
    CHECK (lynx::resolveFlPath ("%ILSharedData%k.wav", {}).empty());
}

// The project FL saved, as channels: every sample is found, none is reported missing.
TEST (flpaths124_fl_saved_project_opens_without_missing_samples)
{
    const auto r = flpaths124::roots();
    const std::vector<std::pair<std::string, std::string>> samples {
        { "%FLStudioUserData%\\Audio\\LYNXTEST185\\k_user.wav", r.userData + "\\Audio\\LYNXTEST185\\k_user.wav" },
        { "%FLStudioDownloadsData%\\LYNXTEST185\\k_dl.wav", r.downloadsData + "\\LYNXTEST185\\k_dl.wav" },
        { "%ILSharedData%LYNXTEST185\\k_shared.wav", r.sharedData + "\\LYNXTEST185\\k_shared.wav" },
        { "E:\\Songs\\Track\\Samples\\k_rel.wav", "E:\\Songs\\Track\\Samples\\k_rel.wav" },
        { "%FLStudioFactoryData%\\Data\\Patches\\Packs\\Drums\\Kicks\\808 Kick.wav", r.factoryData + "\\Data\\Patches\\Packs\\Drums\\Kicks\\808 Kick.wav" },
    };
    std::set<std::string> files;
    flp::Project p;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        files.insert (samples[i].second);
        p.channels.push_back ({ (int) i, flp::ChannelType::sampler, "m" + std::to_string (i), "", samples[i].first, 1 });
    }
    lynx::FlpImportReport report;
    lynx::FlpImportOptions options;
    options.roots = r;
    options.projectFolder = "E:\\Songs\\Track";
    options.fileExists = [&files] (const std::string& path) { return files.count (path) > 0; };
    options.report = &report;
    const auto s = lynx::importFlp (p, options);
    CHECK (report.missingSamples == 0 && report.missingFiles.empty());
    CHECK (s.channels.size() == samples.size());
    for (std::size_t i = 0; i < samples.size() && i < s.channels.size(); ++i)
    {
        CHECK (s.channels[i].instrument == lynx::InstrumentKind::sampler);
        CHECK (s.channels[i].samplePath == samples[i].second);
    }
}

// A project from another PC: its Downloads / Shared data samples (full paths there) re-rooted to this PC's.
TEST (flpaths124_other_pcs_shared_data_is_re_rooted)
{
    const auto r = flpaths124::roots();
    const std::set<std::string> files { r.downloadsData + "\\FL CLOUD Sounds\\Kicks\\Kick 01.wav", r.sharedData + "\\My kicks\\k.wav" };
    auto exists = [&files] (const std::string& p) { return files.count (p) > 0; };
    CHECK (lynx::locateFlSample ("D:\\Docs\\Image-Line\\Downloads\\FL CLOUD Sounds\\Kicks\\Kick 01.wav", r, "E:\\x", exists)
           == r.downloadsData + "\\FL CLOUD Sounds\\Kicks\\Kick 01.wav");
    CHECK (lynx::locateFlSample ("C:\\Users\\bob\\Documents\\Image-Line\\My kicks\\k.wav", r, "E:\\x", exists) == r.sharedData + "\\My kicks\\k.wav");
}
