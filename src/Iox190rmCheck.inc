    // Round 190rm (0.1.32 import / export sweep): one route that opens any project LYNX reads, the way File › Open does
    // (.lynx, .mid, and every format of the .flp import's pipeline), and writes it in any format LYNX writes, the way the
    // File menu's export does, without a window. Included in the app class of Main.cpp.
    //   LYNX --convert190rm <in> <out.(lynx|flp|zip|als|dawproject|flm|mid)> [--savecopyfor <release>] [--package190rm]
    // (--package190rm: <out> is written as File › Export › Zipped loop package; --savecopyfor as Save a copy for an older LYNX)
    // The import report, the export's notes and "result=OK" / "result=ERROR <why>" go to "<out>.txt".
    void runConvert190rm (const juce::String& inputPath, const juce::String& outputPath)
    {
        using namespace lynx;
        juce::String error, report;
        auto ok = false;
        const juce::File input (inputPath), output (outputPath);
        // round 190sa (Q6): how long each step takes (kainproctor's FL -> LYNX -> .als -> Bitwig case): "timing.openMs" /
        // "timing.exportMs" in the report
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto tOpened = t0, tWritten = t0;
        {
            AudioEngine loader (false);
            if (input.hasFileExtension ("mid;midi"))
            {
                if (auto song = MidiFileIo::loadSong (input, error))
                    loader.setSong (std::move (*song)), ok = true;
            }
            else if (input.hasFileExtension (juce::String (ProjectFile::extension).substring (1)) || input.hasFileExtension ("json"))
            {
                if (auto song = ProjectFile::load (input, error))
                    loader.setSong (std::move (*song)), ok = true;
            }
            else
                ok = loader.importFlpFile (input, error);

            tOpened = juce::Time::getMillisecondCounterHiRes();
            if (ok)
            {
                const auto joined = [] (const std::map<std::string, int>& items) {
                    juce::StringArray names;
                    for (const auto& [name, count] : items)
                        names.add (juce::String::fromUTF8 (name.c_str()) + " x" + juce::String (count));
                    return names.joinIntoString ("; ");
                };
                const auto& r = loader.getImportReport();
                report << "import.converted=" << joined (r.converted) << "\nimport.approximated=" << joined (r.approximated)
                       << "\nimport.unsupported=" << joined (r.unsupported) << "\nimport.notReproduced=" << joined (r.notReproduced)
                       << "\nimport.droppedEffects=" << joined (r.droppedEffects) << "\nimport.partialEffects=" << joined (r.partialEffects)
                       << "\nimport.missingSamples=" << r.missingSamples << "\n";
                loader.capturePluginStates();
                const auto& song = loader.getSong();
                juce::StringArray notes;
                output.getParentDirectory().createDirectory();
                const auto args = juce::JUCEApplication::getCommandLineParameterArray();
                if (args.contains ("--savecopyfor"))
                {
                    const auto copy = olderlynx::forRelease (song, args[args.indexOf ("--savecopyfor") + 1].unquoted());
                    for (const auto& l : copy.lost)
                        notes.add ((l.sound ? "sound: " : "view: ") + juce::String::fromUTF8 (l.text).replace ("{0}", l.detail));
                    juce::String why; // (the app's writer: src/ui/MainComponent.cpp's Save a copy)
                    ok = copy.ok && safewrite::writeText (output, copy.json, why, false, false, nullptr);
                    if (! copy.ok)
                        error = copy.error;
                    else if (! ok)
                        error = tr ("Not written: {0}. {1} was not changed.", { tr (why), output.getFileName() });
                }
                else if (args.contains ("--package190rm")) // File › Export › Zipped loop package (the worker's call)
                {
                    ProjectPackage::Result packed;
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    ok = ProjectPackage::exportZip (song, output.getFileNameWithoutExtension(), output, packed, error);
                    report << "package.files=" << packed.files << "\npackage.missing=" << packed.missing.size()
                           << "\npackage.ms=" << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) << "\n";
                }
                else if (output.hasFileExtension (juce::String (ProjectFile::extension).substring (1)))
                    ok = ProjectFile::save (song, output, error);
                else if (output.hasFileExtension ("flp;zip"))
                {
                    flstudio::Written written;
                    const auto zip = output.hasFileExtension ("zip");
                    auto options = flstudio::optionsFor (&loader.getPluginHost(), flpexport::LynxOnly::renderAudio, song);
                    const auto renderDir = zip ? juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("LYNX FL export", {}, false)
                                               : output.getSiblingFile (output.getFileNameWithoutExtension() + " audio");
                    ok = flstudio::exportProject (song, options, output, zip, renderDir, 44100.0,
                                                  [] (const Song& s, const ExportSettings& settings, const juce::File& wav,
                                                      const std::function<bool (double)>& progress, juce::String& err) {
                                                      ExportJob job (s, settings, wav);
                                                      return job.run (progress, err);
                                                  },
                                                  {}, written, error);
                    if (zip)
                        renderDir.deleteRecursively();
                    notes.addLines (flstudio::reportText (written));
                }
                else if (output.hasFileExtension ("als"))
                {
                    // --alscollect (round 190sa): the samples copied into the set's folder, as the app's default
                    const auto collect = args.contains ("--alscollect");
                    const auto als = writeAlsProject (song, &loader.getPluginHost(), AlsVersion::live12, collect, output.getParentDirectory(),
                                                      output.getFileNameWithoutExtension(), notes, error);
                    ok = als != juce::File();
                    report << "als=" << als.getFullPathName() << "\n";
                }
                else if (output.hasFileExtension ("dawproject"))
                    ok = writeDawProjectFile (song, &loader.getPluginHost(), output, notes, error);
                else if (output.hasFileExtension ("flm"))
                {
                    FlmExportResult result;
                    ok = writeFlmCopy (song, output, result, error);
                    for (const auto& l : result.lost)
                        notes.add ((l.sound ? "sound: " : "other: ") + juce::String::fromUTF8 (l.text.c_str()).replace ("{0}", juce::String::fromUTF8 (l.detail.c_str())));
                }
                else if (output.hasFileExtension ("mid;midi"))
                    ok = MidiFileIo::saveSong (song, output, error);
                else
                {
                    ok = false;
                    error = "unknown output type";
                }
                for (const auto& n : notes)
                    report << "export.note=" << n << "\n";
                tWritten = juce::Time::getMillisecondCounterHiRes();
            }
        }
        report << "timing.openMs=" << juce::String (tOpened - t0, 0) << "\ntiming.exportMs=" << juce::String (tWritten - tOpened, 0)
               << "\ntiming.closeMs=" << juce::String (juce::Time::getMillisecondCounterHiRes() - juce::jmax (tOpened, tWritten), 0) << "\n";
        report << "result=" << (ok ? juce::String ("OK") : "ERROR " + error) << "\n";
        juce::File (outputPath + ".txt").replaceWithText (report);
        std::fprintf (stderr, "convert190rm %s", ("result=" + report.fromLastOccurrenceOf ("\nresult=", false, false)).toRawUTF8()); // (also when <out>.txt can't be written)
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // Round 190rm: the sweep's fixes through the app's own writers and readers (--iox190rmtest <report>, --selftest
    // "iox190rm"): MIDI export with the tempo map and the name first, MIDI import past an unknown chunk and with the
    // file's meter, the FL Studio Mobile export's tempo / marker lines, the DAWproject export of an FL Ogg-in-WAV sample
    // (when FL is installed) and an embedded FLAC that keeps its type.
    juce::String iox190rmCheck (juce::String& log)
    {
        using namespace lynx;
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lynx-iox190rm", {}, false);
        folder.createDirectory();
        struct Cleanup
        {
            juce::File f;
            ~Cleanup() { f.deleteRecursively(); }
        } cleanup { folder };

        // a song: one channel with notes, 120 BPM, a tempo clip falling to 60 at bar 2 (hold), a 3/4 marker at bar 3
        Song song;
        song.mode = PlayMode::song;
        song.bpm = 120.0;
        song.channels.resize (1);
        song.channels[0].name = "Lead";
        song.patterns.resize (1);
        for (int k = 0; k < 8; ++k)
        {
            Note n;
            n.start = k * 960;
            n.length = 480;
            n.key = 60 + k;
            song.patterns[0].notes.push_back (n);
        }
        song.playlistTracks.resize (2);
        Clip pc;
        pc.kind = ClipKind::pattern;
        pc.pattern = 0;
        pc.track = 0;
        pc.length = 7680;
        song.playlist.push_back (pc);
        Automation tempo;
        tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
        AutomationPoint p0, p1;
        p0.tick = 0, p0.value = valueFromTempo (120.0), p0.mode = flcurve::hold;
        p1.tick = 3840, p1.value = valueFromTempo (60.0);
        tempo.points = { p0, p1 };
        song.automations.push_back (tempo);
        Clip ac;
        ac.kind = ClipKind::automation;
        ac.automation = 0;
        ac.track = 1;
        ac.length = 7680;
        song.playlist.push_back (ac);
        Marker meter;
        meter.tick = 7680;
        meter.name = "3/4";
        meter.numerator = 3;
        meter.denominator = 4;
        meter.action = Marker::timeSignature;
        song.markers().push_back (meter);

        // 1. MIDI export: every tempo, the signature change, the track's name before its first note
        const auto mid = folder.getChildFile ("song.mid");
        juce::String error;
        check ("MIDI export writes", MidiFileIo::saveSong (song, mid, error));
        {
            juce::FileInputStream in (mid);
            juce::MidiFile file;
            check ("MIDI export reads back", in.openedOk() && file.readFrom (in));
            std::vector<std::pair<double, double>> tempos;
            int signatures = 0;
            for (int t = 0; t < file.getNumTracks(); ++t)
                for (const auto* e : *file.getTrack (t))
                {
                    if (e->message.isTempoMetaEvent())
                        tempos.push_back ({ e->message.getTimeStamp(), 60.0 / e->message.getTempoSecondsPerQuarterNote() });
                    if (e->message.isTimeSignatureMetaEvent())
                        ++signatures;
                }
            check ("MIDI export: the tempo map (120 then 60 BPM at tick 3840)",
                   tempos.size() == 2 && std::abs (tempos[0].second - 120.0) < 0.01 && tempos[1].first == 3840.0 && std::abs (tempos[1].second - 60.0) < 0.01);
            check ("MIDI export: the 3/4 marker as a time signature at tick 7680", signatures == 2);
            const auto* notes = file.getNumTracks() > 1 ? file.getTrack (1) : nullptr;
            check ("MIDI export: the track name first", notes != nullptr && notes->getNumEvents() > 0 && notes->getEventPointer (0)->message.isTrackNameEvent());
        }

        // 2. MIDI import: an unknown chunk before the track (SMF: skipped), the file's 3/4
        {
            const auto be32 = [] (juce::MemoryOutputStream& o, juce::uint32 v) {
                for (int s = 24; s >= 0; s -= 8)
                    o.writeByte ((char) ((v >> s) & 0xFF));
            };
            juce::MemoryOutputStream track;
            const unsigned char events[] = { 0x00, 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08, // 3/4
                                             0x00, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 64, 0x00, 0x90, 64, 100, 0x83, 0x60, 0x80, 64, 64,
                                             0x00, 0x90, 67, 100, 0x83, 0x60, 0x80, 67, 64, 0x00, 0xFF, 0x2F, 0x00 };
            juce::MemoryOutputStream smf;
            smf.write ("MThd", 4);
            be32 (smf, 6);
            const unsigned char header[] = { 0, 1, 0, 1, 0x01, 0xE0 }; // type 1, one track, 480 PPQ
            smf.write (header, sizeof (header));
            smf.write ("XFIH", 4);
            be32 (smf, 4);
            smf.write ("abcd", 4);
            smf.write ("MTrk", 4);
            be32 (smf, (juce::uint32) sizeof (events));
            smf.write (events, sizeof (events));
            const auto file = folder.getChildFile ("chunk.mid");
            file.replaceWithData (smf.getData(), smf.getDataSize());
            const auto opened = MidiFileIo::loadSong (file, error);
            check ("MIDI open past an unknown chunk: 3 notes", opened && ! opened->patterns.empty() && opened->patterns[0].notes.size() == 3);
            check ("MIDI open: the file's 3/4", opened && opened->beatsPerBar == 3 && opened->beatUnit == 4);
        }

        // 3. FL Studio Mobile export: the tempo changes and markers are listed
        {
            auto withMarker = song;
            Marker plain;
            plain.tick = 3840;
            plain.name = "Drop";
            withMarker.markers().insert (withMarker.markers().begin(), plain);
            const auto flm = exportFlm (withMarker, {});
            const auto has = [&flm] (const char* text) {
                return std::any_of (flm.lost.begin(), flm.lost.end(), [text] (const FlmExportLoss& l) { return l.text.find (text) != std::string::npos; });
            };
            check ("FLM export lists the tempo changes", has ("Tempo changes"));
            check ("FLM export lists the time signature changes", has ("Time signature changes"));
            check ("FLM export lists the markers", has ("Playlist markers"));
            // a muted Playlist track's clips: they don't play, and FLM's clips have no mute: left out and listed
            auto muted = song;
            muted.playlistTracks[0].muted = true;
            const auto quiet = exportFlm (muted, {});
            const auto hasQuiet = std::any_of (quiet.lost.begin(), quiet.lost.end(),
                                               [] (const FlmExportLoss& l) { return l.text.find ("muted Playlist tracks") != std::string::npos; });
            check ("FLM export leaves a muted track's clips out and says so", hasQuiet);
        }

        // 4. DAWproject export of an FL Ogg-in-WAV sample: a real format on its Audio element, embedded as PCM WAV
        {
            const auto flRoot = juce::SystemStats::getEnvironmentVariable ("LYNX_FL_INSTALL", {});
            const auto ogg = juce::File (flRoot).getChildFile ("Data/Patches/Packs/Drums/Percussion/808 Conga.wav"); // (FL's Ogg-in-WAV, format tag 0x674F)
            if (flRoot.isEmpty() || ! ogg.existsAsFile())
                log << "skip DAWproject Ogg-in-WAV (no FL installation)\n";
            else
            {
                Song s;
                s.mode = PlayMode::song;
                s.channels.resize (1);
                s.channels[0].name = "Conga";
                s.channels[0].instrument = InstrumentKind::audioClip;
                s.channels[0].samplePath = ogg.getFullPathName().toStdString();
                s.playlistTracks.resize (1);
                Clip c;
                c.kind = ClipKind::audio;
                c.channel = 0;
                c.length = 960;
                s.playlist.push_back (c);
                juce::StringArray notes;
                const auto out = folder.getChildFile ("conga.dawproject");
                check ("DAWproject export with an Ogg-in-WAV sample", writeDawProjectFile (s, nullptr, out, notes, error));
                juce::ZipFile zip (out);
                juce::String xml;
                if (const auto* e = zip.getEntry ("project.xml"))
                    if (std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (*e) })
                        xml = in->readEntireStreamAsString();
                check ("DAWproject: the Audio element has the file's real format", xml.contains ("channels=\"1\" sampleRate=\"44100\"") && ! xml.contains ("channels=\"0\""));
                juce::MemoryBlock wav;
                if (const auto* e = zip.getEntry ("audio/808 Conga.wav"))
                    if (std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (*e) })
                        in->readIntoMemoryBlock (wav);
                // the format tag of the "fmt " chunk (JUCE's WAV writer puts a JUNK chunk before it)
                const auto* b = static_cast<const juce::uint8*> (wav.getData());
                int tag = -1;
                for (std::size_t at = 12; at + 10 <= wav.getSize();)
                {
                    const auto size = (std::size_t) b[at + 4] | ((std::size_t) b[at + 5] << 8) | ((std::size_t) b[at + 6] << 16) | ((std::size_t) b[at + 7] << 24);
                    if (std::memcmp (b + at, "fmt ", 4) == 0)
                    {
                        tag = (int) b[at + 8] | ((int) b[at + 9] << 8);
                        break;
                    }
                    at += 8 + size + (size & 1);
                }
                log << "     (embedded: " << (int) wav.getSize() << " bytes, WAV format tag 0x" << juce::String::toHexString (tag) << ")\n";
                check ("DAWproject: the sample embedded as a PCM / float WAV, not Ogg",
                       wav.getSize() > 44 && std::memcmp (b, "RIFF", 4) == 0 && (tag == 1 || tag == 3 || tag == 0xFFFE));
            }
        }

        // 6. the checked writer (engine/SafeWrite.h): a write that fails half way, a read-only folder, a project save there;
        // the file that was there stays byte-identical and the reason is given (the full-disk case: s/writertest.sh)
        {
            const auto target = folder.getChildFile ("keep.bin");
            target.replaceWithText ("the good file");
            juce::String why;
            const auto failed = ! safewrite::writeWith (target, [] (juce::OutputStream& out) { out.write ("half", 4); return false; }, why);
            check ("checked writer: a failing write reports a failure", failed && why.isNotEmpty());
            check ("checked writer: the file that was there is untouched", target.loadFileAsString() == "the good file");
            check ("checked writer: no temporary file is left", folder.findChildFiles (juce::File::findFiles, false, "*.tmp").isEmpty()
                                                                    && folder.findChildFiles (juce::File::findFiles, false, ".keep*").isEmpty());
            check ("checked writer: a good write replaces it", safewrite::writeText (target, "new", why, false, false, nullptr) && target.loadFileAsString() == "new");
            // (rollup 0.1.32 step 6) a writer that takes its stream (a WAV writer seeks back to its header when it closes): the
            // whole file, over a file of that name (JUCE's createOutputStream appended to it), and a failure keeps that file
            {
                const auto wavFile = folder.getChildFile ("take.wav");
                wavFile.replaceWithText ("an older take");
                const int frames = 48000;
                std::vector<float> left ((std::size_t) frames), right ((std::size_t) frames);
                for (int i = 0; i < frames; ++i)
                {
                    left[(std::size_t) i] = 0.5f * std::sin ((float) i * 0.01f);
                    right[(std::size_t) i] = -left[(std::size_t) i];
                }
                const auto wrote = safewrite::writeWithHandedStream (wavFile, [&] (std::unique_ptr<juce::OutputStream>& stream) {
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 48000.0, 2, 24, {}, 0));
                    if (writer == nullptr)
                        return false;
                    stream.release();
                    const float* ch[2] { left.data(), right.data() };
                    return writer->writeFromFloatArrays (ch, 2, frames);
                }, why);
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (wavFile.createInputStream().release(), true));
                auto same = false;
                if (reader != nullptr && reader->lengthInSamples == frames && reader->numChannels == 2)
                {
                    juce::AudioBuffer<float> back (2, frames);
                    reader->read (&back, 0, frames, 0, true, true);
                    same = std::abs (back.getSample (0, 1234) - left[1234]) < 1.0e-4f && std::abs (back.getSample (1, 47999) - right[47999]) < 1.0e-4f;
                }
                reader.reset();
                check ("checked writer, handed stream: a whole WAV over a file of that name", wrote && same);
                const auto size = wavFile.getSize();
                juce::String whyNot;
                const auto failed = ! safewrite::writeWithHandedStream (wavFile, [] (std::unique_ptr<juce::OutputStream>& stream) {
                    stream->write ("half", 4);
                    return false;
                }, whyNot);
                check ("checked writer, handed stream: a failing writer leaves the file and says why", failed && whyNot.isNotEmpty() && wavFile.getSize() == size);
                wavFile.deleteFile();
            }
           #if ! JUCE_WINDOWS
            const auto ro = folder.getChildFile ("ro");
            ro.createDirectory();
            const auto project = ro.getChildFile ("song.lynx");
            check ("read-only folder: a first save", ProjectFile::save (song, project, error));
            const auto before = project.loadFileAsString();
            ro.setReadOnly (true);
            // (rollup 0.1.32 step 5: root ignores a folder's permissions - the Linux arm64 CI job runs as root - so the case only
            // holds where a file can not be made in the read-only folder)
            const auto probe = ro.getChildFile ("probe.tmp");
            const auto permissionsHold = probe.create().failed();
            probe.deleteFile();
            if (permissionsHold)
            {
                juce::String saveError;
                const auto saved = ProjectFile::save (song, project, saveError);
                ro.setReadOnly (false);
                check ("read-only folder: Save fails and says why (" + saveError + ")", ! saved && saveError.contains ("Your project file was not changed."));
                check ("read-only folder: the project file is byte-identical", project.loadFileAsString() == before);
            }
            else
            {
                ro.setReadOnly (false);
                check ("read-only folder: not checked, this user writes into read-only folders (root)", true);
            }
           #endif
        }

        // 5. DAWproject import of an embedded FLAC: saved as a .flac file
        {
            juce::MemoryBlock flacData;
            {
                juce::AudioBuffer<float> tone (1, 4410);
                for (int i = 0; i < tone.getNumSamples(); ++i)
                    tone.setSample (0, i, 0.5f * std::sin (0.0627f * (float) i));
                auto* stream = new juce::MemoryOutputStream (flacData, false);
                juce::FlacAudioFormat flac;
                if (std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (stream, 44100.0, 1, 16, {}, 0)); w != nullptr)
                    w->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
                else
                    delete stream;
            }
            const juce::String project = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Project version=\"1.0\"><Application name=\"T\" version=\"1\"/>"
                                         "<Transport><Tempo unit=\"bpm\" value=\"120\"/></Transport><Structure><Track id=\"t\" name=\"Tone\" contentType=\"audio\">"
                                         "<Channel id=\"c\" role=\"regular\"/></Track></Structure><Arrangement><Lanes timeUnit=\"beats\"><Lanes track=\"t\"><Clips>"
                                         "<Clip time=\"0\" duration=\"0.2\" contentTimeUnit=\"seconds\" playStart=\"0\"><Audio channels=\"1\" sampleRate=\"44100\" "
                                         "duration=\"0.1\"><File path=\"tone.flac\"/></Audio></Clip></Clips></Lanes></Lanes></Arrangement></Project>";
            juce::ZipFile::Builder builder;
            builder.addEntry (new juce::MemoryInputStream (project.toRawUTF8(), project.getNumBytesAsUTF8(), true), 0, "project.xml", juce::Time());
            builder.addEntry (new juce::MemoryInputStream (flacData, true), 0, "tone.flac", juce::Time());
            const auto dp = folder.getChildFile ("flac.dawproject");
            {
                juce::FileOutputStream out (dp);
                builder.writeToStream (out, nullptr);
            }
            AudioEngine loader (false);
            const auto ok = loader.importFlpFile (dp, error);
            const auto it = std::find_if (loader.getSong().channels.begin(), loader.getSong().channels.end(),
                                          [] (const ChannelDef& c) { return c.instrument == InstrumentKind::audioClip; });
            const auto path = it != loader.getSong().channels.end() ? juce::String::fromUTF8 (it->samplePath.c_str()) : juce::String();
            check ("DAWproject import: an embedded FLAC is saved as .flac (" + juce::File (path).getFileName() + ")",
                   ok && path.endsWithIgnoreCase (".flac") && ! path.containsIgnoreCase (".flac-"));
        }
        return result;
    }
