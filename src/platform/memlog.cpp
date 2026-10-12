// src/platform/memlog.cpp - see include/strata/platform/memlog.hpp.
//
// The reporting decides nothing; the ceiling (STRATA_RAM_CAP_GIB) decides only when it is set, so a run with both
// variables unset is the run before it.
#include "strata/platform/memlog.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace strata::platform {
namespace memlog {

namespace {

const char* yesno(bool v) { return v ? "yes" : "no"; }

// A count can go down (a trimmed working set); an unsigned subtraction would print the wrap as growth.
long long d_signed(long long now, long long was) { return now - was; }
long long d_mib(uint64_t now, uint64_t was) { return now >= was ? (long long) ((now - was) >> 20) : -(long long) ((was - now) >> 20); }

unsigned long long mi(uint64_t bytes) { return (unsigned long long) (bytes >> 20); }

#if !defined(_WIN32)
// /proc/meminfo line parsing is kept to one field each: a field that is not there stays 0.
uint64_t meminfo_kb(const char* key) {
    std::ifstream info("/proc/meminfo");
    if (!info) return 0;
    std::string line;
    while (std::getline(info, line)) {
        std::istringstream fields(line);
        std::string k, unit;
        uint64_t value = 0;
        if (fields >> k >> value >> unit && k == key && unit == "kB") return value * 1024;
    }
    return 0;
}
#endif

}  // namespace

int level() {
    static const int v = [] {
        const char* e = std::getenv("STRATA_MEMLOG");
        if (e == nullptr || e[0] == '\0') return 0;
        int n = std::atoi(e);
        return n > 0 ? 1 : 0;
    }();
    return v;
}

uint64_t cap_bytes() {
    static const uint64_t v = [] {
        uint64_t n = 0;
        const char* e = std::getenv("STRATA_MEMLOG_CAP_MIB");
        if (parse_number(e, 1048576.0, n) == EnvNumber::bad)
            std::fprintf(stderr, "memlog: STRATA_MEMLOG_CAP_MIB=%s is not a nonnegative number of MiB, the reporting "
                                 "ceiling stays off\n", e);
        return n;
    }();
    return v;
}

uint64_t ram_cap_bytes() {
    static const uint64_t v = [] {
        uint64_t n = 0;
        const char* e = std::getenv("STRATA_RAM_CAP_GIB");
        const EnvNumber r = parse_number(e, 1073741824.0, n);
        if (r == EnvNumber::bad)
            std::fprintf(stderr, "strata: STRATA_RAM_CAP_GIB=%s is not a nonnegative number of GiB, the RAM ceiling "
                                 "stays off\n", e);
        else if (r == EnvNumber::ok)
            std::fprintf(stderr, "strata: STRATA_RAM_CAP_GIB=%.2f: this process's RAM ceiling decides the memory gates "
                                 "and gives back mapped expert pages at a request end when the process is over it\n",
                         (double) n / 1073741824.0);
        return n;
    }();
    return v;
}

namespace {
// Set at a request end on the serving thread, read per expert by the release path (other threads included).
std::atomic<bool> over_flag{false};
std::atomic<uint64_t> trimmed{0};
}  // namespace

void add_trimmed(uint64_t bytes) { trimmed.fetch_add(bytes, std::memory_order_relaxed); }
bool over() { return over_flag.load(std::memory_order_relaxed); }

bool mark_over() {
    const uint64_t cap = ram_cap_bytes();
    if (cap == 0) return false;
    const Reading own = read();
    const bool o = own.valid && own.ws > cap;
    over_flag.store(o, std::memory_order_relaxed);
    const uint64_t released = trimmed.exchange(0, std::memory_order_relaxed);
    if (o && own.private_ws > cap) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true))
            std::fprintf(stderr, "strata: WARNING: STRATA_RAM_CAP_GIB: this process's private memory alone (%.1f GiB) "
                                 "is over the %.1f GiB ceiling; giving back mapped pages cannot hold it there. The "
                                 "resident tier was sized at start-up: restart with the ceiling set to size it under "
                                 "the ceiling\n", (double) own.private_ws / 1073741824.0, (double) cap / 1073741824.0);
    }
    if (on())
        std::fprintf(stderr, "memlog request_end: ws=%llu private=%llu shared=%llu cap=%llu | the process holds %s the "
                             "ceiling | released by experts since the last request end %llu MiB (advised)\n",
                     (unsigned long long) (own.ws >> 20), (unsigned long long) (own.private_ws >> 20),
                     (unsigned long long) (own.shared_ws >> 20), (unsigned long long) (cap >> 20),
                     o ? "more than" : "no more than", (unsigned long long) (released >> 20));
    return o;
}

void after_trim(uint64_t advised, double ms) {
    if (advised == 0) {
        static std::atomic<bool> said_none{false};
        if (!said_none.exchange(true))
            std::fprintf(stderr, "strata: STRATA_RAM_CAP_GIB: the process was over its RAM ceiling at a request end, "
                                 "and there is no mapped view it can give back here (Windows only, and not with the "
                                 "view closed); the memory gates still refuse new allocations over it\n");
        return;
    }
    static std::atomic<bool> said{false};
    const bool first = !said.exchange(true);
    if (!first && !on()) return;
    const Reading now = read();
    if (first)
        std::fprintf(stderr, "strata: STRATA_RAM_CAP_GIB: the process was over its %.1f GiB RAM ceiling at a request "
                             "end: the mapped expert pages are given back at each request end while it is (they stay "
                             "in the OS file cache); working set now %.1f GiB\n",
                     (double) ram_cap_bytes() / 1073741824.0, (double) now.ws / 1073741824.0);
    if (on())
        std::fprintf(stderr, "memlog ceiling_trim: advised=%llu MiB in %.0f ms | ws=%llu private=%llu shared=%llu\n",
                     (unsigned long long) (advised >> 20), ms, (unsigned long long) (now.ws >> 20),
                     (unsigned long long) (now.private_ws >> 20), (unsigned long long) (now.shared_ws >> 20));
}

