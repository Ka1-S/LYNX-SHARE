// LYNX addition (round 190lk, not part of PitchNet): the masks of a deferred analysis (Params::deferMasks), built on
// demand for the frames an edit reaches. Licensed as the rest of this folder (AGPL-3.0).
//
// PitchNet's analyze() keeps every active note's masks (partial regions x frames) for the whole recording: about
// 0.6 MB per second of dense chords, so a five-minute take held ~180 MB of masks besides its audio. LYNX analyses a
// clip once, keeps only its notes (and saves them in the project), and builds the masks of the NMF blocks an edit
// reaches when it renders. A block's masks depend only on that block's frames and the notes in it, and a frame's
// spectral peaks only on that frame and its neighbours, so the masks are exactly the ones analyze() would have kept.

#include "PolyInternal.h"

namespace polyphonic
{

namespace
{
constexpr double kLeadSeconds = 0.05;   // = PolyRenderer's kClaimLeadSeconds
constexpr double kTailSeconds = 2.0;    // = PolyRenderer's kClaimTailSeconds

std::vector<std::pair<int, int>> merged(std::vector<std::pair<int, int>> spans)
{
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<int, int>> out;
    for (const auto& s : spans)
    {
        if (s.second < s.first)
            continue;
        if (!out.empty() && s.first <= out.back().second + 1)
            out.back().second = std::max(out.back().second, s.second);
        else
            out.push_back(s);
    }
    return out;
}
} // namespace

std::vector<std::pair<int, int>> framesForEdits(const Analysis& A, const std::map<int, NoteEdit>& edits)
{
    const Params& p = A.params;
    const double fps = p.sampleRate / p.hop;
    const int lead = static_cast<int>(std::ceil(kLeadSeconds * fps)) + 1;
    const int tail = static_cast<int>(std::ceil(kTailSeconds * fps)) + 1;
    const int T = A.numFrames;
    std::vector<std::pair<int, int>> spans;
    for (const auto& [index, edit] : edits)
    {
        if (index < 0 || index >= static_cast<int>(A.notes.size()) || !A.notes[static_cast<size_t>(index)].active)
            continue;
        const auto& n = A.notes[static_cast<size_t>(index)];
        int lo = n.startFrame - p.preFrames - lead;
        int hi = n.endFrame + p.postFrames + tail;
        // where it goes: moved by whole frames (rounded out), lengthened up to 4x
        const int move = static_cast<int>(std::ceil(std::fabs(edit.timeOffsetSeconds) * fps)) + 1;
        if (edit.timeOffsetSeconds > 0.0)
            hi += move;
        else if (edit.timeOffsetSeconds < 0.0)
            lo -= move;
        if (edit.stretch > 1.0f)
            hi += static_cast<int>(std::ceil((n.endFrame - n.startFrame + 1) * (edit.stretch - 1.0f))) + 1;
        spans.push_back({std::max(0, lo), std::min(T - 1, hi)});
    }
    spans = merged(std::move(spans));
    // the whole span of every active note that overlaps (their masks' first / last frames and claims are read too)
    std::vector<std::pair<int, int>> widened = spans;
    for (const auto& n : A.notes)
    {
        if (!n.active)
            continue;
        const int a = n.startFrame - p.preFrames, b = n.endFrame + p.postFrames;
        for (const auto& s : spans)
            if (b >= s.first && a <= s.second)
            {
                widened.push_back({std::max(0, a - lead), std::min(T - 1, b + tail)});
                break;
            }
    }
    return merged(std::move(widened));
}

bool ensureMasks(Analysis& A, const float* mono, int numSamples, int frameLo, int frameHi, const std::atomic<bool>* cancel)
{
    using namespace detail;
    if (mono == nullptr || numSamples <= 0 || A.numFrames <= 0 || frameHi < frameLo)
        return true;
    const auto blocks = maskBlocks(A.params, A.numFrames);
    if (A.maskBlocksBuilt.empty())
    {
        // a full analysis: every block is there unless the masks were released
        bool any = false;
        for (const auto& m : A.masks)
            any = any || !m.partials.empty();
        if (any)
            return true;
        A.maskBlocksBuilt.assign(blocks.size(), 0);
    }
    if (A.masks.size() != A.notes.size())
        A.masks.assign(A.notes.size(), {});

    std::vector<Candidate> active;
    for (const auto& n : A.notes)
    {
        if (!n.active)
            break; // active notes come first (analyze())
        Candidate c;
        c.start = n.startFrame;
        c.end = n.endFrame;
        c.f0 = n.f0;
        c.B = n.inharmonicity;
        c.energy = n.energy;
        c.probability = n.probability;
        active.push_back(std::move(c));
    }
    std::vector<size_t> sizesBefore(A.masks.size());
    for (size_t i = 0; i < A.masks.size(); ++i)
        sizesBefore[i] = A.masks[i].partials.size();
    bool appendedOutOfOrder = false;
    for (size_t b = 0; b < blocks.size(); ++b)
    {
        const auto& block = blocks[b];
        if (A.maskBlocksBuilt[b] || block.coreHi <= frameLo || block.coreLo > frameHi)
            continue;
        if (cancel != nullptr && cancel->load())
            return false;
        Context ctx;
        ctx.p = A.params;
        ctx.audio = mono;
        ctx.numSamples = numSamples;
        analyzeSpectra(ctx, cancel, block.lo, block.hi);
        if (cancel != nullptr && cancel->load())
            return false;
        // a later block may already be built: the new partials then go before them
        for (size_t c = b + 1; c < blocks.size(); ++c)
            appendedOutOfOrder = appendedOutOfOrder || A.maskBlocksBuilt[c] != 0;
        buildMaskBlock(ctx, active, block, A);
        A.maskBlocksBuilt[b] = 1;
    }
    if (appendedOutOfOrder)
        for (size_t i = 0; i < A.masks.size(); ++i)
            if (A.masks[i].partials.size() != sizesBefore[i])
                std::stable_sort(A.masks[i].partials.begin(), A.masks[i].partials.end(), [](const PartialMask& x, const PartialMask& y) {
                    return x.frame != y.frame ? x.frame < y.frame : x.harmonic < y.harmonic;
                });
    return true;
}

void releaseMasks(Analysis& A)
{
    A.masks.assign(A.notes.size(), {});
    A.maskBlocksBuilt.assign(detail::maskBlocks(A.params, A.numFrames).size(), 0);
}

size_t maskBytes(const Analysis& A)
{
    size_t bytes = 0;
    for (const auto& m : A.masks)
        bytes += m.partials.capacity() * sizeof(PartialMask) + m.weights.capacity() * sizeof(float);
    return bytes;
}

} // namespace polyphonic
