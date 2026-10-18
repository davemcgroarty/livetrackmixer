#pragma once

namespace djmix
{

// The stretch of a track a view shows: `start` and `span` in seconds, mapped
// onto `width` pixels. Zoom stays between `minSpan` and the track's duration,
// and the view never scrolls past either end.
class ViewRange
{
public:
    ViewRange() = default;
    ViewRange (double duration, double minSpan);

    double start() const { return startSec; }
    double span() const { return spanSec; }
    double end() const { return startSec + spanSec; }
    double duration() const { return durationSec; }
    double minSpan() const { return minSpanSec; }

    void setWidth (double pixels) { width = pixels > 1.0 ? pixels : 1.0; }
    double pixelWidth() const { return width; }

    double timeToX (double seconds) const { return (seconds - startSec) / spanSec * width; }
    double xToTime (double x) const { return startSec + x / width * spanSec; }
    double pixelsPerSecond() const { return width / spanSec; }

    // Shows the whole track.
    void showAll();

    // Shows [start, start + span], clamped to the limits.
    void show (double start, double span);

    // Zooms by `factor` (above 1 zooms in) keeping the time under pixel `x` still.
    void zoomAt (double x, double factor);

    void scrollBy (double seconds) { show (startSec + seconds, spanSec); }

private:
    double durationSec = 1.0;
    double minSpanSec = 1.0;
    double startSec = 0.0;
    double spanSec = 1.0;
    double width = 1.0;
};

} // namespace djmix
