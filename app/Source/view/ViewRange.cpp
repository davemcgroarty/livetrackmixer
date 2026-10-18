#include "ViewRange.h"

#include <algorithm>

namespace djmix
{

ViewRange::ViewRange (double duration, double minSpan)
    : durationSec (std::max (duration, 0.001)),
      minSpanSec (std::clamp (minSpan, 0.001, std::max (duration, 0.001)))
{
    showAll();
}

void ViewRange::showAll()
{
    startSec = 0.0;
    spanSec = durationSec;
}

void ViewRange::show (double start, double span)
{
    spanSec = std::clamp (span, minSpanSec, durationSec);
    startSec = std::clamp (start, 0.0, durationSec - spanSec);
}

void ViewRange::zoomAt (double x, double factor)
{
    if (factor <= 0.0)
        return;
    const double anchor = xToTime (x);
    const double newSpan = std::clamp (spanSec / factor, minSpanSec, durationSec);
    show (anchor - x / width * newSpan, newSpan);
}

} // namespace djmix
