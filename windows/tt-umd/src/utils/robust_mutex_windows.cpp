// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows implementation of RobustMutex. The Linux implementation is built on POSIX shared memory
// and robust pthread mutexes; the Windows equivalent is a named kernel mutex, which is natively
// robust: if the owning process dies while holding it, the next waiter is granted ownership with
// WAIT_ABANDONED and can recover.
//
// Kernel mutexes are recursive per thread, unlike the pthread mutex. An in-process owner table
// (keyed by mutex name, shared by every RobustMutex object of this process) restores the pthread
// answer where it is observable without deadlocking: probe_lock() from a thread that already holds
// the lock reports it as held, with this process and thread as the owner.
//
// Remaining differences from the Linux implementation, acceptable for a best-effort lock:
//  - lock() from a thread that already holds the lock succeeds (recursion) instead of deadlocking.
//  - The owner of a lock held by another process is not known, so probe_lock() reports {0, 0}.
//  - The name lives in the per-session "Local\" kernel namespace rather than a system-wide file
//    in /dev/shm; cross-session exclusion would need "Global\" (and privileges).
//
// The class layout comes from the shared robust_mutex.hpp, which has no member for a HANDLE.
// Kernel handles fit in 32 bits on 64-bit Windows too (a documented interop guarantee, see
// HandleToLong/LongToHandle), so the mutex handle is kept in shm_fd_, the descriptor slot of the
// Linux implementation, with -1 meaning "not open". The pthread members stay unused.

#include "tt-umd/utils/robust_mutex.hpp"

#include <windows.h>

#include <mutex>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr int NO_HANDLE = -1;

HANDLE to_handle(int slot) { return slot == NO_HANDLE ? nullptr : LongToHandle(slot); }

int to_slot(HANDLE handle) { return handle == nullptr ? NO_HANDLE : HandleToLong(handle); }

// Which thread of this process holds each named lock, and how many times.
struct LocalOwner {
    DWORD thread_id;
    int depth;
};

std::mutex owners_mutex;
std::unordered_map<std::string, LocalOwner> owners;

// Records an acquisition that the kernel granted; returns false if the calling thread already held
// the lock, i.e. the kernel granted a recursive acquisition.
bool record_owner(const std::string& name) {
    std::lock_guard<std::mutex> guard(owners_mutex);
    const DWORD self = GetCurrentThreadId();
    auto [it, inserted] = owners.try_emplace(name, LocalOwner{self, 0});
    const bool first = inserted || it->second.depth == 0;
    it->second = LocalOwner{self, first ? 1 : it->second.depth + 1};
    return first;
}

void release_owner(const std::string& name) {
    std::lock_guard<std::mutex> guard(owners_mutex);
    auto it = owners.find(name);
    if (it != owners.end() && --it->second.depth <= 0) {
        owners.erase(it);
    }
}

}  // namespace

RobustMutex::RobustMutex(std::string_view mutex_name) : mutex_name_(mutex_name) {}

RobustMutex::~RobustMutex() noexcept { close_mutex(); }

void RobustMutex::initialize() {
    if (shm_fd_ != NO_HANDLE) {
        return;
    }

    // Same prefix as the Linux shm files, so lock names line up across platforms in logs.
    const std::string name = std::string(SHM_FILE_PREFIX) + mutex_name_;

    // Creates the mutex or opens the existing one; initial ownership is not requested.
    HANDLE h = CreateMutexA(nullptr, FALSE, name.c_str());
    if (h == nullptr) {
        UMD_THROW(
            error::RuntimeError,
            "Failed to create/open named mutex " + name + ": error " + std::to_string(GetLastError()));
    }
    shm_fd_ = to_slot(h);
}

RobustMutex::RobustMutex(RobustMutex&& other) noexcept :
    shm_fd_(other.shm_fd_), mutex_name_(std::move(other.mutex_name_)) {
    other.shm_fd_ = NO_HANDLE;
}

