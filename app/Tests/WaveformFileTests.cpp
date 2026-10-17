#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>

#include "analysis/WaveformFile.h"

namespace
{
struct TempFile
{
    std::filesystem::path path;

    explicit TempFile (const std::string& name)
        : path (std::filesystem::temp_directory_path() / name)
    {
    }

    ~TempFile() { std::filesystem::remove (path); }
};

template <typename T>
void put (std::ofstream& out, T value)
{
    out.write (reinterpret_cast<const char*> (&value), sizeof (T));
}

void writeWfm (const std::filesystem::path& path, uint64_t declaredPoints, const std::vector<float>& interleaved,
               uint16_t version = 1)
{
    std::ofstream out (path, std::ios::binary);
    out.write ("DJWF", 4);
    put<uint16_t> (out, version);
    put<uint16_t> (out, 1);
    put<uint32_t> (out, 44100);
    put<uint32_t> (out, 256);
    put<uint64_t> (out, declaredPoints);
    put<uint64_t> (out, 0);
    out.write (reinterpret_cast<const char*> (interleaved.data()),
               static_cast<std::streamsize> (interleaved.size() * sizeof (float)));
}
} // namespace

TEST_CASE ("WaveformFile reads peak and RMS points", "[waveform]")
{
    TempFile file ("djmix-test-ok.wfm");
    writeWfm (file.path, 3, { 0.5f, 0.25f, 1.0f, 0.5f, 0.0f, 0.0f });

    std::string error;
    const auto overview = djmix::WaveformFile::read (file.path, &error);
    INFO (error);
    REQUIRE (overview.has_value());
    CHECK (overview->sampleRate == 44100);
    CHECK (overview->samplesPerPoint == 256);
    CHECK (overview->peak == std::vector<float> { 0.5f, 1.0f, 0.0f });
    CHECK (overview->rms == std::vector<float> { 0.25f, 0.5f, 0.0f });
}

TEST_CASE ("WaveformFile rejects bad files", "[waveform]")
{
    TempFile file ("djmix-test-bad.wfm");
    std::string error;

    SECTION ("point count does not match the body")
    {
        writeWfm (file.path, 5, { 0.5f, 0.25f });
        CHECK_FALSE (djmix::WaveformFile::read (file.path, &error).has_value());
    }

    SECTION ("unknown version")
    {
        writeWfm (file.path, 1, { 0.5f, 0.25f }, 9);
        CHECK_FALSE (djmix::WaveformFile::read (file.path, &error).has_value());
    }

    SECTION ("wrong magic")
    {
        std::ofstream (file.path, std::ios::binary) << "RIFF0000000000000000000000000000";
        CHECK_FALSE (djmix::WaveformFile::read (file.path, &error).has_value());
    }

    SECTION ("missing file")
    {
        CHECK_FALSE (djmix::WaveformFile::read (file.path / "nope", &error).has_value());
    }

    CHECK_FALSE (error.empty());
}
