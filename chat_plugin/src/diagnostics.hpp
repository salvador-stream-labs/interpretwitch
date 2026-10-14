// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <functional>

namespace awl::chat {
// State-only diagnostics for the host log (OBS's log in the plugin). Events
// describe connections, worker state and failures. Never pass chat text,
// translations, usernames or the bridge capability token.
enum class LogLevel { Info, Warning, Error };
void set_log_sink(std::function<void(LogLevel, const QString &)> sink);
void log_event(LogLevel level, const QString &event);
} // namespace awl::chat
