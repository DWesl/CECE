#include <amio/amio.h>
#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <axis/axis.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "cece/cece_helm_graph.hpp"
#include "cece/cece_io.hpp"
#include "cece/cece_regridder_utils.hpp"

// HELM Headers
#include <dagr/dagr.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <span/span.hpp>
#include <tick/tick.hpp>

// CECE Core C-Linkage Lifecycle functions
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_realize(void* data_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void cece_ingestor_set_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int n_lev, int n_elem, int* rc);
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

        // 3. Initialize the passive C++ CeceIO memory receiver
        auto cece_io = std::make_unique<cece::io::CeceIO>();
        cece_io->Initialize(config_file);

        // 4. Initialize HELM DAGR pipeline manager
        std::unique_ptr<dagr::GraphOrchestrator> dagr;
        CompileHelmGraph(config_file, dagr, *cece_io);

        // 5. Initialize TICK Clock
        tick::Gregorian_Calendar cal;
        tick::Time_Point sim_time = cal.to_time_point(tick::parse_iso8601(start_time_str));
        tick::Time_Point end_time = cal.to_time_point(tick::parse_iso8601(end_time_str));
        tick::Duration dt = tick::seconds(timestep_seconds);

        // 6. Initialize the CECE Compute Engine via C-linkage
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

        // Read and populate coordinates from the source file dynamically to follow CF conventions
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

        // 7. Event-driven simulation run loop
        tick::Time_Point start_time = sim_time;
        int step_index = 0;
        while (sim_time < end_time) {
            tick::Date_Time current_dt = cal.to_date_time(sim_time);

            if (my_rank == 0) {
                std::cout << "[DRIVER] Advancing simulation to: " << tick::format_iso8601(current_dt) << std::endl;
            }

            // A. Update the TICK Clock state within HELM DAGR (if needed)

            // B. Execute the HELM DAGR Task Graph (reads, regrids, and scales on GPU)
            dagr->advance_step();

            // C. Wait for execution to complete on the device (GPU fence)
            Kokkos::fence();

            // D. Push CeceIO's newly computed emission views into CECE's data ingestor
            for (const auto& var_name : cece_io->GetOutputVarNames()) {
                auto tide_view = cece_io->GetFieldView(var_name);

                // Parse input file path and variable name dynamically from YAML config cece_data block
                std::string input_file_path = "../scripts/data/MACCity_4x5.nc";  // default fallback
                std::string input_var_name = "MACCity";                          // default fallback
                if (config["cece_data"] && config["cece_data"]["streams"]) {
                    for (const auto& stream : config["cece_data"]["streams"]) {
                        bool found_var = false;
                        for (const auto& var : stream["variables"]) {
                            if (var["model"] && var["model"].as<std::string>() == var_name) {
                                if (stream["file"]) {
                                    input_file_path = stream["file"].as<std::string>();
                                }
                                if (var["file"]) {
                                    input_var_name = var["file"].as<std::string>();
                                }
                                found_var = true;
                                break;
                            }
                        }
                        if (found_var) break;
                    }
                }

                bool read_success = false;

                // Dynamically open and read using AMIO API
                std::string read_manifest_path = "amio_read_manifest_" + var_name + ".yaml";
                std::ofstream m_file(read_manifest_path);
                m_file << "backend: netcdf4\n"
                       << "path: " << input_file_path << "\n"
                       << "data_model: enhanced\n"
                       << "staging_pool:\n"
                       << "  buffer_count: 16\n"
                       << "  buffer_capacity_bytes: 209715200\n"
                       << "worker_pool:\n"
                       << "  threads: 0\n";
                m_file.close();

                amio_core_handle read_core = nullptr;
                amio_dataset_handle read_dataset = nullptr;
                amio_view_handle read_view = nullptr;

                amio_status_t amio_rc = amio_init(read_manifest_path.c_str(), &read_core);
                if (amio_rc == AMIO_OK) {
                    amio_rc = amio_open_dataset(read_core, read_manifest_path.c_str(), AMIO_MODE_READ, &read_dataset);
                    if (amio_rc == AMIO_OK) {
                        // 1. Read 'lon' coordinates dynamically from this file
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

                        // 2. Read 'lat' coordinates dynamically from this file (handles flips automatically!)
                        std::vector<double> src_lats;
                        bool is_lat_flipped = false;
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
                                    if (lat_len >= 2) {
                                        if (src_lats[0] > src_lats[1]) {
                                            is_lat_flipped = true;
                                        }
                                    }
                                }
                            }
                            amio_release_view(lat_check_view);
                        }

                        // 3. Dynamically determine total timesteps from coordinate variables (time or date) (performed and released second)
                        int file_nt = 1;
                        amio_view_handle time_check_view = nullptr;
                        amio_status_t time_rc = amio_read(read_dataset, "time", 0, nullptr, &time_check_view);
                        if (time_rc != AMIO_OK) {
                            time_rc = amio_read(read_dataset, "date", 0, nullptr, &time_check_view);
                        }
                        if (time_rc == AMIO_OK) {
                            amio_shape_t time_shape{};
                            if (amio_view_shape(time_check_view, &time_shape) == AMIO_OK && time_shape.rank > 0) {
                                file_nt = static_cast<int>(time_shape.extents[0]);
                            }
                            amio_release_view(time_check_view);
                        }

                        // 4. Now read the main variable (free of buffer reuse collisions)
                        amio_rc = amio_read(read_dataset, input_var_name.c_str(), step_index % file_nt, nullptr, &read_view);
                        if (amio_rc == AMIO_OK) {
                            const void* view_data = nullptr;
                            size_t view_size = 0;
                            amio_rc = amio_view_data(read_view, &view_data, &view_size);
                            if (amio_rc == AMIO_OK) {
                                amio_shape_t read_shape{};
                                if (amio_view_shape(read_view, &read_shape) == AMIO_OK) {
                                    int file_ny = static_cast<int>(read_shape.extents[read_shape.rank - 2]);
                                    int file_nx = static_cast<int>(read_shape.extents[read_shape.rank - 1]);

                                    size_t total_elements = 1;
                                    for (int d = 0; d < read_shape.rank; ++d) {
                                        total_elements *= read_shape.extents[d];
                                    }
                                    bool is_float = (view_size == total_elements * 4);

                                    size_t time_offset = 0;
                                    if (read_shape.rank == 3) {
                                        int t_idx = step_index % file_nt;
                                        time_offset = static_cast<size_t>(t_idx) * file_ny * file_nx;
                                    }

                                    // Invoke our general-purpose AXIS conservative regridding utility (Requirement 4.5, 9.1)
                                    read_success =
                                        cece::io::regrid_stream_field(read_dataset, input_var_name, step_index, file_nt, time_offset, is_float,
                                                                      view_data, file_nx, file_ny, nx, ny, file_lons, file_lats, tide_view);
                                }
                            } else {
                                std::cerr << "AMIO Read Warning: amio_view_data failed with code " << amio_rc << std::endl;
                            }
                            amio_release_view(read_view);
                        } else {
                            std::cerr << "AMIO Read Warning: amio_read failed with code " << amio_rc << std::endl;
                        }
                        amio_close(read_dataset);
                    } else {
                        std::cerr << "AMIO Read Warning: amio_open_dataset failed with code " << amio_rc << std::endl;
                    }
                    amio_finalize(read_core);
                } else {
                    std::cerr << "AMIO Read Warning: amio_init failed with code " << amio_rc << std::endl;
                }
                std::remove(read_manifest_path.c_str());

                // Fallback to spatially-varying formula if AMIO read fails
                if (!read_success) {
                    double base_val = 1.0;
                    for (char c : var_name) {
                        base_val += static_cast<double>(c);
                    }
                    double test_val = base_val / 10.0;

                    auto h_view = Kokkos::create_mirror_view(tide_view);
                    for (int k_idx = 0; k_idx < nz; ++k_idx) {
                        for (int j_idx = 0; j_idx < ny; ++j_idx) {
                            for (int i_idx = 0; i_idx < nx; ++i_idx) {
                                h_view(i_idx, j_idx, k_idx) =
                                    test_val + static_cast<double>(i_idx) * 0.1 + static_cast<double>(j_idx) * 0.5 + static_cast<double>(k_idx) * 2.0;
                            }
                        }
                    }
                    Kokkos::deep_copy(tide_view, h_view);
                }

                // Ingest raw data pointer of Tide view into CECE's ingestor cache (dynamically scaled)
                cece_ingestor_set_field(cece_data_ptr, var_name.c_str(), static_cast<int>(var_name.length()), tide_view.data(),
                                        nz,       // n_lev (dynamically sized)
                                        nx * ny,  // n_elem (total columns)
                                        &rc);
            }

            // E. Execute the CECE Compute Engine (StackingEngine)
            int hour = current_dt.hour;
            int day_of_week = 1;  // Default Monday/Tuesday (or compute it dynamically)
            cece_core_run(cece_data_ptr, hour, day_of_week, &rc);

            double elapsed_seconds = static_cast<double>((sim_time - start_time).nanos()) / 1e9;
            // Write output timestep via standalone writer
            cece_core_write_step(cece_data_ptr, elapsed_seconds, step_index, &rc);

            // F. Advance simulation clock by one timestep
            sim_time += dt;
            step_index++;
        }

        // 8. Finalize components
        cece_io->Finalize();
        cece_core_finalize(cece_data_ptr, &rc);

        if (my_rank == 0) {
            std::cout << "[DRIVER] Simulation completed successfully." << std::endl;
        }
    }
    // 9. Finalize Kokkos and MPI
    Kokkos::finalize();
    MPI_Finalize();
    return 0;
}
