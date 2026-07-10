/**
 * @file cece_config_parser.cpp
 * @brief Implementation of the YAML configuration parser for CECE.
 *
 * This module handles parsing of YAML configuration files containing:
 * - Species definitions and emission layer configurations
 * - Physics scheme parameters and settings
 * - Grid and timing configuration options
 * - Data stream specifications for TIDE integration
 *
 * The parser provides robust error handling, validation, and default value
 * management to ensure configuration consistency across CECE components.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include <sys/stat.h>
#include <yaml-cpp/yaml.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"

namespace cece {

/**
 * @brief Parses the CECE configuration from a YAML file.
 *
 * This function reads species definitions, emission layers, and physics scheme
 * configurations from the specified file.
 *
 * @param filename Path to the YAML configuration file.
 * @return CeceConfig object containing all parsed information.
 */
CeceConfig ParseConfig(const std::string& filename) {
    std::cout << "DEBUG: ParseConfig called with filename: '" << filename << "'" << std::endl;

    // Check if file exists
    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0) {
        std::cout << "ERROR: File does not exist: " << filename << std::endl;
        throw std::runtime_error("File not found: " + filename);
    }
    std::cout << "DEBUG: File exists, proceeding to load" << std::endl;

    CeceConfig config;
    YAML::Node root = YAML::LoadFile(filename);

    // Parse species and their associated emission layers
    if (root["species"]) {
        for (auto const& species_node : root["species"]) {
            std::string species_name = species_node.first.as<std::string>();
            std::vector<EmissionLayer> layers;

            for (auto const& layer_node : species_node.second) {
                EmissionLayer layer;
                layer.operation = layer_node["operation"].as<std::string>();
                layer.field_name = layer_node["field"].as<std::string>();
                if (layer_node["mask"]) {
                    if (layer_node["mask"].IsSequence()) {
                        for (auto const& m : layer_node["mask"]) {
                            layer.masks.push_back(m.as<std::string>());
                        }
                    } else {
                        layer.masks.push_back(layer_node["mask"].as<std::string>());
                    }
                }
                if (layer_node["scale"]) {
                    layer.scale = layer_node["scale"].as<double>();
                }
                if (layer_node["hierarchy"]) {
                    layer.hierarchy = layer_node["hierarchy"].as<int>();
                }
                if (layer_node["category"]) {
                    layer.category = layer_node["category"].as<std::string>();
                }
                if (layer_node["scale_fields"]) {
                    for (auto const& sf_node : layer_node["scale_fields"]) {
                        layer.scale_fields.push_back(sf_node.as<std::string>());
                    }
                }
                if (layer_node["diurnal_cycle"]) {
                    layer.diurnal_cycle = layer_node["diurnal_cycle"].as<std::string>();
                }
                if (layer_node["weekly_cycle"]) {
                    layer.weekly_cycle = layer_node["weekly_cycle"].as<std::string>();
                }
                if (layer_node["seasonal_cycle"]) {
                    layer.seasonal_cycle = layer_node["seasonal_cycle"].as<std::string>();
                }
                if (layer_node["vdist"]) {
                    auto vdist = layer_node["vdist"];
                    if (vdist["method"]) {
                        std::string method_str = vdist["method"].as<std::string>();
                        if (method_str == "range") {
                            layer.vdist_method = VerticalDistributionMethod::RANGE;
                        } else if (method_str == "pressure") {
                            layer.vdist_method = VerticalDistributionMethod::PRESSURE;
                        } else if (method_str == "height") {
                            layer.vdist_method = VerticalDistributionMethod::HEIGHT;
                        } else if (method_str == "pbl") {
                            layer.vdist_method = VerticalDistributionMethod::PBL;
                        } else {
                            layer.vdist_method = VerticalDistributionMethod::SINGLE;
                        }
                    }
                    if (vdist["layer_start"]) {
                        layer.vdist_layer_start = vdist["layer_start"].as<int>();
                    }
                    if (vdist["layer_end"]) {
                        layer.vdist_layer_end = vdist["layer_end"].as<int>();
                    }
                    if (vdist["p_start"]) {
                        layer.vdist_p_start = vdist["p_start"].as<double>();
                    }
                    if (vdist["p_end"]) {
                        layer.vdist_p_end = vdist["p_end"].as<double>();
                    }
                    if (vdist["h_start"]) {
                        layer.vdist_h_start = vdist["h_start"].as<double>();
                    }
                    if (vdist["h_end"]) {
                        layer.vdist_h_end = vdist["h_end"].as<double>();
                    }
                }
                layers.push_back(layer);
            }
            config.species_layers[species_name] = layers;
        }
    }

    // Parse meteorology mapping
    if (root["meteorology"]) {
        for (auto const& met_node : root["meteorology"]) {
            config.met_mapping[met_node.first.as<std::string>()] = met_node.second.as<std::string>();
        }
    }

    // Parse meteorology registry (internal name -> list of external aliases)
    if (root["met_registry"]) {
        for (auto const& reg_node : root["met_registry"]) {
            std::string internal_name = reg_node.first.as<std::string>();
            std::vector<std::string> aliases;
            if (reg_node.second.IsSequence()) {
                for (auto const& alias : reg_node.second) {
                    aliases.push_back(alias.as<std::string>());
                }
            } else if (reg_node.second.IsScalar()) {
                aliases.push_back(reg_node.second.as<std::string>());
            }
            config.met_registry[internal_name] = std::move(aliases);
        }
    }

    // Parse scale factor mapping
    if (root["scale_factors"]) {
        for (auto const& sf_node : root["scale_factors"]) {
            config.scale_factor_mapping[sf_node.first.as<std::string>()] = sf_node.second.as<std::string>();
        }
    }

    // Parse mask mapping
    if (root["masks"]) {
        for (auto const& mask_node : root["masks"]) {
            config.mask_mapping[mask_node.first.as<std::string>()] = mask_node.second.as<std::string>();
        }
    }

    // Parse temporal cycles (backward compatibility)
    if (root["temporal_cycles"]) {
        for (auto const& cycle_node : root["temporal_cycles"]) {
            std::string cycle_name = cycle_node.first.as<std::string>();
            TemporalCycle cycle;
            if (cycle_node.second.IsSequence()) {
                for (auto const& factor : cycle_node.second) {
                    cycle.factors.push_back(factor.as<double>());
                }
            }
            config.temporal_cycles[cycle_name] = cycle;
        }
    }

    // Parse temporal profiles
    if (root["temporal_profiles"]) {
        for (auto const& profile_node : root["temporal_profiles"]) {
            std::string profile_name = profile_node.first.as<std::string>();
            TemporalCycle profile;
            if (profile_node.second.IsSequence()) {
                for (auto const& factor : profile_node.second) {
                    profile.factors.push_back(factor.as<double>());
                }
            }
            config.temporal_profiles[profile_name] = profile;
        }
    }

    // Parse physics schemes and their options
    if (root["physics_schemes"]) {
        for (auto const& scheme_node : root["physics_schemes"]) {
            PhysicsSchemeConfig scheme;
            scheme.name = scheme_node["name"].as<std::string>();
            if (scheme_node["language"]) {
                scheme.language = scheme_node["language"].as<std::string>();
            }
            if (scheme_node["options"]) {
                scheme.options = scheme_node["options"];
            }
            if (scheme_node["refresh_interval_seconds"]) {
                scheme.refresh_interval_seconds = scheme_node["refresh_interval_seconds"].as<int>();
            }
            config.physics_schemes.push_back(scheme);
        }
    }

    // Parse diagnostics
    if (root["diagnostics"]) {
        auto diag_node = root["diagnostics"];
        if (diag_node.IsSequence()) {
            // Backward compatibility for simple list
            for (auto const& item : diag_node) {
                config.diagnostics.variables.push_back(item.as<std::string>());
            }
        } else if (diag_node.IsMap()) {
            if (diag_node["output_interval"]) {
                config.diagnostics.output_interval_seconds = diag_node["output_interval"].as<int>();
            }
            if (diag_node["grid_type"]) {
                config.diagnostics.grid_type = diag_node["grid_type"].as<std::string>();
            }
            if (diag_node["grid_file"]) {
                config.diagnostics.grid_file = diag_node["grid_file"].as<std::string>();
            }
            if (diag_node["nx"]) {
                config.diagnostics.nx = diag_node["nx"].as<int>();
            }
            if (diag_node["ny"]) {
                config.diagnostics.ny = diag_node["ny"].as<int>();
            }
            if (diag_node["variables"]) {
                for (auto const& var_node : diag_node["variables"]) {
                    config.diagnostics.variables.push_back(var_node.as<std::string>());
                }
            }
        }
    }

    // Parse vertical grid configuration
    if (root["vertical_grid"]) {
        auto v_node = root["vertical_grid"];
        if (v_node["type"]) {
            std::string type_str = v_node["type"].as<std::string>();
            if (type_str == "fv3") {
                config.vertical_config.type = VerticalCoordType::FV3;
            } else if (type_str == "mpas") {
                config.vertical_config.type = VerticalCoordType::MPAS;
            } else if (type_str == "wrf") {
                config.vertical_config.type = VerticalCoordType::WRF;
            }
        }
        if (v_node["ak_field"]) {
            config.vertical_config.ak_field = v_node["ak_field"].as<std::string>();
        }
        if (v_node["bk_field"]) {
            config.vertical_config.bk_field = v_node["bk_field"].as<std::string>();
        }
        if (v_node["p_surf_field"]) {
            config.vertical_config.p_surf_field = v_node["p_surf_field"].as<std::string>();
        }
        if (v_node["z_field"]) {
            config.vertical_config.z_field = v_node["z_field"].as<std::string>();
        }
        if (v_node["pbl_field"]) {
            config.vertical_config.pbl_field = v_node["pbl_field"].as<std::string>();
        }
    }

    // Parse cece_data configuration
    YAML::Node data_node;
    if (root["cece_data"]) {
        data_node = root["cece_data"];
    }
    if (data_node && data_node["debug_level"]) {
        config.cece_data.debug_level = data_node["debug_level"].as<int>();
    }
    if (data_node && data_node["streams"]) {
        YAML::Node shortnames_node;
        try {
            shortnames_node = YAML::LoadFile("data/grib2_shortnames.yaml");
        } catch (...) {
            // Silently fall back to hardcoded mappings if file is not found/invalid
        }

        for (auto const& stream_node : data_node["streams"]) {
            CeceDataStreamConfig stream;
            if (stream_node["name"]) {
                stream.name = stream_node["name"].as<std::string>();
            }

            if (stream_node["file"]) {
                if (stream_node["file"].IsSequence()) {
                    for (auto const& f : stream_node["file"]) {
                        stream.file_paths.push_back(f.as<std::string>());
                    }
                } else {
                    stream.file_paths.push_back(stream_node["file"].as<std::string>());
                }
            }

            if (stream_node["variables"]) {
                for (auto const& var_node : stream_node["variables"]) {
                    CeceDataVariableConfig var;
                    if (var_node.IsScalar()) {
                        var.name_in_file = var_node.as<std::string>();
                        var.name_in_model = var.name_in_file;
                    } else if (var_node.IsMap()) {
                        if (var_node["file"]) {
                            var.name_in_file = var_node["file"].as<std::string>();
                        }
                        if (var_node["model"]) {
                            var.name_in_model = var_node["model"].as<std::string>();
                        }

                        // Enhanced human-readable GRIB2 parameter synthesis
                        bool is_grib2_spec = false;
                        int discipline = 0;
                        int category = 0;
                        int parameter = 0;
                        int level_type = 103;  // height above ground by default
                        int level_value = 0;

                        if (var_node["discipline"]) {
                            discipline = var_node["discipline"].as<int>();
                            is_grib2_spec = true;
                        }
                        if (var_node["category"]) {
                            category = var_node["category"].as<int>();
                            is_grib2_spec = true;
                        } else if (var_node["parameter_category"]) {
                            category = var_node["parameter_category"].as<int>();
                            is_grib2_spec = true;
                        }
                        if (var_node["parameter"]) {
                            parameter = var_node["parameter"].as<int>();
                            is_grib2_spec = true;
                        } else if (var_node["parameter_number"]) {
                            parameter = var_node["parameter_number"].as<int>();
                            is_grib2_spec = true;
                        }

                        // Handle human-readable short_name lookup with dictionary fallback
                        if (var_node["short_name"]) {
                            std::string sname = var_node["short_name"].as<std::string>();
                            is_grib2_spec = true;
                            if (sname != "levels" && shortnames_node && shortnames_node[sname]) {
                                auto match = shortnames_node[sname];
                                if (match["discipline"]) discipline = match["discipline"].as<int>();
                                if (match["category"]) category = match["category"].as<int>();
                                if (match["parameter"]) parameter = match["parameter"].as<int>();
                            } else {
                                // Inline hardcoded fallback
                                if (sname == "TMP" || sname == "TSOIL" || sname == "T2M") {
                                    discipline = 0;
                                    category = 0;
                                    parameter = 0;
                                } else if (sname == "UGRD" || sname == "U10M") {
                                    discipline = 0;
                                    category = 2;
                                    parameter = 2;
                                } else if (sname == "VGRD" || sname == "V10M") {
                                    discipline = 0;
                                    category = 2;
                                    parameter = 3;
                                } else if (sname == "GUST") {
                                    discipline = 0;
                                    category = 2;
                                    parameter = 22;
                                } else if (sname == "HGT") {
                                    discipline = 0;
                                    category = 3;
                                    parameter = 5;
                                } else if (sname == "PRES") {
                                    discipline = 0;
                                    category = 3;
                                    parameter = 0;
                                } else if (sname == "PRMSL") {
                                    discipline = 0;
                                    category = 3;
                                    parameter = 1;
                                } else if (sname == "RH") {
                                    discipline = 0;
                                    category = 1;
                                    parameter = 1;
                                } else if (sname == "SPFH") {
                                    discipline = 0;
                                    category = 1;
                                    parameter = 0;
                                } else if (sname == "LAND" || sname == "land_mask") {
                                    discipline = 2;
                                    category = 0;
                                    parameter = 0;
                                }
                            }
                        }

                        // Parse Level metadata
                        if (var_node["level"]) {
                            std::string lvl = var_node["level"].as<std::string>();
                            is_grib2_spec = true;
                            if (lvl == "surface") {
                                level_type = 1;
                                level_value = 0;
                            } else if (lvl.find("m above ground") != std::string::npos) {
                                level_type = 103;
                                std::stringstream ss(lvl);
                                ss >> level_value;
                            } else if (lvl.find("mb") != std::string::npos || lvl.find("hPa") != std::string::npos) {
                                level_type = 100;
                                std::stringstream ss(lvl);
                                double val = 0;
                                ss >> val;
                                level_value = static_cast<int>(val * 100.0);
                            } else if (lvl.find("Pa") != std::string::npos) {
                                level_type = 100;
                                std::stringstream ss(lvl);
                                ss >> level_value;
                            }
                        }
                        if (var_node["level_type"]) {
                            std::string ltype_str = var_node["level_type"].as<std::string>();
                            is_grib2_spec = true;
                            if (shortnames_node && shortnames_node["levels"] && shortnames_node["levels"][ltype_str]) {
                                level_type = shortnames_node["levels"][ltype_str].as<int>();
                            } else {
                                try {
                                    level_type = std::stoi(ltype_str);
                                } catch (...) {
                                }
                            }
                        } else if (var_node["type_of_first_fixed_surface"]) {
                            std::string ltype_str = var_node["type_of_first_fixed_surface"].as<std::string>();
                            is_grib2_spec = true;
                            if (shortnames_node && shortnames_node["levels"] && shortnames_node["levels"][ltype_str]) {
                                level_type = shortnames_node["levels"][ltype_str].as<int>();
                            } else {
                                try {
                                    level_type = std::stoi(ltype_str);
                                } catch (...) {
                                }
                            }
                        }
                        if (var_node["level_value"]) {
                            level_value = var_node["level_value"].as<int>();
                            is_grib2_spec = true;
                        } else if (var_node["scaled_value_first_surface"]) {
                            level_value = var_node["scaled_value_first_surface"].as<int>();
                            is_grib2_spec = true;
                        }

                        if (is_grib2_spec) {
                            std::string synthesized = "d" + std::to_string(discipline) + "_c" + std::to_string(category) + "_n" +
                                                      std::to_string(parameter) + "_s" + std::to_string(level_type) + "_l" +
                                                      std::to_string(level_value);

                            // Handle Aerosols/Chemical Suffixes (tricky segment parsing)
                            int pdt_number = 0;
                            if (var_node["pdt_number"])
                                pdt_number = var_node["pdt_number"].as<int>();
                            else if (var_node["pdt"])
                                pdt_number = var_node["pdt"].as<int>();

                            if (var_node["chemical_constituent_type"]) {
                                int ct = var_node["chemical_constituent_type"].as<int>();
                                synthesized += "_ct" + std::to_string(ct);
                            } else if (var_node["chemical_constituent"]) {
                                int ct = var_node["chemical_constituent"].as<int>();
                                synthesized += "_ct" + std::to_string(ct);
                            } else if (pdt_number == 40 && var_node["constituent"]) {
                                int ct = var_node["constituent"].as<int>();
                                synthesized += "_ct" + std::to_string(ct);
                            }

                            int aerosol_type = -1;
                            if (var_node["aerosol_type"])
                                aerosol_type = var_node["aerosol_type"].as<int>();
                            else if (var_node["aerosol"])
                                aerosol_type = var_node["aerosol"].as<int>();

                            if (aerosol_type >= 0) {
                                synthesized += "_at" + std::to_string(aerosol_type);
                            }

                            if (var_node["optical_property_type"]) {
                                int op = var_node["optical_property_type"].as<int>();
                                synthesized += "_op" + std::to_string(op);
                            } else if (var_node["optical_property"]) {
                                int op = var_node["optical_property"].as<int>();
                                synthesized += "_op" + std::to_string(op);
                            }

                            if (var_node["wavelength_first_nm"] && var_node["wavelength_last_nm"]) {
                                synthesized += "_wl" + var_node["wavelength_first_nm"].as<std::string>() + "_" +
                                               var_node["wavelength_last_nm"].as<std::string>();
                            } else if (var_node["wavelength_first"] && var_node["wavelength_last"]) {
                                synthesized +=
                                    "_wl" + var_node["wavelength_first"].as<std::string>() + "_" + var_node["wavelength_last"].as<std::string>();
                            }

                            if (var_node["ensemble_perturbation_number"]) {
                                synthesized += "_ep" + var_node["ensemble_perturbation_number"].as<std::string>();
                            } else if (var_node["ensemble_perturbation"]) {
                                synthesized += "_ep" + var_node["ensemble_perturbation"].as<std::string>();
                            }

                            if (var_node["statistical_process"]) {
                                synthesized += "_sp" + var_node["statistical_process"].as<std::string>();
                            } else if (var_node["sp"]) {
                                synthesized += "_sp" + var_node["sp"].as<std::string>();
                            }

                            var.name_in_file = synthesized;
                        }
                    }
                    stream.variables.push_back(var);
                }
            } else if (!stream.name.empty()) {
                CeceDataVariableConfig var;
                var.name_in_file = stream.name;
                var.name_in_model = stream.name;
                stream.variables.push_back(var);
            }

            if (stream_node["taxmode"]) {
                stream.taxmode = stream_node["taxmode"].as<std::string>();
            }
            if (stream_node["tintalgo"]) {
                stream.tintalgo = stream_node["tintalgo"].as<std::string>();
            } else if (stream_node["interpolation"]) {
                stream.tintalgo = stream_node["interpolation"].as<std::string>();
            }
            if (stream_node["mapalgo"]) {
                stream.mapalgo = stream_node["mapalgo"].as<std::string>();
            }
            if (stream_node["dtlimit"]) {
                stream.dtlimit = stream_node["dtlimit"].as<int>();
            }
            if (stream_node["yearFirst"]) {
                stream.yearFirst = stream_node["yearFirst"].as<int>();
            }
            if (stream_node["yearLast"]) {
                stream.yearLast = stream_node["yearLast"].as<int>();
            }
            if (stream_node["yearAlign"]) {
                stream.yearAlign = stream_node["yearAlign"].as<int>();
            }
            if (stream_node["offset"]) {
                stream.offset = stream_node["offset"].as<int>();
            }
            if (stream_node["meshfile"]) {
                stream.meshfile = stream_node["meshfile"].as<std::string>();
            }
            if (stream_node["lev_dimname"]) {
                stream.lev_dimname = stream_node["lev_dimname"].as<std::string>();
            }
            if (stream_node["time_var"]) {
                stream.time_var = stream_node["time_var"].as<std::string>();
            }
            if (stream_node["lon_var"]) {
                stream.lon_var = stream_node["lon_var"].as<std::string>();
            }
            if (stream_node["lat_var"]) {
                stream.lat_var = stream_node["lat_var"].as<std::string>();
            }
            if (stream_node["refresh_interval_seconds"]) {
                stream.refresh_interval_seconds = stream_node["refresh_interval_seconds"].as<int>();
            }

            config.cece_data.streams.push_back(stream);
        }
    }

    // Parse standalone output configuration (Requirement 11.12)
    if (root["output"]) {
        auto out_node = root["output"];
        config.output_config.enabled = true;

        if (out_node["directory"]) {
            config.output_config.directory = out_node["directory"].as<std::string>();
        }
        if (out_node["filename_pattern"]) {
            config.output_config.filename_pattern = out_node["filename_pattern"].as<std::string>();
        }
        if (out_node["frequency_steps"]) {
            config.output_config.frequency_steps = out_node["frequency_steps"].as<int>();
        }
        if (out_node["fields"]) {
            for (auto const& f : out_node["fields"]) {
                config.output_config.fields.push_back(f.as<std::string>());
            }
        }
        if (out_node["diagnostics"]) {
            config.output_config.include_diagnostics = out_node["diagnostics"].as<bool>();
        }
        if (out_node["amio_worker_threads"]) {
            int threads = out_node["amio_worker_threads"].as<int>();
            if (threads < 1) {
                std::cerr << "WARNING: Invalid output amio_worker_threads: " << threads << ". Must be >= 1. Defaulting to 1.\n";
                threads = 1;
            }
            config.output_config.amio_worker_threads = threads;
        }

        // Validate output directory writability; log INFO if it needs to be created.
        // Actual directory creation is deferred to CeceStandaloneWriter::Initialize.
        const std::string& dir = config.output_config.directory;
        struct stat st{};
        if (stat(dir.c_str(), &st) != 0) {
            std::cout << "[CECE INFO] Output directory '" << dir << "' does not exist and will be created at runtime.\n";
        } else if (!(st.st_mode & S_IWUSR)) {
            std::cerr << "[CECE ERROR] Output directory '" << dir << "' is not writable.\n";
        }
    }

    // Parse driver configuration (optional, for standalone execution)
    // Requirements: 1.1, 2.1, 3.1, 14.1, 15.1
    if (root["driver"]) {
        auto driver_node = root["driver"];

        if (driver_node["start_time"]) {
            config.driver_config.start_time = driver_node["start_time"].as<std::string>();
        }
        if (driver_node["end_time"]) {
            config.driver_config.end_time = driver_node["end_time"].as<std::string>();
        }
        if (driver_node["timestep_seconds"]) {
            config.driver_config.timestep_seconds = driver_node["timestep_seconds"].as<int>();
        }
        if (driver_node["gridspec_file"]) {
            config.driver_config.gridspec_file = driver_node["gridspec_file"].as<std::string>();
        }
        if (driver_node["grid"]) {
            auto grid_node = driver_node["grid"];
            if (grid_node["nx"]) {
                config.driver_config.grid.nx = grid_node["nx"].as<int>();
            }
            if (grid_node["ny"]) {
                config.driver_config.grid.ny = grid_node["ny"].as<int>();
            }
            if (grid_node["nz"]) {
                config.driver_config.grid.nz = grid_node["nz"].as<int>();
            }
            if (grid_node["lon_min"]) {
                config.driver_config.grid.lon_min = grid_node["lon_min"].as<double>();
            }
            if (grid_node["lon_max"]) {
                config.driver_config.grid.lon_max = grid_node["lon_max"].as<double>();
            }
            if (grid_node["lat_min"]) {
                config.driver_config.grid.lat_min = grid_node["lat_min"].as<double>();
            }
            if (grid_node["lat_max"]) {
                config.driver_config.grid.lat_max = grid_node["lat_max"].as<double>();
            }
        }
        if (driver_node["stacking_refresh_interval_seconds"]) {
            config.driver_config.stacking_refresh_interval_seconds = driver_node["stacking_refresh_interval_seconds"].as<int>();
        }
        if (driver_node["amio_worker_threads"]) {
            int threads = driver_node["amio_worker_threads"].as<int>();
            if (threads < 1) {
                std::cerr << "WARNING: Invalid amio_worker_threads: " << threads << ". Must be >= 1. Defaulting to 1.\n";
                threads = 1;
            }
            config.driver_config.amio_worker_threads = threads;
        }
    }

    return config;
}

// ---------------------------------------------------------------------------
// Runtime dynamic registration helpers
// ---------------------------------------------------------------------------

/**
 * @brief Adds a new emission species with its layers to an existing config at runtime.
 */
void AddSpecies(CeceConfig& config, const std::string& species_name, std::vector<EmissionLayer> layers) {
    config.species_layers[species_name] = std::move(layers);
}

/**
 * @brief Adds a new scale factor mapping to an existing config at runtime.
 */
void AddScaleFactor(CeceConfig& config, const std::string& internal_name, const std::string& external_name) {
    config.scale_factor_mapping[internal_name] = external_name;
}

/**
 * @brief Adds a new mask mapping to an existing config at runtime.
 */
void AddMask(CeceConfig& config, const std::string& internal_name, const std::string& external_name) {
    config.mask_mapping[internal_name] = external_name;
}

}  // namespace cece
