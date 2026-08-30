// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stub for robust_mutex.cpp. The Linux implementation is built on POSIX shared memory
// (shm_open) and robust pthread mutexes, which have no direct Windows equivalent. Every operation
// throws a "not yet supported on Windows" error.

#include "umd/device/utils/robust_mutex.hpp"

#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {
[[noreturn]] void throw_not_supported() {
    UMD_THROW(error::RuntimeError, "RobustMutex is not yet supported on Windows.");
}
}  // namespace

RobustMutex::RobustMutex(std::string_view mutex_name) : mutex_name_(mutex_name) {}

RobustMutex::~RobustMutex() noexcept {}

void RobustMutex::initialize() { throw_not_supported(); }

RobustMutex::RobustMutex(RobustMutex&& other) noexcept :
    shm_fd_(other.shm_fd_), mutex_name_(std::move(other.mutex_name_)) {
    other.shm_fd_ = -1;
}

RobustMutex& RobustMutex::operator=(RobustMutex&& other) noexcept {
    if (this != &other) {
        shm_fd_ = other.shm_fd_;
        mutex_name_ = std::move(other.mutex_name_);
        other.shm_fd_ = -1;
    }
    return *this;
}

void RobustMutex::lock() { throw_not_supported(); }

void RobustMutex::unlock() { throw_not_supported(); }

std::optional<std::pair<pid_t, pid_t>> RobustMutex::probe_lock(std::chrono::seconds /*timeout*/) {
    throw_not_supported();
}

void RobustMutex::close_mutex() noexcept {}

}  // namespace tt::umd
