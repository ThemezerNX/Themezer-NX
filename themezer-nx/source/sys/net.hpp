// HTTP over curl. Each thread keeps its handle and so its connection between requests, and every handle shares DNS
// answers and TLS sessions, so a request to a host already talked to skips most of the connecting.

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

// A request on this thread's handle, which reuses its connection when it has one open to the host. sentHeader, when
// given, is sent with it ("Name: value"). Blocks: for the workers.
Response get(const std::string& url, const CancelToken* token = nullptr, Progress* progress = nullptr,
             long timeoutSeconds = 0, const char* sentHeader = nullptr);

Response post(const std::string& url, const std::string& body, const char* contentType);

// Closes this thread's handle and its connections: for a worker on its way out, before the sockets go.
void release();

std::string escape(std::string_view text);

} // namespace tz::net
