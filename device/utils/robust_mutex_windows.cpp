// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows implementation of RobustMutex. The Linux implementation is built on POSIX shared memory
// and robust pthread mutexes; the Windows equivalent is a named kernel mutex, which is natively
// robust: if the owning process dies while holding it, the next waiter is granted ownership with
// WAIT_ABANDONED and can recover.
//
// Differences from the Linux implementation, acceptable for a best-effort lock:
//  - Named mutexes are recursive per thread (the same thread can re-acquire); the pthread version
//    would deadlock instead.
//  - The owner's PID/TID are not tracked, so a contended probe_lock() reports {0, 0}.
//  - The name lives in the per-session "Local\" kernel namespace rather than a system-wide file
//    in /dev/shm; cross-session exclusion would need "Global\" (and privileges).

#include <windows.h>

#include <string>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/utils/error.hpp"
#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {

RobustMutex::RobustMutex(std::string_view mutex_name) : mutex_name_(mutex_name) {}

RobustMutex::~RobustMutex() noexcept { close_mutex(); }

void RobustMutex::initialize() {
    if (win_mutex_ != nullptr) {
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
    win_mutex_ = h;
}

RobustMutex::RobustMutex(RobustMutex&& other) noexcept :
    shm_fd_(other.shm_fd_), win_mutex_(other.win_mutex_), mutex_name_(std::move(other.mutex_name_)) {
    other.shm_fd_ = -1;
    other.win_mutex_ = nullptr;
}

RobustMutex& RobustMutex::operator=(RobustMutex&& other) noexcept {
    if (this != &other) {
        close_mutex();
        shm_fd_ = other.shm_fd_;
        win_mutex_ = other.win_mutex_;
        mutex_name_ = std::move(other.mutex_name_);
        other.shm_fd_ = -1;
        other.win_mutex_ = nullptr;
    }
    return *this;
}

void RobustMutex::lock() {
    UMD_ASSERT(win_mutex_ != nullptr, error::RuntimeError, "RobustMutex::lock() before initialize()");

    // Mirror the Linux behavior: try for one second first so contention can be reported, then
    // block without a timeout.
    DWORD result = WaitForSingleObject(win_mutex_, 1000);
    if (result == WAIT_TIMEOUT) {
        log_warning(LogUMD, "Waiting on mutex {} held by another thread or process.", mutex_name_);
        result = WaitForSingleObject(win_mutex_, INFINITE);
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

    active_locks_.fetch_add(1);
}

void RobustMutex::unlock() {
    UMD_ASSERT(win_mutex_ != nullptr, error::RuntimeError, "RobustMutex::unlock() before initialize()");
    active_locks_.fetch_sub(1);
    if (!ReleaseMutex(win_mutex_)) {
        active_locks_.fetch_add(1);
        UMD_THROW(
            error::RuntimeError,
            "ReleaseMutex on mutex " + mutex_name_ + " failed: error " + std::to_string(GetLastError()));
    }
}

std::optional<std::pair<pid_t, pid_t>> RobustMutex::probe_lock(std::chrono::seconds timeout) {
    UMD_ASSERT(win_mutex_ != nullptr, error::RuntimeError, "RobustMutex::probe_lock() before initialize()");

    const DWORD wait_ms = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    DWORD result = WaitForSingleObject(win_mutex_, wait_ms);

    if (result == WAIT_ABANDONED) {
        log_warning(LogUMD, "Mutex {} was abandoned by a dead owner; lock recovered.", mutex_name_);
        result = WAIT_OBJECT_0;
    }
    if (result == WAIT_OBJECT_0) {
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
    if (win_mutex_ != nullptr) {
        if (active_locks_.load() != 0) {
            log_warning(LogUMD, "Mutex {} destroyed while held; the kernel will mark it abandoned.", mutex_name_);
        }
        CloseHandle(win_mutex_);
        win_mutex_ = nullptr;
    }
}

}  // namespace tt::umd
