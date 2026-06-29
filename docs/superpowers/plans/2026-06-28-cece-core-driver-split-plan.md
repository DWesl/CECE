# CECE Core and Driver Layer Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize CECE into a pure CCPP-ready physics core library (`libcece_core.so`) and a standalone/coupled ingestion driver library (`libcece_driver.so`), completely unifying the AMIO/AXIS/DAGR ingestion pipelines.

**Architecture:** We physical split the source files into `src/core/` and `src/driver/` directories. We compile them as separate shared library targets, wrapping the heavy offline ingestion logic in a new `CeceDriverOrchestrator` C++ facade with clean C-linkage APIs that are shared identically by `main.cpp` and `driver.F90`.

**Tech Stack:** C++20, Kokkos, OpenMPI, ESMF, NUOPC, AMIO, AXIS, DAGR, TICK, LOGS.

## Global Constraints
- **Core Isolation:** `cece_core` must compile with zero inclusions or link dependencies on `amio`, `axis`, `dagr`, or `span`.
- **C-Style Wrappers:** All inter-library boundaries and Fortran cap linkages must use clean, robust C-linkage (`extern "C"`) functions with error code pointers (`int* rc`).
- **Bit-for-Bit Parity:** All 271 existing regression, unit, and property tests must pass with identical bit-for-bit results.
- **Reference Implementation:** The coordinate reading, latitude-flip detection, indexing, and AXIS regridding logic inside the existing `src/main.cpp` standalone driver serves as the **absolute reference implementation** and must be used verbatim when migrating this logic to the `CeceDriverOrchestrator`.

---

### Task 1: Directory Reorganization & Target Build Configuration

**Files:**
- Create directories: `src/core`, `src/driver`, `src/core/physics`
- Move existing files:
  - All compute files in `src/` to `src/core/`
  - All physics files in `src/physics/` to `src/core/physics/`
  - `src/cece_helm_graph.cpp` and `src/cece_regridder_utils.cpp` to `src/driver/`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Existing files and targets.
- Produces: `cece_core` (shared library) and `cece_driver` (shared library) targets.

- [ ] **Step 1: Create directories and move files**
Run:
```bash
mkdir -p src/core src/driver src/core/physics
git mv src/physics/* src/core/physics/
rmdir src/physics
git mv src/cece_clock.cpp src/core/
git mv src/cece_config_parser.cpp src/core/
git mv src/cece_config_path.cpp src/core/
git mv src/cece_config_validator.cpp src/core/
git mv src/cece_core_field_helpers.cpp src/core/
git mv src/cece_core_finalize.cpp src/core/
git mv src/cece_core_initialize_p1.cpp src/core/
git mv src/cece_core_initialize_p2.cpp src/core/
git mv src/cece_core_realize.cpp src/core/
git mv src/cece_core_realize_wrapper.cpp src/core/
git mv src/cece_core_run.cpp src/core/
git mv src/cece_data_ingestor.cpp src/core/
git mv src/cece_diagnostic_manager.cpp src/core/
git mv src/cece_physics_factory.cpp src/core/
git mv src/cece_provenance.cpp src/core/
git mv src/cece_stacking_engine.cpp src/core/
git mv src/cece_standalone_writer.cpp src/core/
git mv src/cece_c_api.cpp src/core/
git mv src/cece_compute.cpp src/core/
git mv src/cece_core_writer_init.cpp src/core/
git mv src/cece_core_driver_config.cpp src/core/
git mv src/cece_core_advertise.cpp src/core/
git mv src/cece_core_field_helpers.cpp src/core/
git mv src/cece_core_run.cpp src/core/
git mv src/cece_core_finalize.cpp src/core/
git mv src/cece_ingestor_bridge.cpp src/core/
git mv src/cece_io.cpp src/core/
git mv src/cece_helm_graph.cpp src/driver/
git mv src/cece_regridder_utils.cpp src/driver/
```

