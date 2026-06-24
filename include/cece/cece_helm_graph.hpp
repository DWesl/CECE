#ifndef CECE_HELM_GRAPH_HPP
#define CECE_HELM_GRAPH_HPP

#include <dagr/dagr.hpp>
#include <memory>
#include <string>

#include "tide/tide.hpp"

/**
 * @brief Programmatically compiles a high-level CECE streams YAML configuration
 *        into low-level HELM SPAN buffers and DAGR pipeline tasks.
 * @param config_file Path to unified cece_control.yaml file.
 * @param[out] dagr Unique pointer to the allocated dagr::GraphOrchestrator.
 * @param tide Reference to initialized C++ Tide component.
 */
void CompileHelmGraph(const std::string& config_file, std::unique_ptr<dagr::GraphOrchestrator>& dagr, cece::io::Tide& tide);

#endif  // CECE_HELM_GRAPH_HPP
