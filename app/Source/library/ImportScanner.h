#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace djmix
{

// Extensions the importer picks up. WAV, FLAC, AIFF, MP3 and OGG are read by
// libsndfile; the rest need ffmpeg on the analyzer's PATH.
bool isAudioFile (const std::filesystem::path& path);

// Every audio file among `inputs`, searching folders recursively and skipping
// hidden files and folders. Sorted, without duplicates.
std::vector<std::filesystem::path> collectAudioFiles (const std::vector<std::filesystem::path>& inputs);

// Title and artist from a file name like "01 - Artist - Title.flac".
// Without " - " the whole name (minus a leading track number) is the title.
struct TrackName
{
    std::string artist;
    std::string title;
};

TrackName trackNameFromFile (const std::filesystem::path& path);

} // namespace djmix
