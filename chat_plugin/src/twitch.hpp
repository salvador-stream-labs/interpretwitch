// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_core.hpp"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>
#include <functional>

namespace awl::chat {
struct HttpResult {
    int status = 0;
    QJsonObject body;
    QString transport_error;
};
using HttpDone = std::function<void(HttpResult)>;
using HttpHook = std::function<void(QString, QString, QByteArray, QByteArray, HttpDone)>;
class TwitchClient : public QObject {
  public:
    explicit TwitchClient(QObject *parent = nullptr);
    ~TwitchClient();
    std::function<void(QString)> status;
    std::function<void(QString, QString)> authorize;
    std::function<void(Event)> event;
    std::function<void(bool)> connected;
    // Injectable persistence/HTTP for deterministic protocol/auth tests.
    std::function<QString()> load;
    std::function<bool(QString)> save;
    std::function<bool()> erase;
    HttpHook http_hook;
    std::function<void(QString, bool)> socket_hook;
    void ingest(const QString &text, bool replacing = false);
    void start(QString client_id, QString channel, bool remember);
    void disconnect_chat(bool forget = false);
    void poll_authorization();
    bool active() const {
        return running_;
    }

  private:
    QNetworkAccessManager network_;
    QTimer poll_, validation_, retry_, watchdog_;
    QPointer<QWebSocket> socket_, replacement_;
    Protocol protocol_;
    QString client_, channel_, broadcaster_, user_, token_, refresh_, device_;
    qint64 expires_ = 0, device_expires_ = 0, last_message_ = 0;
    int interval_ = 5, retries_ = 0, timeout_ = 30;
    quint64 epoch_ = 0;
    bool running_ = false, remember_ = false, refreshing_ = false, poll_busy_ = false;
    void say(const QString &text);
    void request(QString method, QString url, QByteArray data, QByteArray content_type,
                 HttpDone done, bool auth = false);
    void device_authorization();
    bool accept_tokens(const QJsonObject &body);
    void validate(bool initial);
    void refresh_token(bool initial);
    void resolve_channel();
    void open_socket(const QString &url, bool replacing = false);
    void subscribe(const QString &session);
    void reconnect();
    void fail(const QString &message);
};
} // namespace awl::chat
