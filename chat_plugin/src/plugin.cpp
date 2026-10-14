// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QPointer>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSslSocket>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_name() {
    static QByteArray name;
    name = awl::chat::ui_text("Awl Interpretwitch").toUtf8();
    return name.constData();
}
MODULE_EXPORT const char *obs_module_description() {
    static QByteArray description;
    description = awl::chat::ui_text("Private Twitch chat translation into Russian").toUtf8();
    return description.constData();
}
MODULE_EXPORT const char *obs_module_author() {
    return "AWL contributors";
}
namespace {
QPointer<awl::chat::Panel> panel;
bool registered_callback = false;
constexpr const char *dock_id = "awl-private-chat";
void frontend_event(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        // EXIT is the final frontend event. No frontend API calls are permitted
        // after this callback returns, including from obs_module_unload.
        registered_callback = false;
        if (panel) {
            delete panel;
            panel = nullptr;
        }
    }
}
} // namespace
MODULE_EXPORT void obs_module_set_locale(const char *locale) {
    awl::chat::set_ui_locale(QString::fromUtf8(locale ? locale : "en-US"));
}
MODULE_EXPORT bool obs_module_load() {
    obs_module_set_locale(obs_get_locale());
#if defined(__APPLE__) || defined(_WIN32)
    // OBS ships QtNetwork but not its TLS plugins. Register our matching,
    // bundled backend before constructing any network clients in post_load.
    const char *binary = obs_get_module_binary_path(obs_current_module());
    if (binary) {
        const auto plugins = QFileInfo(QString::fromUtf8(binary))
                                 .dir()
#ifdef __APPLE__
                                 .absoluteFilePath("../PlugIns");
#else
                                 .absoluteFilePath("qt-plugins");
#endif
        if (QDir(plugins).exists())
            QCoreApplication::addLibraryPath(plugins);
    }
#endif
    if (QSslSocket::supportsSsl())
        blog(LOG_INFO, "[Awl Interpretwitch] Secure connections available (%s)",
             qUtf8Printable(QSslSocket::activeBackend()));
    else
        blog(LOG_ERROR, "[Awl Interpretwitch] Secure connection component unavailable; "
                        "reinstall Awl Interpretwitch");
    return true;
}
MODULE_EXPORT void obs_module_post_load() {
    if (panel)
        return;
    char *config = obs_module_config_path("");
    char *manifest = obs_module_file("models.lock.json");
    if (!config || !manifest) {
        bfree(config);
        bfree(manifest);
        blog(LOG_ERROR, "[Awl Interpretwitch] Missing module paths");
        return;
    }
    const QString directory = QString::fromUtf8(config);
    QDir().mkpath(directory);
    const char *binary = obs_get_module_binary_path(obs_current_module());
    const auto helper = QFileInfo(QString::fromUtf8(binary ? binary : ""))
                            .dir()
                            .absoluteFilePath(
#ifdef _WIN32
                                "awl-chat-worker.exe"
#else
                                "awl-chat-worker"
#endif
                            );
    try {
        panel =
            new awl::chat::Panel(QString::fromUtf8(manifest), QDir(directory).filePath("models"),
                                 QDir(directory).filePath("settings.ini"), nullptr, false, helper);
    } catch (...) {
        bfree(config);
        bfree(manifest);
        blog(LOG_ERROR, "[Awl Interpretwitch] Could not create the private chat panel");
        return;
    }
    bfree(config);
    bfree(manifest);
    if (!obs_frontend_add_dock_by_id(
            dock_id, qUtf8Printable(awl::chat::ui_text("Awl Interpretwitch")), panel)) {
        delete panel;
        panel = nullptr;
        return;
    }
    obs_frontend_add_event_callback(frontend_event, nullptr);
    registered_callback = true;
    blog(LOG_INFO, "[Awl Interpretwitch] " AWL_CHAT_VERSION
                   " private dock loaded; translation runs in a separate process");
}
MODULE_EXPORT void obs_module_unload() {
    if (registered_callback) {
        obs_frontend_remove_event_callback(frontend_event, nullptr);
        registered_callback = false;
    }
    if (panel) {
        obs_frontend_remove_dock(dock_id);
        if (panel)
            delete panel;
        panel = nullptr;
    }
}
