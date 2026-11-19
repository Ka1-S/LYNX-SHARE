#include "ui/ArticulatorTools.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/AudioEditTools.h"
#include "core/FlCurve.h"
#include "core/GrossBeat.h"
#include "ui/AudioEditorTools.h"
#include "ui/FileChoosers.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"

namespace lynx::articulator
{
namespace
{
std::optional<std::vector<AutomationPoint>> clipboard; // Copy state / Paste state (ticks relative to the copied length)
std::int64_t clipboardLength = 0;

bool validAutomation (const Song& song, int automation) { return automation >= 0 && automation < (int) song.automations.size(); }

// LYNX keeps a segment's tension on its first point with FL's opposite sign; the envelope tools (FL's / Edison's) on the
// point where it ends with FL's sign
audioedit::Envelope toEnvelope (const std::vector<AutomationPoint>& points, double length)
{
    audioedit::Envelope env;
    for (std::size_t i = 0; i < points.size(); ++i)
        env.points.push_back ({ juce::jlimit (0.0, 1.0, (double) points[i].tick / length), (double) points[i].value,
                                i > 0 ? -points[i - 1].tension : 0.0f });
    return env;
}

std::vector<AutomationPoint> fromEnvelope (audioedit::Envelope env, double length, const std::vector<AutomationPoint>& original)
{
    env.sort();
    std::vector<AutomationPoint> out;
    for (std::size_t i = 0; i < env.points.size(); ++i)
    {
        AutomationPoint p;
        p.tick = (std::int64_t) std::llround (juce::jlimit (0.0, 1.0, env.points[i].x) * length);
        p.value = (float) juce::jlimit (0.0, 1.0, env.points[i].y);
        p.tension = i + 1 < env.points.size() ? -env.points[i + 1].tension : 0.0f;
        p.mode = flcurve::singleCurve; // (a new point: FL's Single curve; a kept one keeps its mode)
        for (const auto& o : original)
            if (o.tick == p.tick)
            {
                p.mode = o.mode;
                break;
            }
        out.push_back (p);
    }
    return out;
}

void setPoints (AudioEngine& engine, int automation, std::vector<AutomationPoint> points, bool undoable)
{
    engine.editSong ([automation, points = std::move (points)] (Song& s) {
        if (automation >= 0 && automation < (int) s.automations.size())
            s.automations[(std::size_t) automation].points = points;
    }, undoable);
}

double lengthOf (const Song& song, int automation, std::int64_t clipLength)
{
    auto length = (double) juce::jmax<std::int64_t> (1, clipLength);
    if (validAutomation (song, automation) && ! song.automations[(std::size_t) automation].points.empty())
        length = juce::jmax (length, (double) song.automations[(std::size_t) automation].points.back().tick);
    return length;
}

// the tool forms' host (one tool window at a time)
struct ToolHost final : audiotools::Host
{
    ToolHost (AudioEngine& e, int a, double len, juce::Component& o) : engine (e), automation (a), length (len), owner (&o)
    {
        doc.audio.sampleRate = 1.0; // (the clip's beats as seconds at toolBpm's 60: Create sequence counts its steps in them)
        doc.audio.channels.assign (1, std::vector<float> ((std::size_t) juce::jmax (1, (int) std::lround (length / ppq)), 0.0f));
        original = engine.getSong().automations[(std::size_t) automation].points;
        env = toEnvelope (original, length);
    }
    AudioEngine& engine;
    int automation;
    double length;
    juce::Component::SafePointer<juce::Component> owner;
    audioedit::Document doc;
    audioedit::Envelope env;
    std::vector<AutomationPoint> original;
    audioedit::NoiseProfile noise;
    audioedit::Audio emptyClip;

    const audioedit::Document& toolDocument() const override { return doc; }
    audioedit::Range toolSelection() const override { return { 0, (std::int64_t) doc.audio.channels[0].size() }; }
    void toolApply (const juce::String&, audioedit::Document, audioedit::Range) override {}
    void toolPreview (const audioedit::Audio&) override {}
    void toolStopPreview() override {}
    double toolBpm() const override { return 60.0; }
    audioedit::NoiseProfile& toolNoiseProfile() override { return noise; }
    const audioedit::Audio& toolClipboard() const override { return emptyClip; }
    audioedit::Envelope* toolEnvelope() override { return &env; }
    double toolEnvelopeUnity() const override { return 1.0; }
    void toolEnvelopeChanged (bool commit, const juce::String&) override
    {
        if (! validAutomation (engine.getSong(), automation))
            return;
        auto points = fromEnvelope (env, length, original);
        if (commit) // one undo step from the points before the form
        {
            setPoints (engine, automation, original, false);
            setPoints (engine, automation, points, true);
            original = std::move (points);
        }
        else
            setPoints (engine, automation, std::move (points), false);
    }
    juce::Component& toolComponent() override { return owner != nullptr ? *owner.getComponent() : dummy; }
    juce::Component dummy;
};
std::unique_ptr<ToolHost> host;

void openTool (AudioEngine& engine, int automation, double length, juce::Component& owner, audiotools::Tool tool)
{
    host = std::make_unique<ToolHost> (engine, automation, length, owner);
    audiotools::showTool (*host, tool);
}

void analyze (AudioEngine& engine, int automation, std::int64_t length, const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        ui::postHint ({ tr ("Could not read {0}", { file.getFileName() }), {} }, 3000);
        return;
    }
    constexpr int slices = 1024;
    const auto total = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1200.0));
    std::vector<float> peaks (slices, 0.0f);
    const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> block (channels, 8192);
    for (juce::int64 pos = 0; pos < total; pos += block.getNumSamples())
    {
        const auto n = (int) std::min<juce::int64> (block.getNumSamples(), total - pos);
        reader->read (&block, 0, n, pos, true, channels > 1);
        for (int i = 0; i < n; ++i)
        {
            auto v = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                v = juce::jmax (v, std::abs (block.getSample (ch, i)));
            auto& peak = peaks[(std::size_t) std::min<juce::int64> (slices - 1, (pos + i) * slices / total)];
            peak = juce::jmax (peak, v);
        }
    }
    setPoints (engine, automation, fromLevels (peaks, length), true);
}
std::unique_ptr<juce::FileChooser> chooser;
} // namespace

