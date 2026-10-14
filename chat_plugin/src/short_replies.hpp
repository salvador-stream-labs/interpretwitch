// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>

namespace awl::chat {
// English, whole-reply matches only. Empty means use the translation model.
QString short_reply(const QString &text, const QString &target);
} // namespace awl::chat
