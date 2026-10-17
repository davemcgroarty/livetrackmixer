#include "RealtimeGuard.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace djmix::rt
{

namespace
{
thread_local int realtimeDepth = 0;

void defaultHandler (const char* what, const char* file, int line)
{
    std::fprintf (stderr, "Real-time violation: %s at %s:%d\n", what, file, line);
    std::abort();
}

std::atomic<ViolationHandler> handler { &defaultHandler };
} // namespace

ScopedRealtimeThread::ScopedRealtimeThread() noexcept { ++realtimeDepth; }
ScopedRealtimeThread::~ScopedRealtimeThread() noexcept { --realtimeDepth; }

ScopedRealtimeExemption::ScopedRealtimeExemption() noexcept : savedDepth (realtimeDepth) { realtimeDepth = 0; }
ScopedRealtimeExemption::~ScopedRealtimeExemption() noexcept { realtimeDepth = savedDepth; }

bool isRealtimeThread() noexcept { return realtimeDepth > 0; }

ViolationHandler setViolationHandler (ViolationHandler newHandler) noexcept
{
    return handler.exchange (newHandler != nullptr ? newHandler : &defaultHandler);
}

void reportViolation (const char* what, const char* file, int line) noexcept
{
    handler.load() (what, file, line);
}

} // namespace djmix::rt
