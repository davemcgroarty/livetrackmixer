#include "EffectiveAnalysis.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace djmix
{

namespace
{
constexpr int beatsPerBar = 4;
constexpr int phraseBars = 8;
constexpr int longPhraseBars = 16;
constexpr int longSectionBars = 32;

int positiveMod (int value, int modulus)
{
    return ((value % modulus) + modulus) % modulus;
}

double medianGap (const std::vector<double>& times)
{
    if (times.size() < 2)
        return 0.5;
    std::vector<double> gaps;
    for (size_t i = 1; i < times.size(); ++i)
        gaps.push_back (times[i] - times[i - 1]);
    std::nth_element (gaps.begin(), gaps.begin() + static_cast<long> (gaps.size() / 2), gaps.end());
    return gaps[gaps.size() / 2];
}

size_t nearestIndex (const std::vector<double>& times, double t)
{
    const auto it = std::lower_bound (times.begin(), times.end(), t);
    if (it == times.begin())
        return 0;
    if (it == times.end())
        return times.size() - 1;
    const auto i = static_cast<size_t> (std::distance (times.begin(), it));
    return (t - times[i - 1] <= times[i] - t) ? i - 1 : i;
}

// The beats and which of them start bars, after the grid override.
void applyGrid (const TrackAnalysis& a, const GridOverride& grid, TrackAnalysis& e)
{
    std::vector<double> beats;
    int firstDownbeat = 0; // index into beats of a bar start before the shift

    if (grid.bpm && *grid.bpm > 0.0)
    {
        const double period = 60.0 / *grid.bpm;
        const double anchor = ! a.downbeats.empty() ? a.downbeats.front() : (! a.beats.empty() ? a.beats.front() : 0.0);
        // Cover the whole track, whatever the offset does later.
        const auto first = static_cast<long> (std::floor ((-anchor - std::abs (grid.offsetSeconds)) / period)) - 1;
        const auto last = static_cast<long> (std::ceil ((a.durationSec - anchor + std::abs (grid.offsetSeconds)) / period)) + 1;
        for (long k = first; k <= last; ++k)
            beats.push_back (anchor + static_cast<double> (k) * period);
        firstDownbeat = positiveMod (static_cast<int> (-first), beatsPerBar);
        e.bpm = *grid.bpm;
        e.tempoVariable = false;
    }
    else
    {
        beats = a.beats;
        if (! a.downbeats.empty() && ! beats.empty())
            firstDownbeat = static_cast<int> (nearestIndex (beats, a.downbeats.front()));
    }

    const int barPhase = firstDownbeat + grid.downbeatShift;
    e.beats.clear();
    e.downbeats.clear();
    e.bars.clear();
    for (size_t i = 0; i < beats.size(); ++i)
    {
        const double t = beats[i] + grid.offsetSeconds;
        if (t < 0.0 || t >= a.durationSec)
            continue;
        const int index = static_cast<int> (e.beats.size());
        e.beats.push_back (t);
        if (positiveMod (static_cast<int> (i) - barPhase, beatsPerBar) == 0)
        {
            e.downbeats.push_back (t);
            e.bars.push_back ({ static_cast<int> (e.bars.size()) + 1, t, index });
        }
    }
}

double barEnd (const TrackAnalysis& e, size_t bar)
{
    if (bar + 1 < e.bars.size())
        return e.bars[bar + 1].start;
    return std::min (e.durationSec, e.bars[bar].start + beatsPerBar * medianGap (e.beats));
}

// The analysed bar (index) playing at time t, or -1.
int analysedBarAt (const TrackAnalysis& a, double t)
{
    if (a.bars.empty() || t < a.bars.front().start)
        return -1;
    const auto it = std::upper_bound (a.bars.begin(), a.bars.end(), t, [] (double x, const Bar& b) { return x < b.start; });
    return static_cast<int> (std::distance (a.bars.begin(), it)) - 1;
}

void applySections (const TrackAnalysis& a, const std::vector<double>& startTimes, TrackAnalysis& e)
{
    e.sections.clear();
    e.phrases.clear();
    e.descriptorsPerSection.clear();
    if (e.bars.empty())
        return;

    std::vector<double> barStarts;
    for (const auto& b : e.bars)
        barStarts.push_back (b.start);

    std::set<size_t> boundaries { 0 };
    for (const double t : startTimes)
        boundaries.insert (nearestIndex (barStarts, t));
    const std::vector<size_t> starts (boundaries.begin(), boundaries.end());

    for (size_t i = 0; i < starts.size(); ++i)
    {
        const size_t first = starts[i];
        const size_t next = i + 1 < starts.size() ? starts[i + 1] : e.bars.size();
        Section s;
        s.startBar = static_cast<int> (first) + 1;
        s.lengthBars = static_cast<int> (next - first);
        s.start = e.bars[first].start;
        s.end = barEnd (e, next - 1);

        // The key of the analysed section it overlaps most.
        double best = 0.0;
        for (const auto& as : a.sections)
        {
            const double overlap = std::min (s.end, as.end) - std::max (s.start, as.start);
            if (overlap > best)
            {
                best = overlap;
                s.key = as.key;
            }
        }
        e.sections.push_back (s);

        // 8-bar phrases (16 in long sections), restarting at the section.
        const int length = s.lengthBars >= longSectionBars && s.lengthBars % longPhraseBars == 0 ? longPhraseBars : phraseBars;
        for (int bar = s.startBar; bar < s.startBar + s.lengthBars; bar += length)
            e.phrases.push_back ({ static_cast<int> (e.phrases.size()) + 1, bar,
                                   std::min (length, s.startBar + s.lengthBars - bar), static_cast<int> (i) });
    }
}

void applyScores (const TrackAnalysis& a, TrackAnalysis& e)
{
    e.descriptorsPerBar.clear();
    e.energyPerBar.clear();
    for (size_t i = 0; i < e.bars.size(); ++i)
    {
        const double middle = 0.5 * (e.bars[i].start + barEnd (e, i));
        const int from = analysedBarAt (a, middle);
        const auto index = static_cast<size_t> (std::max (from, 0));
        e.descriptorsPerBar.push_back (index < a.descriptorsPerBar.size() ? a.descriptorsPerBar[index] : ContentScores {});
        e.energyPerBar.push_back (index < a.energyPerBar.size() ? a.energyPerBar[index] : 0.0);
    }

    e.descriptorsPerSection.clear();
    for (const auto& s : e.sections)
    {
        ContentScores mean;
        int count = 0;
        for (int bar = s.startBar - 1; bar < s.startBar - 1 + s.lengthBars; ++bar)
        {
            const auto& d = e.descriptorsPerBar[static_cast<size_t> (bar)];
            mean.rhythm += d.rhythm;
            mean.bass += d.bass;
            mean.high += d.high;
            mean.melody += d.melody;
            ++count;
        }
        if (count > 0)
            mean = { mean.rhythm / count, mean.bass / count, mean.high / count, mean.melody / count };
        e.descriptorsPerSection.push_back (mean);
    }
}
} // namespace

std::vector<double> sectionStartTimes (const TrackAnalysis& analysis)
{
    std::vector<double> starts;
    for (const auto& s : analysis.sections)
        starts.push_back (s.start);
    return starts;
}

TrackAnalysis effectiveAnalysis (const TrackAnalysis& a, const TrackOverrides& o)
{
    if (o.isEmpty())
        return a;

    TrackAnalysis e = a;
    if (! o.grid.isEmpty())
        applyGrid (a, o.grid, e);
    applySections (a, o.sectionStarts ? *o.sectionStarts : sectionStartTimes (a), e);
    applyScores (a, e);
    return e;
}

namespace edits
{
    TrackOverrides nudge (const TrackOverrides& o, double seconds)
    {
        auto r = o;
        r.grid.offsetSeconds += seconds;
        return r;
    }

    TrackOverrides setBpm (const TrackOverrides& o, std::optional<double> bpm)
    {
        auto r = o;
        r.grid.bpm = bpm && *bpm > 0.0 ? bpm : std::nullopt;
        return r;
    }

    TrackOverrides scaleBpm (const TrackAnalysis& a, const TrackOverrides& o, double factor)
    {
        const double current = o.grid.bpm.value_or (a.bpm);
        return current > 0.0 ? setBpm (o, current * factor) : o;
    }

    TrackOverrides setDownbeat (const TrackAnalysis& a, const TrackOverrides& o, double time)
    {
        const auto e = effectiveAnalysis (a, o);
        if (e.beats.empty() || e.bars.empty())
            return o;
        const auto beat = static_cast<int> (nearestIndex (e.beats, time));
        auto r = o;
        // A bar starts every fourth beat from bars[0].firstBeat; move that phase onto `beat`.
        r.grid.downbeatShift = positiveMod (o.grid.downbeatShift + beat - e.bars.front().firstBeat, beatsPerBar);
        return r;
    }

    TrackOverrides splitSection (const TrackAnalysis& a, const TrackOverrides& o, double time)
    {
        const auto e = effectiveAnalysis (a, o);
        if (e.bars.empty())
            return o;
        std::vector<double> barStarts;
        for (const auto& b : e.bars)
            barStarts.push_back (b.start);
        auto starts = sectionStartTimes (e);
        starts.push_back (barStarts[nearestIndex (barStarts, time)]);
        std::sort (starts.begin(), starts.end());
        starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
        auto r = o;
        r.sectionStarts = starts;
        return r;
    }

    TrackOverrides mergeWithNext (const TrackAnalysis& a, const TrackOverrides& o, int index)
    {
        const auto e = effectiveAnalysis (a, o);
        auto starts = sectionStartTimes (e);
        const auto next = static_cast<size_t> (index) + 1;
        if (index < 0 || next >= starts.size())
            return o;
        starts.erase (starts.begin() + static_cast<long> (next));
        auto r = o;
        r.sectionStarts = starts;
        return r;
    }

    TrackOverrides moveSectionStart (const TrackAnalysis& a, const TrackOverrides& o, int index, double time)
    {
        const auto e = effectiveAnalysis (a, o);
        if (index <= 0 || static_cast<size_t> (index) >= e.sections.size())
            return o;
        std::vector<double> barStarts;
        for (const auto& b : e.bars)
            barStarts.push_back (b.start);

        // Between the bar after the previous section's start and the bar
        // before the next section's start.
        const int lowest = e.sections[static_cast<size_t> (index - 1)].startBar; // 1-based: the bar after
        const auto nextIndex = static_cast<size_t> (index) + 1;
        const int highest = (nextIndex < e.sections.size() ? e.sections[nextIndex].startBar - 1 : static_cast<int> (e.bars.size())) - 1;
        if (lowest > highest)
            return o; // both neighbours are a single bar already
        const int bar = std::clamp (static_cast<int> (nearestIndex (barStarts, time)), lowest, highest);

        auto starts = sectionStartTimes (e);
        starts[static_cast<size_t> (index)] = barStarts[static_cast<size_t> (bar)];
        auto r = o;
        r.sectionStarts = starts;
        return r;
    }

    TrackOverrides resetGrid (const TrackOverrides& o)
    {
        auto r = o;
        r.grid = {};
        return r;
    }

    TrackOverrides resetSections (const TrackOverrides& o)
    {
        auto r = o;
        r.sectionStarts.reset();
        return r;
    }
} // namespace edits

} // namespace djmix