RobustMutex& RobustMutex::operator=(RobustMutex&& other) noexcept {
    if (this != &other) {
        close_mutex();
        shm_fd_ = other.shm_fd_;
        mutex_name_ = std::move(other.mutex_name_);
        other.shm_fd_ = NO_HANDLE;
    }
    return *this;
}

void RobustMutex::lock() {
    UMD_ASSERT(shm_fd_ != NO_HANDLE, error::RuntimeError, "RobustMutex::lock() before initialize()");

    // Mirror the Linux behavior: try for one second first so contention can be reported, then
    // block without a timeout.
    DWORD result = WaitForSingleObject(to_handle(shm_fd_), 1000);
    if (result == WAIT_TIMEOUT) {
        log_warning(LogUMD, "Waiting on mutex {} held by another thread or process.", mutex_name_);
        result = WaitForSingleObject(to_handle(shm_fd_), INFINITE);
    }

    if (result == WAIT_ABANDONED) {
        // The previous owner died while holding the lock; ownership was granted to us and the
        // protected state may be inconsistent. Matches the EOWNERDEAD recovery path on Linux.
        log_warning(LogUMD, "Mutex {} was abandoned by a dead owner; lock recovered.", mutex_name_);
        result = WAIT_OBJECT_0;
    }

    UMD_ASSERT(
        result == WAIT_OBJECT_0,
        error::RuntimeError,
        "WaitForSingleObject on mutex " + mutex_name_ + " failed: error " + std::to_string(GetLastError()));

    record_owner(mutex_name_);
    active_locks_.fetch_add(1);
}

void RobustMutex::unlock() {
    UMD_ASSERT(shm_fd_ != NO_HANDLE, error::RuntimeError, "RobustMutex::unlock() before initialize()");
    active_locks_.fetch_sub(1);
    release_owner(mutex_name_);
    if (!ReleaseMutex(to_handle(shm_fd_))) {
        active_locks_.fetch_add(1);
        UMD_THROW(
            error::RuntimeError,
            "ReleaseMutex on mutex " + mutex_name_ + " failed: error " + std::to_string(GetLastError()));
    }
}

std::optional<std::pair<pid_t, pid_t>> RobustMutex::probe_lock(std::chrono::seconds timeout) {
    UMD_ASSERT(shm_fd_ != NO_HANDLE, error::RuntimeError, "RobustMutex::probe_lock() before initialize()");

    const DWORD wait_ms = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    DWORD result = WaitForSingleObject(to_handle(shm_fd_), wait_ms);

    if (result == WAIT_ABANDONED) {
        log_warning(LogUMD, "Mutex {} was abandoned by a dead owner; lock recovered.", mutex_name_);
        result = WAIT_OBJECT_0;
    }
    if (result == WAIT_OBJECT_0) {
        if (!record_owner(mutex_name_)) {
            // Granted only because this thread already holds it: report it held, as pthread does.
            release_owner(mutex_name_);
            ReleaseMutex(to_handle(shm_fd_));
            return std::make_pair(static_cast<pid_t>(GetCurrentProcessId()), static_cast<pid_t>(GetCurrentThreadId()));
        }
        active_locks_.fetch_add(1);
        return std::nullopt;
    }
    if (result == WAIT_TIMEOUT) {
        // The owner's PID/TID are not tracked on Windows; report a best-effort unknown owner.
        return std::make_pair(pid_t{0}, pid_t{0});
    }

    UMD_THROW(
        error::RuntimeError,
        "WaitForSingleObject on mutex " + mutex_name_ + " failed: error " + std::to_string(GetLastError()));
}

void RobustMutex::close_mutex() noexcept {
    if (shm_fd_ != NO_HANDLE) {
        if (active_locks_.load() != 0) {
            log_warning(LogUMD, "Mutex {} destroyed while held; the kernel will mark it abandoned.", mutex_name_);
        }
        CloseHandle(to_handle(shm_fd_));
        shm_fd_ = NO_HANDLE;
    }
}

}  // namespace tt::umd
