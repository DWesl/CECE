#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <iostream>
#include <memory>
#include <string>

#include "cece/cece_helm_graph.hpp"
#include "tide/tide.hpp"

// HELM Headers
#include <dagr/dagr.hpp>
#include <span/span.hpp>
#include <tick/tick.hpp>

// CECE Core C-Linkage Lifecycle functions
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
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

        // 3. Initialize the passive C++ Tide memory receiver
        auto tide = std::make_unique<cece::io::Tide>();
        tide->Initialize(config_file);

        // 4. Initialize HELM DAGR pipeline manager
        std::unique_ptr<dagr::GraphOrchestrator> dagr;
        CompileHelmGraph(config_file, dagr, *tide);

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
        cece_core_initialize_p2(cece_data_ptr, nx, ny, nz, &rc);

        if (my_rank == 0) {
            std::cout << "[DRIVER] Initialization completed on " << nx << "x" << ny << "x" << nz << " grid. Entering run loop..." << std::endl;
        }

        // 7. Event-driven simulation run loop
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

            // D. Push Tide's newly computed emission views into CECE's data ingestor
            for (const auto& var_name : tide->GetOutputVarNames()) {
                auto tide_view = tide->GetFieldView(var_name);

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

            // F. Advance simulation clock by one timestep
            sim_time += dt;
        }

        // 8. Finalize components
        tide->Finalize();
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
