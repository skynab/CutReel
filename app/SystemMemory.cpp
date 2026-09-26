#include "SystemMemory.h"

#include <algorithm>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

namespace zaro::app {

std::size_t physicalMemoryBytes() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) == 0) {
        return 0;
    }
    return static_cast<std::size_t>(status.ullTotalPhys);
#elif defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t length = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) != 0) {
        return 0;
    }
    return static_cast<std::size_t>(bytes);
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || pageSize <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(pages) * static_cast<std::size_t>(pageSize);
#endif
}

std::size_t renderCacheBudgetBytes() noexcept {
    constexpr std::size_t kGiB = 1024U * 1024U * 1024U;
    const std::size_t physical = physicalMemoryBytes();
    if (physical == 0) {
        return kGiB;
    }
    // A third: the rest of the editor, the decoders' own buffers and whatever
    // else is open need the remainder, and a cache that pushes the machine
    // into swap makes playback worse than no cache at all.
    return std::clamp<std::size_t>(physical / 3, kGiB, 32 * kGiB);
}

}  // namespace zaro::app
