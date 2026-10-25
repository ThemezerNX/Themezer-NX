// HTTP over curl. Every handle shares one cache of connections, DNS answers and TLS sessions, so a request to a host
// already talked to skips the connecting.

#pragma once

#include "sys/jobs.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tz::net
{

bool init();
void exit();

// How much of a download is in, as it goes: total is 0 until its size is known.
struct Progress
{
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> total{0};
};

struct Response
{
    int curlCode = 0; // 0 when the request went through, whatever the status
    long status = 0;
    std::string contentType;
    std::string contentDisposition; // the last response's, after redirects; empty when it had none
    std::vector<uint8_t> body;
    std::string error; // curl's words for what went wrong

    bool ok() const { return curlCode == 0 && status >= 200 && status < 300; }
    bool cancelled() const;
};

// A request on a handle of its own, which takes a connection from the shared cache when one to the host is open.
// Blocks: for the workers.
Response get(const std::string& url, const CancelToken* token = nullptr, Progress* progress = nullptr,
             long timeoutSeconds = 0);

std::string escape(std::string_view text);

} // namespace tz::net
