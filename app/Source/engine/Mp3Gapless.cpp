#include "Mp3Gapless.h"

#include <cstring>

namespace djmix
{

namespace
{
uint32_t bigEndian32 (const uint8_t* p)
{
    return (uint32_t (p[0]) << 24) | (uint32_t (p[1]) << 16) | (uint32_t (p[2]) << 8) | uint32_t (p[3]);
}
} // namespace

size_t id3v2Size (const uint8_t* data, size_t size)
{
    // "ID3", version, flags, then a 28-bit "syncsafe" size.
    if (size < 10 || std::memcmp (data, "ID3", 3) != 0)
        return 0;
    const size_t tagSize = (size_t (data[6] & 0x7f) << 21) | (size_t (data[7] & 0x7f) << 14)
                           | (size_t (data[8] & 0x7f) << 7) | size_t (data[9] & 0x7f);
    const bool footer = (data[5] & 0x10) != 0;
    return 10 + tagSize + (footer ? 10 : 0);
}

std::optional<Mp3Gapless> readMp3Gapless (const uint8_t* data, size_t size)
{
    size_t pos = id3v2Size (data, size);

    // The first frame header: 11 sync bits.
    while (pos + 4 <= size && ! (data[pos] == 0xff && (data[pos + 1] & 0xe0) == 0xe0))
        ++pos;
    if (pos + 4 > size)
        return std::nullopt;

    const int version = (data[pos + 1] >> 3) & 3; // 3: MPEG-1, 2: MPEG-2, 0: MPEG-2.5
    const int layer = (data[pos + 1] >> 1) & 3;   // 1: Layer III
    const bool mono = ((data[pos + 3] >> 6) & 3) == 3;
    if (layer != 1 || version == 1)
        return std::nullopt;

    // The Xing/Info tag sits after the side information.
    const size_t sideInfo = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    size_t tag = pos + 4 + sideInfo;
    if (tag + 8 > size || (std::memcmp (data + tag, "Xing", 4) != 0 && std::memcmp (data + tag, "Info", 4) != 0))
        return std::nullopt;

    const uint32_t flags = bigEndian32 (data + tag + 4);
    tag += 8;
    if (flags & 1u)
        tag += 4; // frame count
    if (flags & 2u)
        tag += 4; // byte count
    if (flags & 4u)
        tag += 100; // seek table
    if (flags & 8u)
        tag += 4; // quality

    // The LAME extension: a 9-character encoder name, 12 bytes of other
    // fields, then 12 bits of encoder delay and 12 bits of padding.
    if (tag + 24 > size)
        return std::nullopt;
    const uint8_t* d = data + tag + 21;
    Mp3Gapless g;
    g.encoderDelay = (d[0] << 4) | (d[1] >> 4);
    g.padding = ((d[1] & 0x0f) << 8) | d[2];
    if (g.encoderDelay == 0 && g.padding == 0)
        return std::nullopt; // an encoder that wrote no gapless information
    return g;
}

} // namespace djmix
