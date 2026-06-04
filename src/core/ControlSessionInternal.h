#pragma once

#include "computer_cpp/ControlSession.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

struct sqlite3;

namespace ComputerCpp::ControlSessionInternal {

ControlSessionResult ErrorResult(std::string code, std::string error);
ControlSessionResult RecordResult(const ControlSessionRecord& record);
ControlSessionResult EnsureCurrentHolder(sqlite3* db, const ControlSessionRecord& record, int64_t nowMs);
ControlSessionAcquireOptions NormalizeAcquireOptions(const ControlSessionAcquireOptions& rawOptions);
ControlSessionRecord InsertNewActiveControlSession(
    sqlite3* db,
    const ControlSessionAcquireOptions& options,
    int64_t nowMs,
    nlohmann::json eventMetadata = nlohmann::json::object()
);
std::optional<int64_t> NextControlSessionExpiry(const ControlSessionRecord& record, int64_t nowMs, int64_t ttlMs);
ControlSessionResult ExpireControlSessionForMaxRuntime(sqlite3* db, const ControlSessionRecord& record, int64_t nowMs);
std::optional<ControlSessionRecord> RenewActiveControlSession(
    sqlite3* db,
    const ControlSessionRecord& record,
    int64_t nowMs,
    int64_t expiresAtMs,
    const std::string& event,
    const std::string& message,
    nlohmann::json metadata
);

} // namespace ComputerCpp::ControlSessionInternal
