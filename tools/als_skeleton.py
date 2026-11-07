#!/usr/bin/env python3
"""Generates src/core/AlsSkeleton.h: the element skeletons LYNX's .als export (round 175a, N2) fills in.

Live's loader needs complete objects: every member, in Live's own order (docs/analise/ALS_FORMAT.md §9). A skeleton is
one element kind's member names and default values. Lists are emptied, user content (names, paths, plugin states)
is removed, and the kind's own Id is set to 0 (the export numbers it). It is made from sets saved by Live on this machine.
Those sets are local only: none is committed, and the generated header holds no song content.

usage: tools/als_skeleton.py --live12 <folder of decompressed Live 12 sets> --probe12 <folder with gainprobe.xml and
       warpref.als (Live-saved plugin probes)> --live11 <a Live 11 set .als> [--live11-extra <more Live 11 sets>]
       [--out src/core/AlsSkeleton.h]
       round 175a used --live11 XRJAM.als (11.0.12) --live11-extra abletoolz 11.0.12.als (GroupTrack, AudioClip, VST3)
       WithPlugins.als (11.3.3, Locator); all public sets, kept local.
"""
import argparse, copy, gzip, os, re, xml.etree.ElementTree as ET, zlib

# containers whose children are items (emptied); "Notes" only inside a KeyTrack (MidiClip/Notes holds KeyTracks)
LISTS = {'Tracks', 'Scenes', 'SendsPre', 'Events', 'KeyTracks', 'WarpMarkers', 'Devices', 'Envelopes', 'ClipSlotList', 'Sends',
         'Grooves', 'ParameterList', 'AutomationLanes', 'EventLists', 'NoteProbabilityGroups', 'Locators', 'TakeLanes',
         'MarkerList', 'ExpressionGrid'}
USER_VALUES = {'EffectiveName', 'UserName', 'Annotation', 'MemorizedFirstClipName', 'Name', 'Path', 'RelativePath',
               'OriginalFileRef', 'BrowserContentPath', 'BranchDeviceId', 'PlugName'}
BLOBS = {'ProcessorState', 'ControllerState', 'Buffer', 'Data'}


def strip(e, top=True, scale=False):
    # (ScaleInformation/Name is the scale, not user text: an index in Live 12, a name such as "Major" in Live 11)
    if not top and e.tag in LISTS and not (e.tag == 'Locators' and e.find('Locators') is not None) \
            and not (e.tag == 'TakeLanes' and e.find('TakeLanes') is not None) and not (e.tag == 'AutomationLanes' and e.find('AutomationLanes') is not None):
        for c in list(e):
            e.remove(c)
    if e.tag == 'Notes' and e.find('KeyTracks') is None:
        for c in list(e):
            if c.tag == 'MidiNoteEvent':
                e.remove(c)
    if e.tag in BLOBS:
        e.text = None
        for c in list(e):
            e.remove(c)
    # (a number is a setting, not user text: ScaleInformation/Name is the scale's index, and Live wants an int there)
    if e.tag in USER_VALUES and 'Value' in e.attrib and not re.fullmatch(r'-?\d+', e.get('Value')) and not scale:
        e.set('Value', '')
    if e.tag in ('LivePackName', 'LivePackId') and 'Value' in e.attrib:
        e.set('Value', '')
    if e.tag == 'ViewData' and 'Value' in e.attrib:
        e.set('Value', '{}')
    if e.tag == 'MidiInputRouting':  # the sample's own MIDI device → Live's default "all inputs"
        for k, v in (('Target', 'MidiIn/External.All/-1'), ('UpperDisplayString', 'Ext: All Ins'), ('LowerDisplayString', '')):
            c = e.find(k)
            if c is not None:
                c.set('Value', v)
    # the kind's own Id is the export's to set; ids inside stay as Live wrote them, unique within each list
    # ("Non-unique list ids" otherwise) [LIVE]. Pointee/target ids are renumbered by the export anyway.
    if 'Id' in e.attrib and top:
        e.set('Id', '0')
    if e.text is not None and not e.text.strip():
        e.text = None
    e.tail = None
    for c in list(e):
        strip(c, False, e.tag == 'ScaleInformation')
    return e


def text(e):
    return ET.tostring(e, encoding='unicode', short_empty_elements=True)


def find(root, path):
    e = root.find(path)
    if e is None:
        raise SystemExit(f'missing {path}')
    return copy.deepcopy(e)


# Live 12 → Live 11: names Live 12 renamed, members it added [DATA: a Live 11.0.12 set vs Live 12.4's]
RENAME_11 = {'MainTrack': 'MasterTrack', 'ViewStateSessionTrackWidth': 'ViewStateSesstionTrackWidth',
             'AutoColorPickerForReturnAndMainTracks': 'AutoColorPickerForReturnAndMasterTracks', 'MidiEditorLaneModel': 'ExpressionLane'}
DROP_11 = {'MpeSettings', 'BreakoutIsExpanded', 'ComplexProEnvelopeModulationTarget', 'ComplexProFormantsModulationTarget',
           'KeepRecordMonitoringLatency', 'TransientEnvelopeModulationTarget', 'ControllerLayoutCustomization', 'ControllerLayoutRemoteable',
           'AccidentalSpellingPreference', 'AutomationEnvelopesListWrapper', 'NoteProbabilityGroups', 'ProbabilityGroupIdGenerator'}


def to11(e):
    """A Live 12 skeleton for a kind no Live 11 sample holds, downgraded by the rules above (unverified in Live 11)."""
    for c in list(e):
        if c.tag in DROP_11:
            e.remove(c)
            continue
        to11(c)
    e.tag = RENAME_11.get(e.tag, e.tag)
    if e.tag == 'ScaleInformation':
        r = e.find('Root')
        if r is not None:
            r.tag = 'RootNote'
    return e


