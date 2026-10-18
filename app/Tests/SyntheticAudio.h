#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "engine/CuePlayer.h"

// Made-up tracks for the engine tests, with beat grids known exactly.
namespace djmix::test
{

constexpr double pi = 3.14159265358979323846;

// The beat times of a constant tempo from `first` seconds, up to `seconds`.
inline std::vector<double> beatGrid (double bpm, double first, double seconds)
{
    std::vector<double> beats;
    for (int i = 0;; ++i)
    {
        const double t = first + i * 60.0 / bpm;
        if (t >= seconds)
            return beats;
        beats.push_back (t);
    }
}

// A click at each of `beats`: a 10 ms burst of `hz`, dying away, at full
// level on the beat and silent between.
inline std::shared_ptr<DecodedAudio> clicksAt (const std::vector<double>& beats, double seconds, double hz = 2000.0, double rate = 44100.0)
{
    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = rate;
    const int n = static_cast<int> (seconds * rate);
    audio->samples.setSize (2, n);
    audio->samples.clear();
    const int burst = static_cast<int> (0.01 * rate);
    for (const double t : beats)
    {
        const int at = static_cast<int> (std::lround (t * rate));
        for (int i = 0; i < burst && at + i < n; ++i)
        {
            const auto v = static_cast<float> (0.8 * std::exp (-i / (0.002 * rate)) * std::sin (2.0 * pi * hz * i / rate));
            audio->samples.setSample (0, at + i, v);
            audio->samples.setSample (1, at + i, v);
        }
    }
    return audio;
}

// A click on every beat of a steady tempo.
inline std::shared_ptr<DecodedAudio> clickTrack (double bpm, double first, double seconds, double hz = 2000.0, double rate = 44100.0)
{
    return clicksAt (beatGrid (bpm, first, seconds), seconds, hz, rate);
}

inline std::shared_ptr<DecodedAudio> sineTrack (double hz, double seconds, double rate = 44100.0, float level = 0.5f)
{
    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = rate;
    const int n = static_cast<int> (seconds * rate);
    audio->samples.setSize (2, n);
    for (int i = 0; i < n; ++i)
    {
        const auto v = static_cast<float> (level * std::sin (2.0 * pi * hz * i / rate));
        audio->samples.setSample (0, i, v);
        audio->samples.setSample (1, i, v);
    }
    return audio;
}

// Where a click starts: the first sample louder than `threshold` within
// `window` seconds either side of `expected`, in seconds. Nothing if none is.
inline std::optional<double> onsetNear (const std::vector<float>& samples, double rate, double expected,
                                        double window = 0.02, float threshold = 0.1f)
{
    const auto from = std::max<long> (0, std::lround ((expected - window) * rate));
    const auto to = std::min<long> (static_cast<long> (samples.size()), std::lround ((expected + window) * rate));
    for (long i = from; i < to; ++i)
        if (std::abs (samples[static_cast<size_t> (i)]) > threshold)
            return static_cast<double> (i) / rate;
    return std::nullopt;
}

// The biggest jump from one sample to the next: a click shows as a jump
// near the signal's level, where a smooth signal moves a little each sample.
inline float maxStep (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, v.size()); ++i)
        m = std::max (m, std::abs (v[i] - v[i - 1]));
    return m;
}

// Zero crossings per second over [from, to) samples: twice the frequency of a sine.
inline double crossingsPerSecond (const std::vector<float>& v, double rate, size_t from, size_t to)
{
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f))
            ++crossings;
    return crossings * rate / static_cast<double> (to - from);
}

} // namespace djmix::test
