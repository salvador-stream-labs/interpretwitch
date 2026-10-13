// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>

namespace awl::chat {
// Set once before constructing UI, using OBS's locale, never the OS locale.
// The helper receives the same choice in its prepare command. English is the
// source text and fallback. No application-wide Qt translator is installed.
void set_ui_locale(QString locale);
QString ui_locale();
QString ui_text(const char *english);
// Only known first-party errors are shown verbatim. Native library diagnostics
// can be unlocalised and expose paths; unknown errors get a translated fallback.
QString ui_error(const char *message, const char *fallback);
} // namespace awl::chat
