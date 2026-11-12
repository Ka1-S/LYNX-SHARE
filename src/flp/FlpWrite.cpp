#include "flp/FlpWrite.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace flp
{
namespace
{
// Event IDs (the same as Flp.cpp's reader; names describe the meaning)
enum : std::uint32_t
{
    evChannelEnabled = 0x00,
    evSongMode = 0x09,
    evShowInfo = 0x0A,
    evSwing = 0x0B,
    evTimeSigNumerator = 0x11,
    evTimeSigDenominator = 0x12,
    evSamplePingPong = 0x14,
    evChannelType = 0x15,
    evPanLaw = 0x17,
    evRegistered = 0x1C,
    evAutoPdc = 0x1D,
    evPlayTruncated = 0x1E,
    evPerformanceMode = 0x1F,
    evChannelSoloLock = 0x20,
    evMarkerNumerator = 0x21,
    evMarkerDenominator = 0x22,
    evTimeSetting = 0x23,
    evArrangementFlag = 0x24,
    evSaved25 = 0x25,
    evMixerFlag26 = 0x26,
    evCompensateAutomations = 0x27,
    evFastDeclick = 0x28,
    evChannel29 = 0x29,
    evMixerColourSet = 0x2A,
    evPlaylistTrackColourSet = 0x2B,
    evAutoCrossfades = 0x2C,
    evEnd2F = 0x2F,
    evChannel30 = 0x30,
    evMixerLatencyBypass = 0x31,
    evChannelGhost = 0x32,
    evChannel33 = 0x33,
    evPattern34 = 0x34,
    evNewChannel = 0x40,
    evNewPattern = 0x41,
    evCurrentPattern = 0x43,
    evChannelEq = 0x45,
    evChannelFxFlags = 0x46,
    evChannelCutoff = 0x47,
    evChannelBoost = 0x4A,
    evChannelFadeOut = 0x4B,
    evChannelFadeIn = 0x4C,
    evMainPitch = 0x50,
    evChannelResonance = 0x53,
    evChannelStereoDelay = 0x55,
    evChannelPogo = 0x56,
    evChannelTimeShift = 0x59,
    evLayerChild = 0x5E,
    evMixerIcon = 0x5F,
    evChannelSwingMix = 0x61,
    evMixerSlotIndex = 0x62,
    evArrangement = 0x63,
    evCurrentArrangement = 0x64,
    evMixerTrackCount = 0x67,
    evChannelMixerTrack = 0x68,
    evChannelColour = 0x80,
    evChannelRingMod = 0x83,
    evChannelCutGroups = 0x84,
    evMixerSelected = 0x85,
    evSampleRootNote = 0x87,
    evChannelEchoModXY = 0x8A,
    evChannelReverb = 0x8B,
    evChannelFineTune = 0x8E,
    evSampleFlags = 0x8F,
    evLayerFlags = 0x90,
    evChannelFilterGroup = 0x91,
    evFilterGroupCurrent = 0x92,
    evMixerOutput = 0x93,
    evTimeMarker = 0x94,
    evMixerColour = 0x95,
    evPatternColour = 0x96,
    evMixerInput = 0x9A,
    evChannelIcon = 0x9B,
    evTempo = 0x9C,
    evPattern9D = 0x9D,
    evPattern9E = 0x9E,
    evBuild = 0x9F,
    evPatternLoopChannel = 0xA0,
    evPatternLoopSetting = 0xA1,
    evPatternLoopMode = 0xA2,
    evPatternLength = 0xA4,
    evMixerRecordSource = 0xA5,
    evMixerMonitor = 0xA6,
    evGlobalA7 = 0xA7,
    evFileFlags = 0xA9,
    evChannelAA = 0xAA,
    evPatternAB = 0xAB,
    evPatternName = 0xC1,
    evTitle = 0xC2,
    evComments = 0xC3,
    evSamplePath = 0xC4,
    evUrl = 0xC5,
    evVersion = 0xC7,
    evPluginInternalName = 0xC9,
    evDataFolder = 0xCA,
    evChannelName = 0xCB,
    evMixerName = 0xCC,
    evMarkerName = 0xCD,
    evGenre = 0xCE,
    evArtist = 0xCF,
    evChannelEcho = 0xD1,
    evPluginNew = 0xD4,
    evPluginState = 0xD5,
    evChannelParams = 0xD7,
    evInitControls = 0xD8,
    evChannelEnvelope = 0xDA,
    evChannelLevels = 0xDB,
    evChannelPolyphony = 0xDD,
    evPatternEvents = 0xDF,
    evPatternNotes = 0xE0,
    evControlValues = 0xE1,
    evAutomationLink = 0xE3,
    evChannelTracking = 0xE4,
    evChannelLevelAdjust = 0xE5,
    evFilterGroupName = 0xE7,
    evPlaylistItems = 0xE9,
    evAutomationPoints = 0xEA,
    evMixerRoutes = 0xEB,
    evMixerParams = 0xEC,
    evProjectTimes = 0xED,
    evPlaylistTrack = 0xEE,
    evPlaylistTrackName = 0xEF,
    evArrangementName = 0xF1,
    evPlaylistF2 = 0xF2,
    evScaleHighlight = 0xF3,
    evPatternFC = 0xFC,
    evPatternFD = 0xFD,
};

constexpr std::uint16_t playlistPatternBase = 20480;
constexpr int playlistTracks = 500;

// FL 2026's default channel record 0xD7 (a new Sampler channel, FL 26.1.7) [DATA]
const std::uint8_t channelParamsDefault[168] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x04, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0xa7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf0, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x3f,
};

std::uint32_t bgr (std::uint32_t rgb) noexcept
{
    return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

struct Out
{
    std::vector<std::uint8_t> bytes;

    void raw (const void* p, std::size_t n)
    {
        const auto* b = (const std::uint8_t*) p;
        bytes.insert (bytes.end(), b, b + n);
    }
    void u8 (std::uint32_t id, std::uint32_t v)
    {
        bytes.push_back ((std::uint8_t) id);
        bytes.push_back ((std::uint8_t) v);
    }
    void u16 (std::uint32_t id, std::uint32_t v)
    {
        bytes.push_back ((std::uint8_t) id);
        bytes.push_back ((std::uint8_t) (v & 0xFF));
        bytes.push_back ((std::uint8_t) ((v >> 8) & 0xFF));
    }
    void u32 (std::uint32_t id, std::uint32_t v)
    {
        bytes.push_back ((std::uint8_t) id);
        for (int k = 0; k < 4; ++k)
            bytes.push_back ((std::uint8_t) (v >> (8 * k)));
    }
    void data (std::uint32_t id, const std::vector<std::uint8_t>& d)
    {
        bytes.push_back ((std::uint8_t) id);
        auto size = d.size();
        do
        {
            auto b = (std::uint8_t) (size & 0x7F);
            size >>= 7;
            bytes.push_back (size != 0 ? (std::uint8_t) (b | 0x80) : b);
        } while (size != 0);
        bytes.insert (bytes.end(), d.begin(), d.end());
    }
    void text (std::uint32_t id, const std::string& s) { data (id, utf16Text (s)); }
};

struct Blob
{
    std::vector<std::uint8_t> b;
    template <typename T>
    void put (T v)
    {
        std::uint8_t tmp[sizeof (T)];
        std::memcpy (tmp, &v, sizeof (T));
        b.insert (b.end(), tmp, tmp + sizeof (T));
    }
    template <typename T>
    void at (std::size_t offset, T v)
    {
        if (offset + sizeof (T) <= b.size())
            std::memcpy (b.data() + offset, &v, sizeof (T));
    }
};

std::vector<std::uint8_t> ansiText (const std::string& s)
{
    std::vector<std::uint8_t> v (s.begin(), s.end());
    v.push_back (0);
    return v;
}

void decodeUtf8 (const std::string& s, std::vector<std::uint32_t>& cps)
{
    for (std::size_t i = 0; i < s.size();)
    {
        const auto c = (unsigned char) s[i];
        std::uint32_t cp = c;
        int extra = 0;
        if (c >= 0xF0)
            cp = c & 0x07, extra = 3;
        else if (c >= 0xE0)
            cp = c & 0x0F, extra = 2;
        else if (c >= 0xC0)
            cp = c & 0x1F, extra = 1;
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | ((unsigned char) s[i] & 0x3F);
        cps.push_back (cp);
    }
}

// one 0xD4 plugin record (13 × i32; FL 2026's layouts [DATA]): track/slot (effects), kind 2 = a plugin (−1 = the
// Sampler, no plugin), the wrapper flags (u16 at +16: 0x10 generator, 0x20 smart disable, 0x40 threaded, 0x100 FL's
// own plugin), the editor's last position (x, y at +36 / +40)
std::vector<std::uint8_t> pluginRecord (int track, int slot, bool plugin, int flags)
{
    Blob r;
    r.put<std::int32_t> (track);
    r.put<std::int32_t> (slot);
    r.put<std::int32_t> (plugin ? 2 : -1);
    r.put<std::int32_t> (0);
    r.put<std::int32_t> (flags);
    r.put<std::int32_t> (plugin ? 0 : 3);
    for (int k = 0; k < 3; ++k)
        r.put<std::int32_t> (0);
    r.put<std::int32_t> (0x88);
    r.put<std::int32_t> (0xb2);
    r.put<std::int32_t> (0);
    r.put<std::int32_t> (0);
    return r.b;
}

std::vector<std::uint8_t> controlRecords (const std::vector<ControlEvent>& list)
{
    Blob b;
    for (const auto& e : list)
    {
        b.put<std::uint32_t> (e.tick);
        b.put<std::uint32_t> (e.id.raw);
        b.put<std::int32_t> (e.value);
    }
    return b.b;
}

void writeMarkers (Out& out, const std::vector<TimeMarker>& markers)
{
    for (const auto& m : markers)
    {
        out.u32 (evTimeMarker, ((std::uint32_t) (m.type & 0xFF) << 24) | (m.position & 0xFFFFFFu));
        if (m.type == 8)
        {
            out.u8 (evMarkerNumerator, (std::uint32_t) std::clamp (m.numerator, 1, 32));
            out.u8 (evMarkerDenominator, (std::uint32_t) std::clamp (m.denominator, 1, 32));
        }
        out.text (evMarkerName, m.name);
    }
}

std::vector<std::uint8_t> envelopeRecord (const Channel::Envelope& env, int record)
{
    if (env.present && env.rawSize >= 68)
        return std::vector<std::uint8_t> (env.raw.begin(), env.raw.begin() + 68);
    auto d = defaultEnvelope (record);
    if (! env.present)
        return d;
    Blob b { d };
    b.at<std::int32_t> (0, env.flags);
    b.at<std::int32_t> (4, env.enabled ? 1 : 0);
    b.at<std::int32_t> (8, env.delay);
    b.at<std::int32_t> (12, env.attack);
    b.at<std::int32_t> (16, env.hold);
    b.at<std::int32_t> (20, env.decay);
    b.at<std::int32_t> (24, env.sustain);
    b.at<std::int32_t> (28, env.release);
    b.at<std::int32_t> (32, env.amount);
    b.at<std::int32_t> (0x24, env.lfoDelay);
    b.at<std::int32_t> (0x28, env.lfoAttack);
    b.at<std::int32_t> (0x2C, env.lfoAmount);
    b.at<std::int32_t> (0x30, env.lfoSpeed);
    b.at<std::int32_t> (0x34, env.lfoShape);
    return b.b;
}

std::vector<std::uint8_t> channelParams (const Channel& c)
{
    Blob b { defaultChannelParams() };
    b.b[0x09] = c.removeDc ? 1 : 0;
    b.b[10] = (std::uint8_t) c.delayFlags;
    b.b[11] = c.mainPitch ? 1 : 0;
    b.at<std::int32_t> (40, c.arpDirection);
    b.at<std::int32_t> (44, c.arpRange);
    b.at<std::int32_t> (48, c.arpChord);
    b.at<std::int32_t> (52, c.arpTime);
    b.at<std::int32_t> (56, c.arpGate);
    b.b[60] = c.arpSlide ? 1 : 0;
    b.b[62] = c.fullPorta ? 1 : 0;
    b.b[63] = c.addToKey ? 1 : 0;
    b.at<std::uint16_t> (64, (std::uint16_t) std::clamp (c.timeGate, 0, 0xFFFF));
    b.at<std::int32_t> (0x44, c.keyRegionLow);
    b.at<std::int32_t> (0x48, c.keyRegionHigh);
    b.at<std::int32_t> (0x4C, c.layerFade);
    b.b[0x50] = c.normalize ? 1 : 0;
    b.b[0x51] = c.reversePolarity ? 1 : 0;
    b.b[0x53] = (std::uint8_t) c.declickMode;
    b.at<std::int32_t> (0x54, c.crossfade);
    b.at<std::int32_t> (0x58, c.trim);
    b.at<std::int32_t> (92, c.arpRepeat);
    b.at<float> (0x60, c.stretchTime); // (file flags bit 0: a float)
    b.at<std::int32_t> (0x64, c.stretchPitch);
    b.at<std::int32_t> (0x68, c.stretchMul);
    b.at<std::int32_t> (0x6C, c.stretchMode);
    b.at<std::int32_t> (112, c.pitchRange);
    b.at<double> (0x80, c.sampleStart);
    b.at<double> (0x88, c.sampleEnd);
    b.at<double> (0x90, c.startOffset);
    return b.b;
}

void writeChannel (Out& out, const Channel& c)
{
    out.u16 (evNewChannel, (std::uint32_t) c.index);
    out.u8 (evChannelType, (std::uint32_t) (c.type == ChannelType::unknown ? ChannelType::sampler : c.type));
    out.text (evPluginInternalName, c.pluginName);
    const auto plugin = ! c.pluginName.empty();
    const auto flags = c.wrapperFlags >= 0 ? c.wrapperFlags : plugin ? 0x150 : 0x50; // (FL 26: FLEX / FL Keys 0x150, samplers 0x50)
    out.data (evPluginNew, pluginRecord (0, 0, plugin, flags));
    out.text (evChannelName, c.name);
    out.u32 (evChannelIcon, (std::uint32_t) c.icon);
    if (c.colour != 0) // (files without it: FL draws its default channel colour)
        out.u32 (evChannelColour, bgr (c.colour));
    out.u8 (evChannel29, 0);
    if (! c.pluginState.empty())
        out.data (evPluginState, c.pluginState);
    out.u8 (evChannelEnabled, c.enabled ? 1 : 0);
    {
        Blob e;
        e.put<std::int32_t> (c.echoFeedback);
        e.put<std::int32_t> (c.echoPan);
        e.put<std::int32_t> (c.echoPitch);
        e.put<std::int32_t> (c.echoes);
        e.put<std::int32_t> (c.echoTime);
        out.data (evChannelEcho, e.b);
    }
    out.u32 (evChannelEchoModXY, (std::uint32_t) (c.echoModX & 0xFFFF) | ((std::uint32_t) (c.echoModY & 0xFFFF) << 16));
    out.u32 (evChannelReverb, (std::uint32_t) (c.reverb & 0xFFFF) | ((std::uint32_t) (c.reverbType & 0xFFFF) << 16));
    out.u16 (evChannelTimeShift, (std::uint32_t) c.timeShift);
    out.u16 (evChannelSwingMix, (std::uint32_t) c.swingMix);
    out.u8 (evChannel30, 0);
    out.u16 (evChannelEq, (std::uint32_t) c.eq);
    out.u16 (evChannelPogo, (std::uint32_t) c.pogo);
    out.u16 (evChannelCutoff, (std::uint32_t) c.cutoff);
    out.u16 (evChannelResonance, (std::uint32_t) c.resonance);
    out.u16 (evChannelBoost, (std::uint32_t) c.boost);
    out.u16 (evChannelFadeOut, (std::uint32_t) c.fadeOut);
    out.u16 (evChannelFadeIn, (std::uint32_t) c.fadeIn);
    out.u16 (evChannelStereoDelay, (std::uint32_t) c.stereoDelay);
    out.u32 (evChannelRingMod, (std::uint32_t) (c.ringMix & 0xFFFF) | ((std::uint32_t) (c.ringFreq & 0xFFFF) << 16));
    out.u16 (evChannelFxFlags, (std::uint32_t) c.fxFlags);
    out.u16 (evChannelMixerTrack, (std::uint32_t) std::max (0, c.mixerTrack));
    out.u8 (evChannelGhost, c.ghostShown ? 1 : 0);
    {
        Blob l;
        l.put<std::int32_t> (c.pan);
        l.put<std::int32_t> (c.volume);
        l.put<std::int32_t> (c.pitchCents);
        l.put<std::int32_t> (c.modX);
        l.put<std::int32_t> (c.modY);
        l.put<std::int32_t> (c.filterType);
        out.data (evChannelLevels, l.b);
    }
    {
        Blob a;
        a.put<std::int32_t> (c.panAdj);
        a.put<std::int32_t> (c.volAdj);
        a.put<std::int32_t> (c.adjUnknown); // (round 190dz: FL's Levels adjustment, whole)
        a.put<std::int32_t> (c.modXAdj);
        a.put<std::int32_t> (c.modYAdj);
        out.data (evChannelLevelAdjust, a.b);
    }
    {
        Blob p;
        p.put<std::int32_t> (c.maxVoices);
        p.put<std::int32_t> (c.slideTime);
        p.b.push_back ((std::uint8_t) ((c.monoFlag ? 1 : 0) | (c.portaFlag ? 2 : 0)));
        out.data (evChannelPolyphony, p.b);
    }
    out.data (evChannelParams, channelParams (c));
    out.u32 (evChannelCutGroups, (std::uint32_t) (c.cutGroup & 0xFFFF) | ((std::uint32_t) c.cutBy << 16));
    out.u32 (evLayerFlags, (std::uint32_t) c.layerFlags);
    for (const auto child : c.layerChildren)
        out.u16 (evLayerChild, (std::uint32_t) child);
    out.u32 (evChannelFilterGroup, (std::uint32_t) std::max (0, c.filterGroup));
    if (c.type == ChannelType::automationClip)
    {
        const auto& lfo = c.automationLfo; // (round 177b: FL's articulator stream, FUN_00fe8640; defaults = an untouched LFO)
        Blob a;
        a.put<std::uint32_t> (1);
        a.put<std::int32_t> (lfo.level);
        a.b.push_back (lfo.mode);
        a.put<std::uint32_t> (4);
        a.put<std::uint32_t> (3);
        a.put<std::uint32_t> ((std::uint32_t) c.automation.size());
        double previous = 0.0;
        for (const auto& p : c.automation)
        {
            a.put<double> (p.position - previous);
            previous = p.position;
            a.put<double> (p.value);
            a.put<float> (p.tension);
            a.b.push_back (p.curve);
            a.b.push_back (0);
            a.b.push_back (0);
            a.b.push_back (0);
        }
        // the main envelope's tail, the A/D/S/R knobs, then the LFO (kind 5, version 3, its points, its knobs)
        a.put<std::int32_t> (lfo.mainFlags);
        a.put<std::int32_t> (lfo.mainEnabled);
        for (const auto v : lfo.mainLoop)
            a.put<std::int32_t> (v);
        for (const auto v : lfo.adsr)
            a.put<std::int32_t> (v);
        a.put<std::int32_t> (5);
        a.put<std::int32_t> (3);
        const auto lfoPoints = lfo.points.empty() ? std::vector<flp::AutomationPoint> { flp::AutomationPoint { 0.0, 1.0, 0.0f, 0 } } : lfo.points;
        a.put<std::uint32_t> ((std::uint32_t) lfoPoints.size());
        if (lfo.rawPoints.size() == lfoPoints.size() * 24)
            a.b.insert (a.b.end(), lfo.rawPoints.begin(), lfo.rawPoints.end()); // as read: byte for byte
        else
        {
            double last = 0.0;
            for (const auto& p : lfoPoints)
            {
                a.put<double> (p.position - last);
                last = p.position;
                a.put<double> (p.value);
                a.put<float> (p.tension);
                a.b.push_back (p.curve);
                a.b.push_back (0);
                a.b.push_back (0);
                a.b.push_back (0);
            }
        }
        a.put<std::int32_t> (lfo.flags);
        a.put<std::int32_t> (lfo.enabled);
        for (const auto v : lfo.lfoLoop)
            a.put<std::int32_t> (v);
        a.put<std::int32_t> (lfo.speed);
        a.put<std::int32_t> (lfo.tension);
        a.put<std::int32_t> (lfo.skew);
        a.put<std::int32_t> (lfo.pulseWidth);
        a.put<std::uint32_t> (lfo.phase);
        out.data (evAutomationPoints, a.b);
    }
    out.u8 (evChannelSoloLock, c.soloLock ? 1 : 0);
    for (int k = 0; k < 2; ++k)
    {
        const auto& t = c.tracking[(std::size_t) k];
        Blob b;
        b.put<std::int32_t> (t.present ? t.mid : (k == 0 ? 100 : 60));
        b.put<std::int32_t> (t.present ? t.pan : 0);
        b.put<std::int32_t> (t.present ? t.modX : 0);
        b.put<std::int32_t> (t.present ? t.modY : 0);
        out.data (evChannelTracking, b.b);
    }
    for (int k = 0; k < 5; ++k)
        out.data (evChannelEnvelope, envelopeRecord (c.envelopes[(std::size_t) k], k));
    if (c.rootNote >= 0)
        out.u32 (evSampleRootNote, (std::uint32_t) c.rootNote);
    if (c.fineTune != 0)
        out.u32 (evChannelFineTune, (std::uint32_t) c.fineTune);
    out.u32 (evSampleFlags, (std::uint32_t) (c.sampleFlags >= 0 ? c.sampleFlags : 2));
    out.u8 (evSamplePingPong, c.pingPong ? 1 : 0);
    if (! c.samplePath.empty())
        out.text (evSamplePath, c.samplePath);
    out.u32 (evChannelAA, 0xFFFFFFFFu);
    out.u8 (evChannel33, 0);
}

void writePatternInfo (Out& out, const Pattern& p)
{
    out.u16 (evNewPattern, (std::uint32_t) p.index);
    if (! p.name.empty())
        out.text (evPatternName, p.name);
    if (p.colour.has_value())
        out.u32 (evPatternColour, bgr (*p.colour));
    out.u32 (evPattern9D, 0xFFFFFFFFu);
    out.u32 (evPattern9E, 0xFFFFFFFFu);
    writeMarkers (out, p.markers);
    if (p.loopOn)
        out.u8 (0x1A, 1);
    if (p.loopOn || ! p.channelLoops.empty())
    {
        out.u32 (evPatternLoopMode, (std::uint32_t) std::clamp (p.loopMode, 1, 3));
        for (const auto& [channel, setting] : p.channelLoops)
        {
            out.u32 (evPatternLoopChannel, (std::uint32_t) channel);
            out.u32 (evPatternLoopSetting, (std::uint32_t) setting);
        }
    }
    out.u32 (evPatternLength, p.fixedLength);
    out.u8 (evPattern34, 0);
    out.u32 (evPatternAB, 0);
    out.text (evPatternFC, "");
    out.text (evPatternFD, "0,0,0,0,0,0,0,0,0,0,0,0");
}

std::vector<std::uint8_t> playlistRecords (const std::vector<PlaylistItem>& items)
{
    Blob b;
    std::uint32_t serial = 0;
    for (const auto& item : items)
    {
        const auto start = b.b.size();
        b.put<std::uint32_t> (item.position);
        b.put<std::uint16_t> (playlistPatternBase);
        const auto index = item.kind == PlaylistItem::Kind::pattern ? playlistPatternBase + item.index : item.index;
        b.put<std::uint16_t> ((std::uint16_t) index);
        b.put<std::uint32_t> (item.length);
        b.put<std::uint16_t> ((std::uint16_t) (playlistTracks - 1 - std::clamp (item.track, 0, playlistTracks - 1)));
        b.put<std::uint16_t> (0);
        b.put<std::uint16_t> (120);
        b.put<std::uint16_t> (item.flags != 0 ? item.flags : 0x40);
        b.put<std::uint32_t> (0x80806440u);
        if (item.kind == PlaylistItem::Kind::pattern)
        {
            b.put<std::int32_t> (item.patternStartTick);
            b.put<std::int32_t> (item.patternEndTick);
        }
        else
        {
            b.put<float> (item.startOffset);
            b.put<float> (item.endOffset);
        }
        b.put<std::uint32_t> (++serial);
        b.put<float> (item.fadeInMs);    // +0x24
        b.put<float> (item.fadeInTension);  // +0x28
        b.put<float> (item.fadeOutMs);   // +0x2C
        b.put<float> (item.fadeOutTension); // +0x30
        b.put<float> (item.gain); // +0x34 (FL 26 saves 0 or 1 for pattern clips; the reader keeps what it finds)
        // +0x38: a written fade is manual; a manual fade of length 0 stays manual (FL plays a sharp cut there, no declick)
        b.put<std::uint32_t> (item.fadeFlags | (item.fadeInMs > 0.0f ? 1u : 0u) | (item.fadeOutMs > 0.0f ? 2u : 0u));
        b.put<float> (item.pitch);       // +0x3C
        b.put<double> (item.stretch);    // +0x40
        b.put<std::uint32_t> (item.reversed ? 1u : 0u); // +0x48
        b.put<std::int32_t> (-1);        // +0x4C
        b.put<std::uint32_t> (0);        // +0x50
        b.put<std::uint32_t> (0);        // +0x54
        (void) start;
    }
    return b.b;
}

std::vector<std::uint8_t> playlistTrackRecord (const PlaylistTrackInfo& t)
{
    Blob b { defaultPlaylistTrack (t.index) };
    b.at<std::uint32_t> (4, bgr (t.colour));
    b.at<std::uint32_t> (8, (std::uint32_t) t.icon);
    b.b[12] = t.enabled ? 1 : 0;
    b.at<float> (13, t.heightScale);
    b.at<std::int32_t> (17, t.lockedHeight);
    b.b[21] = t.lockedToContent ? 1 : 0;
    b.at<std::uint32_t> (22, (std::uint32_t) t.motion);
    b.at<std::uint32_t> (26, (std::uint32_t) t.press);
    b.at<std::uint32_t> (30, (std::uint32_t) t.triggerSync);
    b.at<std::uint32_t> (34, t.queue ? 1u : 0u);
    b.at<std::uint32_t> (38, t.tolerant ? 1u : 0u);
    b.at<std::uint32_t> (42, (std::uint32_t) t.positionSync);
    b.b[46] = t.grouped ? 1 : 0;
    b.at<std::uint32_t> (49, (std::uint32_t) t.mode);
    b.at<std::int32_t> (53, t.mixerTrack);
    b.at<std::int32_t> (57, t.channel);
    b.b[61] = t.expanded ? 1 : 0;
    return b.b;
}

// FL's default control values of mixer track `t` (FL 26.1.7's saved projects [DATA]): slot on + mix 100 % for the 10
// slots, level 12800 (0 dB), pan 0, separation 0, the EQ's gains 0, frequencies and widths
void defaultTrackControls (std::map<std::uint32_t, std::int32_t>& values, std::vector<std::uint32_t>& order, int t)
{
    const auto base = 0x70000000u + ((std::uint32_t) t << 22);
    auto put = [&] (std::uint32_t id, std::int32_t v) {
        if (values.emplace (id, v).second)
            order.push_back (id);
    };
    for (std::uint32_t slot = 0; slot < 10; ++slot)
    {
        put (base + (slot << 16) + 0x1F00, 1);
        put (base + (slot << 16) + 0x1F01, 12800);
    }
    put (base + 0x1FC0, 12800);
    put (base + 0x1FC1, 0);
    put (base + 0x1FC2, 0);
    for (std::uint32_t band = 0; band < 3; ++band)
        put (base + 0x1FD0 + band, 0);
    const std::int32_t hz[3] = { 5777, 33145, 55825 };
    for (std::uint32_t band = 0; band < 3; ++band)
        put (base + 0x1FD8 + band, hz[band]);
    for (std::uint32_t band = 0; band < 3; ++band)
        put (base + 0x1FE0 + band, 17500);
}
} // namespace

std::vector<std::uint8_t> utf16Text (const std::string& utf8)
{
    std::vector<std::uint32_t> cps;
    decodeUtf8 (utf8, cps);
    std::vector<std::uint8_t> out;
    auto unit = [&out] (std::uint32_t u) {
        out.push_back ((std::uint8_t) (u & 0xFF));
        out.push_back ((std::uint8_t) ((u >> 8) & 0xFF));
    };
    for (auto cp : cps)
    {
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            unit (0xD800 + (cp >> 10));
            unit (0xDC00 + (cp & 0x3FF));
        }
        else
            unit (cp);
    }
    unit (0);
    return out;
}

