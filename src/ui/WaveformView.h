#pragma once

// Detailed, frequency-coloured waveforms (Playlist audio clips, the Sampler editor, the Browser preview).
//
//  - WaveformCache: one core/WaveAnalysis Summary per audio file, built on a background thread (never the
//    audio or UI thread), kept in memory and on disk (%APPDATA%\LYNX\wavecache, keyed by path + size + date).
//    It broadcasts a change when a waveform becomes ready, so views just repaint.
//  - paintWaveform(): draws any frame range of a Summary straight into a pixel buffer (one image per call,
//    physical-pixel resolution, anti-aliased peak and RMS edges), so many clips still scroll smoothly.
//  - WaveformDisplay: a small component that shows a whole file (Sampler editor, Browser preview strip).
//
// Settings (Options > Waveforms): the clean one-colour waveform (default: the peak envelope mirrored around
// the centre, filled and anti-aliased, in a light tint of the clip colour) or spectral colours; stereo (left and
// right in two lanes) or merged.

#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Instruments.h"
#include "core/WaveAnalysis.h"

namespace lynx::ui
{
class WaveformCache final : public juce::ChangeBroadcaster,
                            private juce::DeletedAtShutdown
{
public:
    WaveformCache();
    ~WaveformCache() override;

    JUCE_DECLARE_SINGLETON_SINGLETHREADED_INLINE (WaveformCache, true)

    using Source = std::function<std::shared_ptr<const SampleData>()>;

    // UI thread. The waveform of `path`, or nullptr while it is being analysed (or could not be read).
    // The first call starts the analysis: with the audio from `source` when given (already decoded by the
    // engine, so nothing is decoded twice), otherwise the file is decoded on the worker thread.
    std::shared_ptr<const wave::Summary> find (const juce::String& path, const Source& source = {});
    bool isPending (const juce::String& path) const;
    bool hasFailed (const juce::String& path) const;

    // --snapshot / tests: waits until every queued analysis is done.
    bool waitUntilIdle (int timeoutMs);

    // the settings changed: every view repaints
    void settingsChanged() { sendChangeMessage(); }

    static juce::File diskFolder();

    // paintWaveform()'s pixel buffer (message thread only)
    juce::Image& scratchImage() noexcept { return scratch; }

    // Options > Waveforms (kept in the UI settings)
    static inline bool spectral = false;
    static inline bool stereo = false;

private:
    class Job;
    void finished (const juce::String& path, std::shared_ptr<const wave::Summary> summary);

    mutable std::mutex lock;
    std::map<juce::String, std::shared_ptr<const wave::Summary>> ready;
    std::map<juce::String, bool> state; // true = pending, false = failed
    std::atomic<bool> cancel { false };
    juce::AudioFormatManager formats;
    juce::Image scratch;
    juce::ThreadPool pool { juce::ThreadPoolOptions {}.withThreadName ("LYNX waveforms").withNumberOfThreads (2) };
};

struct WaveStyle
{
    juce::Colour classic { 0xff7fa8d6 }; // colour of the clean (one-colour) waveform
    bool spectral = false;
    bool stereo = false;
    float gain = 1.0f; // display gain (normalisation)
};

// Draws the summary into `body`: pixel column x shows frames [startFrame + x * framesPerPixel, + framesPerPixel)
// (a negative framesPerPixel draws backwards, for reversed samples). Only the part inside g's clip is drawn.
void paintWaveform (juce::Graphics& g, juce::Rectangle<float> body, const wave::Summary& summary, double startFrame,
                    double framesPerPixel, const WaveStyle& style);

// The clean style's shape added to `out` instead of painted (round 151: many small clips of one colour are
// filled as one path). `scale` = the physical pixel scale. Returns false for the spectral style (paint it instead).
bool appendWaveformPath (juce::Path& out, float scale, juce::Rectangle<float> body, juce::Rectangle<float> clip, const wave::Summary& summary,
                         double startFrame, double framesPerPixel, const WaveStyle& style);

// Style from the current settings.
WaveStyle currentWaveStyle (juce::Colour classic, float gain = 1.0f);

// Display gain that makes the file's peak fill the height (at most +24 dB, so silence stays silent).
float normalisingGain (const wave::Summary& summary);

// A whole audio file: waveform, optional region (start/end fractions) and a caption.
class WaveformDisplay final : public juce::Component,
                              public juce::SettableTooltipClient,
                              private juce::ChangeListener
{
public:
    WaveformDisplay();
    ~WaveformDisplay() override;

    void setFile (const juce::String& path, WaveformCache::Source source = {});
    const juce::String& getFile() const noexcept { return path; }
    void setRegion (float start, float end);
    void setReversed (bool reversed);
    void setCaption (const juce::String& text); // drawn top-left (empty: file name and length)
    void setEmptyText (const juce::String& text);
    // Round 190w (FL 2026, What's New 20921 / 21138; [SHOT] flgap190w): the Sampler's start offset (a fraction of the
    // region, in play order): the region before it is dimmed and a "Start" marker stands at it (none at 0); and the
    // playback locator (the same fraction, -1 = none): a line with a short fading trail behind it.
    void setStartOffset (float fractionOfRegion);
    void setLocator (float fractionOfRegion);
    float getLocator() const noexcept { return locator; }
    std::function<void()> onClick; // a click on the display (the Sampler previews the sample from its start offset)
    // Round 190ey (live check 190ew SMP-7): a right-click opens the owner's menu (FL's Sampler waveform menu) instead of
    // the click above; and a small text in the bottom-right corner (FL's bit-depth box, "16" / "32"; empty = none)
    std::function<void()> onRightClick;
    void setCornerText (const juce::String& text);
    const juce::String& getCornerText() const noexcept { return cornerText; }

    // Round 190dh (FL's Sampler page): a faded name drawn at the top right ("Sampler"), the caption hidden, the loop
    // (file fractions, -1 = none) drawn as two lines, and a zoom: the part of the file shown (display-order fractions,
    // 0…1 = all). With setZoomable the mouse wheel zooms around the mouse (FL's manual: the wheel zooms the waveform);
    // onViewChanged follows the view (a scroll bar under it).
    void setWatermark (const juce::String& text);
    void setCaptionVisible (bool shown);
    void setLoop (float start, float end);
    void setZoomable (bool zoomable) { zoomEnabled = zoomable; }
    void setView (double start, double end);
    double getViewStart() const noexcept { return viewStart; }
    double getViewEnd() const noexcept { return viewEnd; }
    std::function<void()> onViewChanged;

    // Round 190gw (FL's Sampler waveform menu, live check 190gu B3): this display follows the Sampler's two view switches
    // (core samplerpage::waveViews): Spectrum view draws a spectrogram (core/Spectrogram) instead of the waveform,
    // Multichannel waveform view draws each channel in its own lane.
    void setFollowsSamplerViews (bool follows);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick != nullptr)
            onRightClick();
        else if (onClick != nullptr)
            onClick();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    juce::String path, caption, emptyText;
    WaveformCache::Source source;
    float regionStart = 0.0f, regionEnd = 1.0f;
    bool reversed = false;
    float startOffset = 0.0f, locator = -1.0f;
    juce::String watermark, cornerText;
    bool captionShown = true, zoomEnabled = false;
    float loopStart = -1.0f, loopEnd = -1.0f;
    double viewStart = 0.0, viewEnd = 1.0;
    bool samplerViews = false;
    juce::Image spectrumImage; // the last spectrogram drawn, and what it shows
    juce::String spectrumKey;
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> inner, const SampleData& data);
    float displayX (double displayFraction) const; // the x of a display-order fraction of the file, through the view
    static constexpr int locatorTrail = 20; // px of the fading trail behind the locator (FL's)
    float regionX (float fraction) const; // the x of a play-order fraction of the region
    juce::Rectangle<int> locatorArea (float fraction) const;
};
} // namespace lynx::ui
