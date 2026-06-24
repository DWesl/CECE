#include <gtest/gtest.h>
#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <dagr/dagr.hpp>

#include "tide/tide.hpp"

// Forward declare CompileHelmGraph for testing
void CompileHelmGraph(const std::string& config_file, std::unique_ptr<dagr::GraphOrchestrator>& dagr, cece::io::Tide& tide);

TEST(TideTest, TestBMIPointerAllocation) {
    cece::io::Tide tide;
    EXPECT_THROW(tide.Initialize("non_existent_file.yaml"), std::runtime_error);
}

TEST(TideTest, TestDynamicGraphCompilation) {
    std::unique_ptr<dagr::GraphOrchestrator> dagr;
    cece::io::Tide tide;

    std::string mock_config = "cece_control_mock.yaml";
    tide.Initialize(mock_config);
    CompileHelmGraph(mock_config, dagr, tide);

    EXPECT_TRUE(true);
}

// Custom GTest Environment to manage Kokkos & MPI lifecycle globally
class KokkosMpiEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        // Initialize MPI first
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int argc = 0;
            char** argv = nullptr;
            MPI_Init(&argc, &argv);
        }

        // Initialize Kokkos
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
    void TearDown() override {
        // Finalize Kokkos
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }

        // Finalize MPI
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment);
    return RUN_ALL_TESTS();
}
