#!/usr/bin/env python3
"""
Downloads a real NOAA GFS 1.0° GRIB2 file from AWS S3 for weather forcing
and generates a matching auxiliary NetCDF dataset for non-met physical fields.
"""

import os
import sys
import urllib.request
import numpy as np
from netCDF4 import Dataset


def download_gfs_grib2():
    output_dir = "data"
    os.makedirs(output_dir, exist_ok=True)
    grib_path = os.path.join(output_dir, "gfs_data.grb2")

    # Target GFS 1.0° analysis GRIB2 file from July 1, 2026 (guaranteed public S3 Open Data)
    url = "https://noaa-gfs-bdp-pds.s3.amazonaws.com/gfs.20260701/00/atmos/gfs.t00z.pgrb2.1p00.f000"

    if os.path.exists(grib_path):
        print(f"INFO: GFS GRIB2 file already exists at {grib_path}. Skipping download.")
        return grib_path

    print(f"INFO: Downloading real NOAA GFS GRIB2 data from AWS S3: {url}")

    def report_progress(block_num, block_size, total_size):
        read_so_far = block_num * block_size
        if total_size > 0:
            percent = min(100.0, read_so_far * 100.0 / total_size)
            sys.stdout.write(
                f"\rDownloading... {percent:.1f}% ({read_so_far / (1024 * 1024):.1f} MB of {total_size / (1024 * 1024):.1f} MB)"
            )
            sys.stdout.flush()

    try:
        urllib.request.urlretrieve(url, grib_path, reporthook=report_progress)
        print(f"\nSUCCESS: Real GFS GRIB2 saved to {grib_path}")
    except Exception as e:
        print(f"\nERROR: Failed to download GFS data: {e}", file=sys.stderr)
        sys.exit(1)

    return grib_path


