#include "TrackOverrides.h"

#include <nlohmann/json.hpp>

namespace djmix
{

std::string sectionStartsToJson (const std::vector<double>& starts)
{
    return nlohmann::json (starts).dump();
}

std::optional<std::vector<double>> sectionStartsFromJson (const std::string& text)
{
    if (text.empty())
        return std::nullopt;
    const auto json = nlohmann::json::parse (text, nullptr, false);
    if (! json.is_array())
        return std::nullopt;
    std::vector<double> starts;
    for (const auto& value : json)
    {
        if (! value.is_number())
            return std::nullopt;
        starts.push_back (value.get<double>());
    }
    return starts;
}

} // namespace djmix