std::vector<AutomationPoint> flipped (std::vector<AutomationPoint> points)
{
    for (auto& p : points)
        p.value = 1.0f - p.value, p.tension = -p.tension;
    return points;
}

std::vector<AutomationPoint> normalized (std::vector<AutomationPoint> points)
{
    if (points.empty())
        return points;
    const auto [lo, hi] = std::minmax_element (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.value < b.value; });
    const auto low = lo->value, range = hi->value - lo->value;
    if (range <= 1.0e-6f)
        return points;
    for (auto& p : points)
        p.value = (p.value - low) / range;
    return points;
}

std::vector<AutomationPoint> allSmooth (std::vector<AutomationPoint> points)
{
    for (auto& p : points)
        p.mode = flcurve::smooth;
    return points;
}

std::vector<AutomationPoint> fromLevels (const std::vector<float>& peaks, std::int64_t length)
{
    std::vector<AutomationPoint> out;
    for (const auto& e : grossbeat::envelopeFromLevels (peaks)) // (over the 4-beat bar there: here over the clip)
    {
        AutomationPoint p;
        p.tick = (std::int64_t) std::llround (e.x / grossbeat::barBeats * (double) juce::jmax<std::int64_t> (1, length));
        p.value = e.y;
        p.mode = flcurve::singleCurve;
        out.push_back (p);
    }
    return out;
}

juce::PopupMenu menu (AudioEngine& engine, int automation, std::int64_t clipLength, juce::Component& owner)
{
    juce::PopupMenu m;
    const auto& song = engine.getSong();
    if (! validAutomation (song, automation))
        return m;
    const auto length = lengthOf (song, automation, clipLength);
    const auto points = [&engine, automation] { return engine.getSong().automations[(std::size_t) automation].points; };
    const auto write = [&engine, automation] (std::vector<AutomationPoint> p) { setPoints (engine, automation, std::move (p), true); };
    m.addItem (tr ("Copy state"), [points, length] {
        clipboard = points();
        clipboardLength = (std::int64_t) length;
    });
    m.addItem (tr ("Paste state"), clipboard.has_value(), false, [write, length] {
        if (! clipboard)
            return;
        auto pasted = *clipboard; // (stretched to this clip's length, as the envelope it is)
        const auto k = clipboardLength > 0 ? length / (double) clipboardLength : 1.0;
        for (auto& p : pasted)
            p.tick = (std::int64_t) std::llround ((double) p.tick * k);
        write (std::move (pasted));
    });
    m.addSeparator();
    m.addItem (tr ("Flip vertically"), [write, points] { write (flipped (points())); });
    const juce::Component::SafePointer<juce::Component> safeOwner (&owner);
    const auto tool = [&engine, automation, length, safeOwner] (audiotools::Tool t) {
        return [&engine, automation, length, safeOwner, t] {
            if (safeOwner != nullptr && validAutomation (engine.getSong(), automation))
                openTool (engine, automation, length, *safeOwner.getComponent(), t);
        };
    };
    m.addItem (tr ("Scale levels..."), tool (audiotools::Tool::scaleLevels));
    m.addItem (tr ("Normalize levels"), [write, points] { write (normalized (points())); });
    m.addItem (tr ("Decimate points..."), tool (audiotools::Tool::decimate));
    m.addItem (tr ("Filter..."), tool (audiotools::Tool::envFilter));
    m.addItem (tr ("Smooth up..."), tool (audiotools::Tool::smoothUp));
    // FL's manual (Gross Beat › Tools): "Smooth up abrupt changes - Quick removal of 'spikey' or sudden changes in the
    // envelope": the Smooth up form's work at its default settings, without the form
    m.addItem (tr ("Smooth up abrupt changes"), [write, points, length] {
        const auto before = points();
        auto env = toEnvelope (before, length);
        audioedit::smoothUp (env, 0.5, 0.5, 0.5, 0.5, 0.0, 0.5);
        write (fromEnvelope (env, length, before));
    });
    m.addItem (tr ("Turn all points smooth"), [write, points] { write (allSmooth (points())); });
    m.addItem (tr ("Create sequence..."), tool (audiotools::Tool::createSequence));
    m.addItem (tr ("Analyze audio file..."), [&engine, automation, clipLength, safeOwner] {
        chooser = ui::makeFileChooser (tr ("Analyze audio file"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [&engine, automation, clipLength, safeOwner] (const juce::FileChooser& c) {
                               if (safeOwner != nullptr && c.getResult().existsAsFile() && validAutomation (engine.getSong(), automation))
                                   analyze (engine, automation, clipLength, c.getResult());
                           });
    });
    return m;
}
} // namespace lynx::articulator
