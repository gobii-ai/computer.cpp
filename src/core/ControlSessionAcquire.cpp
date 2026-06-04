#include "computer_cpp/ControlSession.h"

#include "ControlSessionInternal.h"
#include "ControlSessionStore.h"

#include "computer_cpp/Timeline.h"

#include <chrono>
#include <thread>

namespace ComputerCpp {
namespace {

using ControlSessionStore::Db;
using ControlSessionStore::ExpireOldSessions;
using ControlSessionStore::SelectActiveForScope;
using ControlSessionStore::Transaction;
using ControlSessionInternal::ErrorResult;
using ControlSessionInternal::ExpireControlSessionForMaxRuntime;
using ControlSessionInternal::InsertNewActiveControlSession;
using ControlSessionInternal::NextControlSessionExpiry;
using ControlSessionInternal::NormalizeAcquireOptions;
using ControlSessionInternal::RecordResult;
using ControlSessionInternal::RenewActiveControlSession;

} // namespace

ControlSessionResult AcquireControlSession(const ControlSessionAcquireOptions& rawOptions) {
    ControlSessionAcquireOptions options = NormalizeAcquireOptions(rawOptions);
    if (!IsControlSessionScopeValid(options.scope)) {
        return ErrorResult("invalid_control_scope", "invalid control session scope");
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.waitMs);
    ControlSessionResult busy = ErrorResult("control_session_busy", "control session is already held");
    while (true) {
        Db db;
        Transaction transaction(db.get());
        int64_t nowMs = NowMs();
        ExpireOldSessions(db.get(), nowMs);
        auto holder = SelectActiveForScope(db.get(), options.scope, nowMs);
        if (!holder.has_value()) {
            ControlSessionRecord record = InsertNewActiveControlSession(db.get(), options, nowMs);
            transaction.commit();
            return RecordResult(record);
        }
        transaction.commit();
        busy.holder = holder;
        if (options.waitMs <= 0 || std::chrono::steady_clock::now() >= deadline) {
            return busy;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

ControlSessionResult AcquireOrResumeControlSession(const ControlSessionAcquireOptions& rawOptions) {
    ControlSessionAcquireOptions options = NormalizeAcquireOptions(rawOptions);
    if (!IsControlSessionScopeValid(options.scope)) {
        return ErrorResult("invalid_control_scope", "invalid control session scope");
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.waitMs);
    ControlSessionResult busy = ErrorResult("control_session_busy", "control session is already held");
    while (true) {
        Db db;
        Transaction transaction(db.get());
        int64_t nowMs = NowMs();
        ExpireOldSessions(db.get(), nowMs);
        auto holder = SelectActiveForScope(db.get(), options.scope, nowMs);
        if (!holder.has_value()) {
            ControlSessionRecord record = InsertNewActiveControlSession(db.get(), options, nowMs, {{"mode", "exec"}});
            transaction.commit();
            return RecordResult(record);
        }

        if (holder->owner == options.owner &&
            holder->purpose == options.purpose &&
            holder->daemonSession == options.daemonSession) {
            auto nextExpiresAtMs = NextControlSessionExpiry(*holder, nowMs, options.ttlMs);
            if (!nextExpiresAtMs) {
                auto expired = ExpireControlSessionForMaxRuntime(db.get(), *holder, nowMs);
                transaction.commit();
                return expired;
            }

            auto updated = RenewActiveControlSession(
                db.get(),
                *holder,
                nowMs,
                *nextExpiresAtMs,
                "resumed",
                "control session resumed for command",
                {{"ttlMs", options.ttlMs}}
            );
            if (updated.has_value()) {
                transaction.commit();
                return RecordResult(*updated);
            }
            transaction.commit();
            return ErrorResult("control_session_not_found", "control session was not found after resume");
        }

        transaction.commit();
        busy.holder = holder;
        if (options.waitMs <= 0 || std::chrono::steady_clock::now() >= deadline) {
            return busy;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace ComputerCpp
