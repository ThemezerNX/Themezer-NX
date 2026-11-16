#include "domain/report.hpp"
#include "services/themezer.hpp"
#include "sys/log.hpp"
#include <cassert>
#include <cstdio>
int main()
{
    using namespace tz;
    using themezer::ErrorKind;
    assert(!themezer::shareable(themezer::Error{}.kind));
    auto full = themezer::storageError("/Themes/test", ENOSPC);
    assert(full.kind == ErrorKind::NoSpace && full.message == "The SD card is full.");
    assert(!themezer::shareable(full.kind));
    for (auto kind : {ErrorKind::Cancelled, ErrorKind::Network, ErrorKind::Tls, ErrorKind::Server, ErrorKind::Api, ErrorKind::NotFound,
                      ErrorKind::NoSpace})
        assert(!themezer::shareable(kind));
    for (auto kind : {ErrorKind::Storage, ErrorKind::InvalidData, ErrorKind::Unsupported, ErrorKind::OutOfMemory})
        assert(themezer::shareable(kind));
    report::Report report{"4", "21.0.0", "1.10.0", ErrorKind::Storage, "quote\" slash\\\n\x01", "bad\xff", {}};
    report.launch = "application";
    report.memoryUsed = 123;
    report.memoryAvailable = 456;
    auto json = report::toJson(report);
    assert(json.find("\"launch\":\"application\",\"memory\":{\"used\":123,\"available\":456}") != std::string::npos);
    assert(json.find("quote\\u0022 slash\\u005c\\u000a\\u0001") != std::string::npos);
    assert(json.find("bad\xef\xbf\xbd") != std::string::npos);
    for (int i = 0; i < 200; ++i)
        report.log.push_back(std::to_string(i) + std::string(1000, 'x'));
    json = report::toJson(report);
    assert(json.size() <= report::kMaxBody);
    assert(json.find("\"0xxx") == std::string::npos);
    assert(json.find("\"199xxx") != std::string::npos);
    assert(json.find("\"198xxx") < json.find("\"199xxx"));
    log::Ring ring(3);
    for (int i = 0; i < 10; ++i)
        ring.push(std::to_string(i));
    assert((ring.tail() == std::vector<std::string>{"7", "8", "9"}));
    std::puts("report_test: all error kinds, JSON escaping, UTF-8, size trimming, log order passed");
}
