# TIDE and IO restucturing and Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simplify the project's directory structure by moving legacy TIDE and IO files directly into the core `src/` and `include/` folders, renaming the legacy `Tide` class to `CeceIO`, and eliminating the separate `cece_tide` static library.

**Architecture:** We will move `src/io/tide/src/tide.cpp` and `src/io/tide/include/tide/tide.hpp` into the core directories and rename them to `src/cece_io.cpp` and `include/cece/cece_io.hpp`, with the class renamed from `Tide` to `CeceIO`. The existing source files under `src/io/` will be moved directly into the `src/` directory. `CMakeLists.txt` will be simplified to build all these source files directly inside the main `cece` library target.

**Tech Stack:** C++, CMake, Kokkos, yaml-cpp

## Global Constraints

- No hardcoded paths, dimensions, or config fields; rely entirely on dynamic configuration.
- Direct NetCDF operations in the driver are prohibited; rely entirely on AMIO or CECE core helpers.
- Run tests and compilation steps under the standard custom dependency container.

---

### Task 1: Move and Rename Tide to CeceIO

**Files:**
- Create: `include/cece/cece_io.hpp`
- Create: `src/cece_io.cpp`
- Delete: `src/io/tide/include/tide/tide.hpp`
- Delete: `src/io/tide/src/tide.cpp`

**Interfaces:**
- Consumes: yaml-cpp, Kokkos
- Produces: `cece::io::CeceIO` class with `Initialize`, `Finalize`, `GetOutputVarNames`, and `GetFieldView` methods.

- [ ] **Step 1: Write the header file `include/cece/cece_io.hpp`**

```cpp
#ifndef CECE_IO_HPP
#define CECE_IO_HPP

#include <Kokkos_Core.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cece {
namespace io {

class CeceIO {
   public:
    CeceIO() = default;
    ~CeceIO() = default;

    void Initialize(const std::string& config_file);
    void Finalize();

    std::vector<std::string> GetOutputVarNames() const {
        return var_names_;
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> GetFieldView(const std::string& name);

   private:
    int nx_ = 0, ny_ = 0, nz_ = 1;
    using DeviceView = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;
    std::unordered_map<std::string, DeviceView> field_views_;
    std::vector<std::string> var_names_;
};

}  // namespace io
}  // namespace cece

#endif  // CECE_IO_HPP
```

- [ ] **Step 2: Write the implementation file `src/cece_io.cpp`**

```cpp
#include "cece/cece_io.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <stdexcept>

namespace cece {
namespace io {

void CeceIO::Initialize(const std::string& config_file) {
    std::ifstream f(config_file);
    if (!f.good()) {
        throw std::runtime_error("File not found: " + config_file);
    }

    YAML::Node config = YAML::LoadFile(config_file);
    nx_ = config["driver"]["grid"]["nx"].as<int>();
    ny_ = config["driver"]["grid"]["ny"].as<int>();
    nz_ = config["driver"]["grid"]["nz"].as<int>(1);

    if (config["cece_data"] && config["cece_data"]["streams"]) {
        for (const auto& stream : config["cece_data"]["streams"]) {
            for (const auto& var : stream["variables"]) {
                std::string var_name = var["model"].as<std::string>();
                var_names_.push_back(var_name);

                DeviceView view(var_name, nx_, ny_, nz_);
                Kokkos::deep_copy(view, 0.0);
                field_views_[var_name] = view;
            }
        }
    }
}

Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> CeceIO::GetFieldView(const std::string& name) {
    return field_views_.at(name);
}

void CeceIO::Finalize() {
    field_views_.clear();
    var_names_.clear();
}

}  // namespace io
}  // namespace cece
```

- [ ] **Step 3: Remove the old files from `src/io/tide/`**

Run: `rm src/io/tide/src/tide.cpp src/io/tide/include/tide/tide.hpp`

---

### Task 2: Move existing src/io/ files directly to src/