def kinds12(a):
    x = lambda f: ET.parse(os.path.join(a.live12, f)).getroot()
    base, beat, demo, pod = x('DefaultLiveSet.xml'), x('Quick_Start_Beat.xml'), x('Chuck_Sutton_-_Patience__Live_12_Suite_Demo_.xml'), x('Podcast___Radio.xml')
    gain = ET.parse(os.path.join(a.probe12, 'gainprobe.xml')).getroot()
    vst2 = ET.fromstring(gzip.open(os.path.join(a.probe12, 'warpref.als')).read())
    return {
        'Root': ET.Element('Ableton', base.attrib),
        'LiveSet': find(base, 'LiveSet'),
        'MidiTrack': find(base, './/Tracks/MidiTrack'), 'AudioTrack': find(base, './/Tracks/AudioTrack'),
        'ReturnTrack': find(base, './/Tracks/ReturnTrack'), 'GroupTrack': find(pod, './/Tracks/GroupTrack'),
        'MidiClip': find(beat, './/MidiClip'), 'KeyTrack': find(beat, './/KeyTrack'), 'AudioClip': find(demo, './/AudioClip'),
        'Scene': find(base, './/Scenes/Scene'), 'ClipSlot': find(base, './/ClipSlotList/ClipSlot'),
        'TrackSendHolder': find(base, './/Sends/TrackSendHolder'), 'Locator': find(demo, './/Locators/Locators/Locator'),
        'AutomationEnvelope': find(beat, './/AutomationEnvelope'),
        'PluginDeviceVst3': find(gain, './/PluginDevice'), 'PluginDeviceVst2': find(vst2, './/PluginDevice'),
    }


def kinds11(a, k12):
    r = ET.fromstring(gzip.open(a.live11).read())
    out = {
        'Root': ET.Element('Ableton', r.attrib),
        'LiveSet': find(r, 'LiveSet'),
        'MidiTrack': find(r, './/Tracks/MidiTrack'), 'AudioTrack': find(r, './/Tracks/AudioTrack'),
        'ReturnTrack': find(r, './/Tracks/ReturnTrack'),
        'MidiClip': find(r, './/MidiClip'), 'KeyTrack': find(r, './/KeyTrack'),
        'Scene': find(r, './/Scenes/Scene'), 'ClipSlot': find(r, './/ClipSlotList/ClipSlot'),
        'TrackSendHolder': find(r, './/Sends/TrackSendHolder'), 'AutomationEnvelope': find(r, './/AutomationEnvelope'),
        'PluginDeviceVst2': find(r, './/PluginDevice'),
    }
    # kinds the main sample lacks: from further Live 11 sets, else downgraded from Live 12 (to11, unverified)
    extra = [ET.fromstring(gzip.open(f).read()) for f in a.live11_extra]
    paths = {'GroupTrack': './/Tracks/GroupTrack', 'AudioClip': './/AudioClip', 'Locator': './/Locators/Locators/Locator'}
    for k in ('GroupTrack', 'AudioClip', 'Locator', 'PluginDeviceVst3'):
        for x in extra:
            if k == 'PluginDeviceVst3':
                e = next((d for d in x.iter('PluginDevice') if d.find('.//Vst3PluginInfo') is not None), None)
            else:
                e = x.find(paths[k])
            if e is not None:
                out[k] = copy.deepcopy(e)
                break
        else:
            print(f'Live 11: {k} downgraded from Live 12 (no Live 11 sample has one)')
            out[k] = to11(copy.deepcopy(k12[k]))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--live12', required=True)
    p.add_argument('--probe12', required=True)
    p.add_argument('--live11', required=True)
    p.add_argument('--live11-extra', nargs='*', default=[], help='more Live 11 sets, for the kinds the first one lacks (first match wins)')
    p.add_argument('--out', default='src/core/AlsSkeleton.h')
    a = p.parse_args()
    k12 = kinds12(a)
    k11 = kinds11(a, k12)
    lines = ['#pragma once', '',
             '// Generated by tools/als_skeleton.py (round 175a): the element skeletons of Live 12 and Live 11 sets that the .als',
             '// export fills in (member names and default values in Live\'s order; lists empty; no song content). One zlib',
             '// stream per version: <Skeletons><Kind name="MidiTrack">…</Kind>…</Skeletons>. Do not edit by hand.', '',
             '#include <cstddef>', '#include <cstdint>', '', 'namespace lynx::alsskeleton', '{']
    for ver, kinds in (('12', k12), ('11', k11)):
        body = '<Skeletons>' + ''.join(f'<Kind name="{k}">{text(strip(v))}</Kind>' for k, v in kinds.items()) + '</Skeletons>'
        body = re.sub(r'>\s+<', '><', body)
        z = zlib.compress(body.encode('utf-8'), 9)
        lines.append(f'inline constexpr std::size_t live{ver}Size = {len(body.encode("utf-8"))};')
        lines.append(f'inline constexpr std::uint8_t live{ver}[{len(z)}] = {{')
        for i in range(0, len(z), 24):
            lines.append('    ' + ', '.join(f'0x{b:02x}' for b in z[i:i + 24]) + ',')
        lines.append('};')
        print(f'Live {ver}: {len(kinds)} kinds, {len(body)} bytes of XML, {len(z)} compressed')
    lines += ['} // namespace lynx::alsskeleton', '']
    open(a.out, 'w').write('\n'.join(lines))


if __name__ == '__main__':
    main()