def generate_aux_netcdf():
    output_dir = "data"
    os.makedirs(output_dir, exist_ok=True)
    aux_path = os.path.join(output_dir, "physics_static_data.nc")

    # Aligned with standard NOAA GFS 1.0° Global Grid coordinates
    nx, ny, nz = 360, 181, 2
    nt = 3

    lons = np.linspace(-180.0, 180.0, nx, endpoint=False) + 180.0 / nx
    lats = np.linspace(-90.0, 90.0, ny, endpoint=False) + 90.0 / ny
    levels = np.array([1.0, 2.0], dtype=np.float64)
    times = np.array([0.0, 3600.0, 7200.0], dtype=np.float64)  # seconds

    print(f"INFO: Generating matching auxiliary NetCDF dataset at {aux_path}...")

    with Dataset(aux_path, "w", format="NETCDF4") as ds:
        ds.title = "CECE GFS Aligned Auxiliary Input Dataset"
        ds.Conventions = "CF-1.8"

        # Dimensions
        ds.createDimension("time", None)
        ds.createDimension("lon", nx)
        ds.createDimension("lat", ny)
        ds.createDimension("lev", nz)

        # Coordinates
        v_time = ds.createVariable("time", "f8", ("time",))
        v_time[:] = times
        v_time.units = "seconds since 2020-07-01 00:00:00"
        v_time.calendar = "standard"

        v_lon = ds.createVariable("lon", "f8", ("lon",))
        v_lon[:] = lons
        v_lon.units = "degrees_east"
        v_lon.long_name = "longitude"
        v_lon.axis = "X"

        v_lat = ds.createVariable("lat", "f8", ("lat",))
        v_lat[:] = lats
        v_lat.units = "degrees_north"
        v_lat.long_name = "latitude"
        v_lat.axis = "Y"

        v_lev = ds.createVariable("lev", "f8", ("lev",))
        v_lev[:] = levels
        v_lev.units = "level_index"
        v_lev.long_name = "vertical levels"
        v_lev.axis = "Z"

        lon_grid, lat_grid = np.meshgrid(lons, lats)

        # Define Eastern-half land mask to coordinate land vs ocean parameters
        mask_data = np.zeros((nt, nz, ny, nx))
        mask_data[:, :, :, nx // 2 :] = 1.0

        # 1. Marine seawater concentration (for DMS)
        seawater_data = 1.0e-6 * (1.0 - mask_data)  # ocean cells only
        v_sea = ds.createVariable("seawater_conc", "f4", ("time", "lev", "lat", "lon"))
        v_sea[:] = seawater_data
        v_sea.units = "mol/m3"

        # 2. Continuous soil moisture root zone
        moist_data = 0.2 * mask_data  # land cells only
        v_moist = ds.createVariable(
            "soil_moisture", "f4", ("time", "lev", "lat", "lon")
        )
        v_moist[:] = moist_data
        v_moist.units = "m3/m3"

        v_surf_wet = ds.createVariable(
            "surface_soil_wetness", "f4", ("time", "lev", "lat", "lon")
        )
        v_surf_wet[:] = moist_data
        v_surf_wet.units = "m3/m3"

        v_soil_moist_root = ds.createVariable(
            "soil_moisture_root", "f4", ("time", "lev", "lat", "lon")
        )
        v_soil_moist_root[:] = moist_data
        v_soil_moist_root.units = "m3/m3"

        # 3. Desert Erodibility (dust source latitude 15N-35N)
        erod_data = np.zeros((nt, nz, ny, nx))
        for j in range(ny):
            lat = lats[j]
            if 15.0 <= lat <= 35.0:
                erod_data[:, :, j, nx // 2 :] = 0.5
        v_erod = ds.createVariable("erodibility", "f4", ("time", "lev", "lat", "lon"))
        v_erod[:] = erod_data
        v_erod.units = "1"

        v_dust_src = ds.createVariable(
            "dust_source", "f4", ("time", "lev", "lat", "lon")
        )
        v_dust_src[:] = erod_data
        v_dust_src.units = "1"

        # 4. Continuous Vegetation (LAI)
        lai_data = np.zeros((nt, nz, ny, nx))
        for t in range(nt):
            lai_data[t, :, :, :] = (
                np.clip(3.0 * np.cos(lat_grid * np.pi / 180.0), 0.1, 6.0)
                * mask_data[t, :, :, :]
            )

        v_lai = ds.createVariable(
            "leaf_area_index", "f4", ("time", "lev", "lat", "lon")
        )
        v_lai[:] = lai_data
        v_lai.units = "m2/m2"

        v_lai_prev = ds.createVariable(
            "leaf_area_index_prev", "f4", ("time", "lev", "lat", "lon")
        )
        v_lai_prev[:] = lai_data
        v_lai_prev.units = "m2/m2"

        # 5. Diurnal solar indicators
        pardr_data = np.zeros((nt, nz, ny, nx))
        pardf_data = np.zeros((nt, nz, ny, nx))
        suncos_data = np.zeros((nt, nz, ny, nx))
        for t in range(nt):
            hour = (times[t] / 3600.0) % 24
            solar_factor = max(0.0, np.sin((hour - 6) / 12 * np.pi))
            pardr_data[t, :, :, :] = 500.0 * solar_factor
            pardf_data[t, :, :, :] = 100.0 * solar_factor
            suncos_data[t, :, :, :] = solar_factor

        v_pardr = ds.createVariable("par_direct", "f4", ("time", "lev", "lat", "lon"))
        v_pardr[:] = pardr_data
        v_pardr.units = "W/m2"

        v_pardf = ds.createVariable("par_diffuse", "f4", ("time", "lev", "lat", "lon"))
        v_pardf[:] = pardf_data
        v_pardf.units = "W/m2"

        v_suncos = ds.createVariable(
            "solar_cosine", "f4", ("time", "lev", "lat", "lon")
        )
        v_suncos[:] = suncos_data
        v_suncos.units = "1"

        # 6. Granulometric physical soil percentages
        v_clay = ds.createVariable("clay_fraction", "f4", ("time", "lev", "lat", "lon"))
        v_clay[:] = 0.2 * mask_data
        v_clay.units = "1"

        v_sand = ds.createVariable("sand_fraction", "f4", ("time", "lev", "lat", "lon"))
        v_sand[:] = 0.5 * mask_data
        v_sand.units = "1"

        v_silt = ds.createVariable("silt_fraction", "f4", ("time", "lev", "lat", "lon"))
        v_silt[:] = 0.3 * mask_data
        v_silt.units = "1"

        v_drag = ds.createVariable(
            "drag_partition", "f4", ("time", "lev", "lat", "lon")
        )
        v_drag[:] = mask_data
        v_drag.units = "1"

        v_airden = ds.createVariable("air_density", "f4", ("time", "lev", "lat", "lon"))
        v_airden[:] = 1.2 * np.ones((nt, nz, ny, nx))
        v_airden.units = "kg/m3"

        v_fric = ds.createVariable(
            "friction_velocity", "f4", ("time", "lev", "lat", "lon")
        )
        v_fric[:] = 0.2 * mask_data
        v_fric.units = "m/s"

        v_thrs = ds.createVariable(
            "threshold_velocity", "f4", ("time", "lev", "lat", "lon")
        )
        v_thrs[:] = 0.3 * np.ones((nt, nz, ny, nx))
        v_thrs.units = "m/s"

        v_rough = ds.createVariable(
            "roughness_length", "f4", ("time", "lev", "lat", "lon")
        )
        v_rough[:] = 0.05 * mask_data
        v_rough.units = "m"

        v_h = ds.createVariable("height", "f4", ("time", "lev", "lat", "lon"))
        v_h[:] = np.ones((nt, nz, ny, nx))
        v_h.units = "m"

        # 7. Soil textures, categories, indices
        texture_data = np.zeros((nt, nz, ny, nx))
        texture_data[:, :, :, nx // 2 :] = 2.0  # Sandy loam
        v_text = ds.createVariable("soil_texture", "f4", ("time", "lev", "lat", "lon"))
        v_text[:] = texture_data
        v_text.units = "index"

        v_veg_t = ds.createVariable(
            "vegetation_type", "f4", ("time", "lev", "lat", "lon")
        )
        v_veg_t[:] = 4.0 * mask_data  # Deciduous broadleaf forest
        v_veg_t.units = "index"

        v_veg_f = ds.createVariable(
            "vegetation_fraction", "f4", ("time", "lev", "lat", "lon")
        )
        v_veg_f[:] = 0.8 * mask_data
        v_veg_f.units = "1"

        v_ndep = ds.createVariable(
            "nitrogen_deposition", "f4", ("time", "lev", "lat", "lon")
        )
        v_ndep[:] = 1.0e-9 * mask_data
        v_ndep.units = "kg/m2/s"

        v_land_use = ds.createVariable(
            "land_use_type", "f4", ("time", "lev", "lat", "lon")
        )
        v_land_use[:] = 12.0 * mask_data  # Cropland
        v_land_use.units = "index"

        v_biome = ds.createVariable(
            "biome_emission_factors", "f4", ("time", "lev", "lat", "lon")
        )
        v_biome[:] = mask_data
        v_biome.units = "1"

        # 8. Volcanic/Atmospheric properties
        v_cloud = ds.createVariable(
            "cloud_top_height", "f4", ("time", "lev", "lat", "lon")
        )
        v_cloud[:] = 5000.0 * np.ones((nt, nz, ny, nx))
        v_cloud.units = "m"

        v_alt = ds.createVariable(
            "surface_altitude", "f4", ("time", "lev", "lat", "lon")
        )
        v_alt[:] = 100.0 * mask_data
        v_alt.units = "m"

        v_thick = ds.createVariable(
            "layer_thickness", "f4", ("time", "lev", "lat", "lon")
        )
        v_thick[:] = 500.0 * np.ones((nt, nz, ny, nx))
        v_thick.units = "m"

        v_base_nox = ds.createVariable(
            "base_anthropogenic_nox", "f4", ("time", "lev", "lat", "lon")
        )
        v_base_nox[:] = 1.0e-9 * mask_data
        v_base_nox.units = "kg/m2/s"

    print(
        f"SUCCESS: Generated auxiliary static/categorical physics forcing file at {aux_path}"
    )


def main():
    download_gfs_grib2()
    generate_aux_netcdf()


if __name__ == "__main__":
    main()
