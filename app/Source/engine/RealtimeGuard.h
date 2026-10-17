#pragma once

// Debug checks for audio-thread rules: the audio thread never allocates, locks a
// mutex, touches disk or logs.
//
// Mark the audio callback with a ScopedRealtimeThread, and put
// DJMIX_ASSERT_NOT_REALTIME("what") in any function that allocates, locks, logs
// or does I/O. In builds with DJMIX_REALTIME_CHECKS=1 (the default for debug
// builds) a call on a marked thread reports a violation; otherwise the macro
// compiles to nothing.

#ifndef DJMIX_REALTIME_CHECKS
    #ifdef NDEBUG
        #define DJMIX_REALTIME_CHECKS 0
    #else
        #define DJMIX_REALTIME_CHECKS 1
    #endif
#endif

namespace djmix::rt
{

using ViolationHandler = void (*) (const char* what, const char* file, int line);

// Marks the current thread as real-time for the lifetime of the object. Nests.
class ScopedRealtimeThread
{
public:
    ScopedRealtimeThread() noexcept;
    ~ScopedRealtimeThread() noexcept;

    ScopedRealtimeThread (const ScopedRealtimeThread&) = delete;
    ScopedRealtimeThread& operator= (const ScopedRealtimeThread&) = delete;
};

// Temporarily lifts the check, for code that is known to be safe (for example a
// pre-allocated container that the checker cannot see into).
class ScopedRealtimeExemption
{
public:
    ScopedRealtimeExemption() noexcept;
    ~ScopedRealtimeExemption() noexcept;

    ScopedRealtimeExemption (const ScopedRealtimeExemption&) = delete;
    ScopedRealtimeExemption& operator= (const ScopedRealtimeExemption&) = delete;

private:
    int savedDepth;
};

bool isRealtimeThread() noexcept;

// The default handler prints the violation to stderr and aborts.
// Returns the previous handler.
ViolationHandler setViolationHandler (ViolationHandler handler) noexcept;

void reportViolation (const char* what, const char* file, int line) noexcept;

} // namespace djmix::rt

#if DJMIX_REALTIME_CHECKS
    #define DJMIX_ASSERT_NOT_REALTIME(what)                                     \
        do                                                                      \
        {                                                                       \
            if (::djmix::rt::isRealtimeThread())                                \
                ::djmix::rt::reportViolation ((what), __FILE__, __LINE__);      \
        } while (false)
#else
    #define DJMIX_ASSERT_NOT_REALTIME(what) do {} while (false)
#endif
