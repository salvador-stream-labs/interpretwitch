// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
class QLineEdit;
class QTextEdit;
namespace awl::chat {
// Keep the standard editing actions, scoped to our widgets. OBS does not
// consistently translate Qt's default text context menus through its catalogue.
void install_text_menu(QLineEdit *edit);
void install_text_menu(QTextEdit *view);
} // namespace awl::chat
