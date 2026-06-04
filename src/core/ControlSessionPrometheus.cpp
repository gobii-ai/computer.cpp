#include "computer_cpp/ControlSession.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>

namespace ComputerCpp {
namespace {

std::string PromLabelValue(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        if (ch == '\n' || ch == '\r') {
            out.push_back(' ');
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

double Seconds(int64_t ms) {
    return static_cast<double>(ms) / 1000.0;
}

struct PromLabel {
    std::string_view name;
    std::string value;
};

std::string PromLabels(std::initializer_list<PromLabel> labels) {
    std::string out;
    bool first = true;
    for (const auto& label : labels) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out.append(label.name);
        out.append("=\"");
        out.append(PromLabelValue(label.value));
        out.push_back('"');
    }
    return out;
}

template <typename Value>
void EmitGauge(std::ostringstream& out, const char* name, const char* help, const std::string& labels, const Value& value) {
    out << "# HELP " << name << " " << help << "\n";
    out << "# TYPE " << name << " gauge\n";
    out << name << "{" << labels << "} " << value << "\n";
}

template <typename Value>
void EmitSample(std::ostringstream& out, const char* name, const std::string& labels, const Value& value) {
    out << name << "{" << labels << "} " << value << "\n";
}

} // namespace

std::string ControlSessionPrometheus(const std::string& scope, int64_t staleAfterMs, int64_t longRunningAfterMs) {
    auto metrics = ControlSessionMetricsJson(scope, staleAfterMs, longRunningAfterMs, 10);
    std::ostringstream out;
    const std::string scopeLabels = PromLabels({{"scope", metrics.value("scope", scope)}});
    const bool available = metrics.value("available", true);
    EmitGauge(out, "computer_cpp_control_session_active", "Whether an ComputerCpp control session is active for the scope.", scopeLabels, available ? 0 : 1);
    EmitGauge(out, "computer_cpp_control_session_available", "Whether the ComputerCpp control session scope is available.", scopeLabels, available ? 1 : 0);
    if (metrics.contains("active")) {
        const auto& active = metrics["active"];
        const std::string activeLabels = PromLabels({
            {"scope", metrics.value("scope", scope)},
            {"owner", active.value("owner", "")},
            {"purpose", active.value("purpose", "")},
            {"daemon_session", active.value("daemonSession", "")},
        });
        EmitGauge(out, "computer_cpp_control_session_age_seconds", "Age of the active control session.", activeLabels, Seconds(active.value("ageMs", 0LL)));
        EmitGauge(out, "computer_cpp_control_session_idle_seconds", "Seconds since the active lease was last seen.", activeLabels, Seconds(active.value("idleMs", 0LL)));
        EmitGauge(out, "computer_cpp_control_session_expires_in_seconds", "Seconds until the active lease expires without renewal.", activeLabels, Seconds(active.value("expiresInMs", 0LL)));
        EmitGauge(out, "computer_cpp_control_session_seconds_until_expiry", "Compatibility alias for seconds until expiry.", activeLabels, Seconds(active.value("expiresInMs", 0LL)));
        EmitGauge(
            out,
            "computer_cpp_control_session_renew_age_seconds",
            "Seconds since the active lease was renewed.",
            activeLabels,
            Seconds(std::max<int64_t>(0, metrics.value("nowMs", 0LL) - active.value("renewedAtMs", 0LL)))
        );
        EmitGauge(out, "computer_cpp_control_session_last_seen_age_seconds", "Compatibility alias for idle seconds.", activeLabels, Seconds(active.value("idleMs", 0LL)));
        EmitGauge(out, "computer_cpp_control_session_max_runtime_seconds", "Max runtime configured for the active lease, 0 if unlimited.", activeLabels, Seconds(active.value("maxRuntimeMs", 0LL)));
        EmitGauge(out, "computer_cpp_control_session_stale", "Whether the active lease has not been seen recently.", activeLabels, active.value("stale", false) ? 1 : 0);
        EmitGauge(out, "computer_cpp_control_session_long_running", "Whether the active lease is long-running or near its max runtime.", activeLabels, active.value("longRunning", false) ? 1 : 0);
        EmitGauge(out, "computer_cpp_control_session_active_resources", "Active resources attached to the lease.", activeLabels, active.value("activeResourceCount", 0));
    }
    auto eventCounts = metrics.value("eventCounts", nlohmann::json::object());
    out << "# HELP computer_cpp_control_session_events_total Control session lifecycle events recorded by type.\n";
    out << "# TYPE computer_cpp_control_session_events_total counter\n";
    for (auto it = eventCounts.begin(); it != eventCounts.end(); ++it) {
        EmitSample(
            out,
            "computer_cpp_control_session_events_total",
            PromLabels({{"scope", metrics.value("scope", scope)}, {"event", it.key()}}),
            it.value().get<int64_t>()
        );
    }
    return out.str();
}

} // namespace ComputerCpp
