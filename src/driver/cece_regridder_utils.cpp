// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#include "cece/cece_regridder_utils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace cece::io {

axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_axis_mesh(int ni, int nj, const std::vector<double>& lons,
                                                                    const std::vector<double>& lats) {
    size_t n_cells = static_cast<size_t>(ni) * nj;
    Kokkos::View<double*, Kokkos::HostSpace> center_lon("center_lon", n_cells);
    Kokkos::View<double*, Kokkos::HostSpace> center_lat("center_lat", n_cells);

    for (int j = 0; j < nj; ++j) {
        for (int i = 0; i < ni; ++i) {
            size_t idx = static_cast<size_t>(j) * ni + i;
            center_lon(idx) = lons[i];
            center_lat(idx) = lats[j];
        }
    }

    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(ni, nj, center_lon, center_lat, axis::topology::CoordinateSystem::SphericalDeg);

    return grid.to_unstructured();
}

bool build_regrid_plan(amio_dataset_handle read_dataset, int nx, int ny, const std::vector<double>& target_lons,
                       const std::vector<double>& target_lats, const std::string& map_algo, int j0, int j1, RegridPlan& plan) {
    // 1. Read source 'lon' coordinates from the dataset.
    std::vector<double> src_lons;
    amio_view_handle lon_check_view = nullptr;
    if (amio_read(read_dataset, "lon", 0, nullptr, &lon_check_view) == AMIO_OK) {
        const void* lon_data = nullptr;
        size_t lon_size = 0;
        if (amio_view_data(lon_check_view, &lon_data, &lon_size) == AMIO_OK) {
            amio_shape_t lon_shape{};
            if (amio_view_shape(lon_check_view, &lon_shape) == AMIO_OK && lon_shape.rank > 0) {
                int lon_len = static_cast<int>(lon_shape.extents[0]);
                src_lons.resize(lon_len);
                bool is_lon_float = (lon_size == static_cast<size_t>(lon_len) * 4);
                for (int i = 0; i < lon_len; ++i) {
                    src_lons[i] = is_lon_float ? static_cast<const float*>(lon_data)[i] : static_cast<const double*>(lon_data)[i];
                }
            }
        }
        amio_release_view(lon_check_view);
    }

    // 2. Read source 'lat' coordinates from the dataset.
    std::vector<double> src_lats;
    amio_view_handle lat_check_view = nullptr;
    if (amio_read(read_dataset, "lat", 0, nullptr, &lat_check_view) == AMIO_OK) {
        const void* lat_data = nullptr;
        size_t lat_size = 0;
        if (amio_view_data(lat_check_view, &lat_data, &lat_size) == AMIO_OK) {
            amio_shape_t lat_shape{};
            if (amio_view_shape(lat_check_view, &lat_shape) == AMIO_OK && lat_shape.rank > 0) {
                int lat_len = static_cast<int>(lat_shape.extents[0]);
                src_lats.resize(lat_len);
                bool is_lat_float = (lat_size == static_cast<size_t>(lat_len) * 4);
                for (int i = 0; i < lat_len; ++i) {
                    src_lats[i] = is_lat_float ? static_cast<const float*>(lat_data)[i] : static_cast<const double*>(lat_data)[i];
                }
            }
        }
        amio_release_view(lat_check_view);
    }

    if (src_lons.empty() || src_lats.empty()) {
        return false;
    }

    plan.file_nx = static_cast<int>(src_lons.size());
    plan.file_ny = static_cast<int>(src_lats.size());

    if (map_algo == "passthrough") {
        if (nx != plan.file_nx || ny != plan.file_ny) {
            std::cerr << "[DRIVER ERROR] passthrough regridding requested but grid dimensions do not match! "
                      << "Source grid: " << plan.file_nx << "x" << plan.file_ny << ", Target grid: " << nx << "x" << ny << std::endl;
            throw std::runtime_error("passthrough regridding dimension mismatch");
        }
    }

    plan.j0 = j0;
    plan.j1 = j1;

    const int nband = j1 - j0;
    if (nband <= 0) {
        // No destination rows assigned to this rank — nothing to build.
        plan.built = true;
        return true;
    }

    // A. Build the (global) source mesh and the rank-local destination sub-mesh.
    auto src_mesh = build_axis_mesh(plan.file_nx, plan.file_ny, src_lons, src_lats);

    std::vector<double> band_lats(target_lats.begin() + j0, target_lats.begin() + j1);
    auto dst_mesh = build_axis_mesh(nx, nband, target_lons, band_lats);

    // B. Configure weight generation method.
    axis::solver::RegridConfig regrid_cfg;
    regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    if (map_algo == "nearest" || map_algo == "near" || map_algo == "nn") {
        regrid_cfg.method = axis::solver::InterpolationMethod::NearestNeighbor;
    } else if (map_algo == "bilinear" || map_algo == "bilin" || map_algo == "bi") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Bilinear;
    } else if (map_algo == "cubic" || map_algo == "bicubic" || map_algo == "cu") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Bicubic;
    } else if (map_algo == "conss" || map_algo == "conservative2nd" || map_algo == "cons2nd") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Conservative2ndOrder;
    } else if (map_algo == "consd" || map_algo == "conservative" || map_algo == "cons" || map_algo == "conservative1st") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    }
    regrid_cfg.norm_type = axis::solver::NormType::DstArea;
    regrid_cfg.unmapped = axis::solver::UnmappedAction::Ignore;

    // C. Generate the sparse weight matrix once and convert to CSR for fast apply.
    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, regrid_cfg);
    plan.matrix.to_csr();
    plan.built = true;
    return true;
}

bool apply_regrid_plan(const RegridPlan& plan, size_t time_offset, bool is_float, const void* view_data, int file_nx, int file_ny, int nx,
                       std::vector<double>& local_dst) {
    const int nband = plan.j1 - plan.j0;
    local_dst.assign(static_cast<size_t>(nx) * std::max(nband, 0), 0.0);
    if (nband <= 0) {
        return true;  // No rows on this rank.
    }

    // D. Prepare the (global) source field view [file_nx * file_ny].
    Kokkos::View<double*, Kokkos::HostSpace> src_field("src_field", static_cast<size_t>(file_nx) * file_ny);
    const float* float_data = static_cast<const float*>(view_data);
    const double* double_data = static_cast<const double*>(view_data);
    for (int j = 0; j < file_ny; ++j) {
        for (int i = 0; i < file_nx; ++i) {
            size_t src_idx = time_offset + static_cast<size_t>(j) * file_nx + i;
            src_field(static_cast<size_t>(j) * file_nx + i) = is_float ? static_cast<double>(float_data[src_idx]) : double_data[src_idx];
        }
    }

    // E. Apply cached weights to produce the rank-local destination band [nx * nband].
    Kokkos::View<double*, Kokkos::HostSpace> dst_field("dst_field", static_cast<size_t>(nx) * nband);
    axis::field_view<const double, 1> src_view(src_field.data(), static_cast<size_t>(file_nx) * file_ny);
    axis::field_view<double, 1> dst_view(dst_field.data(), static_cast<size_t>(nx) * nband);
    axis::solver::apply(plan.matrix, src_view, dst_view);

    for (size_t k = 0; k < static_cast<size_t>(nx) * nband; ++k) {
        local_dst[k] = dst_field(k);
    }
    return true;
}

}  // namespace cece::io
