// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows implementation of warm_reset.cpp, limited to what the tt-wind driver supports today.
//
// warm_reset() arms each device's self-reset and recovers it through tt-kmd-lib's synchronous
// tt_device_reset() (arm + POST_RESET polling, matching the driver's split reset flow). The Linux
// extras have no Windows counterpart yet:
//  - reset_m3 / secondary_bus_reset (ARC M3 message, sysfs secondary bus reset) are ignored with a
//    warning; a standard link reset is performed instead.
//  - The pre/post-reset notification sockets (WarmResetCommunication) are Unix domain sockets on
//    Linux; monitoring/notifying still throws "not yet supported on Windows".

#include <fmt/format.h>

#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/warm_reset.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_not_supported() {
    UMD_THROW(error::RuntimeError, "Warm reset is not yet supported on Windows.");
}

bool reset_devices(const std::vector<int>& pci_device_ids, bool reset_m3, bool secondary_bus_reset) {
    if (reset_m3 || secondary_bus_reset) {
        log_warning(
            tt::LogUMD,
            "M3 and secondary-bus resets are not supported on Windows; performing a standard link reset instead.");
    }

    bool all_ok = true;
    for (int device_id : pci_device_ids) {
        tt_device_t* dev = nullptr;
        const std::string device_path = std::to_string(device_id);
        int err = tt_device_open(device_path.c_str(), &dev, 0);
        if (err != 0) {
            log_error(tt::LogUMD, "Warm reset: failed to open device {}: errno {}", device_id, -err);
            all_ok = false;
            continue;
        }
        // Synchronous arm + recover; the handle is invalidated by a successful reset.
        err = tt_device_reset(dev, 0);
        tt_device_close(dev);
        if (err != 0) {
            log_error(tt::LogUMD, "Warm reset failed on device {}: errno {}", device_id, -err);
            all_ok = false;
            continue;
        }
        log_info(tt::LogUMD, "Warm reset completed on device {}.", device_id);
    }
    return all_ok;
}

}  // namespace

bool WarmReset::warm_reset(
    std::vector<int> pci_device_ids,
    bool reset_m3,
    bool secondary_bus_reset,
    std::chrono::milliseconds /*m3_delay*/) {
    if (pci_device_ids.empty()) {
        pci_device_ids = PCIDevice::enumerate_devices();
    }
    return reset_devices(pci_device_ids, reset_m3, secondary_bus_reset);
}

bool WarmReset::warm_reset_chip_id(
    const std::vector<int>& chip_ids,
    bool reset_m3,
    bool secondary_bus_reset,
    std::chrono::milliseconds m3_delay) {
    // On Windows chip ids and PCI device ordinals coincide (single-host, PCIe-attached chips only).
    return warm_reset(chip_ids, reset_m3, secondary_bus_reset, m3_delay);
}

bool WarmReset::warm_reset_pci_bdfs(
    const std::vector<std::string>& pci_bdfs,
    bool reset_m3,
    bool secondary_bus_reset,
    std::chrono::milliseconds m3_delay) {
    std::vector<int> device_ids;
    const auto device_infos = PCIDevice::enumerate_devices_info();
    for (const std::string& bdf : pci_bdfs) {
        bool found = false;
        for (const auto& [device_id, info] : device_infos) {
            if (info.pci_bdf == bdf) {
                device_ids.push_back(device_id);
                found = true;
                break;
            }
        }
        if (!found) {
            log_error(tt::LogUMD, "Warm reset: no device with BDF {}.", bdf);
            return false;
        }
    }
    return warm_reset(device_ids, reset_m3, secondary_bus_reset, m3_delay);
}

bool WarmReset::ubb_warm_reset(const std::chrono::milliseconds /*timeout_ms*/) {
    // UBB (Galaxy) hardware is not supported on Windows.
    throw_not_supported();
}

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
