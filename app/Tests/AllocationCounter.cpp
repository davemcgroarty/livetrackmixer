#include "AllocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

#include "engine/RealtimeGuard.h"

namespace
{
std::atomic<int> counted { 0 };
}

int djmix::test::realtimeAllocations() { return counted.load(); }
void djmix::test::resetRealtimeAllocations() { counted = 0; }

#if DJMIX_COUNT_ALLOCATIONS
namespace
{
void* allocate (std::size_t size)
{
    if (djmix::rt::isRealtimeThread())
        ++counted;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#endif
