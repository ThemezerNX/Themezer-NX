// Locks for the workers' threads, which libnx makes: libnx's own mutex and condition variable.

#pragma once

#include <switch.h>

namespace tz
{

// Use with std::lock_guard.
class Lock
{
public:
    Lock() { mutexInit(&mutex_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    void lock() { mutexLock(&mutex_); }
    void unlock() { mutexUnlock(&mutex_); }
    ::Mutex* native() { return &mutex_; }

private:
    ::Mutex mutex_;
};

class Condition
{
public:
    Condition() { condvarInit(&condvar_); }
    Condition(const Condition&) = delete;
    Condition& operator=(const Condition&) = delete;
    // With lock held: lets it go until woken, then holds it again.
    void wait(Lock& lock) { condvarWait(&condvar_, lock.native()); }
    void wakeOne() { condvarWakeOne(&condvar_); }
    void wakeAll() { condvarWakeAll(&condvar_); }

private:
    CondVar condvar_;
};

} // namespace tz
