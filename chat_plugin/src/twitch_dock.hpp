// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
namespace awl::chat {
// Uses only the existing, version-checked OBS browser panel. Never creates one.
bool execute_in_twitch_dock(const QString &script);
} // namespace awl::chat