std::vector<std::uint8_t> defaultChannelParams()
{
    return std::vector<std::uint8_t> (std::begin (channelParamsDefault), std::end (channelParamsDefault));
}

std::vector<std::uint8_t> defaultEnvelope (int record)
{
    // FL 2026's five articulator records of a new channel [DATA]: delay 100, attack / hold 20000, decay 30000, sustain 50
    // (of 128… FL's 50 = the shown default), release 20000, LFO delay 100, attack 20000, speed 32950; the volume
    // record (1) has flags 4 and −101 at +64
    Blob b;
    b.put<std::int32_t> (record == 1 ? 4 : 0);
    b.put<std::int32_t> (0);
    b.put<std::int32_t> (100);
    b.put<std::int32_t> (20000);
    b.put<std::int32_t> (20000);
    b.put<std::int32_t> (30000);
    b.put<std::int32_t> (50);
    b.put<std::int32_t> (20000);
    b.put<std::int32_t> (0);
    b.put<std::int32_t> (100);
    b.put<std::int32_t> (20000);
    b.put<std::int32_t> (0);
    b.put<std::int32_t> (32950);
    b.put<std::int32_t> (0);
    b.put<std::int32_t> (0);
    b.put<std::int32_t> (0);
    b.put<std::int32_t> (record == 1 ? -101 : 0);
    return b.b;
}

