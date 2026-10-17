#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace djmix
{

// A key on the Camelot wheel: number 1–12, letter A (minor) or B (major).
struct CamelotKey
{
    int number = 1;
    bool minor = true;

    bool operator== (const CamelotKey&) const = default;
};

std::optional<CamelotKey> parseCamelot (std::string_view text);
std::string toString (CamelotKey key);

// How two keys sound together (design spec, Arrangement: key warnings).
//  - Compatible: same key, ±1 on the wheel with the same letter, or the relative
//    major/minor (same number, other letter).
//  - Caution: ±2 with the same letter, or ±1 with the other letter.
//  - Clash: anything further.
enum class KeyRelation
{
    compatible,
    caution,
    clash
};

KeyRelation relation (CamelotKey a, CamelotKey b);

// Steps between two wheel numbers, 0–6.
int wheelDistance (int a, int b);

} // namespace djmix
