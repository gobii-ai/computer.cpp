#pragma once

#include "DaemonTargetText.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ComputerCpp {

std::vector<nlohmann::json> RoleTargetCandidates(const std::string& session, const RoleTarget& roleTarget, int limit);
std::vector<nlohmann::json> SnapshotTargetCandidates(const std::string& session, const std::string& query, int limit);

} // namespace ComputerCpp
