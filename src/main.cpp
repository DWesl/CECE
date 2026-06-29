#include <amio/amio.h>
#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <fstream>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_driver_facade.hpp"

// CECE Core C-Linkage Lifecycle functions
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_realize(void* data_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
void cece_core_writer_initialize(void* data_ptr, int nx, int ny, int nz, const char* start_time_iso8601, int start_time_len, int* rc);
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, const double* lat_coords,
                                             const char* start_time_iso8601, int start_time_len, int* rc);
void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc);
void cece_core_set_export_field(void* data_ptr, const char* name, int name_len, const double* field_data, int nx, int ny, int nz, int* rc);
}

int main(int argc, char* argv[]) {
    // 1. Initialize MPI with thread support
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    // 2. Initialize Kokkos (allocates execution resources on GPU or CPU)
    Kokkos::initialize(argc, argv);
    {
        // Initialize the HALO Environment & Communicator
        halo::Environment::initialize();
        halo::Communicator world(MPI_COMM_WORLD);
        const int my_rank = world.rank();

        std::string config_file = "cece_control_mock.yaml";
        if (argc > 1) {
            config_file = argv[1];
        }

        if (my_rank == 0) {
            std::cout << "[DRIVER] Starting CECE-HELM standalone C++ driver with config: " << config_file << std::endl;
        }

        // Set config file path dynamically
        cece_set_config_file_path(config_file.c_str(), static_cast<int>(config_file.length()));

        // --- Dynamic Config Parsing via yaml-cpp ---
        YAML::Node config = YAML::LoadFile(config_file);

        // A. Grid Dimensions
        int nx = config["driver"]["grid"]["nx"].as<int>();
        int ny = config["driver"]["grid"]["ny"].as<int>();
        int nz = config["driver"]["grid"]["nz"].as<int>(1);  // Default to 2D (nz=1)

        // B. Simulation Clock Timing
        std::string start_time_str = config["driver"]["start_time"].as<std::string>();
        std::string end_time_str = config["driver"]["end_time"].as<std::string>();
        int timestep_seconds = config["driver"]["timestep_seconds"].as<int>();

        // 3. Initialize TICK Clock
        tick::Gregorian_Calendar cal;
        tick::Time_Point sim_time = cal.to_time_point(tick::parse_iso8601(start_time_str));
        tick::Time_Point end_time = cal.to_time_point(tick::parse_iso8601(end_time_str));
        tick::Duration dt = tick::seconds(timestep_seconds);

        // 4. Initialize the CECE Compute Engine via C-linkage
        void* cece_data_ptr = nullptr;
        int rc = 0;

        // Phase 1: Allocate internal structures (StackingEngine, DiagnosticManager)
        cece_core_initialize_p1(&cece_data_ptr, &rc);

        // Realize: Validate and lock configuration
        cece_core_realize(cece_data_ptr, &rc);

        // Phase 2: Complete grid-binding (dynamically sized)
        cece_core_initialize_p2(cece_data_ptr, &nx, &ny, &nz, &rc);

        // Register the export fields configured for output
        if (config["output"] && config["output"]["fields"]) {
            for (const auto& field_node : config["output"]["fields"]) {
                std::string field_name = field_node.as<std::string>();
                std::vector<double> field_mem(nx * ny * nz, 0.0);
                cece_core_set_export_field(cece_data_ptr, field_name.c_str(), static_cast<int>(field_name.length()), field_mem.data(), nx, ny, nz,
                                           &rc);
            }
        }

        // Setup coordinate arrays (used as baseline map grid coordinates)
        std::vector<double> file_lons(nx, 0.0);
        std::vector<double> file_lats(ny, 0.0);
        bool has_file_coords = false;

        std::string input_file_path = "../scripts/data/MACCity_4x5.nc";  // default fallback
        if (config["cece_data"] && config["cece_data"]["streams"]) {
            auto stream = config["cece_data"]["streams"][0];
            if (stream["file"]) {
                input_file_path = stream["file"].as<std::string>();
            }
        }

        std::string read_manifest_path = "amio_coord_manifest.yaml";
        std::ofstream m_file_coords(read_manifest_path);
        m_file_coords << "backend: netcdf4\n"
                      << "path: " << input_file_path << "\n"
                      << "data_model: enhanced\n"
                      << "staging_pool:\n"
                      << "  buffer_count: 16\n"
                      << "  buffer_capacity_bytes: 104857600\n"
                      << "worker_pool:\n"
                      << "  threads: 0\n";
        m_file_coords.close();

        amio_core_handle coord_core = nullptr;
        amio_dataset_handle coord_dataset = nullptr;
        amio_view_handle lon_view = nullptr;
        amio_view_handle lat_view = nullptr;

        amio_status_t amio_rc = amio_init(read_manifest_path.c_str(), &coord_core);
        if (amio_rc == AMIO_OK) {
            amio_rc = amio_open_dataset(coord_core, read_manifest_path.c_str(), AMIO_MODE_READ, &coord_dataset);
            if (amio_rc == AMIO_OK) {
                // Read lon coordinate
                if (amio_read(coord_dataset, "lon", 0, nullptr, &lon_view) == AMIO_OK) {
                    const void* view_data = nullptr;
                    size_t view_size = 0;
                    if (amio_view_data(lon_view, &view_data, &view_size) == AMIO_OK) {
                        amio_shape_t lon_shape{};
                        if (amio_view_shape(lon_view, &lon_shape) == AMIO_OK) {
                            int file_nx = static_cast<int>(lon_shape.extents[0]);
                            bool is_float = (view_size == static_cast<size_t>(file_nx) * 4);
                            const float* float_data = static_cast<const float*>(view_data);
                            const double* double_data = static_cast<const double*>(view_data);
                            for (int i = 0; i < nx; ++i) {
                                int src_idx = static_cast<int>(std::round((static_cast<double>(i) / nx) * file_nx));
                                if (src_idx >= file_nx) src_idx = file_nx - 1;
                                file_lons[i] = is_float ? static_cast<double>(float_data[src_idx]) : double_data[src_idx];
                            }
                        }
                    }
                    amio_release_view(lon_view);
                }
                // Read lat coordinate
                if (amio_read(coord_dataset, "lat", 0, nullptr, &lat_view) == AMIO_OK) {
                    const void* view_data = nullptr;
                    size_t view_size = 0;
                    if (amio_view_data(lat_view, &view_data, &view_size) == AMIO_OK) {
                        amio_shape_t lat_shape{};
                        if (amio_view_shape(lat_view, &lat_shape) == AMIO_OK) {
                            int file_ny = static_cast<int>(lat_shape.extents[0]);
                            bool is_float = (view_size == static_cast<size_t>(file_ny) * 4);
                            const float* float_data = static_cast<const float*>(view_data);
                            const double* double_data = static_cast<const double*>(view_data);
                            for (int j = 0; j < ny; ++j) {
                                int src_idx = static_cast<int>(std::round((static_cast<double>(j) / ny) * file_ny));
                                if (src_idx >= file_ny) src_idx = file_ny - 1;
                                file_lats[j] = is_float ? static_cast<double>(float_data[src_idx]) : double_data[src_idx];
                            }
                            has_file_coords = true;
                        }
                    }
                    amio_release_view(lat_view);
                }
                amio_close(coord_dataset);
            }
            amio_finalize(coord_core);
        }
        std::remove(read_manifest_path.c_str());

        // 5. Initialize the cece_driver orchestrator facade
        void* cece_driver_data = nullptr;
        cece_driver_create(config_file.c_str(), static_cast<int>(config_file.length()), nx, ny, nz, file_lons.data(), file_lats.data(),
                           &cece_driver_data, &rc);

        // Standalone Writer: Initialize output writing if configured
        if (has_file_coords) {
            cece_core_writer_initialize_with_coords(cece_data_ptr, nx, ny, nz, file_lons.data(), file_lats.data(), start_time_str.c_str(),
                                                    start_time_str.length(), &rc);
        } else {
            cece_core_writer_initialize(cece_data_ptr, nx, ny, nz, start_time_str.c_str(), start_time_str.length(), &rc);
        }

        if (my_rank == 0) {
            std::cout << "[DRIVER] Initialization completed on " << nx << "x" << ny << "x" << nz << " grid. Entering run loop..." << std::endl;
        }

        // 6. Event-driven simulation run loop
        tick::Time_Point start_time = sim_time;
        int step_index = 0;
        while (sim_time < end_time) {
            tick::Date_Time current_dt = cal.to_date_time(sim_time);

            if (my_rank == 0) {
                std::cout << "[DRIVER] Advancing simulation to: " << tick::format_iso8601(current_dt) << std::endl;
            }

            std::string time_str = tick::format_iso8601(current_dt);

            // A. Let cece_driver handle all offline AMIO reading and AXIS regridding:
            cece_driver_advance_time(cece_driver_data, time_str.c_str(), static_cast<int>(time_str.length()), cece_data_ptr, &rc);

            // B. Execute the CECE Compute Engine
            int hour = current_dt.hour;
            int day_of_week = 1;  // Default Monday/Tuesday
            cece_core_run(cece_data_ptr, hour, day_of_week, &rc);

            double elapsed_seconds = static_cast<double>((sim_time - start_time).nanos()) / 1e9;

            // C. Write output timestep via standalone writer
            cece_core_write_step(cece_data_ptr, elapsed_seconds, step_index, &rc);

            // D. Advance simulation clock by one timestep
            sim_time += dt;
            step_index++;
        }

        // 7. Cleanup and release resources
        if (my_rank == 0) {
            std::cout << "[DRIVER] Standalone execution completed. Cleaning up..." << std::endl;
        }

        cece_driver_destroy(cece_driver_data);
        cece_core_finalize(cece_data_ptr, &rc);
    }
    Kokkos::finalize();
    MPI_Finalize();
    return 0;
}
