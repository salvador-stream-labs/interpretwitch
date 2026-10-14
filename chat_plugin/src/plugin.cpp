// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include "twitch_dock.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QPointer>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QAction>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_name() {
    static QByteArray name;
    name = awl::chat::ui_text("Interpretwitch").toUtf8();
    return name.constData();
}
MODULE_EXPORT const char *obs_module_description() {
    static QByteArray description;
    description = awl::chat::ui_text("Private local translation for Twitch chat").toUtf8();
    return description.constData();
}
MODULE_EXPORT const char *obs_module_author() {
    return "Salvador Stream Labs";
}
namespace {
QPointer<awl::chat::Panel> panel;
bool registered_callback = false;
QPointer<QAction> settings_action;
void frontend_event(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING && panel)
        panel->start();
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        delete settings_action;
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
        blog(LOG_ERROR, "[Interpretwitch] Missing module paths");
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
                                 QDir(directory).filePath("settings.ini"),
                                 static_cast<QWidget *>(obs_frontend_get_main_window()), false,
                                 helper, awl::chat::execute_in_twitch_dock);
    } catch (...) {
        bfree(config);
        bfree(manifest);
        blog(LOG_ERROR, "[Interpretwitch] Could not create the translation settings");
        return;
    }
    bfree(config);
    bfree(manifest);
    settings_action = static_cast<QAction *>(
        obs_frontend_add_tools_menu_qaction(qUtf8Printable(awl::chat::ui_text("Interpretwitch"))));
    if (!settings_action) {
        delete panel;
        return;
    }
    QObject::connect(settings_action, &QAction::triggered, panel, [] {
        if (panel) {
            panel->show();
            panel->raise();
            panel->activateWindow();
        }
    });
    obs_frontend_add_event_callback(frontend_event, nullptr);
    registered_callback = true;
    blog(LOG_INFO, "[Interpretwitch] " AWL_CHAT_VERSION
                   " Twitch chat adapter loaded; translation runs in a separate process");
}
MODULE_EXPORT void obs_module_unload() {
    if (registered_callback) {
        obs_frontend_remove_event_callback(frontend_event, nullptr);
        registered_callback = false;
    }
    delete settings_action;
    delete panel;
}
