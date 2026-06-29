# CECE Core and Driver Layer Split Design Specification

## Overview

This specification details the structural reorganization of the Chemical Emissions Coupling Engine (CECE) into two distinct layers:
1. **`cece_core`**: A stateless, memory-to-memory parallel physics and compute engine that is 100% CCPP-compliant and has zero dependencies on input/output (AMIO), spatial interpolation (AXIS), or scheduling (DAGR).
2. **`cece_driver`**: An orchestration and dataset ingestion facade that manages dynamic reading, regridding, and pipelines for "CECE-only fields" using HELM libraries, feeding the core memory states.

This ensures perfect code sharing and execution symmetry between the standalone C++ driver (`main.cpp`) and the coupled NUOPC cap driver (`driver.F90`), while keeping the core compute kernels lightweight and CCPP-ready.

---

## 1. Directory Structure

To enforce compile-time separation and prevent header leakages, the source codebase will be split into physical subdirectories under `src/`:

```text
src/
├── core/                               <-- Pure Compute & Physics Layer
│   ├── physics/
│   │   ├── bdsnp_kernel.F90
│   │   ├── cece_bdsnp.cpp
│   │   ├── cece_canopy_model.cpp
│   │   ├── cece_dms.cpp
│   │   ├── cece_dust.cpp
│   │   ├── cece_emission_activity.cpp
│   │   ├── cece_fengsha.F90
│   │   ├── cece_fengsha.cpp
│   │   ├── cece_ginoux.cpp
│   │   ├── cece_k14.cpp
│   │   ├── cece_lightning.cpp
│   │   ├── cece_megan.cpp
│   │   ├── cece_megan3.cpp
│   │   ├── cece_native_example.cpp
│   │   ├── cece_soil_nox.cpp
│   │   ├── cece_speciation_config.cpp
│   │   ├── cece_speciation_engine.cpp
│   │   ├── cece_volcano.cpp
│   │   └── (and corresponding Fortran bridges)
│   ├── cece_clock.cpp
│   ├── cece_config_parser.cpp
│   ├── cece_config_path.cpp
│   ├── cece_config_validator.cpp
│   ├── cece_core_field_helpers.cpp
│   ├── cece_core_finalize.cpp
│   ├── ** cece_core_initialize_p1.cpp (Modified to omit TIDE)
│   ├── ** cece_core_initialize_p2.cpp (Modified to accept coordinates)
│   ├── src/cece_core_realize.cpp
│   ├── src/cece_core_run.cpp
│   ├── cece_data_ingestor.cpp
│   ├── cece_diagnostic_manager.cpp
│   ├── cece_physics_factory.cpp
│   ├── cece_provenance.cpp
│   ├── cece_stacking_engine.cpp
│   ├── cece_standalone_writer.cpp
│   └── cece_c_api.cpp
│
├── driver/                             <-- Orchestration & Ingestion Layer
│   ├── cece_helm_graph.cpp
│   ├── cece_regridder_utils.cpp
│   └── cece_driver_facade.cpp          <-- New orchestrator class & C-linkage APIs
│
└── main.cpp                            <-- Standalone driver (calls cece_driver & cece_core)
```

---

## 2. Target Boundaries & CMake Configuration

The root `CMakeLists.txt` will compile two distinct libraries:

```cmake
# --- cece_core Library ---
# Contains zero I/O and zero spatial regridding. CCPP-ready.
add_library(cece_core SHARED ${CECE_CORE_SRCS})
target_link_libraries(
  cece_core
  PUBLIC Kokkos::kokkos HELM::LOGS yaml-cpp
)
if(MPI_FOUND)
  target_link_libraries(cece_core PUBLIC MPI::MPI_CXX)
endif()

# --- cece_driver Library ---
# Handles offline streams via AMIO, AXIS, DAGR, SPAN, and TICK.
add_library(cece_driver SHARED
  src/driver/cece_helm_graph.cpp
  src/driver/cece_regridder_utils.cpp
  src/driver/cece_driver_facade.cpp
)
target_link_libraries(
  cece_driver
  PUBLIC cece_core amio_core HELM::SPAN blend axis HELM::DAGR HELM::HALO HELM::TICK
)
```

---

## 3. Class Design: `CeceDriverOrchestrator`

