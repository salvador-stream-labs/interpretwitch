// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_core.hpp"
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

namespace awl::chat {
// Page adapter. No credentials, model loading or browser ownership.
class InlineChat : public QObject {
  public:
    using Execute = std::function<bool(const QString &)>;
    explicit InlineChat(Execute execute, QObject *parent = nullptr);
    ~InlineChat();
    bool start();
    void stop(bool failed = false);
    void set_ready(bool ready, QString health = {}, QString detail = {});
    void set_target(const QString &target);
    void set_appearance(bool language_label, bool background);
    void deliver(const QString &id, const Translation &translation);
    bool active() const {
        return server_.isListening();
    }
    std::function<void(Message)> message;
    std::function<void()> reset;
    std::function<void()> connected;
    std::function<void(QString)> status;

  private:
    Execute execute_;
    QWebSocketServer server_;
    QPointer<QWebSocket> socket_;
    QTimer attach_;
    QElapsedTimer rate_;
    QHash<QString, QString> pending_;
    QString script_, token_;
    QString target_ = "ru";
    QString health_, detail_;
    bool ready_ = false;
    bool language_label_ = true, background_ = true;
    int attempts_ = 0, received_ = 0;
    void attach();
    bool execute(const QString &script) const noexcept;
    void accept();
    void receive(const QString &text);
    void send(const QJsonObject &value);
};
} // namespace awl::chat
