#include "sys/net.hpp"

#include "sys/log.hpp"
#include "core/certificates.hpp"
#ifndef TZ_DESKTOP
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif
#include <curl/curl.h>
#include <strings.h>
#include <switch.h>

#include <cstdio>
#include <ctime>
#include <memory>
#include <string_view>

namespace tz::net
{

namespace
{

constexpr long kConnectTimeout = 10;
constexpr long kLowSpeedLimit = 1;
constexpr long kLowSpeedTime = 30;

// A current list is strict. An abandoned build retries its first certificate failure once without checking,
// then uses that policy for every worker in the session, as Themezer-3DS does.
constexpr const char* kCaBundlePath = "romfs:/cacert.pem";
int64_t caBundleTime = -1;
std::atomic<bool> verificationOff{false};
#ifndef TZ_DESKTOP
mbedtls_x509_crt caChain;
bool caChainParsed = false;

CURLcode setUpTls(CURL*, void* config, void*)
{
    if (caChainParsed)
        mbedtls_ssl_conf_ca_chain(static_cast<mbedtls_ssl_config*>(config), &caChain, nullptr);
    return CURLE_OK;
}
#endif

void setCertificates(CURL* easy)
{
#ifndef TZ_DESKTOP
    // The immutable chain is parsed once; no connection reads curl's default SD certificate path.
    curl_easy_setopt(easy, CURLOPT_CAINFO, caChainParsed ? nullptr : kCaBundlePath);
    curl_easy_setopt(easy, CURLOPT_SSL_CTX_FUNCTION, setUpTls);
#else
    // The desktop's host curl uses the existing romfs: -> romfs symlink.
    curl_easy_setopt(easy, CURLOPT_CAINFO, kCaBundlePath);
#endif
    curl_easy_setopt(easy, CURLOPT_CAPATH, static_cast<char*>(nullptr));
}

CURLSH* share = nullptr;
// This thread's handle, kept between requests so its connection stays open for the next one. curl doesn't support
// sharing connections between threads, so each worker has its own and shares only DNS answers and TLS sessions.
thread_local CURL* threadEasy = nullptr;
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
    verificationOff.store(false);
    std::string bundle;
    if (FILE* file = std::fopen(kCaBundlePath, "rb"))
    {
        char buffer[4096];
        for (size_t read; (read = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && bundle.size() < 1024 * 1024;)
            bundle.append(buffer, read);
        std::fclose(file);
    }
    caBundleTime = certificates::bundleTime(bundle, __DATE__);
#ifdef TZ_DESKTOP
    if (const char* date = themezerDesktopCaDate())
        caBundleTime = certificates::bundleTime({}, date);
#else
    mbedtls_x509_crt_init(&caChain);
    const int parsed = bundle.empty() ? -1 : mbedtls_x509_crt_parse(
        &caChain, reinterpret_cast<const unsigned char*>(bundle.c_str()), bundle.size() + 1);
    caChainParsed = parsed >= 0 && caChain.version != 0;
    if (!caChainParsed)
        log::warn("Couldn't parse the bundled CA list: curl will read it per connection");
#endif
    for (Mutex& m : shareLocks)
        mutexInit(&m);
    share = curl_share_init();
    if (!share)
        return true;
    CURLSHcode res = CURLSHE_OK;
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_LOCKFUNC, lock));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, unlock));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS));
    res = static_cast<CURLSHcode>(res | curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION));
    if (res != CURLSHE_OK)
    {
        log::warn("curl share setup failed: %d", res);
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
#ifndef TZ_DESKTOP
    mbedtls_x509_crt_free(&caChain);
    caChainParsed = false;
#endif
}

bool Response::cancelled() const
{
    return curlCode == CURLE_ABORTED_BY_CALLBACK;
}

bool Response::tls() const
{
    switch (curlCode)
    {
    case CURLE_PEER_FAILED_VERIFICATION: // also CURLE_SSL_CACERT
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CRL_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    case CURLE_SSL_INVALIDCERTSTATUS:
        return true;
    default:
        return false;
    }
}

static Response request(const std::string& url, const CancelToken* token, Progress* progress, long timeoutSeconds,
                        const char* sentHeader, const std::string* body = nullptr)
{
    Response response;
    if (!threadEasy)
        threadEasy = curl_easy_init();
    CURL* easy = threadEasy;
    if (!easy)
    {
        response.curlCode = CURLE_FAILED_INIT;
        return response;
    }
    // the last request's options point into its finished stack frame; its connection stays open
    curl_easy_reset(easy);
    char error[CURL_ERROR_SIZE] = "";
    Transfer transfer{&response, token, progress};
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    const bool verify = !verificationOff.load();
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
    setCertificates(easy);
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
    curl_slist* headers = sentHeader ? curl_slist_append(nullptr, sentHeader) : nullptr;
    if (headers)
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);

    if (body)
    {
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body->size()));
    }
    bool retried = false;
    response.curlCode = curl_easy_perform(easy);
    if (verify && response.tls() && certificates::outdated(caBundleTime, static_cast<int64_t>(std::time(nullptr))))
    {
        retried = true;
        if (!verificationOff.exchange(true))
            log::warn("The CA certificates are over three years old: connecting without verifying from now on");
        response = Response{};
        error[0] = '\0';
        if (progress)
        {
            progress->received.store(0);
            progress->total.store(0);
        }
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
        response.curlCode = curl_easy_perform(easy);
    }
    curl_slist_free_all(headers); // the next request's reset lets go of the handle's pointer to it
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response.status);
    char* type = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK && type)
        response.contentType = type;
    if (response.curlCode != CURLE_OK)
        response.error = error[0] ? error : curl_easy_strerror(static_cast<CURLcode>(response.curlCode));
    long connects = -1;
    curl_easy_getinfo(easy, CURLINFO_NUM_CONNECTS, &connects);
    char line[220];
    std::snprintf(line, sizeof(line), "%s %.80s: %d, HTTP %ld, %zu bytes, %ld new connections, verification %s",
                  body ? "POST" : "GET", url.c_str(), response.curlCode, response.status, response.body.size(),
                  connects, verify ? (retried ? "on, then off" : "on") : "off");
    // Workers can log directly; they finish before stdout closes.
    log::info("%s", line);
    return response;
}

Response get(const std::string& url, const CancelToken* token, Progress* progress, long timeoutSeconds,
             const char* sentHeader)
{
#ifdef TZ_DESKTOP
    if (themezerDesktopOffline())
    {
        Response response;
        response.curlCode = CURLE_COULDNT_CONNECT;
        response.error = "Desktop offline test";
        return response;
    }
#endif
    return request(url, token, progress, timeoutSeconds, sentHeader);
}

Response post(const std::string& url, const std::string& body, const char* contentType)
{
    const std::string header = std::string("Content-Type: ") + contentType;
    return request(url, nullptr, nullptr, 30, header.c_str(), &body);
}

void release()
{
    if (!threadEasy)
        return;
    curl_easy_reset(threadEasy); // nothing left pointing at a stack frame that's gone while it closes
    curl_easy_cleanup(threadEasy);
    threadEasy = nullptr;
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
