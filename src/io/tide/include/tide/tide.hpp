#ifndef CECE_TIDE_HPP
#define CECE_TIDE_HPP

#include <Kokkos_Core.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cece {
namespace io {

class Tide {
   public:
    Tide() = default;
    ~Tide() = default;

    void Initialize(const std::string& config_file);
    void Update() {}
    void UpdateUntil(double) {}
    void Finalize();

    std::vector<std::string> GetOutputVarNames() const {
        return var_names_;
    }
    void GetValuePtr(const std::string& name, void** ptr);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> GetFieldView(const std::string& name);

   private:
    int nx_ = 0, ny_ = 0, nz_ = 1;
    using DeviceView = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;
    std::unordered_map<std::string, DeviceView> field_views_;
    std::vector<std::string> var_names_;
};

}  // namespace io
}  // namespace cece

#endif  // CECE_TIDE_HPP
