#include "WaveformFile.h"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>

namespace djmix
{

namespace
{
static_assert (std::endian::native == std::endian::little, "WaveformFile assumes a little-endian host");

constexpr std::array<char, 4> magic { 'D', 'J', 'W', 'F' };
constexpr size_t headerSize = 32;

template <typename T>
T readField (const char* bytes)
{
    T value;
    std::memcpy (&value, bytes, sizeof (T));
    return value;
}

std::optional<WaveformOverview> failWith (std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
    return std::nullopt;
}
} // namespace

std::optional<WaveformOverview> WaveformFile::read (const std::filesystem::path& file, std::string* error)
{
    std::ifstream in (file, std::ios::binary);
    if (! in)
        return failWith (error, "cannot open " + file.string());

    std::array<char, headerSize> header {};
    if (! in.read (header.data(), header.size()))
        return failWith (error, "waveform file is shorter than its header");

    if (std::memcmp (header.data(), magic.data(), magic.size()) != 0)
        return failWith (error, "not a waveform file");

    const auto formatVersion = readField<uint16_t> (header.data() + 4);
    const auto channels = readField<uint16_t> (header.data() + 6);
    if (formatVersion != WaveformOverview::supportedFormatVersion)
        return failWith (error, "unsupported waveform format version " + std::to_string (formatVersion));
    if (channels != 1)
        return failWith (error, "unsupported waveform channel count " + std::to_string (channels));

    WaveformOverview overview;
    overview.sampleRate = readField<uint32_t> (header.data() + 8);
    overview.samplesPerPoint = readField<uint32_t> (header.data() + 12);
    const auto numPoints = readField<uint64_t> (header.data() + 16);

    if (overview.sampleRate == 0 || overview.samplesPerPoint == 0)
        return failWith (error, "waveform header has a zero rate");

    // Guard against a corrupt count before allocating.
    in.seekg (0, std::ios::end);
    const auto bodyBytes = static_cast<uint64_t> (in.tellg()) - headerSize;
    if (bodyBytes != numPoints * 2 * sizeof (float))
        return failWith (error, "waveform body size does not match its point count");
    in.seekg (headerSize);

    std::vector<float> interleaved (static_cast<size_t> (numPoints) * 2);
    if (! in.read (reinterpret_cast<char*> (interleaved.data()), static_cast<std::streamsize> (bodyBytes)))
        return failWith (error, "waveform file is truncated");

    overview.peak.resize (static_cast<size_t> (numPoints));
    overview.rms.resize (static_cast<size_t> (numPoints));
    for (size_t i = 0; i < overview.peak.size(); ++i)
    {
        overview.peak[i] = interleaved[2 * i];
        overview.rms[i] = interleaved[2 * i + 1];
    }

    return overview;
}

} // namespace djmix
