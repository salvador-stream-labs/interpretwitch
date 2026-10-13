// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui_text.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

static void init_chat_locale() {
    Q_INIT_RESOURCE(chat_locale);
}

namespace awl::chat {
namespace {
bool russian = false;
const QJsonObject &catalogue() {
    static const auto messages = [] {
        init_chat_locale();
        QFile file(":/awl-chat/locale/ru-RU.json");
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject{};
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    return messages;
}
} // namespace
void set_ui_locale(QString locale) {
    russian = locale.replace('_', '-').section('-', 0, 0).compare("ru", Qt::CaseInsensitive) == 0;
}
QString ui_locale() {
    return russian ? "ru-RU" : "en-US";
}
QString ui_text(const char *english) {
    const auto source = QString::fromUtf8(english);
    return russian ? catalogue().value(source).toString(source) : source;
}
QString ui_error(const char *message, const char *fallback) {
    return catalogue().contains(QString::fromUtf8(message)) ? ui_text(message) : ui_text(fallback);
}
} // namespace awl::chat
