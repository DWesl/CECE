#include "tide/tide.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <stdexcept>

namespace cece {
namespace io {

void Tide::Initialize(const std::string& config_file) {
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

void Tide::GetValuePtr(const std::string& name, void** ptr) {
    auto it = field_views_.find(name);
    if (it == field_views_.end()) {
        throw std::runtime_error("Variable not found: " + name);
    }
    *ptr = static_cast<void*>(it->second.data());
}

Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> Tide::GetFieldView(const std::string& name) {
    return field_views_.at(name);
}

void Tide::Finalize() {
    field_views_.clear();
    var_names_.clear();
}

}  // namespace io
}  // namespace cece
