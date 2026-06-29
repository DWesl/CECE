# C++ Architectural Strategy: NUOPC to HELM Transition for CECE & Tide

This document details the architectural strategy to modernize the Community Emissions Computing Engine (CECE) by deprecating the legacy Fortran-based NUOPC single-model driver and replacing it with a pure C++ standalone driver powered by the High-Performance Earth System Library Modules (HELM) initiative.

## 1. Architectural Overview & Component Responsibilities

We employ the **"Strangler Fig" refactoring pattern** to decouple and systematically replace ESMF and PIO with modern C++ structures. The core idea is to transform the `tide` component from an active, file-reading, regridding framework caller into a **passive memory receiver**.

Crucially, the new driver does not execute individual physics schemes directly. Instead, it leverages the unified **CECE Compute Engine** (`cece_core_run`, which wraps `ComputeEmissions` and the optimized `StackingEngine` internally) to execute the stacked physics kernels in vector space on the device.

```
                 +-----------------------------------------------------+
                 |                Unified CECE YAML                    |
                 +---------------------+-------------------------------+
                                       | (Loads config)
                                       v
                 +-----------------------------------------------------+
                 |             Standalone C++ Driver                   |
                 |  - MPI_Init(), Kokkos::initialize()                 |
                 |  - Parses CECE control options & HELM configurations |
                 +---------+---------------------------------+---------+
                           |                                 |
      (Instantiates &      |                                 | (Instantiates &
       initializes)        v                                 v  configures)
            +--------------+------------+             +------+---------------+
            |    CECE Core C++ Lib      |             |    HELM DAGR Engine  |
            |                           |             |                      |
            |  +---------------------+  |             |  +----------------+  |
            |  |    Tide (C++ BMI)   |  |             |  |   AMIO (I/O)   |  |
            |  |                     |  |             |  +--------+-------+  |
            |  | - Allocates Kokkos  |  |             |           |          |
            |  |   Views on Device   |  |             |  +--------v-------+  |
            |  +----------+----------+  |             |  |  AXIS (Regrid) |  |
            |             |             |             |  +--------+-------+  |
            |             | (Exposes    |             |           |          |
            |             |  Pointers via             |  +--------v-------+  |
            |             |  GetValuePtr)             |  |  BLEND (Math)  |  |
            |             |             |             |  +--------+-------+  |
            |             v             |             |           |          |
            |  +----------+----------+  |             |           |          |
            |  |     SPAN Ledger     |<-+-------------+-----------+          |
            |  |                     |  | (Writes directly into target       |
            |  |  [Zero-Copy Ptrs]   |  |  SPAN pointers on Device)          |
            |  +----------+----------+  |                                    |
            |             |             |                                    |
            |             | (Ingested   |                                    |
            |             |  via Bridge)|                                    |
            |             v             |                                    |
            |  +----------+----------+  |                                    |
            |  |  CECE Compute Engine|  |                                    |
            |  |  (StackingEngine)   |  |                                    |
            |  +---------------------+  |                                    |
            +---------------------------+             +----------------------+
```

### Component Breakdown
1.  **Standalone Driver (`main.cpp`):** Manages process context (MPI, Kokkos). Parses unified control files, coordinates the TICK simulation clock, binds Tide raw pointers into HELM's SPAN ledger, and calls the CECE core lifecycle functions.
2.  **C++ Tide Component:** Exposes standard Basic Model Interface (BMI) C++ endpoints. Allocates destination `Kokkos::View` buffers on the execution space and passes raw pointers via `GetValuePtr` to the driver.
3.  **HELM Stack:**
    *   **DAGR (Tier 3 - Control):** Orchestrates tasks in a directed acyclic graph.
    *   **SPAN (Tier 2 - Data):** Tracks zero-copy host and device memory coordinates.
    *   **TICK (Tier 1 - Chronology):** Handles simulation timestamps and calendar math.
    *   **AMIO (Tier 1 - Execution):** Parallel disk-to-device I/O (Zarr/NetCDF).
    *   **AXIS (Tier 1 - Execution):** Device-resident regridding via SpMV (Sparse Matrix-Vector product).
    *   **BLEND (Tier 1 - Execution):** Stateless math (scaling kernels) executing directly on the device memory.

---

## 2. The Build System (CMake)

The build system replaces legacy Fortran/NUOPC compilation with a pure C++ toolchain targeting C++20. HELM is ingested as a Git submodule.

