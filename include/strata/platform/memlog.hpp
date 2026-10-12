// include/strata/platform/memlog.hpp - memory reporting for the RAM-growth investigation, and the operator's ceiling.
//
// The reporting decides nothing.  It says what a memory gate read, what this process really holds, and what the same
// gate would have answered under a ceiling, so the two can be compared on a real run.
//
// STRATA_MEMLOG: unset or 0 = off (the default path prints nothing and takes no reading); 1 = one line per request
// phase and per gate decision.
// STRATA_MEMLOG_CAP_MIB: the ceiling the reporting compares against (0 = none).  Reporting only: it does not limit
// an allocation, it only says what the gate would have said with that ceiling.
// STRATA_RAM_CAP_GIB: the ceiling that decides (GiB, unset or 0 = off).  It is this process's RAM, in two halves:
//   - a gate that asks what is left gets min(what the OS says is left, the ceiling minus the private working set this
//     process already holds);
//   - at a request end, a process whose whole working set (private + the mapped expert pages) is over the ceiling
//     gives the mapped pages back (Windows: they move to the standby list, still cached, counted as available).
// The commit limit is not charged to the ceiling: the machine's commit capacity already bounds every gate, and a RAM
// number charged against commit clamps the tier while the RAM is still free.  With the variable unset the decision
// path is byte-identical.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace strata::platform {
namespace memlog {

/// 0 = off, 1 = the lines.  Read once, at the first use.
int level();
inline bool on() { return level() >= 1; }

/// What an environment number said: not there, there and 0 (off), a usable value, or text that is not a nonnegative
/// number - which must not silently read as "off", or as the number in front of the junk ("38x" is not 38).
enum class EnvNumber { unset, off, ok, bad };

/// `text` is the variable's value (nullptr when unset), `scale` the unit in bytes.  `out` is the bytes when `ok`,
/// otherwise 0.
inline EnvNumber parse_number(const char* text, double scale, uint64_t& out) {
    out = 0;
    if (text == nullptr || text[0] == '\0') return EnvNumber::unset;
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text) return EnvNumber::bad;
    while (*end == ' ') ++end;
    if (*end != '\0') return EnvNumber::bad;
    if (!(v >= 0.0) || !std::isfinite(v)) return EnvNumber::bad;   // negative, NaN, inf
    const double bytes = v * scale;
    if (!(bytes < 1.8e19)) return EnvNumber::bad;                  // past uint64_t
    out = (uint64_t) bytes;
    return out == 0 ? EnvNumber::off : EnvNumber::ok;
}

/// Reporting ceiling in bytes, 0 when none.
uint64_t cap_bytes();

/// The deciding ceiling in bytes (STRATA_RAM_CAP_GIB), 0 when off.
uint64_t ram_cap_bytes();
inline bool deciding() { return ram_cap_bytes() != 0; }
/// What a probe line prints as `cap`: the deciding ceiling when there is one, otherwise the reporting ceiling.
inline uint64_t probe_cap() { const uint64_t d = ram_cap_bytes(); return d != 0 ? d : cap_bytes(); }

inline uint64_t sub_or_zero(uint64_t a, uint64_t b) { return a > b ? a - b : 0; }

/// The room a ceiling leaves for one decision: min(avail, own's share of the ceiling), 0 when the ceiling is already
/// full.  The OS reading and the ceiling measure the same room from two sides, so the tighter of the two is the room.
inline uint64_t cap_room(uint64_t avail, uint64_t own, uint64_t cap) {
    if (cap == 0) return avail;
    const uint64_t left = sub_or_zero(cap, own);
    return avail < left ? avail : left;
}

/// What a ceiling decision is made from, as numbers only, so a test drives it with fixtures instead of the OS
/// counters.  `own_private` is the private working set this process already holds; `reserve` is what the caller
/// keeps for memory it will take outside this decision (0 when it has nothing to add).
struct CeilingInput {
    uint64_t avail = 0, own_private = 0, reserve = 0, cap = 0;
};

/// `room` is what the gate may take; `by_ceiling` is true when the ceiling, not the machine's reading, set it (a tie
/// goes to the machine: the ceiling changed nothing).
struct CeilingAnswer {
    uint64_t room = 0;
    bool by_ceiling = false;
};

inline CeilingAnswer ceiling_room(const CeilingInput& in) {
    CeilingAnswer a;
    a.room = in.avail;
    if (in.cap == 0) return a;
    const uint64_t held = in.own_private > UINT64_MAX - in.reserve ? UINT64_MAX : in.own_private + in.reserve;
    const uint64_t left = sub_or_zero(in.cap, held);
    if (left < in.avail) {
        a.room = left;
        a.by_ceiling = true;
    }
    return a;
}

/// One line for a ceiling decision, printed from the same numbers a test checks.  Nothing when reporting is off.
void ceiling_line(const char* stage, const CeilingInput& in, const CeilingAnswer& a);

