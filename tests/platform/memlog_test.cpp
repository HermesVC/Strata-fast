// tests/platform/memlog_test.cpp - the reporting arithmetic, checked against the specification, not against the code.
//
// gate_probe is reporting only: for the inputs the gate really passes it must answer exactly what
// platform::file_cache_keeps answers, and differ only where the double charge or a ceiling would change the answer.
// No GPU, no model, no OS counters.
#include <cstdint>
#include <cstdio>

#include "strata/platform/memory.hpp"
#include "strata/platform/memlog.hpp"

using namespace strata::platform;
using namespace strata::platform::memlog;

static int failed = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failed; } } while (0)

static uint64_t gib(double g) { return (uint64_t) (g * 1073741824.0); }

int main() {
    const uint64_t G = 1ull << 30;

    // Expected answers are written from the rule (room = avail - arena - margin; need = the share of the read, never
    // below the floor and never above the whole read; a ceiling that cannot cover what this process already holds
    // leaves no room), not by running the code.
    struct Case { const char* what; double avail, own, cap, arena, read, margin, hot, floor;
                  bool plain, no_double, under_cap; };
    const Case cases[] = {
        {"32 avail, 4 arena, 3 read, 4 margin: room 24, fits", 32, 0, 0, 4, 3, 4, 1.0, 0, true, true, true},
        {"10 avail, 4 own, 4 arena, 3 read: plain room 2, double charge 4", 10, 4, 0, 4, 3, 4, 1.0, 0,
         false, true, true},
        {"10 avail, 4 own, cap 20, 4 arena, 3 read: room under cap 8", 10, 4, 20, 4, 3, 4, 1.0, 0,
         false, true, true},
        {"a cap (3) below what is already held (4): no room, plain gate still fits", 10, 4, 3, 0, 1, 4, 1.0, 0,
         true, true, false},
        // Kirill's shape: a 24 GiB mirror already held, a 5 GiB ceiling, 8 GiB the cache would like to keep
        {"36 avail, 24 own, cap 5, read 8", 36, 24, 5, 0, 8, 4, 1.0, 0, true, true, false},
        {"36 avail, 24 own, cap 40, read 8", 36, 24, 40, 0, 8, 4, 1.0, 0, true, true, true},
        {"2 avail, 10 arena: the room saturates at 0, it is not negative", 2, 0, 0, 10, 1, 4, 1.0, 0,
         false, false, false},
        // #1194's Linux file tier: a twentieth of the reads, at least 1.5 GiB
        {"#1194: 6.4 avail, 0 arena, 13.6 read, margin 1, hot 0.05, floor 1.5", 6.4, 0, 0, 0, 13.6, 1, 0.05, 1.5,
         true, true, true},
        {"the same with a 2 GiB cap over 6 GiB held: the plain gate fits, the cap has nothing left", 6.4, 6, 2, 0, 13.6, 1, 0.05, 1.5,
         true, true, false},
        {"need clamped by the whole read (hot 2.0): need 10, room 16", 20, 0, 0, 0, 10, 4, 2.0, 0,
         true, true, true},
        {"floor above the read: the read is the cap, need 1", 10, 0, 0, 0, 1, 4, 1.0, 2, true, true, true},
        {"need 1.5 against a room of exactly 1.5: fits", 6.5, 0, 0, 1, 30, 4, 0.05, 1.5, true, true, true},
    };

    for (const Case& c : cases) {
        const GateProbe p = gate_probe(gib(c.avail), gib(c.own), gib(c.cap), gib(c.arena), gib(c.read),
                                       gib(c.margin), c.hot, gib(c.floor));
        const bool wrong = p.keep_plain != c.plain || p.keep_no_double != c.no_double || p.keep_under_cap != c.under_cap;
        std::printf("gate_probe %-62s plain=%s no_double=%s under_cap=%s%s\n", c.what,
                    p.keep_plain ? "yes" : "no ", p.keep_no_double ? "yes" : "no ", p.keep_under_cap ? "yes" : "no ",
                    wrong ? "   <-- WRONG" : "");
        CHECK(p.keep_plain == c.plain);
        CHECK(p.keep_no_double == c.no_double);
        CHECK(p.keep_under_cap == c.under_cap);
        // the plain answer must be the gate's own answer, not a copy of its arithmetic
        CHECK(p.keep_plain == file_cache_keeps(gib(c.avail), gib(c.arena), gib(c.read), gib(c.margin), c.hot,
                                               gib(c.floor)));
    }

    // The reported numbers, checked by hand: so a real run's line can be re-derived from the same rule
    const GateProbe p = gate_probe(gib(10), gib(4), gib(20), gib(4), gib(3), gib(4), 1.0, 0);
    CHECK(p.need == gib(3));
    CHECK(p.room_plain == gib(2));
    CHECK(p.room_no_double == gib(6));
    CHECK(p.room_under_cap == gib(8));   // 20 - 4 held - 4 arena - 4 margin

    // no wrap-around: a process that already holds nearly the whole range, with a small ceiling
    const GateProbe big = gate_probe(UINT64_MAX / 2, UINT64_MAX - 1, 10 * G, 4 * G, 1 * G, 4 * G, 1.0, 0);
    CHECK(big.room_under_cap == 0);          // 10 GiB cannot cover what is already held
    CHECK(big.room_no_double < UINT64_MAX);

    // The deciding ceiling's room, from the rule: room = min(the OS reading, cap - the private working set already
    // held - the caller's reserve), 0 when the ceiling is already full; `by_ceiling` only when the ceiling is strictly
    // the tighter side (a tie changed nothing).  The commit capacity is not part of the rule.  Fixtures only.
    const uint64_t M = 1ull << 20;
    auto run = [&](uint64_t avail, uint64_t priv, uint64_t reserve, uint64_t cap) {
        return ceiling_room(CeilingInput{ avail * M, priv * M, reserve * M, cap * M });
    };
    CeilingAnswer a = run(40960, 6144, 2048, 40960);    // 40 GiB cap, 6 GiB private, 2 GiB reserved: 32 GiB left
    CHECK(a.room == 32768 * M && a.by_ceiling);
    a = run(15360, 6144, 2048, 40960);                  // the machine is the tighter side
    CHECK(a.room == 15360 * M && !a.by_ceiling);
    a = run(15360, 6144, 2048, 0);                      // off: the reading, unchanged
    CHECK(a.room == 15360 * M && !a.by_ceiling);
    a = run(15360, 46080, 0, 40960);                    // the ceiling cannot cover what is held: 0, never negative
    CHECK(a.room == 0 && a.by_ceiling);
    a = run(10240, 30720, 0, 40960);                    // a tie: the machine's number, the ceiling changed nothing
    CHECK(a.room == 10240 * M && !a.by_ceiling);
    a = run(UINT64_MAX / M, UINT64_MAX / M, UINT64_MAX / M, 1);   // saturating: held past the range is "full"
    CHECK(a.room == 0 && a.by_ceiling);
    // Kirill's runs, read from the log lines.  At the decision the mapped pages were just trimmed, so the process
    // holds ~1 MiB private: a 38 GiB ceiling over 40757 MiB available leaves 38911 MiB - the ceiling decides, by 1.8
    // GiB, and the tier (24.8 GiB + 4 GiB headroom) still fits.  Run 3: 14111 MiB available, the machine decides.
    a = run(40757, 1, 0, 38912);
    CHECK(a.room == 38911 * M && a.by_ceiling);
    a = run(14111, 1, 0, 38912);
    CHECK(a.room == 14111 * M && !a.by_ceiling);

    // The environment numbers: unset and 0 are "off" without a complaint; anything that is not a nonnegative number is
    // "bad", never a silent off and never the number in front of the junk.
    uint64_t n = 7;
    CHECK(parse_number(nullptr, 1.0, n) == EnvNumber::unset && n == 0);
    CHECK(parse_number("", 1.0, n) == EnvNumber::unset);
    CHECK(parse_number("0", G, n) == EnvNumber::off && n == 0);
    CHECK(parse_number("38", G, n) == EnvNumber::ok && n == 38 * G);
    CHECK(parse_number("38.5", G, n) == EnvNumber::ok && n == (uint64_t) (38.5 * (double) G));
    CHECK(parse_number("38 ", G, n) == EnvNumber::ok && n == 38 * G);
    CHECK(parse_number("38x", G, n) == EnvNumber::bad && n == 0);
    CHECK(parse_number("abc", G, n) == EnvNumber::bad);
    CHECK(parse_number("-1", G, n) == EnvNumber::bad);
    CHECK(parse_number("nan", G, n) == EnvNumber::bad);
    CHECK(parse_number("inf", G, n) == EnvNumber::bad);
    CHECK(parse_number("1e30", G, n) == EnvNumber::bad);
    // the simpler gate helper agrees with the fixture rule when nothing else is charged
    CHECK(cap_room(15566 * M, 25394 * M, 40960 * M) == 15566 * M);
    CHECK(cap_room(41984 * M, 41984 * M, 40960 * M) == 0);
    CHECK(cap_room(20480 * M, 38912 * M, 40960 * M) == 2048 * M);
    CHECK(gate_probe(15566 * M, 25394 * M, 40960 * M, 0, 0, 0, 1.0, 0).room_under_cap == cap_room(15566 * M, 25394 * M, 40960 * M));

    if (failed == 0) std::printf("memlog_test: ok\n");
    return failed ? 1 : 0;
}