### Main `CMakeLists.txt` Structure
```cmake
cmake_minimum_required(VERSION 3.20)
project(CECE LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Fetch external dependencies ---
include(FetchContent)

# 1. Ingest Kokkos
FetchContent_Declare(kokkos GIT_REPOSITORY https://github.com/kokkos/kokkos.git GIT_TAG 4.2.00)
FetchContent_MakeAvailable(kokkos)

# 2. Ingest yaml-cpp
FetchContent_Declare(yaml-cpp GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git GIT_TAG 0.8.0)
FetchContent_MakeAvailable(yaml-cpp)

# --- Ingest HELM Submodule ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/helm/CMakeLists.txt")
  add_subdirectory(extern/helm EXCLUDE_FROM_ALL)
else()
  message(FATAL_ERROR "HELM submodule not found at extern/helm. Please run 'git submodule update --init --recursive'")
endif()

# --- Compile C++ Tide ---
add_subdirectory(src/io/tide)

# --- Compile CECE Library ---
file(GLOB_RECURSIVE CECE_SOURCES "src/*.cpp")
list(FILTER CECE_SOURCES EXCLUDE REGEX ".*_fortran\\.cpp$")
list(FILTER CECE_SOURCES EXCLUDE REGEX ".*_cap\\.F90$")

add_library(cece_core STATIC ${CECE_SOURCES})

target_include_directories(cece_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/io/tide/include>
)

target_link_libraries(cece_core PUBLIC
    Kokkos::kokkos
    yaml-cpp
    cece_tide
    helm::tick
    helm::span
)

# --- Standalone Driver Executable ---
add_executable(cece_standalone_driver src/main.cpp)
target_link_libraries(cece_standalone_driver PRIVATE
    cece_core
    helm::dagr
    helm::span
    helm::tick
    helm::amio
    helm::axis
    helm::blend
    Kokkos::kokkos
)

find_package(MPI REQUIRED COMPONENTS CXX)
target_link_libraries(cece_standalone_driver PRIVATE MPI::MPI_CXX)
```

---

## 3. Passive Memory Handoff (C++ Tide & BMI Interface)

All `ESMF_FieldRead` and `ESMF_RouteHandle` references are eliminated. Tide allocates its memory layout on the default execution space (CPU/GPU) and registers its raw pointers to the SPAN ledger.

### `tide.hpp` Header Definition
```cpp
#ifndef CECE_TIDE_HPP
#define CECE_TIDE_HPP

#include <Kokkos_Core.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace cece {
namespace io {

class Tide {
public:
    Tide() = default;
    ~Tide() = default;

    // Standard C++ BMI
    void Initialize(const std::string& config_file);
    void Update(); // No-op, execution driven by DAGR
    void UpdateUntil(double time_seconds);
    void Finalize();

    std::vector<std::string> GetOutputVarNames() const { return var_names_; }
    void GetValuePtr(const std::string& name, void** ptr);

    // CECE Access View
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> GetFieldView(const std::string& name);

private:
    int nx_ = 0, ny_ = 0, nz_ = 1;
    using DeviceView = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;
    std::unordered_map<std::string, DeviceView> field_views_;
    std::vector<std::string> var_names_;
};

} // namespace io
} // namespace cece

#endif
```

---

## 4. Initialization & Dynamic Graph Translation

To maximize usability, we compile the high-level `cece_control.yaml` directly into HELM SPAN buffers and DAGR execution tasks behind the scenes. This eliminates the need for low-level HELM task configurations in user inputs.

At the same time, the standalone driver replaces the legacy Fortran-based `NUOPC_Initialize` phase by invoking CECE's C-linkage lifecycle functions (`cece_core_initialize_p1`, `cece_core_realize`, and `cece_core_initialize_p2`) directly, preserving full compatibility with the existing internal engine.

All parameters (grid dimensions, simulation start/end times, and simulation timestep) are parsed dynamically from the YAML config file, ensuring a completely data-driven application.

