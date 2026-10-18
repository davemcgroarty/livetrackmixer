#include "TrackStructure.h"

#include <algorithm>

namespace djmix
{

TrackStructure::TrackStructure (const TrackAnalysis& a)
    : analysis (a)
{
    phraseStarts.assign (analysis.bars.size(), false);
    sectionStarts.assign (analysis.bars.size(), false);
    auto mark = [this] (std::vector<bool>& flags, int bar)
    {
        if (bar >= 1 && static_cast<size_t> (bar) <= flags.size())
            flags[static_cast<size_t> (bar - 1)] = true; // bar numbers are 1-based
    };
    for (const auto& p : analysis.phrases)
        mark (phraseStarts, p.startBar);
    for (const auto& s : analysis.sections)
        mark (sectionStarts, s.startBar);

    if (analysis.beats.size() >= 2)
    {
        std::vector<double> gaps;
        for (size_t i = 1; i < analysis.beats.size(); ++i)
            gaps.push_back (analysis.beats[i] - analysis.beats[i - 1]);
        std::nth_element (gaps.begin(), gaps.begin() + static_cast<long> (gaps.size() / 2), gaps.end());
        period = gaps[gaps.size() / 2];
    }
    else if (analysis.bpm > 0.0)
    {
        period = 60.0 / analysis.bpm;
    }
}

int TrackStructure::barAt (double seconds) const
{
    const auto& bars = analysis.bars;
    auto it = std::upper_bound (bars.begin(), bars.end(), seconds, [] (double t, const Bar& b) { return t < b.start; });
    if (it == bars.begin())
        return -1;
    const int index = static_cast<int> (std::distance (bars.begin(), it)) - 1;
    return seconds < barEnd (index) ? index : -1;
}

int TrackStructure::sectionAt (double seconds) const
{
    for (size_t i = 0; i < analysis.sections.size(); ++i)
        if (seconds >= analysis.sections[i].start && seconds < analysis.sections[i].end)
            return static_cast<int> (i);
    return -1;
}

double TrackStructure::barEnd (int bar) const
{
    const auto next = static_cast<size_t> (bar) + 1;
    if (next < analysis.bars.size())
        return analysis.bars[next].start;
    return std::min (analysis.durationSec, barStart (bar) + 4.0 * period);
}

bool TrackStructure::startsPhrase (int bar) const
{
    return bar >= 0 && static_cast<size_t> (bar) < phraseStarts.size() && phraseStarts[static_cast<size_t> (bar)];
}

std::pair<int, int> TrackStructure::phraseAround (int bar) const
{
    for (const auto& p : analysis.phrases)
    {
        const int first = p.startBar - 1;
        if (bar >= first && bar < first + p.lengthBars)
            return { first, std::min (first + p.lengthBars, numBars()) - 1 };
    }
    const int first = bar - bar % 8;
    return { first, std::min (first + 8, std::max (numBars(), bar + 1)) - 1 };
}

bool TrackStructure::startsSection (int bar) const
{
    return bar >= 0 && static_cast<size_t> (bar) < sectionStarts.size() && sectionStarts[static_cast<size_t> (bar)];
}

TrackStructure::GridVisibility TrackStructure::gridAt (double pixelsPerSecond) const
{
    const double perBeat = pixelsPerSecond * period;
    return { perBeat >= minBeatSpacing, perBeat * 4.0 >= minBarSpacing };
}

} // namespace djmix