/// One reading: this process and what the OS says is left.  A zero field is a field that could not be read, not a
/// measured zero; `valid` is false when nothing was readable.
struct Reading {
    bool valid = false;
    uint64_t ws = 0;            ///< this process's working set (Windows WorkingSetSize, Linux resident)
    uint64_t private_ws = 0;    ///< the part of it that is private (Windows EX2; Linux resident - shared)
    uint64_t shared_ws = 0;     ///< the part that is a file mapping the OS can hand back.  A mapping this process
                                ///  wrote itself counts as private, so this is a classification, not a proof that the
                                ///  bytes are reclaimable.
    uint64_t commit = 0;        ///< what this process is charged against RAM + page file (Windows; 0 elsewhere)
    uint64_t avail = 0;         ///< what the memory gates read as available
    uint64_t faults = 0;        ///< cumulative page faults of this process
    uint64_t major_faults = 0;  ///< the ones that came from storage (Linux only; Windows has one fault counter and
                                ///  cannot separate them, so this stays 0 there)
};

Reading read();

/// The room the deciding ceiling leaves a gate that only has the OS reading `avail`: the rule of `ceiling_room`, with
/// this process's private working set read now.  Without a ceiling it reads nothing and returns `avail`.
inline uint64_t room_for(uint64_t avail, bool* by_ceiling = nullptr) {
    if (by_ceiling != nullptr) *by_ceiling = false;
    const uint64_t cap = ram_cap_bytes();
    if (cap == 0) return avail;
    const Reading own = read();
    const CeilingInput in{ avail, own.private_ws, 0, cap };
    const CeilingAnswer a = ceiling_room(in);
    ceiling_line("ceiling", in, a);
    if (by_ceiling != nullptr) *by_ceiling = a.by_ceiling;
    return a.room;
}

/// The request end, the ceiling's runtime half.  True when this process's whole working set is over the deciding
/// ceiling, so the caller gives the mapped expert pages back; the answer also arms `over()` until the next request
/// end, so the per-expert release trims what lands in VRAM in between.  Without a ceiling it reads nothing and
/// answers false.
bool mark_over();
bool over();
/// What the per-expert release advised since the last request end (bytes advised, not RAM measured).
void add_trimmed(uint64_t bytes);
/// After the request-end trim: `advised` bytes of mapping were given back in `ms`.  Says so once on stderr, and one
/// line per trim when reporting is on.
void after_trim(uint64_t advised, double ms);

/// One line to stderr: `after`, with what changed since `before`.  Does nothing when reporting is off.
/// `tok_per_s` is the rate the phase measured, -1 when the phase had no tokens to rate.
void phase(const char* tag, const Reading& before, const Reading& after, double ms, double tok_per_s = -1.0);

/// The file-cache gate's numbers as three answers.  `keep_plain` is the decision the gate really makes, so the
/// comparison never changes it.
struct GateProbe {
    uint64_t avail = 0, own = 0, cap = 0, arena = 0, read_bytes = 0, margin = 0, need = 0, floor = 0;
    uint64_t room_plain = 0, room_no_double = 0, room_under_cap = 0;
    bool keep_plain = false, keep_no_double = false, keep_under_cap = false;
};

/// `own` is what this process already holds; `arena` is what the gate charges again, so the double charge is
/// min(arena, own).  `cap` 0 = no ceiling.  The arithmetic follows platform::file_cache_keeps and is kept here, not
/// inside that function, so a test can compare the two without changing the decision.
inline GateProbe gate_probe(uint64_t avail, uint64_t own, uint64_t cap, uint64_t arena, uint64_t read_bytes,
                            uint64_t margin, double hot_fraction, uint64_t floor) {
    GateProbe p;
    p.avail = avail; p.own = own; p.cap = cap; p.arena = arena; p.read_bytes = read_bytes;
    p.margin = margin; p.floor = floor;

    // The gate's need: the share of the read bytes, never below the floor and never above the whole read.
    uint64_t need = (uint64_t) ((double) read_bytes * hot_fraction);
    if (need < floor) need = floor;
    if (need > read_bytes) need = read_bytes;
    p.need = need;

    // Saturating, so a transient oversized read cannot make the room look huge.
    const uint64_t over = arena > UINT64_MAX - margin ? UINT64_MAX : arena + margin;
    p.room_plain = avail > over ? avail - over : 0;
    const uint64_t double_charge = arena < own ? arena : own;
    p.room_no_double = p.room_plain > UINT64_MAX - double_charge ? UINT64_MAX : p.room_plain + double_charge;
    // Under a ceiling the ceiling is the whole room: this process's own bytes, the arena and the margin come out of it,
    // and the file cache can only keep what is left.
    if (cap == 0) p.room_under_cap = p.room_no_double;
    else {
        const uint64_t held = own > UINT64_MAX - over ? UINT64_MAX : own + over;
        p.room_under_cap = cap > held ? cap - held : 0;
    }

    p.keep_plain = p.room_plain >= need;
    p.keep_no_double = p.room_no_double >= need;
    p.keep_under_cap = p.room_under_cap >= need;
    return p;
}

void gate(const char* tag, const GateProbe& p, const char* why);

}  // namespace memlog
}  // namespace strata::platform
