// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stub for warm_reset.cpp. The Linux implementation drives resets through /dev/tenstorrent
// ioctls, sysfs globbing and Unix domain sockets, none of which exist on Windows yet. Every
// operation throws a "not yet supported on Windows" error.

#include "umd/device/utils/error.hpp"
#include "umd/device/warm_reset.hpp"

namespace tt::umd {

namespace {
[[noreturn]] void throw_not_supported() {
    UMD_THROW(error::RuntimeError, "Warm reset is not yet supported on Windows.");
}
}  // namespace

bool WarmReset::warm_reset(
    std::vector<int> /*pci_device_ids*/,
    bool /*reset_m3*/,
    bool /*secondary_bus_reset*/,
    std::chrono::milliseconds /*m3_delay*/) {
    throw_not_supported();
}

bool WarmReset::warm_reset_chip_id(
    const std::vector<int>& /*chip_ids*/,
    bool /*reset_m3*/,
    bool /*secondary_bus_reset*/,
    std::chrono::milliseconds /*m3_delay*/) {
    throw_not_supported();
}

bool WarmReset::warm_reset_pci_bdfs(
    const std::vector<std::string>& /*pci_bdfs*/,
    bool /*reset_m3*/,
    bool /*secondary_bus_reset*/,
    std::chrono::milliseconds /*m3_delay*/) {
    throw_not_supported();
}

bool WarmReset::ubb_warm_reset(const std::chrono::milliseconds /*timeout_ms*/) { throw_not_supported(); }

bool WarmResetCommunication::Monitor::start_monitoring(
    std::function<void()>&& /*on_cleanup_request*/, std::function<void()>&& /*post_cleanup_request*/) {
    throw_not_supported();
}

void WarmResetCommunication::Monitor::stop_monitoring() { throw_not_supported(); }

void WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds /*timeout_ms*/) {
    throw_not_supported();
}

void WarmResetCommunication::Notifier::notify_all_listeners_post_reset() { throw_not_supported(); }

}  // namespace tt::umd
