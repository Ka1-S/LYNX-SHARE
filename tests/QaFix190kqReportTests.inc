// --- round qafix190kq (QA of 0.1.30, QA-5): the import report's lists. `unsupported` ("No equivalent (played with a synth
// preset)") holds only instruments a LYNX synth preset plays instead; automation, note effects, project info... go to
// `notReproduced` ("Not reproduced (left out or played differently)"). A hand-written DAWproject (core/DawProject.h).

#include "core/DawProject.h"

TEST (qafix190kq_report_instrument_stays_unsupported_the_rest_is_not_reproduced)
{
    using namespace lynx;
    using namespace dawproject190jf;
    const std::string project = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Application name="Handmade" version="1"/>
  <Transport><Tempo id="t" unit="bpm" value="120"/><TimeSignature id="ts" numerator="4" denominator="4"/></Transport>
  <Structure>
    <Track id="syn" name="Synth" contentType="notes">
      <Channel id="synCh" role="regular" destination="mst">
        <Devices>
          <Device deviceName="Some Arp" deviceRole="noteFX"/>
          <BuiltinDevice deviceName="Mystery Synth" deviceRole="instrument"/>
        </Devices>
        <Volume unit="linear" value="1"/>
      </Channel>
    </Track>
    <Track id="m" name="Main" contentType="audio notes"><Channel id="mst" role="master"><Volume unit="linear" value="1"/></Channel></Track>
  </Structure>
  <Arrangement id="arr">
    <Lanes timeUnit="beats">
      <Lanes track="syn">
        <Clips><Clip time="0" duration="4"><Notes><Note time="0" duration="1" channel="0" key="60" vel="0.8" rel="0.5"/></Notes></Clip></Clips>
      </Lanes>
    </Lanes>
    <Markers><Marker time="0" name="Intro" color="#00ff00"/></Markers>
  </Arrangement>
</Project>
)";
    const std::string metadata = "<MetaData><Title>Report</Title><Album>X</Album></MetaData>";
    const auto file = zipOf ({ { "project.xml", bytesOf (project) }, { "metadata.xml", bytesOf (metadata) } });
    FlpImportReport report;
    DawProjectImportOptions io;
    io.common.report = &report;
    std::string err;
    const auto song = importDawProject (file, io, &err);
    CHECK (song.has_value());
    // the instrument LYNX has no counterpart for: played with the built-in synth, under "played with a synth preset"
    CHECK (report.unsupported.count ("Instrument: Mystery Synth (LYNX's synth plays the notes)") == 1);
    CHECK (report.notReproduced.count ("Instrument: Mystery Synth (LYNX's synth plays the notes)") == 0);
    // a note effect, the marker colours and the album are no instruments: listed as not reproduced, not as synth presets
    CHECK (reported (report.notReproduced, "Note effect: Some Arp") && ! reported (report.unsupported, "Note effect"));
    CHECK (reported (report.notReproduced, "Marker colours") && ! reported (report.unsupported, "Marker colours"));
    CHECK (reported (report.notReproduced, "Project info: Album") && ! reported (report.unsupported, "Project info"));
    // nothing else under "played with a synth preset"
    CHECK (report.unsupported.size() == 1);
}
