# tt-umd on Windows (fork overlay)

This fork adds a native Windows host build of tt-umd on top of the
[tt-wind](https://github.com/InputNamePlz/tt-wind) kernel driver, which plays the role of tt-kmd.
Everything Windows-specific lives in this `windows/` directory (plus `.github/workflows/windows-build.yml`);
upstream files are not edited, apart from one OS-neutral portability patch carried until its upstream PR
merges.

## Build

Requirements: Visual Studio 2022 Build Tools (C++ workload; its bundled CMake 3.31 and Ninja), and the
tt-wind driver to talk to a device. From a "x64 Native Tools" prompt:

```powershell
cmake -S . -B .build/windows-msvc -G Ninja -C windows/cmake/msvc-cache.cmake -DTT_UMD_BUILD_TESTS=ON
cmake --build .build/windows-msvc
.build\windows-msvc\test\umd\misc\umd_misc_tests.exe
.build\windows-msvc\test\umd\baremetal\baremetal_tests.exe
```

A parent project that adds UMD with `add_subdirectory()` (any generator, including Visual Studio) sets
`CMAKE_PROJECT_umd_INCLUDE=<umd>/windows/cmake/umd_windows.cmake` and `TT_UMD_BUILD_TOOLS=OFF` (plus
`TT_UMD_BUILD_STATIC=ON`, `TT_UMD_BUILD_SIMULATION=OFF`) instead of using the whole initial cache, which
would also set `CMAKE_BUILD_TYPE` for the parent. The overlay only changes UMD's own directory tree and
targets; consumers of `umd::tt-umd` get no `<windows.h>` and none of its macros.

## How it is wired

| Piece | What it does |
|---|---|
| `cmake/msvc-cache.cmake` | Initial cache: build options, and `CMAKE_PROJECT_umd_INCLUDE` pointing at the overlay. |
| `cmake/umd_windows.cmake` | Runs right after `project(umd)`: directory-level MSVC flags, defers the target fix-ups. |
| `cmake/umd_windows_targets.cmake` | Runs when all targets exist: swaps Linux-only sources for `windows/**/*_windows.*`, drops Linux libraries (hwloc, rt, pthread) and GCC warning flags, adds include paths and forced includes, filters fork/POSIX-signal tests. Every upstream name it relies on is checked; drift is a configure `FATAL_ERROR`. |
| `include/tt_umd_windows_prelude.h` | Forced include (`/FI`, INTERFACE, so consumers get it): `pid_t`, `__atomic_always_lock_free`. Includes nothing. |
| `private/tt_umd_windows_private_prelude.h` | Forced include for tt-umd's own sources (PRIVATE) and tests: the public prelude, then `windows.h` plus `#undef IGNORE`. |
| `private/tt_umd_windows_{gnu_attributes,sigbus}.h` | Per-file forced includes for `tlb_window.cpp` / `silicon_tlb_window.cpp`. |
| `include/tt-umd/driver_atomics.hpp` | Shadow of the upstream header (MSVC intrinsics). Its upstream original is hash-checked at configure time. |
| `compat/public`, `compat/private` | POSIX stand-in headers (`dlfcn.h`, `execinfo.h`, `cxxabi.h`, `pthread.h`; `unistd.h`, `sys/mman.h`) with backends in `compat/posix_compat_windows.cpp`. Each supports only the documented subset and aborts otherwise. |
| `tests/` | Test prelude (`setenv`, `fcntl` stub), test-only `sys/wait.h`, `gtest_exclude.txt`. |

## Not supported on Windows

JTAG library loading is untested, IOMMU/DMA buffers, dma-buf export, simulation, UBB warm reset, and the
multi-process warm-reset notification sockets. SIGBUS-style recovery from failed device accesses is not
implemented (accesses are not guarded).
