/*
 * Copyright (c) 2018 - 2026 Marcel Walter
 * Copyright (c) 2023 - present Chair for Design Automation, Technical University of Munich
 * All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License
 */

/**
 * @file
 * @brief Comparison experiment: Flood Fill vs. Radial Search with operational domain distance metrics.
 * @author Mateus Vasconcelos (mateusvmv)
 */

#include <fiction/synthesis/truth_tables.hpp>                                  
#include <fiction/technology/sidb/io/read_sqd_layout.hpp>                      
#include <fiction/technology/sidb/model/simulation_parameters.hpp>             
#include <fiction/technology/sidb/simulation/engine.hpp>                       
#include <fiction/technology/sidb/simulation/io/write_operational_domain.hpp>  
#include <fiction/technology/sidb/simulation/logic/is_operational.hpp>
#include <fiction/technology/sidb/simulation/logic/operational_domain.hpp>  

#include <fmt/format.h>  
#include <kitty/dynamic_truth_table.hpp>
#include <mockturtle/utils/stopwatch.hpp>  

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <experiments.hpp>

using namespace fiction;
using namespace fiction::sidb::io;
using namespace fiction::sidb::model;
using namespace fiction::sidb::simulation;
using namespace fiction::sidb::simulation::io;
using namespace fiction::sidb::simulation::logic;
using namespace fiction::synthesis;

int main()
try
{
    // Extended experiment table schema to include operational domain distance metrics
    experiments::experiment<std::string, uint64_t, 
                            uint64_t, double, uint64_t, double, double, // Flood Fill (samples, op %, sim calls, time, distance)
                            uint64_t, double, uint64_t, double, double> // Radial Search (samples, op %, sim calls, time, distance)
        opdomain_exp{
            "Flood Fill vs Radial Search with Domain Distance",
            "Name",
            "#SiDBs",  
            "#Samples (FF)",
            "op. (FF)",
            "sim calls (FF)",
            "t in s (FF)",
            "dist (FF)",  
            "#Samples (Ra)",
            "op. (Ra)",
            "sim calls (Ra)",
            "t in s (Ra)",
            "dist (Ra)"  
    };

    // Simulation parameters
    const simulation_parameters sim_params{2, -0.32};

    // Operational domain sweep parameters
    const operational_domain_params op_domain_params{
        .operational_params = {.sim_params   = sim_params,
                               .sim_engine   = engine::QUICKEXACT,
                               .op_condition = is_operational_params::operational_condition::TOLERATE_KINKS},
        .sweep_dimensions   = {{.dimension = sweep_parameter::EPSILON_R, .min = 1.0, .max = 10.0, .step = 0.05},
                               {.dimension = sweep_parameter::LAMBDA_TF, .min = 1.0, .max = 10.0, .step = 0.05}}};

    const write_operational_domain_params write_op_domain_params{
        .operational_tag     = "1",
        .non_operational_tag = "0",
        .writing_mode        = write_operational_domain_params::sample_writing_mode::ALL_SAMPLES};

    static const std::string folder = fmt::format("{}sidb_gate_libraries/bestagon_gates/", EXPERIMENTS_PATH);

    // Selected subset of Bestagon gates
    static const std::array<std::pair<std::string, std::vector<kitty::dynamic_truth_table>>, 4> gates = {
        std::make_pair("and", std::vector<kitty::dynamic_truth_table>{create_and_tt()}),
        std::make_pair("or", std::vector<kitty::dynamic_truth_table>{create_or_tt()}),
        std::make_pair("xor", std::vector<kitty::dynamic_truth_table>{create_xor_tt()}),
        std::make_pair("wire", std::vector<kitty::dynamic_truth_table>{create_id_tt()})};

    for (const auto& [gate, truth_table] : gates)
    {
        auto lyt = read_sqd_layout(fmt::format("{}/{}.sqd", folder, gate), gate);

        operational_domain_stats op_domain_stats_ff{};
        operational_domain_stats op_domain_stats_ra{};

        // 1. Run Flood Fill to map the domain
        const auto op_domain_ff =
            operational_domain_flood_fill(lyt, truth_table, 250, op_domain_params, &op_domain_stats_ff);

        // Find the central parameter point of the flood-fill domain
        const parameter_point center_point = op_domain_ff.domain_center();

        // 2. Run Radial Search starting from the computed center point
        const auto op_domain_ra =
            operational_domain_radial_search(lyt, truth_table, center_point, op_domain_params, &op_domain_stats_ra);

        // Write outputs to CSV files
        write_operational_domain(op_domain_ff,
                                 fmt::format("{}operational_domain_flood_fill_distance_test_{}.csv", folder, gate),
                                 write_op_domain_params);
        write_operational_domain(op_domain_ra,
                                 fmt::format("{}operational_domain_radial_search_distance_test_{}.csv", folder, gate),
                                 write_op_domain_params);

        // Compute operational percentages
        const auto operational_percentage_ff =
            static_cast<double>(op_domain_stats_ff.num_operational_parameter_combinations) /
            static_cast<double>(op_domain_stats_ff.num_evaluated_parameter_combinations);

        const auto operational_percentage_ra =
            static_cast<double>(op_domain_stats_ra.num_operational_parameter_combinations) /
            static_cast<double>(op_domain_stats_ra.num_evaluated_parameter_combinations);

        // Extract operational domain distance metrics (e.g., maximum radius or distance to border from center)
        const double distance_ff = op_domain_ff.domain_distance(center_point);
        const double distance_ra = op_domain_ra.domain_distance(center_point);

        // Record metrics into the experiment table
        opdomain_exp(
            gate, lyt.num_dots(),

            // Flood Fill stats + distance
            op_domain_stats_ff.num_evaluated_parameter_combinations, operational_percentage_ff,
            op_domain_stats_ff.num_simulator_invocations, mockturtle::to_seconds(op_domain_stats_ff.time_total),
            distance_ff,

            // Radial Search stats + distance
            op_domain_stats_ra.num_evaluated_parameter_combinations, operational_percentage_ra,
            op_domain_stats_ra.num_simulator_invocations, mockturtle::to_seconds(op_domain_stats_ra.time_total),
            distance_ra
        );

        opdomain_exp.save();
        opdomain_exp.table();
    }

    return EXIT_SUCCESS;
}
catch (const std::exception& exception)
{
    static_cast<void>(std::fputs("[e] Experiment failed: ", stderr));
    static_cast<void>(std::fputs(exception.what(), stderr));
    static_cast<void>(std::fputc('\n', stderr));
    return EXIT_FAILURE;
}
