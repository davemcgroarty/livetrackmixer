#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace djmix
{

// Waveform overview written by djanalyze (docs/wfm-format.md): one peak and one
// RMS value per block of samplesPerPoint samples of the mono mix.
struct WaveformOverview
{
    static constexpr uint16_t supportedFormatVersion = 1;

    uint32_t sampleRate = 0;
    uint32_t samplesPerPoint = 0;
    std::vector<float> peak;
    std::vector<float> rms;

    double secondsPerPoint() const { return sampleRate == 0 ? 0.0 : double (samplesPerPoint) / sampleRate; }
};

class WaveformFile
{
public:
    static std::optional<WaveformOverview> read (const std::filesystem::path& file, std::string* error = nullptr);
};

} // namespace djmix
