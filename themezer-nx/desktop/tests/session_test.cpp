#include "domain/session.hpp"

#include <cassert>
#include <cstdio>

int main()
{
    using namespace tz::session;
    Snapshot sample;
    sample.report.version = "4.1.2";
    sample.report.launch = "application";
    sample.report.memoryUsed = 123;
    sample.report.memoryAvailable = 456;
    sample.report.log = {"01:02:03.004 I started", "01:02:04.000 W last line"};
    sample.programId = 0x010000000000100d;
    sample.started = 1700000000;
    auto text = toText(sample);
    auto back = parse(text);
    assert(back && back->programId == sample.programId && back->report.log == sample.report.log);
    assert(back->report.memoryUsed == 123 && back->report.launch == "application");
    assert(!parse("") && !parse("not a session"));
    assert(!parse(text.substr(0, text.find("log\n"))));
    auto malformed = text;
    malformed.replace(malformed.find("started "), 8, "started bad");
    assert(!parse(malformed));
    malformed = text;
    malformed.replace(malformed.find("application"), 11, "unknown");
    assert(!parse(malformed));
    const std::string body = "Atmosphère Crash Report (v1.7):\nProcess Info:\n    Program ID: private\n"
                             "Exception Info:\n    Type: Data Abort\n    Fault Address: 0xc\n"
                             "Crashed Thread Info:\n    PC: 0x123 (themezer-nx + 0x23)\n"
                             "    LR: 0x456\n    SP: 0x789\n    X[0]: not useful\n";
    auto crash = crashReport("01700000001_010000000000100d.log", body);
    assert(crash && crash->detail.find("Data Abort") != std::string::npos);
    assert(crash->detail.find("themezer-nx + 0x23") != std::string::npos);
    assert(crash->detail.find("private") == std::string::npos && crash->detail.find("X[0]") == std::string::npos);
    assert(ending(sample, crash).kind == "crash");
    Snapshot unknownTime = sample;
    unknownTime.started = 0;
    assert(ending(unknownTime, crash).kind == "unclean exit");
    crash->programId++;
    assert(ending(sample, crash).kind == "unclean exit");
    crash->programId = sample.programId;
    crash->timestamp = sample.started - 1;
    assert(ending(sample, crash).kind == "unclean exit");
    assert(!crashReport("bad.log", body));
    assert(!crashReport("01700000001_010000000000100d.log", "not a crash report"));
    sample.stalledMs = 9999;
    assert(ending(sample, {}).kind == "unclean exit");
    sample.stalledMs = 10000;
    assert(ending(sample, {}).kind == "freeze");
    sample.foreground = false;
    assert(ending(sample, {}).kind == "unclean exit");
    sample.foreground = true;
    sample.stalledMs = 0;
    sample.report.log.push_back("01:02:04.005 E the app stopped on a fatal error");
    assert(ending(sample, {}).kind == "fatal error");
    crash->timestamp = sample.started + 1;
    assert(ending(sample, crash).kind == "fatal error");
    assert(ending(sample, crash).detail.find("PC:") != std::string::npos);
    sample.report.log.push_back("01:02:05.000 I another line");
    assert(ending(sample, {}).kind == "unclean exit");
    std::puts("session: matching crash, other/older report ignored, freeze threshold/focus, fatal ending, unclean and malformed snapshots passed");
}
