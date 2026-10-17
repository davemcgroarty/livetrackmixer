#include "Camelot.h"

#include <cstdlib>

namespace djmix
{

std::optional<CamelotKey> parseCamelot (std::string_view text)
{
    if (text.size() < 2 || text.size() > 3)
        return std::nullopt;

    const char letter = text.back();
    if (letter != 'A' && letter != 'B' && letter != 'a' && letter != 'b')
        return std::nullopt;

    int number = 0;
    for (const char c : text.substr (0, text.size() - 1))
    {
        if (c < '0' || c > '9')
            return std::nullopt;
        number = number * 10 + (c - '0');
    }

    if (number < 1 || number > 12)
        return std::nullopt;

    return CamelotKey { number, letter == 'A' || letter == 'a' };
}

std::string toString (CamelotKey key)
{
    return std::to_string (key.number) + (key.minor ? "A" : "B");
}

int wheelDistance (int a, int b)
{
    const int d = std::abs (a - b) % 12;
    return d > 6 ? 12 - d : d;
}

KeyRelation relation (CamelotKey a, CamelotKey b)
{
    const int distance = wheelDistance (a.number, b.number);

    if (a.minor == b.minor)
    {
        if (distance <= 1)
            return KeyRelation::compatible;
        return distance == 2 ? KeyRelation::caution : KeyRelation::clash;
    }

    if (distance == 0)
        return KeyRelation::compatible;
    return distance == 1 ? KeyRelation::caution : KeyRelation::clash;
}

} // namespace djmix