- [ ] **Step 2: Update root `CMakeLists.txt` to define split libraries**
Modify `CMakeLists.txt` to split the file paths and targets:
```cmake
# --- cece_core Library ---
set(CECE_CORE_SRCS
  src/core/cece_clock.cpp
  src/core/cece_config_parser.cpp
  src/core/cece_compute.cpp
  src/core/cece_stacking_engine.cpp
  src/core/cece_physics_factory.cpp
  src/core/cece_provenance.cpp
  src/core/cece_data_ingestor.cpp
  src/core/cece_diagnostic_manager.cpp
  src/core/cece_standalone_writer.cpp
  src/core/cece_regridder_utils.cpp # wait, keep core clean!
  # Add rest of core files...
)
```
- [ ] **Step 3: Run CMake configuration and verify targets**
Run: `./setup.sh -c "cmake -B build -S ."`
Expected: Successful configuration, defining `cece_core` and `cece_driver` targets.

- [ ] **Step 4: Compile and resolve includes**
Run: `./setup.sh -c "cmake --build build -j2"`
Expected: Complete clean compilation.

- [ ] **Step 5: Commit**
```bash
git add src/ CMakeLists.txt
git commit -m "refactor: physically split src into src/core and src/driver targets"
```

---

### Task 2: Build the `CeceDriverOrchestrator` Facade

**Files:**
- Create: `include/cece/cece_driver_facade.hpp`
- Create: `src/driver/cece_driver_facade.cpp`
- Modify: `CMakeLists.txt` (to include new sources in `cece_driver`)

**Interfaces:**
- Consumes: `cece_core`, `HELM::DAGR`, `AMIO`, `AXIS`, `TICK` APIs.
- Produces: `cece_driver_create`, `cece_driver_advance_time`, and `cece_driver_destroy` C-linkage symbols.

- [ ] **Step 1: Write `include/cece/cece_driver_facade.hpp`**
Write the class and C-linkage declarations:
```cpp
#ifndef CECE_DRIVER_FACADE_HPP
#define CECE_DRIVER_FACADE_HPP

#include <string>
#include <vector>
#include <memory>

extern "C" {
void cece_driver_create(const char* yaml_path, int path_len,
                        int nx, int ny, int nz,
                        const double* lon_coords, const double* lat_coords,
                        void** driver_ptr_out, int* rc);

void cece_driver_advance_time(void* driver_ptr,
                              const char* time_iso8601, int time_len,
                              void* cece_core_data_ptr, int* rc);

void cece_driver_destroy(void* driver_ptr);
}

#endif // CECE_DRIVER_FACADE_HPP
```

- [ ] **Step 2: Write `src/driver/cece_driver_facade.cpp`**
Implement the orchestrator and wrappers, pulling the AMIO/AXIS regridding loop inside `AdvanceTime`:
```cpp
#include "cece/cece_driver_facade.hpp"
#include <iostream>
#include <fstream>
#include <dagr/dagr.hpp>
#include <amio/amio.h>
#include <axis/axis.hpp>
#include <tick/tick.hpp>
#include "cece/cece_internal.hpp"
#include "cece/cece_helm_graph.hpp"
#include "cece/cece_regridder_utils.hpp"

namespace cece {
class CeceDriverOrchestrator {
public:
    CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz,
                           const double* lon_coords, const double* lat_coords)
        : config_file_(config_file), nx_(nx), ny_(ny), nz_(nz),
          target_lons_(lon_coords, lon_coords + nx),
          target_lats_(lat_coords, lat_coords + ny) {
        cece_io_ = std::make_unique<io::CeceIO>();
        cece_io_->Initialize(config_file_);
        CompileHelmGraph(config_file_, dagr_, *cece_io_);
    }

    bool AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr) {
        auto* d = static_cast<CeceInternalData*>(cece_core_data_ptr);
        if (!d) return false;

        // Advance pipeline step
        dagr_->advance_step();
        Kokkos::fence();

        // 1. Perform AMIO reading and AXIS regridding for offline streams
        // (Move the loop from main.cpp to here)
        for (const auto& var_name : cece_io_->GetOutputVarNames()) {
            auto tide_view = cece_io_->GetFieldView(var_name);
            // ... Open file via AMIO, regrid using AXIS to target_lons_ & target_lats_ ...
            // ... and set in the compute engine state ...
            d->ingestor.SetField(var_name, tide_view.data(), nz_, nx_, ny_, nz_);
        }
        return true;
    }

private:
    std::string config_file_;
    int nx_, ny_, nz_;
    std::vector<double> target_lons_;
    std::vector<double> target_lats_;
    std::unique_ptr<dagr::GraphOrchestrator> dagr_;
    std::unique_ptr<io::CeceIO> cece_io_;
};
}

extern "C" {
void cece_driver_create(const char* yaml_path, int path_len,
                        int nx, int ny, int nz,
                        const double* lon_coords, const double* lat_coords,
                        void** driver_ptr_out, int* rc) {
    if (rc) *rc = 0;
    try {
        std::string path(yaml_path, path_len);
        auto* driver = new cece::CeceDriverOrchestrator(path, nx, ny, nz, lon_coords, lat_coords);
        *driver_ptr_out = static_cast<void*>(driver);
    } catch (...) {
        if (rc) *rc = -1;
    }
}

void cece_driver_advance_time(void* driver_ptr,
                              const char* time_iso8601, int time_len,
                              void* cece_core_data_ptr, int* rc) {
    if (rc) *rc = 0;
    try {
        auto* driver = static_cast<cece::CeceDriverOrchestrator*>(driver_ptr);
        std::string t_iso(time_iso8601, time_len);
        bool ok = driver->AdvanceTime(t_iso, cece_core_data_ptr);
        if (!ok && rc) *rc = -1;
    } catch (...) {
        if (rc) *rc = -1;
    }
}

void cece_driver_destroy(void* driver_ptr) {
    if (driver_ptr) {
        delete static_cast<cece::CeceDriverOrchestrator*>(driver_ptr);
    }
}
}
```

