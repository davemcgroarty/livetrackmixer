#pragma once

// Every allocation in the app test program goes through a replaced operator
// new, so an allocation on a thread marked real-time (an audio callback) is
// counted. Sanitizers replace operator new themselves, so under them nothing
// is counted and allocationsAreCounted is false.

#if defined(__SANITIZE_ADDRESS__)
    #define DJMIX_COUNT_ALLOCATIONS 0
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define DJMIX_COUNT_ALLOCATIONS 0
    #endif
#endif
#ifndef DJMIX_COUNT_ALLOCATIONS
    #define DJMIX_COUNT_ALLOCATIONS 1
#endif

namespace djmix::test
{
constexpr bool allocationsAreCounted = DJMIX_COUNT_ALLOCATIONS != 0;

int realtimeAllocations();
void resetRealtimeAllocations();
} // namespace djmix::test
