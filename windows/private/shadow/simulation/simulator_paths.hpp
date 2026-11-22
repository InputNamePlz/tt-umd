// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows shadow of tt-umd/src/simulation/simulator_paths.hpp. windows/private/shadow is placed
// BEFORE tt-umd's src/ directory on the private include path of tt-umd-base and tt-umd, so
// "simulation/simulator_paths.hpp" resolves here on Windows. topology_discovery.cpp includes the
// header in every build, and two of its inline functions return a std::filesystem::path as a
// std::string, which only converts implicitly where path::value_type is char (POSIX). The only
// change from upstream is the explicit .string() in those two returns.
//
// windows/cmake/umd_windows_targets.cmake records a hash of the upstream header this copy was
// synced against and fails the configure when upstream changes it.

#pragma once

#include <filesystem>
#include <string>

#include "tt-umd/types/arch.hpp"

namespace tt::umd {

// Simulator artifacts such as the descriptor files live beside the simulator: next to a TTSim .so, or inside an RTL
// simulator directory.
inline std::filesystem::path get_simulator_artifacts_dir(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? simulator_path.parent_path() : simulator_path;
}

inline std::string get_simulator_soc_descriptor_path(const std::filesystem::path& simulator_path) {
    return (get_simulator_artifacts_dir(simulator_path) / "soc_descriptor.yaml").string();
}

// The simulator's soc_descriptor.yaml when it ships one, else empty. A libttsim image of Wormhole or
// Blackhole needs none: it is then described by its architecture's built-in SoC descriptor.
inline std::string find_simulator_soc_descriptor_path(const std::filesystem::path& simulator_path) {
    std::string soc_descriptor_path = get_simulator_soc_descriptor_path(simulator_path);
    return std::filesystem::exists(soc_descriptor_path) ? soc_descriptor_path : "";
}

// The simulator's architecture: declared by its soc_descriptor.yaml when it ships one, else read from
// the PCI device id its libttsim image reports, the way KMD tells silicon apart. The first call for an
// image without a descriptor has to come before any simulator is brought up in this process: it starts
// and stops the image to read its config space, and the answer is cached for later calls.
// Throws error::RuntimeError if that first call comes after a simulator is already running, if an RTL
// simulator ships no descriptor, or if the image's device id has no built-in SoC descriptor.
tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path);

inline std::string get_simulator_cluster_descriptor_path(const std::filesystem::path& simulator_path) {
    return (get_simulator_artifacts_dir(simulator_path) / "cluster_descriptor.yaml").string();
}

}  // namespace tt::umd
