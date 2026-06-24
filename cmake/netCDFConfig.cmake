# Dummy netCDFConfig.cmake for JCSDA Docker environment
set(netCDF_FOUND TRUE)
set(NETCDF_FOUND TRUE)
set(netCDF_VERSION "4.9.2")

set(netCDF_INCLUDE_DIRS "/opt/views/view/include")
set(netCDF_LIBRARIES "/opt/views/view/lib/libnetcdf.so")

if(NOT TARGET netCDF::netcdf)
  add_library(netCDF::netcdf SHARED IMPORTED)
  set_target_properties(
    netCDF::netcdf
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${netCDF_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${netCDF_LIBRARIES}"
  )
endif()
