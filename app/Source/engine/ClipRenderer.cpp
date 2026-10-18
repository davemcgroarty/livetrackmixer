#include "ClipRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include <rubberband/RubberBandStretcher.h>

#include "model/BeatMap.h"

namespace djmix
{

namespace
{
constexpr int chunk = 8192;
constexpr int beatsPerKeyFrame = 4;      // where the grid may be pinned: on bar lines
constexpr double keyFrameTolerance = 0.001; // seconds a bar line may be off a straight stretch

// Which points (input frame, output frame) must be pinned for the stretch to
// pass within `tolerance` input frames of all of them, the first and last
// being fixed: the Douglas-Peucker simplification of the line through them.
void keepNeeded (const std::vector<std::pair<int64_t, int64_t>>& points, size_t first, size_t last,
                 double tolerance, std::vector<bool>& keep)
{
    if (last <= first + 1)
        return;
    const auto [in0, out0] = points[first];
    const auto [in1, out1] = points[last];
    const double slope = static_cast<double> (in1 - in0) / static_cast<double> (out1 - out0);
    size_t worst = first;
    double worstOff = 0.0;
    for (size_t i = first + 1; i < last; ++i)
    {
        const double expected = static_cast<double> (in0) + slope * static_cast<double> (points[i].second - out0);
        const double off = std::abs (static_cast<double> (points[i].first) - expected);
        if (off > worstOff)
        {
            worstOff = off;
            worst = i;
        }
    }
    if (worstOff <= tolerance)
        return;
    keep[worst] = true;
    keepNeeded (points, first, worst, tolerance, keep);
    keepNeeded (points, worst, last, tolerance, keep);
}
} // namespace

std::shared_ptr<RenderedClip> renderClip (const DecodedAudio& audio,
                                          const std::vector<double>& beats,
                                          double fromBeat,
                                          double toBeat,
                                          const RenderSettings& settings,
                                          const std::atomic<bool>* cancelled)
{
    using Stretcher = RubberBand::RubberBandStretcher;

    auto result = std::make_shared<RenderedClip>();
    result->settings = settings;
    result->fromBeat = fromBeat;
    result->toBeat = toBeat;

    // The input: the source range, cut to the track.
    const double sourceRate = audio.sampleRate;
    const auto length = static_cast<int64_t> (audio.samples.getNumSamples());
    const auto toFrame = [&] (double seconds)
    { return std::clamp (static_cast<int64_t> (std::llround (seconds * sourceRate)), int64_t { 0 }, length); };
    const auto inStart = toFrame (timeAtBeat (beats, fromBeat));
    const auto inEnd = toFrame (timeAtBeat (beats, toBeat));
    const double first = beatAtTime (beats, static_cast<double> (inStart) / sourceRate);
    const double last = beatAtTime (beats, static_cast<double> (inEnd) / sourceRate);
    result->firstSourceBeat = first;

    const double framesPerBeat = result->framesPerBeat();
    const auto outLength = static_cast<int64_t> (std::llround ((last - first) * framesPerBeat));
    const auto inLength = inEnd - inStart;
    if (inLength <= 0 || outLength <= 0)
    {
        result->samples.setSize (2, 0);
        return result;
    }
    result->samples.setSize (2, static_cast<int> (outLength));
    result->samples.clear();

    const double timeRatio = static_cast<double> (outLength) / static_cast<double> (inLength);
    const double pitchScale = sourceRate / settings.sampleRate * std::pow (2.0, settings.transposeSemitones / 12.0);
    Stretcher stretcher (static_cast<size_t> (sourceRate), 2,
                         Stretcher::OptionProcessOffline | Stretcher::OptionEngineFiner | Stretcher::OptionPitchHighQuality,
                         timeRatio, pitchScale);
    stretcher.setExpectedInputDuration (static_cast<size_t> (inLength));

    const auto& src = audio.samples;
    const int right = src.getNumChannels() > 1 ? 1 : 0;
    int64_t written = 0;
    std::array<std::vector<float>, 2> spill { std::vector<float> (chunk), std::vector<float> (chunk) };

    // Takes what the stretcher has ready, up to the render's length.
    auto collect = [&]
    {
        for (int available; (available = stretcher.available()) > 0;)
        {
            const auto n = std::min (available, chunk);
            float* out[2] = { spill[0].data(), spill[1].data() };
            stretcher.retrieve (out, static_cast<size_t> (n));
            const auto keep = static_cast<int> (std::clamp<int64_t> (outLength - written, 0, n));
            for (int ch = 0; ch < 2; ++ch)
                result->samples.copyFrom (ch, static_cast<int> (written), spill[static_cast<size_t> (ch)].data(), keep);
            written += keep;
        }
    };

    // Studies the whole input (offline mode), or stretches it.
    auto feed = [&] (bool study)
    {
        for (int64_t done = 0; done < inLength;)
        {
            if (cancelled != nullptr && cancelled->load())
                return false;
            const auto n = static_cast<int> (std::min<int64_t> (chunk, inLength - done));
            const auto at = static_cast<int> (inStart + done);
            const float* in[2] = { src.getReadPointer (0, at), src.getReadPointer (right, at) };
            done += n;
            const bool final = done >= inLength;
            if (study)
            {
                stretcher.study (in, static_cast<size_t> (n), final);
            }
            else
            {
                stretcher.process (in, static_cast<size_t> (n), final);
                collect();
            }
        }
        return true;
    };

    if (! feed (true))
        return nullptr;

    // Pin the bar lines of the source to their places on the project grid,
    // where the track's tempo moves enough to need it. Only there: Rubber
    // Band slips by a fraction of a sample at each key frame, which over
    // hundreds of bars would add up to an audible drift.
    std::vector<std::pair<int64_t, int64_t>> points { { 0, 0 } };
    for (auto beat = static_cast<int64_t> (std::ceil (first / beatsPerKeyFrame)) * beatsPerKeyFrame;
         static_cast<double> (beat) < last; beat += beatsPerKeyFrame)
    {
        const auto in = std::llround (timeAtBeat (beats, static_cast<double> (beat)) * sourceRate) - inStart;
        const auto out = std::llround ((static_cast<double> (beat) - first) * framesPerBeat);
        if (in > points.back().first && in < inLength && out > points.back().second && out < outLength)
            points.emplace_back (in, out);
    }
    points.emplace_back (inLength, outLength);

    std::vector<bool> keep (points.size(), false);
    keepNeeded (points, 0, points.size() - 1, keyFrameTolerance * sourceRate, keep);
    std::map<size_t, size_t> keyFrames;
    for (size_t i = 1; i + 1 < points.size(); ++i)
        if (keep[i])
            keyFrames[static_cast<size_t> (points[i].first)] = static_cast<size_t> (points[i].second);
    if (! keyFrames.empty())
        stretcher.setKeyFrameMap (keyFrames);

    if (! feed (false))
        return nullptr;
    collect();
    return result;
}

} // namespace djmix
