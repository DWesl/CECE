#include "cece/cece_helm_graph.hpp"

#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <dagr/pipeline_config.hpp>
#include <fstream>
#include <halo/communicator.hpp>
#include <stdexcept>
#include <tick/duration.hpp>

void CompileHelmGraph(const std::string& config_file, std::unique_ptr<dagr::GraphOrchestrator>& dagr, cece::io::Tide& tide) {
    std::ifstream f(config_file);
    if (!f.good()) {
        throw std::runtime_error("File not found: " + config_file);
    }

    YAML::Node config = YAML::LoadFile(config_file);

    // Build the Pipeline_Config dynamically from standard YAML
    dagr::Pipeline_Config pc;
    pc.max_concurrency = 4;
    pc.deadlock_timeout_s = 30;
    pc.shutdown_timeout_s = 30;

    // Load active variables from Tide and dynamically compile them into HELM Stream Descriptors
    for (const auto& var_name : tide.GetOutputVarNames()) {
        dagr::Stream_Descriptor stream;
        stream.name = var_name;
        stream.temporal_profile = dagr::Temporal_Profile::linear;
        stream.oob_policy = dagr::OutOfBounds_Policy::cycle;
        stream.dataset_path = "data/emissions/" + var_name + ".nc";
        stream.snapshot_interval = tick::seconds(3600);  // 1 hour intervals
        pc.streams.push_back(std::move(stream));
    }

    // Populate the default scheduling tasks list
    pc.task_names.push_back("regrid_and_scale");

    // Instantiating GraphOrchestrator requires world communicator.
    // Wrap MPI_COMM_WORLD in halo::Communicator (which uses MPI)
    halo::Communicator world(MPI_COMM_WORLD);

    // Allocate the GraphOrchestrator
    dagr = std::make_unique<dagr::GraphOrchestrator>(std::move(pc), std::move(world));
}
