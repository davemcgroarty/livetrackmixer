#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "engine/RealtimeGuard.h"

namespace
{
int violations = 0;

void countViolation (const char*, const char*, int) { ++violations; }

void allocatingFunction()
{
    DJMIX_ASSERT_NOT_REALTIME ("allocation");
}
} // namespace

TEST_CASE ("Real-time guard reports violations only on marked threads", "[realtime]")
{
    const auto previous = djmix::rt::setViolationHandler (&countViolation);
    violations = 0;

    allocatingFunction();
    CHECK (violations == 0);

    {
        djmix::rt::ScopedRealtimeThread audioThread;
        CHECK (djmix::rt::isRealtimeThread());

        allocatingFunction();
        CHECK (violations == 1);

        {
            djmix::rt::ScopedRealtimeExemption exempt;
            allocatingFunction();
            CHECK (violations == 1);
        }

        allocatingFunction();
        CHECK (violations == 2);

        // The mark is per thread.
        std::thread other ([] { allocatingFunction(); });
        other.join();
        CHECK (violations == 2);
    }

    CHECK_FALSE (djmix::rt::isRealtimeThread());
    allocatingFunction();
    CHECK (violations == 2);

    djmix::rt::setViolationHandler (previous);
}
