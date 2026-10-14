// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.hpp"

namespace awl::chat {
namespace {
// Main-thread only, like the bridge and supervisor that report through it.
std::function<void(LogLevel, const QString &)> &sink() {
    static std::function<void(LogLevel, const QString &)> value;
    return value;
}
} // namespace
void set_log_sink(std::function<void(LogLevel, const QString &)> value) {
    sink() = std::move(value);
}
void log_event(LogLevel level, const QString &event) {
    if (sink())
        sink()(level, event);
}
} // namespace awl::chat
