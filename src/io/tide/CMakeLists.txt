cmake_minimum_required(VERSION 3.20)

# --- Modern C++ Tide ---
add_library(cece_tide STATIC src/tide.cpp)
target_include_directories(cece_tide PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(cece_tide PUBLIC Kokkos::kokkos yaml-cpp)
