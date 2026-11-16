#include "sys/log.hpp"

#include <switch.h>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace tz::log
{
namespace
{
std::mutex mutex;
std::condition_variable ready;
Ring ring;
std::deque<std::string> pending;
std::thread writer;
bool stopping = false;

void write(char level, const char* format, va_list args)
{
    char text[4096];
    vsnprintf(text, sizeof(text), format, args);
    std::string message(text);
    while (!message.empty() && message.back() == '\n')
        message.pop_back();
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::system_clock::to_time_t(now);
    tm local{};
    localtime_r(&seconds, &local);
    char prefix[40];
    snprintf(prefix, sizeof(prefix), "%02d:%02d:%02d.%03d %c ", local.tm_hour, local.tm_min, local.tm_sec,
             int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000), level);
    std::lock_guard lock(mutex);
    size_t start = 0;
    do
    {
        auto end = message.find('\n', start);
        std::string line = prefix + message.substr(start, end - start);
        ring.push(line);
        std::puts(line.c_str());
        if (pending.size() < 200)
            pending.push_back(std::move(line));
        if (end == std::string::npos)
            break;
        start = end + 1;
    } while (start < message.size());
    ready.notify_one();
}
} // namespace

Memory memory()
{
    u64 used = 0, total = 0;
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    return {used, total >= used ? total - used : 0};
}

void logMemory()
{
    const auto now = memory();
    info("memory: %llu bytes used, %llu bytes available", static_cast<unsigned long long>(now.used),
         static_cast<unsigned long long>(now.available));
}

void init()
{
    // SD writes can stall or fail; keep them off the UI and network workers.
    writer = std::thread([] {
        mkdir("/Themes", 0777);
        mkdir("/Themes/ThemezerNX", 0777);
        unlink("/Themes/ThemezerNX/log.old.txt");
        rename("/Themes/ThemezerNX/log.txt", "/Themes/ThemezerNX/log.old.txt");
        FILE* file = std::fopen("/Themes/ThemezerNX/log.txt", "wb");
        size_t bytes = 0;
        for (;;)
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, [] { return stopping || !pending.empty(); });
            if (pending.empty() && stopping)
                break;
            auto lines = std::move(pending);
            pending.clear();
            lock.unlock();
            for (const auto& line : lines)
            {
                if (!file)
                    continue;
                if (bytes + line.size() + 1 > 512 * 1024 - 80)
                {
                    std::fprintf(file, "%.12s W Log file limit reached; continuing in memory.\n", line.c_str());
                    std::fclose(file);
                    file = nullptr;
                    continue;
                }
                if (std::fprintf(file, "%s\n", line.c_str()) < 0 || std::fflush(file) != 0)
                {
                    std::fclose(file);
                    file = nullptr;
                }
                bytes += line.size() + 1;
            }
        }
        if (file)
            std::fclose(file);
    });
}

void exit()
{
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    ready.notify_one();
    if (writer.joinable())
        writer.join();
}

void info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    write('I', format, args);
    va_end(args);
}

void warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    write('W', format, args);
    va_end(args);
}

void error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    write('E', format, args);
    va_end(args);
}

std::vector<std::string> tail()
{
    std::lock_guard lock(mutex);
    return ring.tail();
}
} // namespace tz::log
