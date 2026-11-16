#pragma once

namespace tz::themezer
{
enum class ErrorKind
{
    Cancelled,
    Network,
    Tls,
    Server,
    Api,
    NotFound,
    NoSpace,
    Storage,
    OutOfMemory,
    InvalidData,
    Unsupported
};
constexpr bool shareable(ErrorKind kind)
{
    return kind == ErrorKind::OutOfMemory || kind == ErrorKind::Storage || kind == ErrorKind::InvalidData || kind == ErrorKind::Unsupported;
}

constexpr const char* kindName(ErrorKind kind)
{
    switch (kind)
    {
    case ErrorKind::Cancelled:
        return "cancelled";
    case ErrorKind::Network:
        return "network";
    case ErrorKind::Tls:
        return "tls";
    case ErrorKind::Server:
        return "server";
    case ErrorKind::Api:
        return "api";
    case ErrorKind::NotFound:
        return "not found";
    case ErrorKind::NoSpace:
        return "no space";
    case ErrorKind::Storage:
        return "storage";
    case ErrorKind::OutOfMemory:
        return "out of memory";
    case ErrorKind::InvalidData:
        return "invalid data";
    case ErrorKind::Unsupported:
        return "unsupported";
    }
    return "unknown";
}
} // namespace tz::themezer