std::vector<std::uint8_t> defaultPlaylistTrack (int index)
{
    Blob b;
    b.put<std::uint32_t> ((std::uint32_t) index + 1);
    b.put<std::uint32_t> (0x565148); // FL's default grey (0x485156 as 0x00BBGGRR)
    b.put<std::uint32_t> (0);
    b.b.push_back (1);
    b.put<float> (1.0f);
    b.put<std::int32_t> (-16);
    b.b.push_back (0);
    b.put<std::uint32_t> (0);
    b.put<std::uint32_t> (0);
    b.put<std::uint32_t> (5);
    b.put<std::uint32_t> (0);
    b.put<std::uint32_t> (1);
    b.put<std::uint32_t> (0);
    b.b.push_back (0);
    b.b.push_back (0);
    b.b.push_back (0);
    b.put<std::uint32_t> (0);
    b.put<std::int32_t> (-1);
    b.put<std::int32_t> (-1);
    b.b.push_back (1);
    b.b.resize (70, 0);
    return b.b;
}

std::vector<std::uint8_t> writeProject (const Project& p, const WriteOptions& options)
{
    Out out;

    // --- the project ---
    out.data (evVersion, ansiText (options.version));
    out.u32 (evBuild, options.build);
    out.u32 (evFileFlags, 15); // stretch times as floats, 88-byte playlist records (REA_rodada2 §4.2)
    out.u8 (evRegistered, 1);
    out.u8 (evSaved25, 1);
    out.u32 (evTempo, (std::uint32_t) std::lround (std::clamp (p.tempo, 10.0, 522.0) * 1000.0));
    out.u16 (evCurrentPattern, (std::uint32_t) std::clamp (p.currentPattern > 0 ? p.currentPattern : 1, 1, 999));
    out.u8 (evSongMode, p.songMode == 1 ? 1 : 0);
    out.u8 (evSwing, (std::uint32_t) std::clamp (p.swing, 0, 128));
    out.u16 (evMainPitch, (std::uint32_t) (std::uint16_t) (std::int16_t) std::clamp (p.mainPitch, -1200, 1200));
    out.u8 (evTimeSigNumerator, (std::uint32_t) std::clamp (p.beatsPerBar, 1, 32));
    out.u8 (evTimeSigDenominator, 4);
    out.u8 (evTimeSetting, p.timeSignatureMode ? 1 : 0);
    out.u8 (evPanLaw, (std::uint32_t) p.panLaw);
    out.u8 (evAutoCrossfades, p.autoCrossfades ? 1 : 0);
    out.u8 (evPlayTruncated, p.playTruncatedNotes ? 1 : 0);
    out.u8 (evShowInfo, p.showInfo ? 1 : 0);
    out.text (evTitle, p.title);
    out.text (evGenre, p.genre);
    out.text (evArtist, p.artist);
    out.u32 (evGlobalA7, 0);
    out.text (evDataFolder, p.dataFolder);
    {
        auto comments = p.comments;
        std::replace (comments.begin(), comments.end(), '\n', '\r'); // (FL saves CR line breaks)
        out.text (evComments, comments);
    }
    if (! p.url.empty())
        out.text (evUrl, p.url);
    {
        Blob t;
        t.put<double> (p.createdDays);
        t.put<double> (p.workDays);
        out.data (evProjectTimes, t.b);
    }
    for (const auto& name : p.filterGroups)
        out.text (evFilterGroupName, name);
    if (p.filterGroups.empty())
        out.text (evFilterGroupName, "Unsorted");
    out.u32 (evFilterGroupCurrent, 0xFFFFFFFFu);
    out.data (evInitControls, controlRecords (p.initControls));

    // --- the patterns' notes and events (FL writes them before the channels) ---
    for (const auto& pattern : p.patterns)
    {
        if (pattern.notes.empty() && pattern.controlEvents.empty())
            continue;
        out.u16 (evNewPattern, (std::uint32_t) pattern.index);
        if (! pattern.controlEvents.empty())
            out.data (evPatternEvents, controlRecords (pattern.controlEvents));
        if (! pattern.notes.empty())
        {
            auto notes = pattern.notes;
            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.position < b.position; });
            Blob b;
            for (const auto& n : notes)
            {
                b.put<std::uint32_t> (n.position);
                b.put<std::uint16_t> (n.flags);
                b.put<std::uint16_t> (n.channel);
                b.put<std::uint32_t> (n.length);
                b.put<std::uint16_t> (n.key);
                b.put<std::uint16_t> (n.group);
                b.b.push_back (n.finePitch);
                b.b.push_back (n.repeats);
                b.b.push_back (n.release);
                b.b.push_back (n.extra);
                b.b.push_back (n.pan);
                b.b.push_back (n.velocity);
                b.b.push_back (n.modX);
                b.b.push_back (n.modY);
            }
            out.data (evPatternNotes, b.b);
        }
    }

    // automation clip and controller links (0xE3: {u32 source, u32 0, u32 target, u32 flags (8 = clip), u32 0x1D5})
    if (! p.automationLinks.empty() || ! p.controllerLinks.empty())
    {
        for (const auto& link : p.automationLinks)
        {
            Blob b;
            b.put<std::uint32_t> ((std::uint32_t) link.channel << 16);
            b.put<std::uint32_t> (0);
            b.put<std::uint32_t> (link.target.raw);
            b.put<std::uint32_t> (8);
            b.put<std::uint32_t> (0x1D5);
            out.data (evAutomationLink, b.b);
        }
        for (const auto& link : p.controllerLinks)
        {
            Blob b;
            b.put<std::uint32_t> (link.source.raw);
            b.put<std::uint32_t> (0);
            b.put<std::uint32_t> (link.target.raw);
            b.put<std::uint32_t> (8);
            b.put<std::uint32_t> (0x1D5);
            out.data (evAutomationLink, b.b);
        }
    }

    // --- the channels; the patterns' names, colours and settings after the first one, as FL does ---
    bool patternInfoWritten = false;
    auto writeInfo = [&] {
        if (patternInfoWritten)
            return;
        for (const auto& pattern : p.patterns) // (FL leaves out the patterns that keep every default, e.g. g1.flp)
            if (! pattern.name.empty() || pattern.colour.has_value() || ! pattern.markers.empty() || pattern.loopOn
                || ! pattern.channelLoops.empty() || pattern.fixedLength != 0)
                writePatternInfo (out, pattern);
        patternInfoWritten = true;
    };
    for (const auto& c : p.channels)
    {
        writeChannel (out, c);
        writeInfo();
    }
    writeInfo();

    // --- the arrangements ---
    for (const auto& a : p.arrangements)
    {
        out.u16 (evArrangement, (std::uint32_t) a.index);
        out.text (evArrangementName, a.name.empty() ? "Arrangement" : a.name);
        out.u8 (evArrangementFlag, 0);
        auto items = a.items;
        std::stable_sort (items.begin(), items.end(), [] (const PlaylistItem& x, const PlaylistItem& y) { return x.position < y.position; });
        out.data (evPlaylistItems, playlistRecords (items));
        writeMarkers (out, a.markers);
        std::map<int, const PlaylistTrackInfo*> described;
        for (const auto& t : a.tracks)
            if (t.index >= 0 && t.index < playlistTracks)
                described[t.index] = &t;
        for (int t = 0; t < playlistTracks; ++t)
        {
            const auto it = described.find (t);
            PlaylistTrackInfo info;
            info.index = t;
            if (it != described.end())
                info = *it->second;
            out.data (evPlaylistTrack, playlistTrackRecord (info));
            if (! info.name.empty())
                out.text (evPlaylistTrackName, info.name);
            out.u8 (evPlaylistTrackColourSet, (info.colour & 0xFFFFFF) != 0x485156 ? 1 : 0);
        }
    }
    {
        // the Playlist's view settings record FL writes after the arrangements [DATA]
        Blob v;
        for (int k = 0; k < 2; ++k)
            v.put<std::uint32_t> (0);
        v.put<std::uint32_t> (0);
        v.put<std::uint32_t> (0);
        v.put<std::uint32_t> (0x40200000u);
        v.put<std::uint32_t> (0);
        v.put<std::uint32_t> (20);
        out.data (evPlaylistF2, v.b);
    }
    out.u16 (evCurrentArrangement, (std::uint32_t) std::max (0, p.currentArrangement));
    out.u8 (evAutoPdc, p.autoPdc ? 1 : 0);
    out.u8 (evCompensateAutomations, p.compensateAutomations ? 1 : 0);
    out.u8 (evFastDeclick, p.fastDeclick ? 1 : 0);
    out.u8 (evPerformanceMode, p.performanceMode ? 1 : 0);
    out.u8 (evMixerFlag26, 1);

    // --- the mixer: Master, the inserts and FL's "current" track ---
    const auto described = p.mixer.empty() ? 1 : p.mixer.back().index + 1;
    const auto tracks = std::clamp (std::max (described + 1, options.minMixerTracks), 3, 502);
    out.u16 (evMixerTrackCount, (std::uint32_t) tracks);
    std::map<int, const MixerInsert*> inserts;
    for (const auto& m : p.mixer)
        if (m.index >= 0 && m.index < tracks)
            inserts[m.index] = &m;
    for (int t = 0; t < tracks; ++t)
    {
        MixerInsert insert;
        insert.index = t;
        insert.flags = t == 0 ? 0x0C : 0x4C;
        if (t > 0)
            insert.routes = { 0 }; // an insert the project leaves out: FL's default, routed to the Master
        // the last record is FL's "Current" track: FL plays the selected insert through it, so FL writes it with no
        // route to the Master (flags 0x0C, routes 00); routed to the Master, everything played twice (+6.02 dB in
        // FL's render of every LYNX export, round 171e2 [FL])
        const auto current = t > 0 && t == tracks - 1;
        if (current)
        {
            insert.flags = 0x0C;
            insert.routes.clear();
            insert.monitor = 1;
        }
        if (const auto it = inserts.find (t); it != inserts.end())
            insert = *it->second;
        if (insert.icon > 0)
            out.u16 (evMixerIcon, (std::uint32_t) insert.icon);
        if (insert.colour != 0)
            out.u32 (evMixerColour, bgr (insert.colour));
        if (! insert.name.empty())
            out.text (evMixerName, insert.name);
        out.u8 (evMixerColourSet, insert.colourSet > 0 || (insert.colourSet < 0 && insert.colour != 0) ? 1 : 0);
        {
            Blob b;
            b.put<float> (insert.latencyMs);
            b.put<std::uint32_t> (insert.flags);
            b.put<std::uint32_t> (0);
            out.data (evMixerParams, b.b);
        }
        std::map<int, const MixerInsert::Slot*> slots;
        for (const auto& s : insert.slots)
            if (s.index >= 0 && s.index < 10)
                slots[s.index] = &s;
        for (int s = 0; s < 10; ++s)
        {
            if (const auto it = slots.find (s); it != slots.end())
            {
                const auto& slot = *it->second;
                out.text (evPluginInternalName, slot.plugin);
                const auto wrapper = slot.plugin == "Fruity Wrapper";
                const auto flags = slot.wrapperFlags >= 0 ? slot.wrapperFlags : wrapper ? 0x140 : 0x160;
                out.data (evPluginNew, pluginRecord (t, s, true, flags));
                out.u32 (evChannelIcon, 0);
                out.u32 (evChannelColour, bgr (0x5C656A));
                out.u8 (evChannel29, 0);
                out.data (evPluginState, slot.state);
            }
            out.u16 (evMixerSlotIndex, (std::uint32_t) s);
        }
        {
            std::vector<std::uint8_t> routes;
            for (const auto r : insert.routes)
                if (r >= 0 && r < tracks && r != t)
                {
                    if ((int) routes.size() <= r)
                        routes.resize ((std::size_t) r + 1, 0);
                    routes[(std::size_t) r] = 1;
                }
            if (routes.empty())
                routes.push_back (0);
            out.data (evMixerRoutes, routes);
        }
        out.u32 (evMixerRecordSource, (std::uint32_t) (insert.recordSource >= 0 ? insert.recordSource : 3));
        out.u32 (evMixerMonitor, (std::uint32_t) (insert.monitor >= 0 ? insert.monitor : 0));
        out.u8 (evMixerLatencyBypass, (std::uint32_t) insert.latencyBypass);
        out.u32 (evMixerInput, (std::uint32_t) insert.input);
        out.u32 (evMixerOutput, (std::uint32_t) (insert.output != -2 ? insert.output : t == 0 ? 0 : -1));
    }

    // --- control values: FL's defaults for every track, then the project's own ---
    {
        std::map<std::uint32_t, std::int32_t> values;
        std::vector<std::uint32_t> order;
        values[0x40000000u] = 12800; // the main volume
        order.push_back (0x40000000u);
        for (int t = 0; t < tracks - 1; ++t) // (not the "Current" track: FL writes none for it)
            defaultTrackControls (values, order, t);
        for (const auto& c : p.controls)
        {
            if (values.find (c.id.raw) == values.end())
                order.push_back (c.id.raw);
            values[c.id.raw] = c.value;
        }
        Blob b;
        for (const auto id : order)
        {
            b.put<std::uint32_t> (0);
            b.put<std::uint32_t> (id);
            b.put<std::int32_t> (values[id]);
        }
        out.data (evControlValues, b.b);
    }
    out.u32 (evMixerSelected, 0);
    {
        Blob s;
        s.put<std::int32_t> (p.hasScaleHighlight ? p.scaleHighlight : -2);
        s.put<std::int32_t> (p.hasScaleHighlight ? p.scaleRoot : 0);
        out.data (evScaleHighlight, s.b);
    }
    out.u8 (evEnd2F, 0);

    // --- header + data chunk ---
    std::vector<std::uint8_t> file;
    auto put32 = [&file] (std::uint32_t v) {
        for (int k = 0; k < 4; ++k)
            file.push_back ((std::uint8_t) (v >> (8 * k)));
    };
    auto put16 = [&file] (std::uint32_t v) {
        file.push_back ((std::uint8_t) (v & 0xFF));
        file.push_back ((std::uint8_t) ((v >> 8) & 0xFF));
    };
    file.insert (file.end(), { 'F', 'L', 'h', 'd' });
    put32 (6);
    put16 (0);
    put16 ((std::uint32_t) p.channels.size());
    put16 (p.header.ppq > 0 ? p.header.ppq : 96);
    file.insert (file.end(), { 'F', 'L', 'd', 't' });
    put32 ((std::uint32_t) out.bytes.size());
    file.insert (file.end(), out.bytes.begin(), out.bytes.end());
    return file;
}
} // namespace flp
