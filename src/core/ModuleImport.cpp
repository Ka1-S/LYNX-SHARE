#include "core/ModuleImport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>

#include "core/MarkerPlan.h"
#include "core/SamplerImport.h"
#include "core/TempoMarkers.h"

#ifndef LIBXMP_STATIC
 #define LIBXMP_STATIC 1 // (the library is linked in: no dllimport on Windows)
#endif
#include "../../third_party/libxmp-lite/include/libxmp-lite/xmp.h"
#include "../../third_party/libxmp-lite/src/lynx_xmp_extra.h"

namespace lynx
{
namespace
{
// libxmp's effect numbers after its loaders converted each format's commands (src/effects.h; MIT)
enum : int
{
    fxArpeggio = 0x00, fxPortaUp = 0x01, fxPortaDown = 0x02, fxTonePorta = 0x03, fxVibrato = 0x04, fxTonePortaVolSlide = 0x05,
    fxVibratoVolSlide = 0x06, fxTremolo = 0x07, fxSetPan = 0x08, fxOffset = 0x09, fxVolSlide = 0x0a, fxJump = 0x0b,
    fxSetVolume = 0x0c, fxBreak = 0x0d, fxExtended = 0x0e, fxSpeed = 0x0f, fxGlobalVolume = 0x10, fxGlobalVolSlide = 0x11,
    fxKeyOff = 0x14, fxEnvelopePos = 0x15, fxPanSlide = 0x19, fxMultiRetrig = 0x1b, fxTremor = 0x1d, fxExtraFinePorta = 0x21,
    fxChannelVolume = 0x80, fxChannelVolSlide = 0x81, fxChannelFineVolSlide = 0x82, fxItInstrumentFunction = 0x83,
    fxFilterCutoff = 0x84, fxFilterResonance = 0x85, fxItTempo = 0x87, fxItRowDelay = 0x88, fxItPanSlide = 0x89,
    fxPanbrello = 0x8a, fxPanbrelloWave = 0x8b, fxHighOffset = 0x8c, fxSurround = 0x8d, fxItBreak = 0x8e, fxReverse = 0x8f,
    fxS3mSpeed = 0xa3, fxVolSlide2 = 0xa4, fxFinetune = 0xa6, fxS3mTempo = 0xab, fxFineVibrato = 0xac, fxPatternDelay = 0xb3,
    fxS3mArpeggio = 0xb4, fxPanSlideNoMemory = 0xb5, fxMacroSet = 0xbd, fxMacro = 0xbe, fxMacroSmooth = 0xbf,
    fxVolColSlideUp = 0xc0, fxVolColSlideDown = 0xc1, fxVolColFineUp = 0xc2, fxVolColFineDown = 0xc3
};
enum : int
{
    exFilter = 0x0, exFinePortaUp = 0x1, exFinePortaDown = 0x2, exGlissando = 0x3, exVibratoWave = 0x4, exFinetune = 0x5,
    exPatternLoop = 0x6, exTremoloWave = 0x7, exSetPan = 0x8, exRetrig = 0x9, exFineVolUp = 0xa, exFineVolDown = 0xb,
    exCut = 0xc, exDelay = 0xd, exPatternDelay = 0xe, exInvertLoop = 0xf
};

struct Context
{
    xmp_context c = xmp_create_context();
    bool loaded = false, playing = false;
    ~Context()
    {
        if (playing)
            xmp_end_player (c);
        if (loaded)
            xmp_release_module (c);
        xmp_free_context (c);
    }
};

std::string trimmed (const char* text, std::size_t max)
{
    std::string s (text, strnlen (text, max));
    for (auto& ch : s)
        if ((unsigned char) ch < 32 || (unsigned char) ch == 127)
            ch = ' ';
    const auto a = s.find_first_not_of (' ');
    if (a == std::string::npos)
        return {};
    return s.substr (a, s.find_last_not_of (' ') - a + 1);
}

std::string fileSafe (std::string s)
{
    for (auto& ch : s)
        if (std::strchr ("\\/:*?\"<>|", ch) != nullptr)
            ch = '_';
    return s;
}

const xmp_event* eventAt (const xmp_module& mod, int pattern, int row, int channel)
{
    if (pattern < 0 || pattern >= mod.pat || mod.xxp[pattern] == nullptr || row < 0 || row >= mod.xxp[pattern]->rows || channel >= mod.chn)
        return nullptr;
    const auto track = mod.xxp[pattern]->index[channel];
    if (track < 0 || track >= mod.trk || mod.xxt[track] == nullptr || row >= mod.xxt[track]->rows)
        return nullptr;
    return &mod.xxt[track]->event[row];
}

bool isKey (int note) noexcept { return note >= 1 && note <= XMP_MAX_KEYS; }
bool isEnd (int note) noexcept { return note == XMP_KEY_OFF || note == XMP_KEY_CUT || note == XMP_KEY_FADE; }

bool hasFx (const xmp_event& e, int type) noexcept { return e.fxt == type || e.f2t == type; }
int extendedFrame (const xmp_event& e, int sub) noexcept // EDx / ECx: x, -1 = none
{
    if (e.fxt == fxExtended && (e.fxp >> 4) == sub)
        return e.fxp & 15;
    if (e.f2t == fxExtended && (e.f2p >> 4) == sub)
        return e.f2p & 15;
    return -1;
}
int keyOffFrame (const xmp_event& e) noexcept
{
    if (e.fxt == fxKeyOff)
        return e.fxp;
    if (e.f2t == fxKeyOff)
        return e.f2p;
    return -1;
}
bool tonePorta (const xmp_event& e) noexcept { return hasFx (e, fxTonePorta) || hasFx (e, fxTonePortaVolSlide); }

// What the report says about an effect: 0 = imported exactly (or the song's flow, libxmp's own), 1 = imported at the
// note's start only (approximated), 2 = not imported, 3 = imported as the channel's volume automation (round 190ob: what
// changes the volume of a sounding note). `name` = how the report names it.
int effectClass (int type, int param, std::string& name)
{
    switch (type)
    {
        case fxArpeggio: name = "arpeggio (0xy)"; return param != 0 ? 2 : 0;
        case fxPortaUp: name = "pitch slide up (1xx)"; return 2;
        case fxPortaDown: name = "pitch slide down (2xx)"; return 2;
        case fxTonePorta: name = "tone portamento (3xx), played as the note's porta"; return 1;
        case fxVibrato: name = "vibrato (4xy)"; return 2;
        case fxTonePortaVolSlide: name = "tone portamento + volume slide (5xy): the slide as volume automation, the portamento as the note's porta"; return 1;
        case fxVibratoVolSlide: name = "vibrato of 6xy (its volume slide is imported)"; return 2;
        case fxTremolo: name = "tremolo (7xy)"; return 3;
        case fxSetPan: return 0;
        case fxOffset: name = "sample offset (9xx)"; return 2;
        case fxVolSlide:
        case fxVolSlide2: name = "volume slide (Axy)"; return 3;
        case fxJump: case fxBreak: case fxItBreak: case fxSpeed: case fxS3mSpeed: case fxS3mTempo: case fxItTempo:
        case fxItRowDelay: case fxPatternDelay: case fxSetVolume: case fxKeyOff: return 0;
        case fxGlobalVolume: name = "global volume"; return 3;
        case fxGlobalVolSlide: name = "global volume slide"; return 3;
        case fxChannelVolume: name = "channel volume"; return 3;
        case fxChannelVolSlide: case fxChannelFineVolSlide: name = "channel volume slide"; return 3;
        case fxEnvelopePos: name = "envelope position (Lxx)"; return 2;
        case fxPanSlide: case fxItPanSlide: case fxPanSlideNoMemory: name = "panning slide"; return 2;
        case fxMultiRetrig: name = "retrigger (Rxy)"; return 2;
        case fxTremor: name = "tremor (Txy)"; return 3;
        case fxExtraFinePorta: name = "extra fine portamento"; return 2;
        case fxItInstrumentFunction: name = "IT instrument functions (S7x: new note actions, envelopes)"; return 2;
        case fxFilterCutoff: case fxFilterResonance: case fxMacroSet: case fxMacro: case fxMacroSmooth:
            name = "IT filter / MIDI macros (Zxx, SFx)"; return 2;
        case fxPanbrello: case fxPanbrelloWave: name = "panbrello"; return 2;
        case fxHighOffset: name = "sample offset (9xx)"; return 2;
        case fxSurround: name = "surround (S91)"; return 2;
        case fxReverse: name = "play backwards (S9F)"; return 2;
        case fxFinetune: name = "set finetune"; return 2;
        case fxFineVibrato: name = "vibrato (4xy)"; return 2;
        case fxS3mArpeggio: name = "arpeggio (0xy)"; return 2;
        case fxVolColSlideUp: case fxVolColSlideDown: case fxVolColFineUp: case fxVolColFineDown: name = "volume slide (Axy)"; return 3;
        case fxExtended:
            switch (param >> 4)
            {
                case exFilter: name = "Amiga filter (E0x)"; return 2;
                case exFinePortaUp: case exFinePortaDown: name = "fine pitch slide (E1x / E2x)"; return (param & 15) != 0 ? 2 : 0;
                case exGlissando: name = "glissando (E3x)"; return 2;
                case exVibratoWave: case exTremoloWave: name = "vibrato / tremolo waveform (E4x / E7x)"; return 0;
                case exFinetune: name = "set finetune (E5x)"; return 2;
                case exPatternLoop: case exCut: case exDelay: case exPatternDelay: case exSetPan: return 0;
                case exRetrig: name = "retrigger (E9x)"; return (param & 15) != 0 ? 2 : 0;
                case exFineVolUp: case exFineVolDown: name = "fine volume slide (EAx / EBx)"; return (param & 15) != 0 ? 3 : 0;
                case exInvertLoop: name = "invert loop (EFx)"; return 2;
                default: name = "extended effect"; return 2;
            }
        default:
        {
            char buffer[48];
            std::snprintf (buffer, sizeof buffer, "effect %02X", type);
            name = buffer;
            return 2;
        }
    }
}

// ------------------------------------------------------------------------------------------------- the flow
struct ChannelRow
{
    int triggerFrame = -1;   // a note starts on this frame of the row (-1 = none)
    int endFrame = -1;       // the sounding note ends on this frame (a cut, an off, the next note's start)
    int key = -1;            // libxmp's key of the started note (0-based)
    bool porta = false;
    int instrument = -1;     // libxmp's instrument playing it
    int volume = 0, pan = 128; // libxmp's final volume (0..64) and pan (0..255) at the start
    int maxVolume = 0;
};

struct PlayedRow
{
    int pos = 0, pattern = 0, row = 0;
    int frames = 0;
    double ms = 0.0;
    std::vector<ChannelRow> ch;
    std::vector<std::uint8_t> volumes; // round 190ob: libxmp's final volume (0..64) of every channel on every frame
};
constexpr std::uint8_t ended = 255; // (PlayedRow::volumes: the channel's sample has ended, nothing sounds)

int startingFrame (const xmp_event& e)
{
    if (! isKey (e.note) && ! isEnd (e.note))
        return -1;
    return std::max (0, extendedFrame (e, exDelay));
}

struct Flow
{
    std::vector<PlayedRow> rows;
    int loopPos = -1, loopRow = -1;
    double seconds = 0.0;
};

Flow walk (Context& ctx, const xmp_module& mod, double maxSeconds)
{
    Flow flow;
    // (stereo: libxmp's final pan is only computed for a stereo mix; at the module's full stereo separation, as the
    // reference renders are made)
    if (xmp_start_player (ctx.c, 8000, 0) != 0)
        return flow;
    ctx.playing = true;
    xmp_set_player (ctx.c, XMP_PLAYER_INTERP, XMP_INTERP_NEAREST);
    xmp_set_player (ctx.c, XMP_PLAYER_MIX, 100);
    xmp_frame_info fi {};
    PlayedRow* current = nullptr;
    const auto chn = std::clamp (mod.chn, 0, XMP_MAX_CHANNELS);
    while (xmp_play_frame (ctx.c) == 0)
    {
        xmp_get_frame_info (ctx.c, &fi);
        if (fi.loop_count > 0)
        {
            flow.loopPos = fi.pos;
            flow.loopRow = fi.row;
            break;
        }
        if (fi.frame == 0 || current == nullptr)
        {
            flow.rows.push_back ({});
            current = &flow.rows.back();
            current->pos = fi.pos;
            current->pattern = fi.pattern;
            current->row = fi.row;
            current->ch.resize ((std::size_t) chn);
            for (int c = 0; c < chn; ++c)
                if (const auto* e = eventAt (mod, fi.pattern, fi.row, c))
                {
                    auto& r = current->ch[(std::size_t) c];
                    const auto start = startingFrame (*e);
                    if (isKey (e->note))
                    {
                        r.triggerFrame = start;
                        r.key = e->note - 1;
                        r.porta = tonePorta (*e);
                    }
                    // what ends the note that sounds: a new note (also a porta note: LYNX starts one), an off / cut /
                    // fade, an ECx cut, a Kxx off — the earliest
                    auto end = isKey (e->note) || isEnd (e->note) ? start : -1;
                    for (auto f : { extendedFrame (*e, exCut), keyOffFrame (*e) })
                        if (f >= 0 && (end < 0 || f < end))
                            end = f;
                    // (a cut on the note's own row ends that note, not the one before: kept apart below)
                    r.endFrame = end;
                }
        }
        const auto frame = current->frames;
        ++current->frames;
        current->ms += lynx_xmp_frame_ms (ctx.c);
        for (int c = 0; c < chn; ++c)
        {
            auto& r = current->ch[(std::size_t) c];
            const auto& info = fi.channel_info[c];
            // (a channel whose sample has played to its end: silent whatever its volume, marked as no volume at all)
            current->volumes.push_back (lynx_xmp_sample_ended (ctx.c, c) != 0 ? ended : (std::uint8_t) std::clamp ((int) info.volume, 0, 64));
            if (r.triggerFrame >= 0 && frame == r.triggerFrame)
            {
                r.instrument = info.instrument;
                r.volume = info.volume;
                r.pan = info.pan;
            }
            if (r.triggerFrame >= 0 && frame >= r.triggerFrame)
                r.maxVolume = std::max (r.maxVolume, (int) info.volume);
        }
        flow.seconds += lynx_xmp_frame_ms (ctx.c) / 1000.0;
        if (flow.seconds > maxSeconds)
            break;
    }
    // a delay past the row's last frame: the note never starts
    for (auto& r : flow.rows)
        for (auto& c : r.ch)
        {
            if (c.triggerFrame >= r.frames)
                c.triggerFrame = -1;
            if (c.endFrame >= r.frames)
                c.endFrame = -1;
        }
    return flow;
}

// ------------------------------------------------------------------------------------------------- the samples
struct WrittenSample
{
    std::string path;
    std::int64_t loopStart = -1, loopEnd = -1;
    bool sustainLoop = false;
};

WrittenSample writeSample (Context& ctx, const xmp_module& mod, int sid, const ModuleImportOptions& options, const std::string& prefix,
                           int& bidirectional)
{
    WrittenSample out;
    if (sid < 0 || sid >= mod.smp || ! options.saveSample)
        return out;
    const auto& s = mod.xxs[sid];
    if (s.len <= 1 || s.data == nullptr || (s.flg & XMP_SAMPLE_SYNTH) != 0)
        return out;
    const auto stereo = (s.flg & XMP_SAMPLE_STEREO) != 0;
    const auto channels = stereo ? 2 : 1;
    const auto frames = (std::int64_t) s.len;
    std::vector<std::int16_t> pcm ((std::size_t) (frames * channels));
    if ((s.flg & XMP_SAMPLE_16BIT) != 0)
        std::memcpy (pcm.data(), s.data, pcm.size() * sizeof (std::int16_t));
    else
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = (std::int16_t) (((std::int8_t) s.data[i]) * 256);

    std::int64_t ls = -1, le = -1;
    int sus = 0, sue = 0;
    if (lynx_xmp_sustain_loop (ctx.c, sid, &sus, &sue) != 0 && sue > sus && sue <= frames)
    {
        ls = sus;
        le = sue;
        out.sustainLoop = true;
    }
    else if ((s.flg & XMP_SAMPLE_LOOP) != 0 && s.lpe > s.lps && s.lpe <= frames)
    {
        ls = s.lps;
        le = s.lpe;
    }
    auto frameAt = [&] (std::int64_t f, int ch) { return pcm[(std::size_t) (f * channels + ch)]; };
    std::vector<std::int16_t> body;
    if (ls >= 0)
    {
        const auto bidi = (s.flg & (out.sustainLoop ? XMP_SAMPLE_SLOOP_BIDIR : XMP_SAMPLE_LOOP_BIDIR)) != 0;
        // the sample up to its loop's end, the loop played backwards after it when it is a ping-pong loop (LYNX's zones
        // loop forwards: the unrolled loop plays the same frames)
        body.assign (pcm.begin(), pcm.begin() + (std::ptrdiff_t) (le * channels));
        if (bidi && le - ls > 2)
        {
            ++bidirectional;
            for (auto f = le - 2; f > ls; --f)
                for (int ch = 0; ch < channels; ++ch)
                    body.push_back (frameAt (f, ch));
            le = (std::int64_t) body.size() / channels;
        }
        // a tiny loop (a chip sound's few frames) is repeated to 64 frames at least: LYNX's zones need more than 16
        const auto loopLength = le - ls;
        if (loopLength > 0 && loopLength < 64)
        {
            const auto copies = (64 + loopLength - 1) / loopLength;
            std::vector<std::int16_t> loop (body.begin() + (std::ptrdiff_t) (ls * channels), body.begin() + (std::ptrdiff_t) (le * channels));
            for (std::int64_t k = 1; k < copies; ++k)
                body.insert (body.end(), loop.begin(), loop.end());
            le = ls + loopLength * copies;
        }
        // one frame past the loop's end (the loop start's): the loop ends before the sample's last frame
        for (int ch = 0; ch < channels; ++ch)
            body.push_back (body[(std::size_t) (ls * channels + ch)]);
        out.loopStart = ls;
        out.loopEnd = le;
    }
    else
        body = std::move (pcm);

    const auto rate = std::clamp ((int) std::lround (lynx_xmp_sample_rate (ctx.c, sid)), 1000, 384000);
    const auto wav = msimport::wavFile (reinterpret_cast<const std::uint8_t*> (body.data()), body.size() * sizeof (std::int16_t), channels, 16,
                                        (double) rate, out.loopStart, out.loopEnd >= 0 ? out.loopEnd - 1 : -1);
    char number[16];
    std::snprintf (number, sizeof number, "%02d", sid + 1);
    auto name = trimmed (s.name, sizeof s.name);
    out.path = options.saveSample (wav, fileSafe (prefix + number + (name.empty() ? std::string() : " " + name)) + ".wav");
    return out;
}

void count (FlpImportReport* report, std::map<std::string, int> FlpImportReport::* list, const std::string& what, int n = 1)
{
    if (report != nullptr && n > 0)
        (report->*list)[what] += n;
}

// ------------------------------------------------------------------------------------------ volume automation (190ob)
// Stage 2 start: the volume a sounding note changes to (volume slides and sets, the volume column, global / channel
// volume, tremolo, tremor, the volume envelope while the note is held — libxmp's final volume on every frame) becomes the
// automation of its channel's Levels adjustment "Volume multiplier" (FL's channel parameter 0x11): gain = the volume now
// / the note's velocity, one point where the multiplier changes. The note keeps its velocity (its start's volume), so a
// note without a change plays as before. Per channel, not per note (LYNX's notes have no volume curve, and FL's none),
// so an instrument that sounds in two module channels at once with its own changes gets a channel per module channel.
// The values are kept on the 1/10000 steps a .lynx project stores, so they come back unchanged through .lynx and .flp.

// one played note: where it starts (song tick, played row and frame) and which note of which pattern it is
struct Instance
{
    int pattern = -1, note = -1; // the LYNX pattern and the note's index in notesOf
    int moduleChannel = 0;
    std::int64_t row = 0; // in flow.rows
    int frame = 0;
    std::int64_t start = 0;
};

// The automation's value in steps of 1/10000 (the steps a .lynx project stores an automation point's value in): 0..10000
// = the multiplier 0..200 %, 5000 = 100 %
constexpr int unityRaw = 5000, maxRaw = 10000;

// The multiplier (in those steps) whose gain is volume / noteVolume (levelAdjustGain: (11^x − 1) / 10, x = raw / 5000).
int multiplierRaw (int volume, int noteVolume) noexcept
{
    if (volume == noteVolume)
        return unityRaw;
    if (noteVolume <= 0 || volume <= 0)
        return volume <= 0 ? 0 : maxRaw;
    const auto gain = (double) volume / (double) noteVolume;
    const auto x = std::log (10.0 * gain + 1.0) / std::log (11.0);
    return std::clamp ((int) std::lround (x * unityRaw), 0, maxRaw);
}

struct VolumeAutomationResult
{
    int changedNotes = 0;   // played notes whose volume changes while they sound
    int automations = 0, points = 0;
    int splitChannels = 0;  // channels added: an instrument in several module channels at once
    int raisedNotes = 0;    // notes that start silent (or far below their loudest) and get their loudest volume
    int leftOut = 0;        // changes not imported (too many channels to split into)
};

template <typename TickOf>
VolumeAutomationResult volumeAutomation (Song& song, const Flow& flow, int chn, std::int64_t songEnd, std::vector<std::vector<Note>>& notesOf,
                                         const std::vector<std::vector<std::int64_t>>& lengthOf, std::vector<Instance> instances, TickOf tickOf)
{
    VolumeAutomationResult result;
    const auto n = (std::int64_t) flow.rows.size();
    auto noteOfInstance = [&] (const Instance& i) -> Note& { return notesOf[(std::size_t) i.pattern][(std::size_t) i.note]; };
    auto endOf = [&] (const Instance& i) { return i.start + std::max<std::int64_t> (1, lengthOf[(std::size_t) i.pattern][(std::size_t) i.note]); };
    auto noteVolume = [&] (const Instance& i) { return (int) std::lround (std::clamp (noteOfInstance (i).velocity, 0.0f, 1.0f) * 64.0f); };
    // each frame the instance sounds on, up to `until` (a song tick): fn (tick, libxmp's volume, the next frame's tick)
    auto frames = [&] (const Instance& i, std::int64_t until, auto&& fn) {
        auto k = i.row;
        auto f = i.frame;
        while (k < n)
        {
            const auto& r = flow.rows[(std::size_t) k];
            if (f >= r.frames)
            {
                ++k;
                f = 0;
                continue;
            }
            const auto tick = tickOf (k, f);
            if (tick >= until)
                return;
            const auto at = (std::size_t) f * (std::size_t) chn + (std::size_t) i.moduleChannel;
            const auto v = at < r.volumes.size() ? r.volumes[at] : ended;
            if (v != ended) // (a sample played to its end is silent at any level: not a change)
                fn (tick, (int) v, tickOf (k, f + 1), f == 0);
            ++f;
        }
    };

    // 1. a note that starts silent, or so soft that the multiplier's +21.6 dB cannot reach its loudest, gets its loudest
    // volume as its velocity (the automation brings it down where it is softer)
    std::map<std::pair<int, int>, int> loudest;
    for (const auto& i : instances)
    {
        auto& top = loudest[{ i.pattern, i.note }];
        frames (i, endOf (i), [&top] (std::int64_t, int v, std::int64_t, bool) { top = std::max (top, v); });
    }
    for (const auto& [key, top] : loudest)
    {
        auto& note = notesOf[(std::size_t) key.first][(std::size_t) key.second];
        const auto v = (int) std::lround (std::clamp (note.velocity, 0.0f, 1.0f) * 64.0f);
        if (top > 0 && (v == 0 || multiplierRaw (top, v) >= maxRaw))
        {
            note.velocity = (float) top / 64.0f;
            ++result.raisedNotes;
        }
    }

    // 2. which played notes change their volume
    std::vector<char> changes (instances.size(), 0);
    for (std::size_t k = 0; k < instances.size(); ++k)
    {
        const auto v = noteVolume (instances[k]);
        frames (instances[k], endOf (instances[k]), [&] (std::int64_t, int volume, std::int64_t, bool) {
            if (multiplierRaw (volume, v) != unityRaw)
                changes[k] = 1;
        });
        result.changedNotes += changes[k];
    }

    // 3. per channel: when two notes of different module channels sound at once and want different multipliers there,
    // the channel is copied once per module channel it plays in (the one it plays most in stays)
    std::map<std::size_t, std::vector<std::pair<std::int64_t, int>>> series; // a note's (tick, multiplier) on its frames
    auto seriesOf = [&] (std::size_t k) -> const std::vector<std::pair<std::int64_t, int>>& {
        auto it = series.find (k);
        if (it == series.end())
        {
            std::vector<std::pair<std::int64_t, int>> list;
            const auto v = noteVolume (instances[k]);
            frames (instances[k], endOf (instances[k]), [&] (std::int64_t tick, int volume, std::int64_t, bool) { list.emplace_back (tick, multiplierRaw (volume, v)); });
            it = series.emplace (k, std::move (list)).first;
        }
        return it->second;
    };
    // two notes sounding at once want different multipliers somewhere (where one's sample has ended it is silent)
    auto disagree = [&] (std::size_t a, std::size_t b) {
        if (! changes[a] && ! changes[b])
            return false;
        const auto lo = std::max (instances[a].start, instances[b].start), hi = std::min (endOf (instances[a]), endOf (instances[b]));
        const auto& x = seriesOf (a);
        const auto& y = seriesOf (b);
        auto j = std::lower_bound (y.begin(), y.end(), std::make_pair (lo, -1));
        for (auto i = std::lower_bound (x.begin(), x.end(), std::make_pair (lo, -1)); i != x.end() && i->first < hi; ++i)
        {
            while (j != y.end() && j->first < i->first)
                ++j;
            if (j != y.end() && j->first == i->first && j->second != i->second)
                return true;
        }
        return false;
    };
    auto byChannel = [&] {
        std::map<int, std::vector<std::size_t>> out;
        for (std::size_t k = 0; k < instances.size(); ++k)
            out[noteOfInstance (instances[k]).channel].push_back (k);
        for (auto& [c, list] : out)
            std::stable_sort (list.begin(), list.end(), [&] (std::size_t a, std::size_t b) { return instances[a].start < instances[b].start; });
        return out;
    };
    constexpr int channelLimit = 900;
    for (const auto& [channel, list] : byChannel())
    {
        bool conflict = false, any = false;
        std::vector<std::size_t> active; // the notes still sounding (sorted by start)
        for (auto k : list)
        {
            any = any || changes[k] != 0;
            active.erase (std::remove_if (active.begin(), active.end(), [&] (std::size_t a) { return endOf (instances[a]) <= instances[k].start; }), active.end());
            for (auto a : active)
                if (! conflict && instances[a].moduleChannel != instances[k].moduleChannel && disagree (a, k))
                    conflict = true;
            active.push_back (k);
        }
        if (! conflict || ! any)
            continue;
        std::map<int, int> uses;
        for (auto k : list)
            ++uses[instances[k].moduleChannel];
        if ((int) song.channels.size() + (int) uses.size() - 1 > channelLimit)
        {
            for (auto k : list)
                result.leftOut += changes[k];
            for (auto k : list)
                changes[k] = 0;
            continue;
        }
        const auto keep = std::max_element (uses.begin(), uses.end(), [] (const auto& a, const auto& b) { return a.second < b.second; })->first;
        std::map<int, int> copyOf;
        for (const auto& [m, count] : uses)
        {
            if (m == keep)
                continue;
            auto copy = song.channels[(std::size_t) channel];
            copy.name += " (ch " + std::to_string (m + 1) + ")";
            copyOf[m] = (int) song.channels.size();
            song.channels.push_back (std::move (copy));
            ++result.splitChannels;
        }
        for (auto k : list)
            if (const auto it = copyOf.find (instances[k].moduleChannel); it != copyOf.end())
                noteOfInstance (instances[k]).channel = it->second;
    }

    // 4. the automation of each channel with a change. A change on a row's first frame (a set: Cxx, the volume column) is a
    // jump (Hold) there, where libxmp makes it; a change on a later frame (a slide's tick, tremolo, an envelope) goes from
    // the old multiplier to the new one in a straight line over its frame, centred on the frame's start: the renderer
    // takes a channel's level once per chunk, so a jump on every tick would click where libxmp ramps (its anticlick), and
    // a slide becomes a smooth line. A note's own start is a jump, halfway through the silence before it when there is one.
    for (const auto& [channel, list] : byChannel())
    {
        if (std::none_of (list.begin(), list.end(), [&] (std::size_t k) { return changes[k] != 0; }))
            continue;
        std::vector<AutomationPoint> points;
        const auto valueOf = [] (int raw) { return (float) raw / (float) maxRaw; };
        // a point at `tick` (a later one; one on the last point's tick replaces its value); `rampTo`: the segment before it
        // is a straight line (else the value before it holds and jumps there)
        auto put = [&] (std::int64_t tick, int raw, bool rampTo) {
            if (! points.empty() && points.back().tick >= tick)
            {
                points.back().value = valueOf (raw);
                return;
            }
            if (! points.empty())
                points.back().mode = rampTo ? (std::uint8_t) flcurve::singleCurve : (std::uint8_t) flcurve::hold;
            AutomationPoint p;
            p.tick = tick;
            p.value = valueOf (raw);
            p.mode = flcurve::hold;
            points.push_back (p);
        };
        int current = -1;
        std::int64_t lastEnd = 0;
        for (std::size_t j = 0; j < list.size(); ++j)
        {
            const auto& i = instances[list[j]];
            const auto v = noteVolume (i);
            const auto until = std::min (endOf (i), j + 1 < list.size() ? std::max (i.start, instances[list[j + 1]].start) : endOf (i));
            bool first = true;
            frames (i, until, [&] (std::int64_t tick, int volume, std::int64_t next, bool rowStart) {
                const auto raw = multiplierRaw (volume, v);
                if (raw != current)
                {
                    if (first || current < 0 || rowStart)
                        put (first && lastEnd < i.start ? (points.empty() ? 0 : lastEnd + (i.start - lastEnd) / 2) : tick, raw, false);
                    else
                    {
                        const auto half = std::max<std::int64_t> (1, (next - tick) / 2);
                        if (points.empty() || points.back().tick < tick - half)
                            put (tick - half, current, false); // (held until here)
                        put (tick + half, raw, true);
                    }
                    current = raw;
                }
                first = false;
            });
            lastEnd = std::max (lastEnd, endOf (i));
        }
        if (points.empty())
            continue;
        Automation a;
        a.name = song.channels[(std::size_t) channel].name + " volume";
        a.target = { AutomationTargetKind::channelAdjust, channel, 0, leveladjust::volume };
        a.points = std::move (points);
        result.points += (int) a.points.size();
        song.automations.push_back (std::move (a));
        Clip clip;
        clip.kind = ClipKind::automation;
        clip.automation = (int) song.automations.size() - 1;
        const auto track = trackmodes::firstFreeTrack (song);
        clip.track = track >= 0 ? track : 1 + result.automations;
        clip.start = 0;
        clip.offset = 0;
        clip.length = std::max<std::int64_t> (1, songEnd);
        song.playlist.push_back (clip);
        ++result.automations;
    }
    return result;
}

int chooseRowsPerBeat (const std::vector<PlayedRow>& rows)
{
    double shortest = 1.0e9, longest = 0.0;
    for (const auto& r : rows)
        if (r.ms > 0.0)
        {
            shortest = std::min (shortest, r.ms);
            longest = std::max (longest, r.ms);
        }
    if (longest <= 0.0)
        return 4;
    for (const auto rpb : { 4, 8, 16, 2, 6, 12, 3, 1 })
    {
        const auto fastest = 60000.0 / (rpb * shortest), slowest = 60000.0 / (rpb * longest);
        if (fastest <= maxTempo && slowest >= minTempo)
            return rpb;
    }
    return 4;
}
} // namespace

bool isModuleFileName (const std::string& name)
{
    const auto dot = name.find_last_of ('.');
    if (dot == std::string::npos)
        return false;
    auto ext = name.substr (dot + 1);
    for (auto& ch : ext)
        ch = (char) std::tolower ((unsigned char) ch);
    return ext == "mod" || ext == "xm" || ext == "s3m" || ext == "it";
}

const char* moduleWildcards() noexcept { return "*.mod;*.xm;*.s3m;*.it"; }
const char* moduleExtensions() noexcept { return "mod;xm;s3m;it"; }

std::optional<Song> importModule (const std::vector<std::uint8_t>& bytes, const ModuleImportOptions& options, std::string* error)
{
    auto fail = [error] (const char* why) -> std::optional<Song> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (bytes.size() < 64)
        return fail ("not a module LYNX can read (.mod, .xm, .s3m, .it)");
    Context ctx;
    if (xmp_load_module_from_memory (ctx.c, bytes.data(), (long) bytes.size()) != 0)
        return fail ("not a module LYNX can read (.mod, .xm, .s3m, .it), or a damaged one");
    ctx.loaded = true;
    xmp_module_info info {};
    xmp_get_module_info (ctx.c, &info);
    if (info.mod == nullptr || info.mod->chn <= 0)
        return fail ("the module has no channels");
    const auto& mod = *info.mod;
    auto* report = options.report;
    ModuleImportSummary summary;
    summary.format = trimmed (mod.type, sizeof mod.type);
    summary.channels = mod.chn;
    summary.instruments = mod.ins;
    summary.samples = mod.smp;

    Song song;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (0);
    song.mode = PlayMode::song;
    song.arrangements = { Arrangement { "Arrangement", {}, 0, 0, {} } };
    song.beatsPerBar = 4;
    song.beatUnit = 4;
    song.info.title = trimmed (mod.name, sizeof mod.name);
    song.info.comments = "Imported from a " + summary.format + " module (" + std::to_string (mod.chn) + " channels)";
    // FL 2026's playback rules for new projects (as the .flp / .flm imports): one clip of a pattern plays at a time
    song.patternClipTakeover = true;
    song.muteRunsEffects = true;

    // ---- the song as libxmp plays it
    auto flow = walk (ctx, mod, options.maxSeconds);
    summary.seconds = flow.seconds;
    summary.playedRows = (std::int64_t) flow.rows.size();
    const auto rpb = chooseRowsPerBeat (flow.rows);
    summary.rowsPerBeat = rpb;
    song.tracker.rowsPerBeat = rpb;
    const std::int64_t rt = ppq / rpb;

    // ---- instruments → Sampler channels
    std::vector<int> channelOfInstrument ((std::size_t) std::max (0, mod.ins), -1);
    std::map<int, WrittenSample> written;
    int bidirectional = 0, sustainLoops = 0, envelopes = 0, volumeEnvelopes = 0, fadeouts = 0, autoVibrato = 0, newNoteActions = 0;
    std::vector<bool> panMoves; // per channel: its instrument has a pan envelope
    for (int i = 0; i < mod.ins; ++i)
    {
        const auto& ins = mod.xxi[i];
        if (ins.nsm <= 0 || ins.sub == nullptr)
            continue;
        ChannelDef channel;
        channel.instrument = InstrumentKind::sampler;
        channel.name = trimmed (ins.name, sizeof ins.name);
        if (channel.name.empty())
        {
            char buffer[32];
            std::snprintf (buffer, sizeof buffer, "Instrument %02d", i + 1);
            channel.name = buffer;
        }
        // the level libxmp mixes a voice at (a quarter of full scale per side at the centre: 4 full-scale channels
        // without clipping), as the Sampler's Gain with the channel at FL's default volume; the module's volumes are the
        // notes' velocities. (LYNX's pan law is constant power, libxmp's linear: round 190ob adapts the pans and this gain below.)
        channel.volume = ChannelDef {}.volume;
        channel.sampler.gainDb = (float) (20.0 * std::log10 (0.25 / (double) channelGain (channel.volume)));
        channel.sampler.release = 0.003f; // a note ends where its channel's next note, off or cut is (not one-shot)
        // zones: each run of keys playing the same sample with the same transpose
        int key = 0;
        while (key < XMP_MAX_KEYS)
        {
            const auto sub = ins.map[key].ins;
            const auto xpo = ins.map[key].xpo;
            auto last = key;
            while (last + 1 < XMP_MAX_KEYS && ins.map[last + 1].ins == sub && ins.map[last + 1].xpo == xpo)
                ++last;
            if (sub < ins.nsm)
            {
                const auto& sb = ins.sub[sub];
                if (sb.sid >= 0 && sb.sid < mod.smp)
                {
                    auto it = written.find (sb.sid);
                    if (it == written.end())
                    {
                        char prefix[16];
                        std::snprintf (prefix, sizeof prefix, "%02d-", i + 1);
                        it = written.emplace (sb.sid, writeSample (ctx, mod, sb.sid, options, prefix, bidirectional)).first;
                        if (it->second.sustainLoop)
                            ++sustainLoops;
                    }
                    if (! it->second.path.empty())
                    {
                        SampleZone z;
                        z.path = it->second.path;
                        // libxmp plays key k at c5spd · 2^((k + xpo + fin/128 − 60) / 12): the zone's root is where the
                        // file (written at c5spd) plays at its own rate
                        z.rootKey = 60 - sb.xpo - xpo;
                        z.tune = (float) sb.fin / 128.0f;
                        z.lowKey = std::clamp (key, 0, 127);
                        z.highKey = std::clamp (last, 0, 127);
                        if (it->second.loopEnd > it->second.loopStart)
                        {
                            z.loopStart = it->second.loopStart;
                            z.loopEnd = it->second.loopEnd;
                            z.loopMode = it->second.sustainLoop ? 2 : 1;
                        }
                        channel.zones.push_back (z);
                    }
                    autoVibrato += sb.vde > 0 && sb.vra > 0 ? 1 : 0;
                    newNoteActions += sb.nna != XMP_INST_NNA_CUT ? 1 : 0;
                }
            }
            key = last + 1;
        }
        if (channel.zones.empty())
            continue;
        // the last key of the map reaches up to 127
        auto& top = *std::max_element (channel.zones.begin(), channel.zones.end(), [] (const SampleZone& a, const SampleZone& b) { return a.highKey < b.highKey; });
        if (top.highKey >= XMP_MAX_KEYS - 1)
            top.highKey = 127;
        channel.samplePath = channel.zones.front().path;
        envelopes += (ins.aei.flg & XMP_ENVELOPE_ON) != 0 || (ins.pei.flg & XMP_ENVELOPE_ON) != 0 || (ins.fei.flg & XMP_ENVELOPE_ON) != 0 ? 1 : 0;
        volumeEnvelopes += (ins.aei.flg & XMP_ENVELOPE_ON) != 0 ? 1 : 0;
        fadeouts += ins.rls > 0 && (ins.aei.flg & XMP_ENVELOPE_ON) != 0 ? 1 : 0;
        channelOfInstrument[(std::size_t) i] = (int) song.channels.size();
        song.channels.push_back (std::move (channel));
        panMoves.push_back ((ins.pei.flg & XMP_ENVELOPE_ON) != 0); // (round 190ob: its pan envelope moves the pan after the note's start)
    }
    count (report, &FlpImportReport::converted, "Module instruments as Sampler channels", (int) song.channels.size());
    count (report, &FlpImportReport::converted, "Module samples with a ping-pong loop (unrolled into the file)", bidirectional);
    count (report, &FlpImportReport::notReproduced, "Module samples with a sustain loop (it loops while the note is held, the loop after it is left out)", sustainLoops);
    count (report, &FlpImportReport::converted, "Module instrument volume envelopes, while the note is held (as volume automation)", volumeEnvelopes);
    count (report, &FlpImportReport::notReproduced, "Module instrument envelopes (pan, pitch or filter; a volume envelope after the note's end)", envelopes);
    count (report, &FlpImportReport::notReproduced, "Module instrument fadeout", fadeouts);
    count (report, &FlpImportReport::notReproduced, "Module instrument auto-vibrato", autoVibrato);
    count (report, &FlpImportReport::notReproduced, "IT new note actions (a new note in a channel ends the one before)", newNoteActions);

    // ---- patterns
    std::vector<int> lynxPattern ((std::size_t) std::max (0, mod.pat), -1);
    auto patternFor = [&] (int p) {
        if (p < 0 || p >= mod.pat || mod.xxp[p] == nullptr)
            return -1;
        if (lynxPattern[(std::size_t) p] < 0)
        {
            Pattern pattern;
            char name[32];
            std::snprintf (name, sizeof name, "Pattern %02d", p);
            pattern.name = name;
            pattern.lengthTicks = std::max (1, mod.xxp[p]->rows) * rt;
            lynxPattern[(std::size_t) p] = (int) song.patterns.size();
            song.patterns.push_back (std::move (pattern));
        }
        return lynxPattern[(std::size_t) p];
    };
    // in the order list's order (the patterns the song never plays too, after them)
    for (int o = 0; o < mod.len && o < XMP_MAX_MOD_LENGTH; ++o)
        if (mod.xxo[o] < mod.pat)
            patternFor (mod.xxo[o]);
    for (const auto& r : flow.rows)
        patternFor (r.pattern);

    // ---- clips: each run of rows played one after another in one pattern
    const auto n = (std::int64_t) flow.rows.size();
    std::vector<std::int64_t> clipEnd ((std::size_t) n, 0);
    for (std::int64_t k = 0; k < n;)
    {
        auto e = k + 1;
        while (e < n && flow.rows[(std::size_t) e].pattern == flow.rows[(std::size_t) k].pattern
               && flow.rows[(std::size_t) e].pos == flow.rows[(std::size_t) k].pos
               && flow.rows[(std::size_t) e].row == flow.rows[(std::size_t) e - 1].row + 1)
            ++e;
        Clip clip;
        clip.kind = ClipKind::pattern;
        clip.pattern = patternFor (flow.rows[(std::size_t) k].pattern);
        clip.track = 0;
        clip.start = k * rt;
        clip.offset = flow.rows[(std::size_t) k].row * rt;
        clip.length = (e - k) * rt;
        if (clip.pattern >= 0)
            song.playlist.push_back (clip);
        for (auto j = k; j < e; ++j)
            clipEnd[(std::size_t) j] = e * rt;
        k = e;
    }
    summary.clips = (int) song.playlist.size();
    if (song.playlistTracks.empty())
        song.playlistTracks.resize (1);
    song.playlistTracks[0].name = "Order list";

    // ---- the notes the song plays (their first time), each to its channel's next note / off / cut
    const auto chn = std::clamp (mod.chn, 0, XMP_MAX_CHANNELS);
    auto tickOf = [&] (std::int64_t k, int frame) {
        const auto& r = flow.rows[(std::size_t) k];
        const auto frac = r.frames > 0 ? (double) frame / (double) r.frames : 0.0;
        return k * rt + (std::int64_t) std::llround (frac * (double) rt);
    };
    std::map<std::tuple<int, int, int>, int> noteOf; // (module pattern, row, channel) → index in its LYNX pattern's notes
    std::vector<std::vector<Note>> notesOf (song.patterns.size());
    std::vector<std::vector<std::int64_t>> lengthOf (song.patterns.size());
    std::set<std::tuple<int, int, int>> heldOver;
    std::map<std::string, std::array<int, 3>> effects; // name → (approximated, not imported, as volume automation) counts
    std::set<std::tuple<int, int, int>> counted;
    std::set<std::tuple<int, int, int>> silentNotes;
    int silent = 0;
    std::vector<Instance> instances; // round 190ob: every played note (for the volume automation)
    for (int c = 0; c < chn; ++c)
    {
        if ((mod.xxc[c].flg & XMP_CHANNEL_MUTE) != 0)
            continue;
        // the next end in this channel at or after row k (a tick), computed backwards
        std::vector<std::int64_t> nextEnd ((std::size_t) n + 1, n * rt);
        for (auto k = n - 1; k >= 0; --k)
        {
            const auto& r = flow.rows[(std::size_t) k].ch[(std::size_t) c];
            nextEnd[(std::size_t) k] = r.endFrame >= 0 ? tickOf (k, r.endFrame) : nextEnd[(std::size_t) k + 1];
        }
        for (std::int64_t k = 0; k < n; ++k)
        {
            const auto& row = flow.rows[(std::size_t) k];
            const auto& r = row.ch[(std::size_t) c];
            if (const auto* e = eventAt (mod, row.pattern, row.row, c); e != nullptr && counted.insert ({ row.pattern, row.row, c }).second)
            {
                if (e->vol > 0 && ! isKey (e->note))
                    ++effects["volume column on a sounding note"][2];
                for (const auto& [type, param] : { std::pair<int, int> { e->fxt, e->fxp }, std::pair<int, int> { e->f2t, e->f2p } })
                {
                    if (type == 0 && param == 0)
                        continue;
                    std::string name;
                    auto cls = effectClass (type, param, name);
                    // a volume or pan set on a row without a note changes the sounding note: the volume as the channel's
                    // automation (round 190ob), the pan not imported yet
                    if (! isKey (e->note) && (type == fxSetVolume || type == fxSetPan || (type == fxExtended && (param >> 4) == exSetPan)))
                    {
                        name = type == fxSetVolume ? "volume set on a sounding note (Cxx without a note)" : "pan set on a sounding note (8xx without a note)";
                        cls = type == fxSetVolume ? 3 : 2;
                    }
                    if (cls == 1)
                        ++effects[name][0];
                    else if (cls == 2)
                        ++effects[name][1];
                    else if (cls == 3)
                        ++effects[name][2];
                }
            }
            if (r.triggerFrame < 0 || r.key < 0)
                continue;
            const auto lp = lynxPattern[(std::size_t) row.pattern];
            const auto ins = r.instrument;
            if (lp < 0 || ins < 0 || ins >= mod.ins || channelOfInstrument[(std::size_t) ins] < 0)
            {
                // an instrument without a sample LYNX can play (an S3M AdLib instrument, an empty slot): left out
                if (silentNotes.insert ({ row.pattern, row.row, c }).second)
                    ++silent;
                continue;
            }
            const auto start = tickOf (k, r.triggerFrame);
            // its end: a cut later in its own row, else the channel's next end; at most its clip's end
            auto end = nextEnd[(std::size_t) k + 1];
            for (auto f : { extendedFrame (*eventAt (mod, row.pattern, row.row, c), exCut), keyOffFrame (*eventAt (mod, row.pattern, row.row, c)) })
                if (f > r.triggerFrame && f < row.frames)
                    end = std::min (end, tickOf (k, f));
            const auto clipStop = clipEnd[(std::size_t) k];
            const auto id = std::make_tuple (row.pattern, row.row, c);
            if (end > clipStop)
                heldOver.insert (id);
            end = std::min (end, clipStop);
            const auto rowStartInSong = k * rt;
            const auto inPattern = (std::int64_t) row.row * rt + (start - rowStartInSong);
            const auto length = std::max<std::int64_t> (1, end - start);
            if (auto found = noteOf.find (id); found != noteOf.end())
            {
                auto& l = lengthOf[(std::size_t) lp][(std::size_t) found->second];
                l = std::min (l, length);
                instances.push_back ({ lp, found->second, c, k, r.triggerFrame, start });
                continue;
            }
            Note note;
            note.start = inPattern;
            note.length = length;
            note.channel = channelOfInstrument[(std::size_t) ins];
            note.key = std::clamp (r.key, 0, 127);
            note.velocity = std::clamp ((float) std::max (r.volume, r.maxVolume) / 64.0f, 0.0f, 1.0f);
            note.pan = r.pan < 128 ? (float) (r.pan - 128) / 128.0f : (float) (r.pan - 128) / 127.0f;
            note.pan = std::clamp (note.pan, -1.0f, 1.0f);
            note.porta = r.porta;
            noteOf[id] = (int) notesOf[(std::size_t) lp].size();
            instances.push_back ({ lp, (int) notesOf[(std::size_t) lp].size(), c, k, r.triggerFrame, start });
            notesOf[(std::size_t) lp].push_back (note);
            lengthOf[(std::size_t) lp].push_back (length);
        }
    }
    summary.heldOverClipEnd = (int) heldOver.size();
    std::vector<std::size_t> playedNotes; // per pattern: its notes before the unplayed rows' (round 190ob)
    for (const auto& notes : notesOf)
        playedNotes.push_back (notes.size());

    // ---- the notes in the rows the song never plays (pattern data all the same): each to the next note / off / cut of
    // its channel in the pattern (or the pattern's end), at its event's volume
    int unplayed = 0;
    for (int p = 0; p < mod.pat; ++p)
    {
        const auto lp = p < (int) lynxPattern.size() ? lynxPattern[(std::size_t) p] : -1;
        if (lp < 0 || mod.xxp[p] == nullptr)
            continue;
        const auto rows = mod.xxp[p]->rows;
        for (int c = 0; c < chn; ++c)
        {
            int lastInstrument = -1;
            for (int row = 0; row < rows; ++row)
            {
                const auto* e = eventAt (mod, p, row, c);
                if (e == nullptr)
                    continue;
                if (e->ins > 0)
                    lastInstrument = e->ins - 1;
                if (! isKey (e->note) || noteOf.count ({ p, row, c }) > 0 || (mod.xxc[c].flg & XMP_CHANNEL_MUTE) != 0)
                    continue;
                if (lastInstrument < 0 || lastInstrument >= mod.ins || channelOfInstrument[(std::size_t) lastInstrument] < 0)
                    continue;
                const auto start = (std::int64_t) row * rt + std::max (0, extendedFrame (*e, exDelay)) * rt / std::max (1, mod.spd);
                auto end = (std::int64_t) rows * rt;
                for (int r2 = row + 1; r2 < rows; ++r2)
                    if (const auto* e2 = eventAt (mod, p, r2, c); e2 != nullptr && (isKey (e2->note) || isEnd (e2->note) || extendedFrame (*e2, exCut) >= 0))
                    {
                        end = (std::int64_t) r2 * rt;
                        break;
                    }
                const auto& ins = mod.xxi[lastInstrument];
                int volume = 64;
                if (e->vol > 0)
                    volume = e->vol - 1;
                else if (hasFx (*e, fxSetVolume))
                    volume = e->fxt == fxSetVolume ? e->fxp : e->f2p;
                else if (ins.nsm > 0 && ins.sub != nullptr && e->note - 1 < XMP_MAX_KEYS && ins.map[e->note - 1].ins < ins.nsm)
                    volume = ins.sub[ins.map[e->note - 1].ins].vol;
                Note note;
                note.start = start;
                note.length = std::max<std::int64_t> (1, end - start);
                note.channel = channelOfInstrument[(std::size_t) lastInstrument];
                note.key = std::clamp (e->note - 1, 0, 127);
                note.velocity = std::clamp (volume / 64.0f, 0.0f, 1.0f);
                note.porta = tonePorta (*e);
                notesOf[(std::size_t) lp].push_back (note);
                lengthOf[(std::size_t) lp].push_back (note.length);
                ++unplayed;
            }
        }
    }
    // ---- round 190ob: the volume changes of sounding notes as the channels' Volume multiplier automation
    const auto channelsBefore = song.channels.size();
    const auto volume = volumeAutomation (song, flow, chn, n * rt, notesOf, lengthOf, std::move (instances), tickOf);
    for (auto c = channelsBefore; c < song.channels.size(); ++c) // (a channel copied for a module channel: its instrument's)
    {
        const auto source = std::find_if (song.channels.begin(), song.channels.begin() + (std::ptrdiff_t) channelsBefore, [&] (const ChannelDef& d) {
            return d.zones == song.channels[c].zones && d.samplePath == song.channels[c].samplePath;
        });
        panMoves.push_back (source != song.channels.begin() + (std::ptrdiff_t) channelsBefore && panMoves[(std::size_t) (source - song.channels.begin())]);
    }
    summary.volumeChangedNotes = volume.changedNotes;
    summary.volumeAutomations = volume.automations;
    summary.volumePoints = volume.points;
    summary.splitChannels = volume.splitChannels;
    count (report, &FlpImportReport::converted, "Module notes whose volume changes while they sound (volume slides and sets, volume column, tremolo, "
                                                "global / channel volume, the volume envelope while held): as the channel's Volume multiplier automation",
           volume.changedNotes - volume.leftOut);
    count (report, &FlpImportReport::converted, "Module instruments sounding in several module channels at once, each with its own volume: one channel per module channel",
           volume.splitChannels);
    count (report, &FlpImportReport::notReproduced, "Module notes whose volume changes while they sound (too many channels to give each its own)", volume.leftOut);
    // ---- round 190ob: libxmp pans linearly (a voice panned hard is twice as loud on its side as at the centre: 0x80 ∓ pan
    // per side), LYNX's voices with constant power (√2 at the side, 1 at the centre). A channel whose played notes all
    // have one pan level (a MOD's or S3M's channel: its module channel's pan) gets that level as its Sampler gain and
    // each note the pan with libxmp's left / right balance: exact. Notes of several pan levels on one channel (XM / IT pan
    // commands), and an instrument whose pan envelope moves the pan after the start, stay as they were (one gain cannot be
    // exact for them). The velocities stay the module's.
    {
        struct Level
        {
            double low = 1.0e9, high = 0.0;
        };
        std::vector<Level> levels (song.channels.size());
        const auto voicePanOf = [] (const Note& note) { return (double) std::lround (note.pan >= 0.0f ? note.pan * 127.0f : note.pan * 128.0f); }; // (libxmp's −128…127)
        const auto levelOf = [] (double voicePan) {
            const auto left = (128.0 - voicePan) / 128.0, right = (128.0 + voicePan) / 128.0;
            return std::sqrt (left * left + right * right) / std::sqrt (2.0);
        };
        for (std::size_t p = 0; p < notesOf.size(); ++p)
            for (std::size_t k = 0; k < playedNotes[p]; ++k)
                if (const auto& note = notesOf[p][k]; note.channel >= 0 && note.channel < (int) song.channels.size())
                {
                    auto& l = levels[(std::size_t) note.channel];
                    const auto level = levelOf (voicePanOf (note));
                    l.low = std::min (l.low, level);
                    l.high = std::max (l.high, level);
                }
        int moved = 0;
        for (std::size_t p = 0; p < notesOf.size(); ++p)
            for (std::size_t k = 0; k < playedNotes[p]; ++k)
            {
                auto& note = notesOf[p][k];
                if (note.channel < 0 || note.channel >= (int) song.channels.size())
                    continue;
                const auto& l = levels[(std::size_t) note.channel];
                if (l.high > l.low * 1.0012 || panMoves[(std::size_t) note.channel]) // (more than 0.01 dB apart: several pan levels)
                    continue;
                const auto voicePan = voicePanOf (note);
                if (voicePan == 0.0)
                    continue;
                const auto left = (128.0 - voicePan) / 128.0, right = (128.0 + voicePan) / 128.0;
                note.pan = (float) std::clamp (std::atan2 (right, left) / (0.25 * 3.14159265358979) - 1.0, -1.0, 1.0);
                ++moved;
            }
        for (std::size_t c = 0; c < song.channels.size(); ++c)
            if (levels[c].high > 0.0 && levels[c].high <= levels[c].low * 1.0012 && ! panMoves[c])
                song.channels[c].sampler.gainDb += (float) (20.0 * std::log10 (std::sqrt (levels[c].low * levels[c].high)));
        count (report, &FlpImportReport::converted, "Module notes panned off the centre, as libxmp mixes them (its linear pan law: the note's pan and the channel's gain)", moved);
    }
    for (std::size_t p = 0; p < song.patterns.size(); ++p)
    {
        auto& notes = notesOf[p];
        for (std::size_t i = 0; i < notes.size(); ++i)
            notes[i].length = std::max<std::int64_t> (1, lengthOf[p][i]);
        song.patterns[p].notes = std::move (notes);
        song.patterns[p].sortNotes();
        summary.notes += (int) song.patterns[p].notes.size();
    }
    count (report, &FlpImportReport::notReproduced, "Module notes held past a pattern's end (they end with their clip)", summary.heldOverClipEnd);
    count (report, &FlpImportReport::notReproduced, "Module notes of instruments without a sample (S3M AdLib instruments, empty slots): left out", silent);
    for (const auto& [name, n2] : effects)
    {
        count (report, &FlpImportReport::notReproduced, "Module effect played differently: " + name, n2[0]);
        count (report, &FlpImportReport::notReproduced, "Module effect not imported: " + name, n2[1]);
        count (report, &FlpImportReport::converted, "Module effect as volume automation: " + name, n2[2]);
    }
    (void) unplayed;

    // ---- the tempo: each row's real length (speed, tempo, row delay) as the tempo of its beat
    if (! flow.rows.empty())
    {
        auto bpmOf = [rpb] (const PlayedRow& r) { return r.ms > 0.0 ? std::clamp (60000.0 / (rpb * r.ms), minTempo, maxTempo) : 120.0; };
        song.bpm = bpmOf (flow.rows.front());
        auto current = song.bpm;
        for (std::int64_t k = 1; k < n; ++k)
        {
            const auto bpm = bpmOf (flow.rows[(std::size_t) k]);
            if (std::abs (bpm - current) > 1.0e-6)
            {
                tempomarkers::add (song, k * rt, bpm);
                current = bpm;
                ++summary.tempoChanges;
            }
        }
        if (summary.tempoChanges > 0)
            if (const auto a = tempomarkers::automationIndex (song); a >= 0)
                song.automations[(std::size_t) a].points.front().value = valueFromTempo (song.bpm);
    }
    else
        song.bpm = std::clamp (mod.bpm > 0 && mod.spd > 0 ? mod.bpm * 6.0 / mod.spd : 125.0, minTempo, maxTempo);

    // ---- the module's loop point: the song loop marker
    if (flow.loopPos >= 0)
        for (std::int64_t k = 0; k < n; ++k)
            if (flow.rows[(std::size_t) k].pos == flow.loopPos && flow.rows[(std::size_t) k].row == flow.loopRow)
            {
                markers::placeSongLoop (song, k * rt);
                summary.loopTick = k * rt;
                break;
            }
    summary.patterns = (int) song.patterns.size();
    if (song.patterns.empty())
    {
        Pattern empty;
        empty.name = "Pattern 1";
        song.patterns.push_back (empty);
    }
    song.currentPattern = song.playlist.empty() ? 0 : song.playlist.front().pattern;
    if (options.summary != nullptr)
        *options.summary = summary;
    return song;
}
} // namespace lynx