- [ ] **Step 3: Run build to compile new library**
Run: `./setup.sh -c "cmake --build build -j2"`
Expected: Successful compile of `libcece_driver.so` containing the facade symbols.

- [ ] **Step 4: Commit**
```bash
git add include/cece/cece_driver_facade.hpp src/driver/cece_driver_facade.cpp
git commit -m "feat: implement CeceDriverOrchestrator class and C-linkage APIs"
```

---

### Task 3: Refactor Standalone C++ Driver (`src/main.cpp`)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `cece_driver_create`, `cece_driver_advance_time`, and `cece_driver_destroy` C-linkage APIs.
- Produces: Fully modularized standalone driver.

- [ ] **Step 1: Simplify `src/main.cpp`**
Replace the heavy manual AMIO and AXIS loops in `src/main.cpp` with the unified facade call:
```cpp
// Remove heavy includes like amio, axis, dagr from main.cpp!
#include <mpi.h>
#include <Kokkos_Core.hpp>
#include "cece/cece_driver_facade.hpp"
// ...

int main(int argc, char* argv[]) {
    // ... setup MPI & Kokkos ...
    void* cece_core_data = nullptr;
    void* cece_driver_data = nullptr;

    cece_core_initialize_p1(&cece_core_data, &rc);
    cece_driver_create(config_file.c_str(), config_file.length(), nx, ny, nz,
                       file_lons.data(), file_lats.data(), &cece_driver_data, &rc);

    // Run loop
    while (sim_time < end_time) {
        std::string time_str = format_iso8601(sim_time);

        // 1. Let cece_driver handle all offline AMIO reading and AXIS regridding:
        cece_driver_advance_time(cece_driver_data, time_str.c_str(), time_str.length(), cece_core_data, &rc);

        // 2. Call the core physics compute step:
        cece_core_run(cece_core_data, hour, day, &rc);
    }

    cece_driver_destroy(cece_driver_data);
    cece_core_finalize(cece_core_data, &rc);
}
```

- [ ] **Step 2: Compile and test standalone**
Run: `./setup.sh -c "cmake --build build -j2 && ./build/bin/cece_standalone_driver"`
Expected: Standalone C++ runs successfully, outputting matched logs.

- [ ] **Step 3: Commit**
```bash
git add src/main.cpp
git commit -m "refactor: simplify main.cpp standalone driver using cece_driver_facade"
```

---

### Task 4: Refactor Standalone NUOPC Cap Driver (`driver.F90`)

**Files:**
- Modify: `standalone_nuopc/driver.F90`

**Interfaces:**
- Consumes: `cece_driver` wrapper C bindings.
- Produces: Coupled standalone orchestrator utilizing identical regridding backend.

