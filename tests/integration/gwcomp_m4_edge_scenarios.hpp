#ifndef GLASSWYRM_TESTS_GWCOMP_M4_EDGE_SCENARIOS_HPP
#define GLASSWYRM_TESTS_GWCOMP_M4_EDGE_SCENARIOS_HPP

#include <string_view>

// Returns -1 when the scenario is not handled by this module.
int run_gwcomp_m4_edge_scenario(const char* socket, std::string_view scenario);

#endif