### The Standalone C++ Driver (`src/main.cpp`)
```cpp
#include <iostream>
#include <memory>
#include <string>
#include <mpi.h>
#include <Kokkos_Core.hpp>
#include <yaml-cpp/yaml.h>

#include "tide/tide.hpp"

// HELM Headers
#include <helm/dagr/dagr.hpp>
#include <helm/span/span.hpp>
#include <helm/tick/tick.hpp>

// CECE Core C-Linkage Lifecycle functions
extern "C" {
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_realize(void* data_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int nx, int ny, int nz, int* rc);
void cece_ingestor_set_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int n_lev, int n_elem, int* rc);
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
}

int main(int argc, char* argv[]) {
    // 1. Initialize MPI
    MPI_Init(&argc, &argv);
    int my_rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    // 2. Initialize Kokkos (allocates execution resources on GPU or CPU)
    Kokkos::initialize(argc, argv);
    {
        std::string config_file = "cece_control.yaml";
        if (argc > 1) {
            config_file = argv[1];
        }

        if (my_rank == 0) {
            std::cout << "[DRIVER] Starting CECE-HELM standalone C++ driver with config: " << config_file << std::endl;
        }

        // --- Dynamic Config Parsing via yaml-cpp ---
        YAML::Node config = YAML::LoadFile(config_file);

        // A. Grid Dimensions
        int nx = config["driver"]["grid"]["nx"].as<int>();
        int ny = config["driver"]["grid"]["ny"].as<int>();
        int nz = config["driver"]["grid"]["nz"].as<int>(1); // Default to 2D (nz=1)

        // B. Simulation Clock Timing
        std::string start_time = config["driver"]["start_time"].as<std::string>();
        std::string end_time   = config["driver"]["end_time"].as<std::string>();
        int timestep_seconds   = config["driver"]["timestep_seconds"].as<int>();
        std::string calendar   = config["driver"]["calendar"].as<std::string>("gregorian");

        // 3. Initialize the passive C++ Tide memory receiver
        auto tide = std::make_unique<cece::io::Tide>();
        tide->Initialize(config_file);

        // 4. Register Tide's allocated device pointers with HELM SPAN
        auto& ledger = helm::span::get_ledger();
        for (const auto& var_name : tide->GetOutputVarNames()) {
            void* raw_device_ptr = nullptr;
            tide->GetValuePtr(var_name, &raw_device_ptr);

            // Map the pointer directly to SPAN (dynamically sized)
            ledger.register_external_pointer(
                var_name,
                static_cast<double*>(raw_device_ptr),
                {nx, ny, nz},                     // Destination shape (dynamically determined)
                helm::span::LayoutType::LayoutLeft, // Match Fortran ordering
                helm::span::MemorySpace::Device     // Located on active device
            );
        }

        // 5. Initialize HELM DAGR pipeline manager
        auto dagr = std::make_unique<helm::dagr::DagrEngine>();
        dagr->LoadPipelineFromYaml(config_file);

        // 6. Initialize TICK Clock (dynamically configured)
        auto sim_clock = std::make_unique<helm::tick::Clock>(
            start_time,
            end_time,
            timestep_seconds,
            calendar
        );

        // 7. Initialize the CECE Compute Engine via C-linkage
        void* cece_data_ptr = nullptr;
        int rc = 0;

        // Phase 1: Allocate internal structures (StackingEngine, DiagnosticManager)
        cece_core_initialize_p1(&cece_data_ptr, &rc);

        // Realize: Validate and lock configuration
        cece_core_realize(cece_data_ptr, &rc);

        // Phase 2: Complete grid-binding (dynamically sized)
        cece_core_initialize_p2(cece_data_ptr, nx, ny, nz, &rc);

        if (my_rank == 0) {
            std::cout << "[DRIVER] Initialization completed on " << nx << "x" << ny << "x" << nz
                      << " grid. Entering run loop..." << std::endl;
        }

        // 8. Event-driven simulation run loop
        while (!sim_clock->IsFinished()) {
            double current_sim_time = sim_clock->GetCurrentTimeSeconds();

            if (my_rank == 0) {
                std::cout << "[DRIVER] Advancing simulation to: " << sim_clock->GetISO8601() << std::endl;
            }

            // A. Update the TICK Clock state within HELM DAGR
            dagr->SetSimulationTime(sim_clock->GetTimeState());

            // B. Execute the HELM DAGR Task Graph (reads, regrids, and scales on GPU)
            dagr->Execute();

            // C. Wait for execution to complete on the device (GPU fence)
            Kokkos::fence();

            // D. Push Tide's newly computed emission views into CECE's data ingestor
            for (const auto& var_name : tide->GetOutputVarNames()) {
                auto tide_view = tide->GetFieldView(var_name);

                // Ingest raw data pointer of Tide view into CECE's ingestor cache (dynamically scaled)
                cece_ingestor_set_field(
                    cece_data_ptr,
                    var_name.c_str(),
                    static_cast<int>(var_name.length()),
                    tide_view.data(),
                    nz, // n_lev (dynamically sized)
                    nx * ny, // n_elem (total columns)
                    &rc
                );
            }

            // E. Execute the CECE Compute Engine (StackingEngine)
            cece_core_run(cece_data_ptr, sim_clock->GetHour(), sim_clock->GetDayOfWeek(), &rc);

            // F. Advance simulation clock by one timestep
            sim_clock->Advance();
        }

        // 9. Finalize components
        tide->Finalize();
        cece_core_finalize(cece_data_ptr, &rc);

        if (my_rank == 0) {
            std::cout << "[DRIVER] Simulation completed successfully." << std::endl;
        }
    }
    // 10. Finalize Kokkos and MPI
    Kokkos::finalize();
    MPI_Finalize();
    return 0;
}
```

---

## 5. The Run Loop & Advanced Temporal Scaling (BLEND Kernels)

TICK drives temporal tracking, outputting current hour and day-of-week coordinates. These dynamic clock states are ingested by BLEND, triggering an optimized parallel MDRange kernel directly on the device memory layout:

