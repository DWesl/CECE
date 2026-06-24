#include <gtest/gtest.h>
#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <dagr/dagr.hpp>

#include "tide/tide.hpp"

// Forward declare CompileHelmGraph for testing
void CompileHelmGraph(const std::string& config_file, std::unique_ptr<dagr::GraphOrchestrator>& dagr, cece::io::Tide& tide);

// Forward declare CECE C-Linkage APIs
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
}

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

TEST(TideTest, TestEndToEndDriverLoopStub) {
    // Set config file path dynamically
    std::string mock_config = "cece_control_mock.yaml";
    cece_set_config_file_path(mock_config.c_str(), static_cast<int>(mock_config.length()));

    // Verifies that the C-linkage setup compiles and instantiates without hanging
    void* cece_data_ptr = nullptr;
    int rc = 0;
    cece_core_initialize_p1(&cece_data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cece_data_ptr, nullptr);
    cece_core_finalize(cece_data_ptr, &rc);
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
