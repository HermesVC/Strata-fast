#include "strata/core/conversation_memory.hpp"

#include "strata/platform/memlog.hpp"

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace strata::core {

std::optional<uint64_t> conversation_mem_available(std::istream& meminfo) {
    std::optional<uint64_t> result;
    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream fields(line);
        std::string key, value, unit, extra;
        if (!(fields >> key) || key != "MemAvailable:") continue;
        if (result || !(fields >> value >> unit) || unit != "kB" || (fields >> extra)) return {};
        uint64_t kb = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), kb);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            kb > std::numeric_limits<uint64_t>::max() / 1024) return {};
        result = kb * 1024;
    }
    if (meminfo.bad() || (meminfo.fail() && !meminfo.eof())) return {};
    return result;
}

std::optional<uint64_t> conversation_available_memory() {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (!GlobalMemoryStatusEx(&status)) return {};
    // STRATA_RAM_CAP_GIB: the same room as every other memory gate; without the ceiling the reading is unchanged
    return strata::platform::memlog::room_for(status.ullAvailPhys);
#elif defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo) return {};
    const std::optional<uint64_t> avail = conversation_mem_available(meminfo);
    if (!avail) return {};
    return strata::platform::memlog::room_for(*avail);
#else
    return {};
#endif
}

} // namespace strata::core