```cpp
namespace helm {
namespace blend {

struct ProfileScalingKernel {
    using ViewConst = Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>;
    using ViewOut   = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>;
    using FactorView = Kokkos::View<const double*, Kokkos::DefaultExecutionSpace>;

    ViewConst input_field_;
    ViewOut   output_field_;
    FactorView diurnal_factors_;
    FactorView weekly_factors_;
    int hour_;
    int day_of_week_;

    KOKKOS_INLINE_FUNCTION
    void operator()(const int i, const int j, const int k) const {
        double base_val = input_field_(i, j, k);
        double diurnal_factor = diurnal_factors_(hour_);
        double weekly_factor  = weekly_factors_(day_of_week_);
        output_field_(i, j, k) = base_val * diurnal_factor * weekly_factor;
    }
};

void ExecuteScaling(double* input_ptr, double* output_ptr, int nx, int ny, int nz,
                    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> d_factors,
                    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> w_factors,
                    int hour, int day_of_week) {

    ProfileScalingKernel::ViewConst input_view(input_ptr, nx, ny, nz);
    ProfileScalingKernel::ViewOut   output_view(output_ptr, nx, ny, nz);

    ProfileScalingKernel kernel{
        input_view, output_view, d_factors, w_factors, hour, day_of_week
    };

    Kokkos::parallel_for("HELM_BLEND_TemporalScaling",
        Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
        kernel
    );
    Kokkos::DefaultExecutionSpace().fence();
}

} // namespace blend
} // namespace helm
```

---

## 6. Risk Identification & Mitigation

1.  **Risk: Fortran-to-C++ Memory Layout Misalignment (Array Transpositions)**
    *   *Description:* C++ defaults to row-major layout, while Fortran defaults to column-major. Swap transpositions during regridding/math will scramble the spatial coordinates.
    *   *Mitigation:* Enforce `Kokkos::LayoutLeft` strictly on all views allocated by Tide and registered with HELM SPAN. Create explicit transpose validation unit tests in GTest comparing output matrix structures.
2.  **Risk: GPU Memory Allocation Fragmentation & Latency**
    *   *Description:* Instantiating many separate scratchpad views for variable reading on GPU can lead to dynamic pool exhaustion or performance overheads.
    *   *Mitigation:* Design a static memory pre-allocation map inside HELM SPAN. Reuse raw device buffers for consecutive independent variables sequentially, minimizing the overall device footprint.
3.  **Risk: MPI Communicator Matching & Type Serialization**
    *   *Description:* Mismatched communicator representations between Fortran calling layers and C++ MPI libraries.
    *   *Mitigation:* Fully migrate the driver's environment initialization to C++ `MPI_Init`, standardizing on `MPI_Comm_f2c`/`c2f` conversions if any remaining Fortran utility requires communicating.

---

## 7. Step-by-Step Sprint Plan

### Phase 1: Build Setup & Submodule Integration (Weeks 1-2)
*   Draft new root `CMakeLists.txt` integrating the HELM submodule (`extern/helm`) and basic Kokkos support.
*   Compile an empty `cece_standalone_driver` executable.
*   Verify toolchains compile and link on targeted platforms (GCC/Intel, macOS/Linux).

### Phase 2: C++ Tide BMI Implementation (Weeks 3-4)
*   Delete `tide_mod.F90` and `dshr_strdata_mod.F90`.
*   Implement `Tide` C++ class inside `src/io/tide/src/tide.cpp` with standard BMI methods (`Initialize`, `GetValuePtr`, `Finalize`).
*   Verify Tide allocates `Kokkos::View` layouts correctly on execution spaces.

### Phase 3: Dynamic Graph Translation & TICK Clock Integration (Weeks 5-6)
*   Build the translator logic inside the driver that parses standard CECE YAML configurations and generates SPAN buffers and DAGR pipeline tasks.
*   Integrate TICK as the central source of truth for clock stepping and calendar tracking.
*   Verify AMIO successfully ingests NetCDF coordinates directly.

### Phase 4: Execution Kernels & Parallel Math (Weeks 7-8)
*   Implement optimized `ProfileScalingKernel` within BLEND.
*   Wire the scaling tasks to execute dynamically on Device views based on Tick's hour/day coordinates.
*   Implement `CeceDataIngestor` C++ bridge interface matching Tide's views with physics schemes.

### Phase 5: Verification, Benchmarking, and Parity Checking (Weeks 9-10)
*   Rewrite legacy integration tests (e.g., `test_tide_passthrough.F90`) into C++ using GTest.
*   Execute rigorous numerical parity comparisons between the legacy NUOPC-Fortran run and the new HELM-C++ driver.
*   Benchmark regridding/scaling execution times to verify GPU acceleration scaling factor.
