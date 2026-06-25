# Dockerfile
# CECE Development and Verification Environment (Independent of JCSDA/ESMF)
# Based on Ubuntu 24.04 with GCC-13, OpenMPI, NetCDF-C, Kokkos 5.1.1, RapidCheck, and KokkosKernels
FROM ubuntu:24.04

# Prevent interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install Core HPC, C++20 Toolchain, and NetCDF-C
RUN apt-get update && apt-get install -y \
    build-essential \
    g++-13 \
    gcc-13 \
    gfortran-13 \
    cmake \
    ninja-build \
    git \
    wget \
    curl \
    openmpi-bin \
    libopenmpi-dev \
    libnetcdf-dev \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# Set GCC-13 as the default compiler (C, C++, and Fortran)
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && update-alternatives --install /usr/bin/gfortran gfortran /usr/bin/gfortran-13 100

# 2. Compile and Install Google Test globally
RUN cd /usr/src/gtest \
    && cmake CMakeLists.txt \
    && make \
    && cp lib/*.a /usr/lib/ \
    && mkdir -p /usr/local/lib/gtest/ \
    && ln -s /usr/lib/libgtest.a /usr/local/lib/gtest/libgtest.a \
    && ln -s /usr/lib/libgtest_main.a /usr/local/lib/gtest/libgtest_main.a

# 3. Clone and install Kokkos 5.1.1 (C++20 minimum, OpenMP backend)
RUN git clone -b 5.1.1 https://github.com/kokkos/kokkos.git /tmp/kokkos \
    && cd /tmp/kokkos \
    && cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_CXX_STANDARD=20 \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DKokkos_ENABLE_OPENMP=ON \
      -DKokkos_ENABLE_SERIAL=ON \
    && cmake --build build --parallel $(nproc) \
    && cmake --install build \
    && rm -rf /tmp/kokkos

# 4. Clone and install RapidCheck (property-based testing)
RUN git clone https://github.com/emil-e/rapidcheck.git /tmp/rapidcheck \
    && cd /tmp/rapidcheck \
    && cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DRC_ENABLE_GTEST=ON \
      -DRC_INSTALL_ALL_EXTRAS=ON \
    && cmake --build build --parallel $(nproc) \
    && cmake --install build \
    && rm -rf /tmp/rapidcheck

# 5. Clone and install KokkosKernels
RUN git clone --depth 1 https://github.com/kokkos/kokkos-kernels.git /tmp/kokkos-kernels \
    && cd /tmp/kokkos-kernels \
    && cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_CXX_STANDARD=20 \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DKokkosKernels_ENABLE_ALL_COMPONENTS=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_SPARSE=ON \
      -DKokkosKernels_ENABLE_COMPONENT_BLAS=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_GRAPH=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_BATCHED=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_LAPACK=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_ODE=OFF \
      -DKokkosKernels_ADD_DEFAULT_ETI=OFF \
    && cmake --build build --parallel $(nproc) \
    && cmake --install build \
    && rm -rf /tmp/kokkos-kernels

# Set standard environment variables
ENV OMPI_ALLOW_RUN_AS_ROOT=1
ENV OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

# Workspace Setup
WORKDIR /work
CMD ["/bin/bash"]
