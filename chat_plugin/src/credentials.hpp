// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
namespace awl::chat {
// Tokens never enter OBS profile/scene settings or logs.
QString read_credentials();
bool write_credentials(const QString &json);
bool clear_credentials();
} // namespace awl::chat
