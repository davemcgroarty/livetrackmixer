#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace djmix
{

// Gapless information from an MP3's LAME (Xing/Info) header: how many
// samples the encoder added at the start and the end.
//
// Decoders that honour it (mpg123 inside libsndfile, which djanalyze uses,
// and ffmpeg) skip `encoderDelay + decoderDelay` samples at the start and
// trim `padding - decoderDelay` at the end. JUCE's MP3 reader does not, so
// without this its audio runs about 25 ms behind the analysis.
struct Mp3Gapless
{
    static constexpr int decoderDelay = 529;

    int encoderDelay = 0;
    int padding = 0;

    int samplesToSkip() const { return encoderDelay + decoderDelay; }
    int samplesToTrim() const { return padding > decoderDelay ? padding - decoderDelay : 0; }
};

// The size of an ID3v2 tag at the start of a file (from its first 10 bytes),
// or 0 if there is none. The audio frames start after it.
size_t id3v2Size (const uint8_t* data, size_t size);

// Reads the header from the start of an MP3 file (a few kilobytes past any
// ID3v2 tag is enough). Nothing if the file has no LAME tag.
std::optional<Mp3Gapless> readMp3Gapless (const uint8_t* data, size_t size);

} // namespace djmix
