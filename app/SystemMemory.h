// How much memory this machine has, for sizing caches to it.
#pragma once

#include <cstddef>

namespace zaro::app {

/// Physical memory in bytes, or 0 when the platform will not say.
[[nodiscard]] std::size_t physicalMemoryBytes() noexcept;

/// The render cache's budget on this machine.
///
/// A share of physical memory rather than a fixed figure: pre-rendered frames
/// are 16 MB each at 1080p, so a fixed gigabyte holds about two seconds, and
/// the length of timeline that can be played back smoothly should grow with
/// the machine rather than be pinned to the smallest one anybody might have.
[[nodiscard]] std::size_t renderCacheBudgetBytes() noexcept;

}  // namespace zaro::app
