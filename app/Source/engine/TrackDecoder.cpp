#include "TrackDecoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "Mp3Gapless.h"
#include "RealtimeGuard.h"
#include "analysis/AnalyzerProtocol.h"
#include "library/LibraryDatabase.h"

namespace djmix
{

namespace
{
juce::File toJuceFile (const std::filesystem::path& path)
{
    return juce::File (juce::String::fromUTF8 (pathToUtf8 (path).c_str()));
}

constexpr int ffmpegRate = 44100;

// The LAME header of an MP3, read from just past any ID3v2 tag (which can be
// megabytes of cover art).
std::optional<Mp3Gapless> gaplessInfo (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return std::nullopt;
    uint8_t id3[10] {};
    if (in.read (id3, sizeof (id3)) != static_cast<int> (sizeof (id3)))
        return std::nullopt;
    in.setPosition (static_cast<juce::int64> (id3v2Size (id3, sizeof (id3))));
    std::vector<uint8_t> head (16384);
    const auto got = in.read (head.data(), static_cast<int> (head.size()));
    return got > 0 ? readMp3Gapless (head.data(), static_cast<size_t> (got)) : std::nullopt;
}

std::shared_ptr<DecodedAudio> decodeWithJuce (const juce::File& file, std::string& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
    {
        error = "the file has no audio";
        return {};
    }
    if (reader->lengthInSamples > static_cast<juce::int64> (TrackDecoder::maxSeconds * reader->sampleRate))
    {
        error = "the file is longer than an hour";
        return {};
    }

    auto audio = std::make_shared<DecodedAudio>();
    const auto length = static_cast<int> (reader->lengthInSamples);
    audio->sampleRate = reader->sampleRate;
    audio->samples.setSize (2, length);
    // Mono files play on both sides; files with more channels use the first two.
    reader->read (&audio->samples, 0, length, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        audio->samples.copyFrom (1, 0, audio->samples, 0, 0, length);

    // JUCE's MP3 reader keeps the encoder's and decoder's delay; drop them as
    // djanalyze's decoder does, so analysis times line up with the audio.
    if (reader->getFormatName().containsIgnoreCase ("MP3"))
    {
        if (const auto gapless = gaplessInfo (file))
        {
            const int skip = std::min (gapless->samplesToSkip(), length);
            const int keep = std::max (0, length - skip - gapless->samplesToTrim());
            juce::AudioBuffer<float> trimmed (2, keep);
            for (int ch = 0; ch < 2; ++ch)
                trimmed.copyFrom (ch, 0, audio->samples, ch, skip, keep);
            audio->samples = std::move (trimmed);
        }
    }
    return audio;
}
} // namespace

std::shared_ptr<DecodedAudio> TrackDecoder::decode (const std::filesystem::path& path, std::string& error)
{
    DJMIX_ASSERT_NOT_REALTIME ("decoding audio");
    error.clear();

    const auto file = toJuceFile (path);
    if (! file.existsAsFile())
    {
        error = "the file is missing";
        return {};
    }

    if (auto audio = decodeWithJuce (file, error); audio != nullptr || ! error.empty())
        return audio;

    if (const auto ffmpeg = findOnPath ("ffmpeg"))
        return decodeWithFfmpeg (path, *ffmpeg, error);

    error = "the format is not supported (install ffmpeg to play it)";
    return {};
}

std::shared_ptr<DecodedAudio> TrackDecoder::decodeWithFfmpeg (const std::filesystem::path& path,
                                                              const std::filesystem::path& ffmpeg,
                                                              std::string& error)
{
    DJMIX_ASSERT_NOT_REALTIME ("decoding audio");

    // The same conversion djanalyze's fallback uses: 32-bit float, stereo, 44.1 kHz.
    juce::StringArray command { juce::String::fromUTF8 (pathToUtf8 (ffmpeg).c_str()),
                                "-v", "error", "-nostdin",
                                "-i", toJuceFile (path).getFullPathName(),
                                "-f", "f32le", "-ac", "2", "-ar", juce::String (ffmpegRate), "-" };

    juce::ChildProcess process;
    if (! process.start (command, juce::ChildProcess::wantStdOut))
    {
        error = "ffmpeg could not be started";
        return {};
    }

    juce::MemoryBlock data;
    const auto limit = static_cast<size_t> (maxSeconds * ffmpegRate) * 2 * sizeof (float);
    std::vector<char> chunk (1 << 20);
    for (;;)
    {
        const int read = process.readProcessOutput (chunk.data(), static_cast<int> (chunk.size()));
        if (read <= 0)
            break;
        data.append (chunk.data(), static_cast<size_t> (read));
        if (data.getSize() > limit)
        {
            process.kill();
            error = "the file is longer than an hour";
            return {};
        }
    }
    process.waitForProcessToFinish (10000);

    const auto frames = static_cast<int> (data.getSize() / (2 * sizeof (float)));
    if (process.getExitCode() != 0 || frames == 0)
    {
        error = "ffmpeg could not decode the file";
        return {};
    }

    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = ffmpegRate;
    audio->samples.setSize (2, frames);
    const auto* interleaved = static_cast<const float*> (data.getData());
    for (int ch = 0; ch < 2; ++ch)
    {
        float* out = audio->samples.getWritePointer (ch);
        for (int i = 0; i < frames; ++i)
            out[i] = interleaved[2 * i + ch];
    }
    return audio;
}

} // namespace djmix
