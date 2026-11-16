#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace tz::log
{
class Ring
{
public:
    explicit Ring(size_t capacity = 200) : capacity_(capacity) {}

    void push(std::string line)
    {
        lines_.push_back(std::move(line));
        while (lines_.size() > capacity_)
            lines_.pop_front();
    }

    std::vector<std::string> tail() const { return {lines_.begin(), lines_.end()}; }

private:
    size_t capacity_;
    std::deque<std::string> lines_;
};

struct Memory
{
    uint64_t used = 0, available = 0; // bytes, at the time of collection
};
Memory memory();
void logMemory();

void init();
void exit();
void info(const char* format, ...);
void warn(const char* format, ...);
void error(const char* format, ...);
std::vector<std::string> tail();
} // namespace tz::log
