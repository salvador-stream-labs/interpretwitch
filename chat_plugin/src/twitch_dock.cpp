// SPDX-License-Identifier: GPL-2.0-or-later
#include "twitch_dock.hpp"
#include "../third_party/obs-browser/browser-panel.hpp"
#include <obs-frontend-api.h>
#include <QDockWidget>
#include <QMainWindow>

namespace awl::chat {
bool execute_in_twitch_dock(const QString &script) {
    // Virtual interface layout is versioned by obs-browser. Reject unknown ABIs.
    if (obs_browser_qcef_version() != 3)
        return false;
    auto main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!main)
        return false;
    auto dock = main->findChild<QDockWidget *>("twitchChat");
    auto widget = dock ? dock->widget() : nullptr;
    if (!widget || !widget->inherits("QCefWidget"))
        return false;
    // Exact upstream interface, not the internal BrowserDock/CEF implementation.
    static_cast<QCefWidget *>(widget)->executeJavaScript(script.toStdString());
    return true;
}
} // namespace awl::chat
