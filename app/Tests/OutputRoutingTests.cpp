#include <catch2/catch_test_macros.hpp>

#include "engine/OutputRouting.h"

using djmix::OutputRouting;
using djmix::ResolvedRouting;
using djmix::resolveRouting;

TEST_CASE ("Output routing defaults to separate pairs on four or more outputs", "[audio][routing]")
{
    CHECK (OutputRouting::defaultFor (2).splitCue);
    CHECK_FALSE (OutputRouting::defaultFor (4).splitCue);
    CHECK (OutputRouting::defaultFor (4).masterFirst == 0);
    CHECK (OutputRouting::defaultFor (4).cueFirst == 2);
    CHECK (OutputRouting::defaultFor (0).splitCue);
}

TEST_CASE ("Output routing resolves to the callback's active channels", "[audio][routing]")
{
    OutputRouting routing; // Master 1-2, Cue 3-4

    // All four outputs on: callback channels are device channels.
    auto r = resolveRouting (routing, 0b1111);
    CHECK (r == ResolvedRouting { 0, 1, 2, 3, false });

    // Only 1-2 and 5-6 on, Cue on 5-6: the callback sees 4 channels.
    routing.cueFirst = 4;
    r = resolveRouting (routing, 0b110011);
    CHECK (r == ResolvedRouting { 0, 1, 2, 3, false });

    // Master on 3-4, Cue on 1-2.
    routing.masterFirst = 2;
    routing.cueFirst = 0;
    r = resolveRouting (routing, 0b1111);
    CHECK (r == ResolvedRouting { 2, 3, 0, 1, false });
}

TEST_CASE ("Cue falls back to split cue when its outputs are missing", "[audio][routing]")
{
    OutputRouting routing; // Cue wants 3-4

    // A stereo device: Cue mono on the left, Master mono on the right.
    auto r = resolveRouting (routing, 0b11);
    CHECK (r.split);
    CHECK (r.cueLeft == 0);
    CHECK (r.masterRight == 1);
    CHECK (r.cueRight == -1);
    CHECK (r.masterLeft == -1);

    // Chosen explicitly on a bigger device.
    routing.splitCue = true;
    r = resolveRouting (routing, 0b1111);
    CHECK (r.split);
    CHECK (r.cueLeft == 0);
    CHECK (r.masterRight == 1);

    // Cue and Master on the same pair would put both on one pair: split instead.
    routing = {};
    routing.cueFirst = 0;
    CHECK (resolveRouting (routing, 0b1111).split);

    // The Master pair is off too: the first active pair carries both.
    routing = {};
    routing.masterFirst = 6;
    r = resolveRouting (routing, 0b11);
    CHECK (r.split);
    CHECK (r.cueLeft == 0);
    CHECK (r.masterRight == 1);

    // Nothing active: nowhere to play.
    r = resolveRouting (OutputRouting {}, 0);
    CHECK (r.cueLeft == -1);
    CHECK (r.cueRight == -1);
}

TEST_CASE ("Output routing names the channels it needs", "[audio][routing]")
{
    OutputRouting routing;
    CHECK (djmix::channelsNeeded (routing) == 0b1111);
    routing.splitCue = true;
    CHECK (djmix::channelsNeeded (routing) == 0b11);
    routing = {};
    routing.masterFirst = 4;
    routing.cueFirst = 8;
    CHECK (djmix::channelsNeeded (routing) == 0b1100110000);

    CHECK (djmix::stereoPairs (5) == std::vector<int> { 0, 2 });
    CHECK (djmix::stereoPairs (1).empty());
    CHECK (djmix::pairName (2) == "3-4");
}
