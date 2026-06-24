# Custom KokkosConfig to satisfy find_package(Kokkos 5.1.1 REQUIRED) when using FetchContent
set(Kokkos_FOUND TRUE)
set(Kokkos_VERSION "5.1.1")
set(Kokkos_VERSION_MAJOR 5)
set(Kokkos_VERSION_MINOR 1)
set(Kokkos_VERSION_PATCH 1)
set(Kokkos_DIR "${CMAKE_BINARY_DIR}/_deps/kokkos-src")

# We alias Kokkos targets if needed, but since they are already defined globally
# by FetchContent, we just declare the package found.
if(NOT TARGET Kokkos::kokkos)
  message(
    WARNING
    "Kokkos::kokkos target not defined yet. Ensure FetchContent_MakeAvailable(kokkos) runs before find_package."
  )
endif()