The new class `CeceDriverOrchestrator` inside `src/driver/cece_driver_facade.cpp` wraps the AMIO, AXIS, and DAGR pipeline:

```cpp
#ifndef CECE_DRIVER_FACADE_HPP
#define CECE_DRIVER_FACADE_HPP

#include <string>
#include <vector>
#include <memory>
#include <dagr/dagr.hpp>
#include "cece/cece_io.hpp"

namespace cece {

class CeceDriverOrchestrator {
public:
    CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz,
                           const double* lon_coords, const double* lat_coords);
    ~CeceDriverOrchestrator() = default;

    // Directs AMIO to read and AXIS to regrid offline datasets for the current step
    bool AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr);

private:
    std::string config_file_;
    int nx_{0}, ny_{0}, nz_{0};
    std::vector<double> target_lons_;
    std::vector<double> target_lats_;

    // HELM orchestrators
    std::unique_ptr<dagr::GraphOrchestrator> dagr_;
    std::unique_ptr<io::CeceIO> cece_io_;
};

} // namespace cece

#endif // CECE_DRIVER_FACADE_HPP
```

---

## 4. Shared C-Linkage Wrappers

These C-style wrapper functions allow both standalone `main.cpp` and standalone NUOPC `driver.F90` to execute identical ingestion pipelines:

```cpp
#include "driver/cece_driver_facade.hpp"
#include "cece/cece_internal.hpp"

extern "C" {

void cece_driver_create(const char* yaml_path, int path_len,
                        int nx, int ny, int nz,
                        const double* lon_coords, const double* lat_coords,
                        void** driver_ptr_out, int* rc) {
    if (rc) *rc = 0;
    try {
        std::string path(yaml_path, path_len);
        auto* driver = new cece::CeceInternalData_HELM_Driver(); // internal wrapper or state
        // Populate and construct GraphOrchestrator...
        *data_ptr_ptr = static_cast<void*>(driver);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_driver_create: " << e.what() << std::endl;
        if (rc) *rc = -1;
    }
}

void cece_driver_advance(void* driver_ptr, void* cece_data_ptr, const char* time_iso, int time_len, int* rc) {
    if (rc) *rc = 0;
    try {
        // 1. Advance the HELM DAGR pipeline
        // 2. Read fields using AMIO and regrid using AXIS
        // 3. Push views to cece_state via cece_ingestor_set_field
    } catch (...) {
        if (rc) *rc = -1;
    }
}

void cece_driver_finalize(void* driver_ptr, int* rc) {
    if (rc) *rc = 0;
    // Release GraphOrchestrator and resources
}

} // extern "C"
```

---

# 5. Pipeline Coordination and Data Ingestion Flow

In both Standalone and Coupled runs, the operations are unified:

1. **Pre-Step Activation:** The parent driver (standalone run loop or parent NUOPC component) decides to advance simulation time to `sim_time`.
2. **Coupled Inputs:** For coupled fields (such as wind velocity or temperature coming from other components), ESMF transfers field data into CECE's `importState` fields, which are automatically synced to device memory.
3. **CECE-Only Ingestion:** For offline inventories, the cap / standalone loop calls:
   `cece_driver_advance_time(driver, "2026-06-28T01:00:00", core_data, &rc)`
   The driver internally:
   - Evaluates if the time is due.
   - Advances `dagr`.
   - Opens files via AMIO and regrids via AXIS.
   - Calls `cece_ingestor_set_field` to cache them in the `cece_core` state.
4. **Execution:** The cap / standalone loop calls `cece_core_run(core_data, hour, day, &rc)`. The core pulls the complete state of combined variables from its local cache, runs physical schemes, and stacks emission layers.

---

## 6. Testing Strategy

We preserve high-signal coverage and ensure no regressions are introduced:
- **Unit & Property Parity:** Ensure `test_properties` and `test_suite_idempotence` pass, asserting that the compute engine's layer stacking remains bit-wise identical when isolated.
- **Verification of CCPP Readiness:** Add a static verification test in `tests/test_ccpp_readiness.cpp` that builds a small C++ target *linking only against `cece_core`*. This verifies that no unresolved symbols to `amio`, `axis`, or `dagr` are introduced.
- **Standalone App Tests:** Run both `cece_standalone_driver` and `cece_nuopc_app` with identical YAML files and assert that their output diagnostics (logs, printed fields) are identical.
