// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace awl::chat {
// Static dock colours and spacing; no behaviour or external resources.
inline constexpr auto chat_style = R"(
        #awlChatPanel { background: #18181b; color: #efeff1; }
        #awlTwitchSetup QScrollArea, #awlTwitchSetup QWidget#awlSetupContent { background: #18181b; border: none; }
        #awlChatPanel QLabel, #awlChatPanel QCheckBox { color: #efeff1; background: transparent; }
        #awlChatPanel QLabel#awlChatStatus { color: #adadb8; padding: 2px 12px 7px; font-size: 10pt; }
        #awlChatPanel QWidget#awlChatHeader { border-bottom: 1px solid #2f2f35; }
        #awlChatPanel QWidget#awlChatFooter { border-top: 1px solid #2f2f35; }
        #awlChatPanel QWidget#awlChatConnection { background: #1f1f23; }
        #awlChatSettingsScroll { background: #1f1f23; border: none; }
        #awlChatPanel QPushButton, #awlChatPanel QToolButton {
            color: #efeff1; background: #2f2f35; border: 1px solid transparent;
            border-radius: 4px; padding: 5px 8px; font-size: 10pt;
        }
        #awlChatPanel QPushButton:hover, #awlChatPanel QToolButton:hover { background: #3a3a40; }
        #awlChatPanel QPushButton:focus, #awlChatPanel QToolButton:focus { border-color: #a970ff; }
        #awlChatPanel QPushButton:disabled { color: #8f8f9d; }
        #awlChatPanel QPushButton#awlChatConnect { background: #9146ff; color: #efeff1; font-weight: bold; }
        #awlChatPanel QPushButton#awlChatConnect:hover { background: #772ce8; }
        #awlChatPanel QLineEdit { color: #efeff1; background: #18181b; border: 1px solid #53535f; border-radius: 4px; padding: 6px; }
        #awlChatPanel QLineEdit:focus { border: 1px solid #a970ff; }
        #awlChatPanel QComboBox { color: #efeff1; background: #2f2f35; border: 1px solid #53535f; border-radius: 4px; padding: 5px 8px; font-size: 10pt; }
        #awlChatPanel QComboBox:focus { border-color: #a970ff; }
        #awlChatPanel QComboBox:disabled { color: #8f8f9d; }
        #awlChatPanel QComboBox QAbstractItemView { color: #efeff1; background: #2f2f35; selection-background-color: #5c3696; }
        #awlChatPanel QLabel#awlChatDeviceHelp { color: #adadb8; font-size: 10pt; }
        #awlChatPanel QCheckBox { font-size: 10pt; spacing: 5px; }
        #awlChatPanel QTextEdit#awlChatMessages { background: #18181b; color: #efeff1; border: none; selection-background-color: #5c3696; selection-color: #efeff1; }
        #awlChatPanel QScrollBar:vertical { background: #18181b; width: 8px; margin: 0; }
        #awlChatPanel QScrollBar::handle:vertical { background: #53535f; min-height: 28px; border-radius: 4px; }
        #awlChatPanel QScrollBar::add-line:vertical, #awlChatPanel QScrollBar::sub-line:vertical { height: 0; }
        #awlChatPanel QScrollBar::add-page:vertical, #awlChatPanel QScrollBar::sub-page:vertical { background: none; }
    )";
} // namespace awl::chat