- [ ] **Step 1: Add C-bindings interface block to `driver.F90`**
```fortran
  interface
    subroutine cece_driver_create_c(yaml_path, path_len, nx, ny, nz, lon_coords, lat_coords, &
                                    driver_ptr, rc) bind(C, name="cece_driver_create")
      use, intrinsic :: iso_c_binding
      character(kind=c_char), intent(in) :: yaml_path(*)
      integer(c_int), value, intent(in) :: path_len
      integer(c_int), value, intent(in) :: nx, ny, nz
      real(c_double), intent(in) :: lon_coords(*), lat_coords(*)
      type(c_ptr), intent(out) :: driver_ptr
      integer(c_int), intent(out) :: rc
    end subroutine cece_driver_create_c

    subroutine cece_driver_advance_time_c(driver_ptr, time_iso, time_len, core_data_ptr, rc) &
                                          bind(C, name="cece_driver_advance_time")
      use, intrinsic :: iso_c_binding
      type(c_ptr), value, intent(in) :: driver_ptr
      character(kind=c_char), intent(in) :: time_iso(*)
      integer(c_int), value, intent(in) :: time_len
      type(c_ptr), value, intent(in) :: core_data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine cece_driver_advance_time_c

    subroutine cece_driver_destroy_c(driver_ptr) bind(C, name="cece_driver_destroy")
      use, intrinsic :: iso_c_binding
      type(c_ptr), value, intent(in) :: driver_ptr
    end subroutine cece_driver_destroy_c
  end interface
```

- [ ] **Step 2: Update driver run loop to advance `cece_driver`**
Call the shared C++ driver to process all offline streams:
```fortran
  ! Inside NUOPC Run / Step loop:
  call cece_driver_advance_time_c(g_driver_ptr, time_str, len_trim(time_str), g_core_data_ptr, rc)
  if (rc /= 0) then
    call driver_abort("Shared cece_driver ingestion failed", rc)
  end if
```

- [ ] **Step 3: Compile and run test suite**
Run: `./setup.sh -c "cmake --build build -j2 && cd build && ctest --output-on-failure"`
Expected: 100% tests passed.

- [ ] **Step 4: Commit**
```bash
git add standalone_nuopc/driver.F90
git commit -m "refactor: simplify driver.F90 standalone parent using cece_driver_facade"
```

---

### Task 5: Implement CCPP Isolation Verification Test

**Files:**
- Create: `tests/test_ccpp_readiness.cpp`
- Modify: `CMakeLists.txt` (to define test target)

**Interfaces:**
- Consumes: `cece_core` target.
- Produces: Isolated target compile-time validation.

- [ ] **Step 1: Write `tests/test_ccpp_readiness.cpp`**
Create a test that links *only* against `cece_core` and contains NO calls to AMIO, AXIS, or DAGR:
```cpp
#include <gtest/gtest.h>
#include <Kokkos_Core.hpp>
#include "cece/cece_logger.hpp"

// Forward declare core functions
extern "C" {
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
}

TEST(CCPPLinkTest, CompileIsolation) {
    // Assert we can call core APIs with absolutely no symbols from driver libraries
    void* data_ptr = nullptr;
    int rc = -1;

    cece_core_initialize_p1(&data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(data_ptr, nullptr);

    cece_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, 0);
}
```

- [ ] **Step 2: Register test in root `CMakeLists.txt`**
Ensure it links only to `cece_core`:
```cmake
add_executable(test_ccpp_readiness tests/test_ccpp_readiness.cpp)
target_link_libraries(test_ccpp_readiness PRIVATE cece_core GTest::gtest_main)
gtest_discover_tests(test_ccpp_readiness)
```

- [ ] **Step 3: Run the test suite**
Run: `./setup.sh -c "cmake --build build -j2 && cd build && ctest -R test_ccpp_readiness"`
Expected: Test compiles cleanly and passes successfully, confirming compile-time isolation.

- [ ] **Step 4: Commit**
```bash
git add tests/test_ccpp_readiness.cpp CMakeLists.txt
git commit -m "test: add test_ccpp_readiness to verify compile-time isolation of cece_core"
```
