#include "core/certificates.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main()
{
    using namespace tz::certificates;
    const auto parse = [](const char* stamp) {
        return bundleTime(std::string("## Certificate data from Mozilla as of: ") + stamp, "Jan  1 1970");
    };
    assert(bundleTime({}, "Jan  1 1970") == 0);
    assert(bundleTime("not a header", "Jan  2 1970") == 86400);
    assert(parse("Thu Jan  1 00:00:00 1970 GMT") == 0);
    assert(parse("Thu Jan  1 01:02:03 1970 GMT") == 3723);
    assert(parse("Thu Feb 29 00:00:00 2024 GMT") - parse("Wed Feb 28 00:00:00 2024 GMT") == 86400);
    assert(parse("Tue Feb 29 00:00:00 2023 GMT") == 0);
    assert(parse("Thu Nov  5 04:12:01 2026 GMT") == 1793851921);
    assert(parse("Thu Nov  5 25:12:01 2026 GMT") == 0);
    assert(parse("Thu Nov  5 04:12:01 2026 UTC") == 0);
    assert(parse("invalid") == 0);
    assert(bundleTime({}, "invalid") == -1);
    assert(!outdated(-1, 2000000000));
    assert(!outdated(100, 99));
    assert(!outdated(100, 100 + kLifetimeSeconds));
    assert(outdated(100, 101 + kLifetimeSeconds));
    std::puts("certificates: timestamp, fallback, invalid dates, leap day, clock and 1096-day boundary passed");
}
