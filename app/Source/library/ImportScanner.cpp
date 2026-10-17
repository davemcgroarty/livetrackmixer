#include "ImportScanner.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include "LibraryDatabase.h"

namespace djmix
{

namespace
{
constexpr std::array<std::string_view, 10> audioExtensions {
    ".wav", ".wave", ".flac", ".aif", ".aiff", ".mp3", ".ogg", ".opus", ".m4a", ".aac",
};

std::string lowercase (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) { return std::tolower (c); });
    return text;
}

bool isHidden (const std::filesystem::path& path)
{
    const auto name = path.filename().string();
    return ! name.empty() && name.front() == '.';
}

std::string trim (std::string_view text)
{
    const auto first = text.find_first_not_of (" \t._-");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of (" \t");
    return std::string (text.substr (first, last - first + 1));
}

// A leading track number: "01 Title", "01 - Title", "1. Title", "3_Title".
// Kept when it may be part of a name: "808 State", "50 Cent", "10cc".
std::string_view stripTrackNumber (std::string_view name)
{
    size_t digits = 0;
    while (digits < name.size() && std::isdigit (static_cast<unsigned char> (name[digits])))
        ++digits;
    if (digits == 0 || digits > 3)
        return name;

    size_t end = digits;
    bool punctuated = false;
    while (end < name.size() && (name[end] == ' ' || name[end] == '.' || name[end] == '-' || name[end] == '_'))
        punctuated |= name[end++] != ' ';
    if (end == digits || end == name.size())
        return name;

    const bool leadingZero = name[0] == '0';
    return leadingZero || punctuated ? name.substr (end) : name;
}
} // namespace

bool isAudioFile (const std::filesystem::path& path)
{
    const auto extension = lowercase (path.extension().string());
    return std::find (audioExtensions.begin(), audioExtensions.end(), extension) != audioExtensions.end();
}

std::vector<std::filesystem::path> collectAudioFiles (const std::vector<std::filesystem::path>& inputs)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> found;
    std::error_code error;

    for (const auto& input : inputs)
    {
        if (fs::is_regular_file (input, error))
        {
            if (isAudioFile (input))
                found.push_back (fs::absolute (input, error));
            continue;
        }

        if (! fs::is_directory (input, error))
            continue;

        fs::recursive_directory_iterator it (input, fs::directory_options::skip_permission_denied, error), end;
        for (; ! error && it != end; it.increment (error))
        {
            if (isHidden (it->path()))
            {
                if (it->is_directory (error))
                    it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file (error) && isAudioFile (it->path()))
                found.push_back (fs::absolute (it->path(), error));
        }
    }

    std::sort (found.begin(), found.end());
    found.erase (std::unique (found.begin(), found.end()), found.end());
    return found;
}

TrackName trackNameFromFile (const std::filesystem::path& path)
{
    const auto stem = pathToUtf8 (path.stem());
    const auto name = stripTrackNumber (stem);
    const auto separator = name.find (" - ");
    if (separator == std::string_view::npos)
        return { {}, trim (name) };

    auto artist = trim (name.substr (0, separator));
    auto title = trim (name.substr (separator + 3));
    if (artist.empty() || title.empty())
        return { {}, trim (name) };
    return { artist, title };
}

} // namespace djmix
