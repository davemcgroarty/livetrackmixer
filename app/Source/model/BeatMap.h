#pragma once

#include <vector>

namespace djmix
{

// Fractional beat positions on a track's beat grid (its effective beats, in
// seconds). Beat i is beats[i]; between two beats time moves evenly; before
// the first beat and after the last, the grid carries on at the first or last
// beat period. A grid of fewer than two beats is taken as 120 BPM.
double timeAtBeat (const std::vector<double>& beats, double beat);

// The inverse: the fractional beat at a time.
double beatAtTime (const std::vector<double>& beats, double seconds);

} // namespace djmix