**Files:**
- Create: `src/cece_standalone_writer.cpp`
- Create: `src/cece_regridder_utils.cpp`
- Create: `src/cece_data_ingestor.cpp`
- Create: `src/cece_diagnostic_manager.cpp`
- Delete: `src/io/cece_standalone_writer.cpp`
- Delete: `src/io/cece_regridder_utils.cpp`
- Delete: `src/io/cece_data_ingestor.cpp`
- Delete: `src/io/cece_diagnostic_manager.cpp`

**Interfaces:**
- Consumes: Core headers
- Produces: In-place implementations in `src/` core directory.

- [ ] **Step 1: Move files directly using shell commands**

Run: `mv src/io/cece_standalone_writer.cpp src/cece_standalone_writer.cpp && mv src/io/cece_regridder_utils.cpp src/cece_regridder_utils.cpp && mv src/io/cece_data_ingestor.cpp src/cece_data_ingestor.cpp && mv src/io/cece_diagnostic_manager.cpp src/cece_diagnostic_manager.cpp`

---

### Task 3: Update Driver, Helm Graph compilation, and Test references

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/cece/cece_helm_graph.hpp`
- Modify: `src/cece_helm_graph.cpp`
- Modify: `tests/test_tide_cpp.cpp`

**Interfaces:**
- Consumes: `cece/cece_io.hpp`
- Produces: Updated driver loop and helper functions signature matching `cece::io::CeceIO`.

- [ ] **Step 1: Modify `src/main.cpp` to include `cece/cece_io.hpp` and rename `Tide` to `CeceIO`**

Replace `#include "tide/tide.hpp"` with `#include "cece/cece_io.hpp"`, and replace `cece::io::Tide` with `cece::io::CeceIO`.

- [ ] **Step 2: Modify `include/cece/cece_helm_graph.hpp` and `src/cece_helm_graph.cpp` to use `cece/cece_io.hpp`**

Replace `#include "tide/tide.hpp"` with `#include "cece/cece_io.hpp"` and replace any reference of `cece::io::Tide` with `cece::io::CeceIO`.

- [ ] **Step 3: Modify `tests/test_tide_cpp.cpp` to include `cece/cece_io.hpp`**

Replace `#include "tide/tide.hpp"` with `#include "cece/cece_io.hpp"`, replace any reference of `cece::io::Tide` with `cece::io::CeceIO`, and change GTest names if appropriate.

---

### Task 4: Simplify CMakeLists.txt and remove legacy Tide CMake subdirectory

**Files:**
- Modify: `CMakeLists.txt`
- Delete: `src/io/tide/CMakeLists.txt`
- Delete: `src/io/tide/share/` (and remaining legacy TIDE folders)

**Interfaces:**
- Consumes: Updated `CECE_SRCS` structure in main CMake lists.
- Produces: Streamlined standalone library and test binary build configurations.

- [ ] **Step 1: Update `CMakeLists.txt` to remove `src/io/tide` subdirectory and list TIDE & IO files directly under `src/`**

Remove the `add_subdirectory(src/io/tide)` call.
Update the list of source files under `CECE_SRCS` by replacing `src/io/` files with their new locations under `src/` directly, and appending `src/cece_io.cpp`.
Remove any link dependence of `cece` or `test_tide_cpp` on `cece_tide`.

- [ ] **Step 2: Delete legacy directories and clean up `src/io/` completely**

Run: `rm -rf src/io/`

---

### Task 5: Build and verify all unit tests and simulation driver

**Files:**
- Create/Modify: Standalone build output directory (build)

- [ ] **Step 1: Execute CMake configuration inside build directory**

Run: `cd build && cmake ..`

- [ ] **Step 2: Compile the entire target suite**

Run: `cd build && make -j$(nproc)`

- [ ] **Step 3: Run all regression and unit tests to verify parity and success**

Run: `cd build && ctest --output-on-failure`
