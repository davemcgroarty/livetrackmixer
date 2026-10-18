#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "engine/Mp3Gapless.h"

using djmix::readMp3Gapless;

namespace
{
// The start of an MP3 file: an optional ID3v2 tag, then a first frame
// carrying a Xing/Info tag with a LAME extension.
std::vector<uint8_t> mp3Start (bool id3, bool mpeg1Stereo, const char* tagName, int delay, int padding)
{
    std::vector<uint8_t> b;
    auto put = [&] (std::initializer_list<int> bytes)
    {
        for (int x : bytes)
            b.push_back (static_cast<uint8_t> (x));
    };
    auto text = [&] (const char* s)
    {
        for (; *s != 0; ++s)
            b.push_back (static_cast<uint8_t> (*s));
    };

    if (id3)
    {
        text ("ID3");
        put ({ 3, 0, 0, 0, 0, 2, 1 }); // 257 bytes, as a syncsafe integer
        b.insert (b.end(), 257, 0);
    }

    if (mpeg1Stereo)
        put ({ 0xff, 0xfb, 0x90, 0x00 }); // MPEG-1 Layer III, stereo
    else
        put ({ 0xff, 0xf3, 0x90, 0xc0 }); // MPEG-2 Layer III, mono
    b.insert (b.end(), mpeg1Stereo ? 32 : 9, 0); // side information

    text (tagName);
    put ({ 0, 0, 0, 0x0f });       // frames, bytes, seek table and quality present
    b.insert (b.end(), 4 + 4 + 100 + 4, 0);
    text ("LAME3.100");
    b.insert (b.end(), 12, 0);
    put ({ delay >> 4, ((delay & 0x0f) << 4) | (padding >> 8), padding & 0xff });
    b.insert (b.end(), 64, 0);
    return b;
}
} // namespace

TEST_CASE ("MP3 gapless information comes from the LAME header", "[audio][mp3]")
{
    for (const bool id3 : { false, true })
    {
        for (const bool mpeg1 : { true, false })
        {
            INFO ("ID3 " << id3 << ", MPEG-1 " << mpeg1);
            const auto data = mp3Start (id3, mpeg1, "Info", 576, 1044);
            const auto g = readMp3Gapless (data.data(), data.size());
            REQUIRE (g.has_value());
            CHECK (g->encoderDelay == 576);
            CHECK (g->padding == 1044);
            CHECK (g->samplesToSkip() == 1105);
            CHECK (g->samplesToTrim() == 515);
        }
    }

    const auto vbr = mp3Start (false, true, "Xing", 1105, 300);
    const auto g = readMp3Gapless (vbr.data(), vbr.size());
    REQUIRE (g.has_value());
    CHECK (g->encoderDelay == 1105);
    CHECK (g->samplesToTrim() == 0); // padding shorter than the decoder delay
}

TEST_CASE ("MP3s without a LAME header have no gapless information", "[audio][mp3]")
{
    auto data = mp3Start (true, true, "Nope", 576, 1044);
    CHECK_FALSE (readMp3Gapless (data.data(), data.size()));

    data = mp3Start (false, true, "Info", 0, 0); // an encoder that left it empty
    CHECK_FALSE (readMp3Gapless (data.data(), data.size()));

    data = mp3Start (false, true, "Info", 576, 1044);
    data.resize (60); // cut off before the LAME fields
    CHECK_FALSE (readMp3Gapless (data.data(), data.size()));

    const std::string text = "not an mp3 at all";
    CHECK_FALSE (readMp3Gapless (reinterpret_cast<const uint8_t*> (text.data()), text.size()));
    CHECK (djmix::id3v2Size (reinterpret_cast<const uint8_t*> (text.data()), text.size()) == 0);
}
