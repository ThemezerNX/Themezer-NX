#include "sys/files.hpp"

#include "services/updates.hpp"
#include <cstdio>
#include <unistd.h>

namespace tz::files
{
std::expected<void, themezer::Error> write(const std::string& path, std::span<const uint8_t> bytes)
{
    const std::string part = path + ".part";
    FILE* file = std::fopen(part.c_str(), "wb");
    if (!file)
    {
        const auto error = themezer::storageError(part);
        unlink(part.c_str());
        return std::unexpected(error);
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    const int writeError = errno;
    const int closed = std::fclose(file);
    const int failure = written != bytes.size() ? writeError : errno;
    if (written != bytes.size() || closed != 0)
    {
        unlink(part.c_str());
        return std::unexpected(themezer::storageError(path, failure));
    }
    return updates::replace(part, path);
}
}
