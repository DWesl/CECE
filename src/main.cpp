#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    Kokkos::initialize(argc, argv);
    {
        std::cout << "[STUB] Driver compiled successfully." << std::endl;
    }
    Kokkos::finalize();
    MPI_Finalize();
    return 0;
}
