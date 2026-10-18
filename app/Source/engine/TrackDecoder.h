#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "CuePlayer.h"

namespace djmix
{

// Decodes a whole audio file into memory as stereo float samples, for
// auditions now and the render cache later. Uses JUCE's own readers (WAV,
// AIFF, FLAC, Ogg Vorbis, MP3, plus whatever the platform adds) and falls back
// to ffmpeg on PATH for anything else, as djanalyze does.
//
// Blocking and slow (a second or two for a long MP3): call it off the message
// thread and never on the audio thread.
class TrackDecoder
{
public:
    // Files longer than this are refused rather than filling memory.
    static constexpr double maxSeconds = 60.0 * 60.0;

    static std::shared_ptr<DecodedAudio> decode (const std::filesystem::path& file, std::string& error);

    // Only ffmpeg, for tests. `ffmpeg` is its path.
    static std::shared_ptr<DecodedAudio> decodeWithFfmpeg (const std::filesystem::path& file,
                                                           const std::filesystem::path& ffmpeg,
                                                           std::string& error);
};

} // namespace djmix