void ceiling_line(const char* stage, const CeilingInput& in, const CeilingAnswer& a) {
    if (!on()) return;
    std::fprintf(stderr, "memlog %s: avail=%llu private=%llu reserve=%llu cap=%llu | room=%llu | %s decides\n",
                 stage, (unsigned long long) (in.avail >> 20), (unsigned long long) (in.own_private >> 20),
                 (unsigned long long) (in.reserve >> 20), (unsigned long long) (in.cap >> 20),
                 (unsigned long long) (a.room >> 20), a.by_ceiling ? "the ceiling" : "the machine");
}

Reading read() {
    Reading r;
#if defined(_WIN32)
    // PROCESS_MEMORY_COUNTERS_EX2 is what gives the private share of the working set; it is accepted by
    // K32GetProcessMemoryInfo from Windows 8 on.  If it is refused, the plain counters still give the working set,
    // so the line stays readable rather than empty.
    PROCESS_MEMORY_COUNTERS_EX2 ex2{};
    bool got = K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&ex2, (DWORD) sizeof ex2);
    if (got) {
        r.ws = (uint64_t) ex2.WorkingSetSize;
        r.private_ws = (uint64_t) ex2.PrivateWorkingSetSize;
        r.commit = (uint64_t) ex2.PagefileUsage;
        r.faults = (uint64_t) ex2.PageFaultCount;
    } else {
        PROCESS_MEMORY_COUNTERS pmc{};
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return r;
        r.ws = (uint64_t) pmc.WorkingSetSize;
        r.commit = (uint64_t) pmc.PagefileUsage;
        r.faults = (uint64_t) pmc.PageFaultCount;
    }
    if (r.private_ws > r.ws) r.private_ws = r.ws;   // never report a negative share
    r.shared_ws = r.ws - r.private_ws;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) r.avail = (uint64_t) ms.ullAvailPhys;
    // Windows has one fault counter (PageFaultCount); the share that came from storage cannot be separated, so it is
    // left unreported instead of repeating the total under another name.
    r.valid = true;
#else
    // statm: size resident shared text lib data dirty.  resident is the working set, shared the file mappings.
    std::ifstream statm("/proc/self/statm");
    if (statm) {
        unsigned long long size = 0, resident = 0, shared = 0;
        if (statm >> size >> resident >> shared) {
            (void) size;   // the mapping size is not the working set; resident is
            const uint64_t page = (uint64_t) sysconf(_SC_PAGESIZE);
            r.ws = resident * page;
            r.shared_ws = shared * page;
            r.private_ws = r.shared_ws <= r.ws ? r.ws - r.shared_ws : 0;
            r.valid = true;
        }
    }
    if (std::FILE* f = std::fopen("/proc/self/stat", "r")) {
        char buf[1024];
        const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        std::fclose(f);
        if (const char* p = std::strrchr(buf, ')')) {   // fields after the command: ... minflt cminflt majflt
            unsigned long long minflt = 0, cminflt = 0, majflt = 0;
            if (std::sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %llu %llu %llu", &minflt, &cminflt, &majflt) == 3) {
                r.faults = minflt + cminflt;
                r.major_faults = majflt;
                r.valid = true;
            }
        }
    }
    r.avail = meminfo_kb("MemAvailable:");
    if (r.avail) r.valid = true;
#endif
    return r;
}

void phase(const char* tag, const Reading& before, const Reading& after, double ms, double tok_per_s) {
    if (!on() || !after.valid) return;
    char rate[16] = "";
    if (tok_per_s >= 0.0) std::snprintf(rate, sizeof rate, " tok/s=%.1f", tok_per_s);
    std::fprintf(stderr, "memlog %s: ws=%llu (%+lld) private=%llu (%+lld) shared=%llu (%+lld) commit=%llu (%+lld) "
                         "avail=%llu (%+lld) faults=%llu (+%lld) major=%llu (+%lld) ms=%.0f%s\n",
                 tag, mi(after.ws), d_mib(after.ws, before.ws), mi(after.private_ws), d_mib(after.private_ws, before.private_ws),
                 mi(after.shared_ws), d_mib(after.shared_ws, before.shared_ws), mi(after.commit), d_mib(after.commit, before.commit),
                 mi(after.avail), d_mib(after.avail, before.avail),
                 (unsigned long long) after.faults, d_signed((long long) after.faults, (long long) before.faults),
                 (unsigned long long) after.major_faults, d_signed((long long) after.major_faults, (long long) before.major_faults),
                 ms, rate);
}

void gate(const char* tag, const GateProbe& p, const char* why) {
    if (!on()) return;
    std::fprintf(stderr, "memlog %s: avail=%llu own=%llu cap=%llu arena=%llu read=%llu margin=%llu floor=%llu need=%llu | "
                         "room plain=%llu no_double=%llu under_cap=%llu | keeps plain=%s no_double=%s under_cap=%s | %s\n",
                 tag, mi(p.avail), mi(p.own), mi(p.cap), mi(p.arena), mi(p.read_bytes), mi(p.margin), mi(p.floor),
                 mi(p.need), mi(p.room_plain), mi(p.room_no_double), mi(p.room_under_cap),
                 yesno(p.keep_plain), yesno(p.keep_no_double), yesno(p.keep_under_cap), why);
}

}  // namespace memlog
}  // namespace strata::platform
