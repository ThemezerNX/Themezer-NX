#include "sys/net.hpp"

#include <curl/curl.h>
#include <strings.h>
#include <switch.h>

#include <cstdio>
#include <memory>
#include <string_view>

namespace tz::net
{

namespace
{

constexpr long kConnectTimeout = 10;
constexpr long kLowSpeedLimit = 1;
constexpr long kLowSpeedTime = 30;

CURLSH* share = nullptr;
Mutex shareLocks[CURL_LOCK_DATA_LAST];

void lock(CURL*, curl_lock_data data, curl_lock_access, void*)
{
    mutexLock(&shareLocks[data]);
}

void unlock(CURL*, curl_lock_data data, void*)
{
    mutexUnlock(&shareLocks[data]);
}

struct Transfer
{
    Response* response;
    const CancelToken* token;
    Progress* progress;
};

size_t write(char* data, size_t size, size_t count, void* user)
{
    auto* transfer = static_cast<Transfer*>(user);
    const size_t length = size * count;
    transfer->response->body.insert(transfer->response->body.end(), data, data + length);
    return length;
}

// The headers' lines: of each response a redirect goes through, the last one's Content-Disposition is kept.
size_t header(char* data, size_t size, size_t count, void* user)
{
    auto* transfer = static_cast<Transfer*>(user);
    const size_t length = size * count;
    std::string_view line(data, length);
    constexpr std::string_view kName = "content-disposition:";
    if (line.starts_with("HTTP/"))
    {
        transfer->response->contentDisposition.clear();
    }
    else if (line.size() > kName.size() && strncasecmp(line.data(), kName.data(), kName.size()) == 0)
    {
        line.remove_prefix(kName.size());
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.remove_suffix(1);
        transfer->response->contentDisposition = line;
    }
    return length;
}

int progressed(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
    auto* transfer = static_cast<Transfer*>(user);
    if (transfer->token && transfer->token->cancelled())
        return 1;
    if (transfer->progress)
    {
        transfer->progress->total.store(static_cast<uint64_t>(total > 0 ? total : 0));
        transfer->progress->received.store(static_cast<uint64_t>(now));
    }
    return 0;
}

} // namespace

bool init()
{
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
        return false;
    for (Mutex& m : shareLocks)
        mutexInit(&m);
    share = curl_share_init();
    if (!share)
        return true;
    CURLSHcode res = CURLSHE_OK;
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_LOCKFUNC, lock));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, unlock));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION));
    if (res != CURLSHE_OK)
    {
        std::printf("curl share setup failed: %d\n", res);
        curl_share_cleanup(share);
        share = nullptr;
    }
    return true;
}

void exit()
{
    if (share && curl_share_cleanup(share) == CURLSHE_OK)
        share = nullptr;
    curl_global_cleanup();
}

bool Response::cancelled() const
{
    return curlCode == CURLE_ABORTED_BY_CALLBACK;
}

Response get(const std::string& url, const CancelToken* token, Progress* progress, long timeoutSeconds)
{
    Response response;
    CURL* easy = curl_easy_init();
    if (!easy)
    {
        response.curlCode = CURLE_FAILED_INIT;
        return response;
    }
    char error[CURL_ERROR_SIZE] = "";
    Transfer transfer{&response, token, progress};
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "themezer-nx/" APP_VERSION);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, kConnectTimeout);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedLimit);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, kLowSpeedTime);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    if (share)
        curl_easy_setopt(easy, CURLOPT_SHARE, share);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, progressed);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, error);

    response.curlCode = curl_easy_perform(easy);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response.status);
    char* type = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK && type)
        response.contentType = type;
    if (response.curlCode != CURLE_OK)
        response.error = error[0] ? error : curl_easy_strerror(static_cast<CURLcode>(response.curlCode));
    long connects = -1;
    curl_easy_getinfo(easy, CURLINFO_NUM_CONNECTS, &connects);
    char line[160];
    std::snprintf(line, sizeof(line), "GET %.80s: %d, HTTP %ld, %zu bytes, %ld new connections\n", url.c_str(),
                  response.curlCode, response.status, response.body.size(), connects);
    // stdio only on the main thread
    jobs::post([text = std::string(line)] { std::fputs(text.c_str(), stdout); });
    curl_easy_cleanup(easy);
    return response;
}

std::string escape(std::string_view text)
{
    std::string out;
    static const char* kHex = "0123456789ABCDEF";
    for (unsigned char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
        {
            out += static_cast<char>(c);
        }
        else
        {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

} // namespace tz::net
