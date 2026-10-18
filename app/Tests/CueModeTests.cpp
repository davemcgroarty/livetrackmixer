#include <catch2/catch_test_macros.hpp>

#include "engine/CueMode.h"

using djmix::CueMode;

TEST_CASE ("The cue mode button steps through once, loop and continue", "[audio][cue]")
{
    CHECK (djmix::nextCueMode (CueMode::once) == CueMode::loop);
    CHECK (djmix::nextCueMode (CueMode::loop) == CueMode::continueTrack);
    CHECK (djmix::nextCueMode (CueMode::continueTrack) == CueMode::once);

    for (const auto mode : { CueMode::once, CueMode::loop, CueMode::continueTrack })
        CHECK (djmix::cueModeFromKey (djmix::cueModeKey (mode)) == mode);
    CHECK (djmix::cueModeFromKey ("") == CueMode::once);
    CHECK (djmix::cueModeFromKey ("sideways") == CueMode::once);
    CHECK (djmix::cueModeLabel (CueMode::continueTrack) == "Continue");
}
