// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui_text.hpp"
#include "limits.hpp"
#include "twitch.hpp"
#include "credentials.hpp"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslSocket>
#include <algorithm>
#include <memory>

namespace awl::chat {
static QByteArray form(std::initializer_list<std::pair<QString, QString>> fields) {
    QByteArray result;
    for (const auto &field : fields) {
        if (!result.isEmpty())
            result += '&';
        result +=
            QUrl::toPercentEncoding(field.first) + '=' + QUrl::toPercentEncoding(field.second);
    }
    return result;
}
static qint64 now() {
    return QDateTime::currentSecsSinceEpoch();
}
static QString missing_tls() {
    return ui_text("Secure connections are unavailable. Quit OBS, reinstall Awl Interpretwitch, "
                   "then reopen OBS.");
}
static QString transport_error(QNetworkReply::NetworkError error) {
    // Fixed descriptions only: never display/log response bodies, URLs or tokens.
    switch (error) {
    case QNetworkReply::NoError:
        return {};
    case QNetworkReply::HostNotFoundError:
        return ui_text(
            "Cannot find Twitch's server. Check your internet connection or DNS, then reconnect.");
    case QNetworkReply::TimeoutError:
        return ui_text("The connection to Twitch timed out. Try connecting again.");
    case QNetworkReply::SslHandshakeFailedError:
        return ui_text("The secure connection to Twitch failed. Check your computer's date and "
                       "network connection, then reconnect.");
    default:
        return ui_text("Cannot reach Twitch. Check your internet connection, then reconnect.");
    }
}
static QString device_failure(const HttpResult &result) {
    if (result.status == 0)
        return result.transport_error.isEmpty()
                   ? ui_text("Cannot reach Twitch. Check your internet connection, then reconnect.")
                   : result.transport_error;
    if (result.status == 429)
        return ui_text("Twitch is limiting sign-in attempts. Wait a minute, then reconnect.");
    if (result.status >= 500)
        return ui_text("Twitch sign-in is temporarily unavailable (HTTP %1). Try again shortly.")
            .arg(result.status);
    if (result.status == 400 || result.status == 401)
        return ui_text("Twitch rejected the sign-in request (HTTP %1). Check the Client ID and "
                       "that its application is Public.")
            .arg(result.status);
    return ui_text("Twitch could not start sign-in (HTTP %1). Try connecting again.")
        .arg(result.status);
}
TwitchClient::TwitchClient(QObject *parent) : QObject(parent), network_(this) {
    load = read_credentials;
    save = write_credentials;
    erase = clear_credentials;
    poll_.setSingleShot(true);
    retry_.setSingleShot(true);
    connect(&poll_, &QTimer::timeout, this, [this] {
        poll_authorization();
    });
    connect(&validation_, &QTimer::timeout, this, [this] {
        validate(false);
    });
    connect(&retry_, &QTimer::timeout, this, [this] {
        if (running_)
            validate(true);
    });
    connect(&watchdog_, &QTimer::timeout, this, [this] {
        if (running_ && now() - last_message_ > timeout_ + 2)
            reconnect();
    });
}
TwitchClient::~TwitchClient() {
    disconnect_chat();
}
void TwitchClient::say(const QString &text) {
    if (status)
        status(text);
}
void TwitchClient::request(QString method, QString url, QByteArray data, QByteArray content_type,
                           HttpDone done, bool auth) {
    const auto epoch = epoch_;
    auto guarded = [this, epoch, done = std::move(done)](HttpResult result) {
        if (running_ && epoch == epoch_)
            done(std::move(result));
    };
    if (http_hook) {
        http_hook(method, url, data, auth ? token_.toUtf8() : QByteArray{}, std::move(guarded));
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        guarded({0, {}, missing_tls()});
        return;
    }
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Client-Id", client_.toUtf8());
    if (auth)
        req.setRawHeader("Authorization",
                         (url.endsWith("/validate") ? "OAuth " : "Bearer ") + token_.toUtf8());
    if (!content_type.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, content_type);
    auto reply = method == "GET" ? network_.get(req) : network_.post(req, data);
    reply->setReadBufferSize(1024 * 1024 + 1);
    // Transfer timeout covers inactivity; also bound a response that trickles
    // data forever. The reply owns this timer, so completed requests release it.
    auto deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(10000);
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, guarded = std::move(guarded)] {
        const auto bytes = reply->readAll();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body =
            bytes.size() <= 1024 * 1024 ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
        const auto error = transport_error(reply->error());
        reply->deleteLater();
        guarded({code, body, error});
    });
}
void TwitchClient::disconnect_chat(bool forget) {
    running_ = false;
    ++epoch_;
    poll_.stop();
    retry_.stop();
    validation_.stop();
    watchdog_.stop();
    for (auto socket : {socket_, replacement_})
        if (socket) {
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
        }
    socket_.clear();
    replacement_.clear();
    for (auto reply : network_.findChildren<QNetworkReply *>()) {
        reply->abort();
        reply->deleteLater();
    }
    token_.clear();
    refresh_.clear();
    device_.clear();
    broadcaster_.clear();
    user_.clear();
    refreshing_ = poll_busy_ = false;
    protocol_.reset();
    if (connected)
        connected(false);
    if (forget && !erase())
        say(ui_text(
            "Disconnected. Saved sign-in could not be removed from the system credential store."));
}
void TwitchClient::fail(const QString &message) {
    disconnect_chat();
    say(message);
}
void TwitchClient::start(QString client_id, QString channel, bool remember) {
    disconnect_chat();
    client_ = client_id.trimmed();
    channel_ = normal_channel(channel);
    remember_ = remember;
    retries_ = 0;
    if (!valid_client_id(client_)) {
        say(ui_text("Complete Twitch setup with your public Client ID."));
        return;
    }
    if (channel_.isEmpty()) {
        say(ui_text("Enter a Twitch channel name."));
        return;
    }
    if (!http_hook && !QSslSocket::supportsSsl()) {
        say(missing_tls());
        return;
    }
    running_ = true;
    if (remember_) {
        const auto stored = QJsonDocument::fromJson(load().toUtf8()).object();
        if (stored["client_id"].toString() == client_) {
            token_ = stored["access_token"].toString();
            refresh_ = stored["refresh_token"].toString();
        }
    }
    if (token_.isEmpty())
        device_authorization();
    else
        validate(true);
}
void TwitchClient::device_authorization() {
    say(ui_text("Preparing Twitch sign-in…"));
    request("POST", "https://id.twitch.tv/oauth2/device",
            form({{"client_id", client_}, {"scopes", "user:read:chat"}}),
            "application/x-www-form-urlencoded", [this](HttpResult result) {
                if (result.status != 200) {
                    fail(device_failure(result));
                    return;
                }
                device_ = result.body["device_code"].toString();
                const auto url = result.body["verification_uri"].toString();
                const QUrl parsed(url, QUrl::StrictMode);
                const auto code = result.body["user_code"].toString();
                if (device_.isEmpty() || device_.size() > 2048 || code.isEmpty() ||
                    code.size() > 32 || !parsed.isValid() || parsed.scheme() != "https" ||
                    parsed.host() != "www.twitch.tv" || !parsed.userInfo().isEmpty() ||
                    (parsed.port() != -1 && parsed.port() != 443)) {
                    fail(ui_text("Twitch returned an invalid sign-in response."));
                    return;
                }
                interval_ = std::clamp(result.body["interval"].toInt(5), 5, 60);
                device_expires_ =
                    now() + std::clamp(result.body["expires_in"].toInt(1800), 30, 3600);
                if (authorize)
                    authorize(url, code);
                say(ui_text("Approve read-only chat access in your browser."));
                poll_.start(interval_ * 1000);
            });
}
void TwitchClient::poll_authorization() {
    if (!running_ || device_.isEmpty() || poll_busy_)
        return;
    if (now() >= device_expires_) {
        fail(ui_text("Twitch sign-in expired. Click Connect Twitch again."));
        return;
    }
    poll_busy_ = true;
    request("POST", "https://id.twitch.tv/oauth2/token",
            form({{"client_id", client_},
                  {"scopes", "user:read:chat"},
                  {"device_code", device_},
                  {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}}),
            "application/x-www-form-urlencoded", [this](HttpResult result) {
                poll_busy_ = false;
                if (result.status == 200 && accept_tokens(result.body)) {
                    device_.clear();
                    poll_.stop();
                    validate(true);
                    return;
                }
                const auto error =
                    result.body.value("message").toString(result.body.value("error").toString());
                if (error.contains("authorization_pending") || result.status == 0 ||
                    result.status >= 500) {
                    poll_.start(interval_ * 1000);
                    return;
                }
                if (error.contains("slow_down") || result.status == 429) {
                    interval_ = std::min(60, interval_ + 5);
                    poll_.start(interval_ * 1000);
                    return;
                }
                fail(ui_text(
                    "Twitch sign-in was declined or expired. Click Connect Twitch to try again."));
            });
}
bool TwitchClient::accept_tokens(const QJsonObject &body) {
    const auto token = body["access_token"].toString(), refresh = body["refresh_token"].toString();
    if (token.isEmpty() || refresh.isEmpty())
        return false;
    token_ = token;
    refresh_ = refresh;
    expires_ = now() + body["expires_in"].toInt(14400);
    if (remember_) {
        const auto json = QJsonDocument(QJsonObject{{"client_id", client_},
                                                    {"access_token", token_},
                                                    {"refresh_token", refresh_}})
                              .toJson(QJsonDocument::Compact);
        if (!save(QString::fromUtf8(json))) {
            remember_ = false;
            say(ui_text("Connected for this session. Your system could not save the sign-in."));
        }
    }
    return true;
}
void TwitchClient::validate(bool initial) {
    if (!running_ || refreshing_)
        return;
    if (token_.isEmpty()) {
        device_authorization();
        return;
    }
    request(
        "GET", "https://id.twitch.tv/oauth2/validate", {}, {},
        [this, initial](HttpResult result) {
            if (result.status == 401) {
                refresh_token(initial);
                return;
            }
            if (result.status == 0 || result.status >= 500 || result.status == 429) {
                reconnect();
                return;
            }
            const auto body = result.body;
            if (result.status != 200 || body["client_id"].toString() != client_ ||
                !body["scopes"].toArray().contains("user:read:chat") ||
                body["user_id"].toString().isEmpty()) {
                erase();
                fail(
                    ui_text("Twitch authorization no longer permits reading chat. Connect again."));
                return;
            }
            user_ = body["user_id"].toString();
            expires_ = now() + body["expires_in"].toInt();
            if (expires_ - now() < 120) {
                refresh_token(initial);
                return;
            }
            validation_.start(int(std::min<qint64>(3600, expires_ - now() - 60) * 1000));
            if (initial)
                resolve_channel();
        },
        true);
}
void TwitchClient::refresh_token(bool initial) {
    if (refreshing_)
        return;
    if (refresh_.isEmpty()) {
        fail(ui_text("Twitch sign-in expired. Connect again."));
        return;
    }
    refreshing_ = true;
    request(
        "POST", "https://id.twitch.tv/oauth2/token",
        form(
            {{"client_id", client_}, {"grant_type", "refresh_token"}, {"refresh_token", refresh_}}),
        "application/x-www-form-urlencoded", [this, initial](HttpResult result) {
            refreshing_ = false;
            if (result.status == 0 || result.status >= 500 || result.status == 429) {
                reconnect();
                return;
            }
            if (result.status != 200 || !accept_tokens(result.body)) {
                erase();
                fail(ui_text("Twitch sign-in expired. Connect again."));
                return;
            }
            validate(initial);
        });
}
void TwitchClient::resolve_channel() {
    say(ui_text("Connecting to #%1…").arg(channel_));
    request(
        "GET", "https://api.twitch.tv/helix/users?login=" + channel_, {}, {},
        [this](HttpResult result) {
            if (result.status == 401) {
                refresh_token(true);
                return;
            }
            if (result.status == 0 || result.status >= 500 || result.status == 429) {
                reconnect();
                return;
            }
            const auto data = result.body["data"].toArray();
            if (result.status != 200 || data.isEmpty()) {
                fail(ui_text("Channel not found. Check the channel name."));
                return;
            }
            broadcaster_ = data[0].toObject()["id"].toString();
            if (broadcaster_.isEmpty()) {
                fail(ui_text("Twitch returned an invalid channel."));
                return;
            }
            open_socket("wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=30");
        },
        true);
}
void TwitchClient::open_socket(const QString &url, bool replacing) {
    if (!safe_reconnect_url(url)) {
        fail(ui_text("Twitch returned an invalid reconnect address."));
        return;
    }
    if (replacing && replacement_)
        return;
    if (socket_hook) {
        socket_hook(url, replacing);
        return;
    }
    if (!replacing && socket_) {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_.clear();
    }
    auto ws = new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this);
    ws->setMaxAllowedIncomingFrameSize(limits::event_bytes);
    ws->setMaxAllowedIncomingMessageSize(limits::event_bytes);
    if (replacing)
        replacement_ = ws;
    else
        socket_ = ws;
    const auto epoch = epoch_;
    connect(ws, &QWebSocket::textMessageReceived, this, [this, ws, epoch](const QString &text) {
        if (!running_ || epoch != epoch_)
            return;
        ingest(text, replacement_ == ws);
    });
    connect(ws, &QWebSocket::disconnected, this, [this, ws, epoch] {
        if (running_ && epoch == epoch_ && (socket_ == ws || replacement_ == ws))
            reconnect();
    });
    connect(ws, &QWebSocket::errorOccurred, this, [this, epoch](auto) {
        if (running_ && epoch == epoch_)
            reconnect();
    });
    last_message_ = now();
    timeout_ = 30;
    watchdog_.start(1000);
    ws->open(QUrl(url));
}
void TwitchClient::ingest(const QString &text, bool replacing) {
    if (!running_)
        return;
    const auto value = protocol_.accept(text.toUtf8(), broadcaster_);
    if (value.kind == EventKind::Ignore)
        return;
    last_message_ = now();
    if (value.kind == EventKind::Welcome) {
        retries_ = 0;
        timeout_ = value.timeout;
        watchdog_.start(1000);
        if (replacing) {
            if (socket_) {
                socket_->disconnect(this);
                socket_->close();
                socket_->deleteLater();
            }
            socket_ = replacement_;
            replacement_.clear();
            say(ui_text("Reading #%1 · translating into Russian").arg(channel_));
        } else
            subscribe(value.session);
    } else if (value.kind == EventKind::Reconnect)
        open_socket(value.reconnect_url, true);
    else if (value.kind == EventKind::Revoked) {
        erase();
        fail(ui_text("Twitch revoked chat access. Connect again."));
    } else if (value.kind != EventKind::Keepalive && event)
        event(value);
}
void TwitchClient::subscribe(const QString &session) {
    const QStringList types{"channel.chat.message", "channel.chat.message_delete",
                            "channel.chat.clear_user_messages", "channel.chat.clear"};
    auto remaining = std::make_shared<int>(int(types.size()));
    for (const auto &type : types) {
        QJsonObject body{
            {"type", type},
            {"version", "1"},
            {"condition", QJsonObject{{"broadcaster_user_id", broadcaster_}, {"user_id", user_}}},
            {"transport", QJsonObject{{"method", "websocket"}, {"session_id", session}}}};
        request(
            "POST", "https://api.twitch.tv/helix/eventsub/subscriptions",
            QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json",
            [this, remaining](HttpResult result) {
                if (result.status == 401) {
                    reconnect();
                    return;
                }
                if (result.status == 0 || result.status >= 500 || result.status == 429) {
                    reconnect();
                    return;
                }
                if (result.status != 202) {
                    fail(ui_text("Twitch could not enable chat reading. Check channel access and "
                                 "reconnect."));
                    return;
                }
                if (--*remaining == 0) {
                    say(ui_text("Reading #%1 · translating into Russian").arg(channel_));
                    if (connected)
                        connected(true);
                }
            },
            true);
    }
}
void TwitchClient::reconnect() {
    if (!running_ || retry_.isActive())
        return;
    ++epoch_;
    watchdog_.stop();
    validation_.stop();
    for (auto reply : network_.findChildren<QNetworkReply *>()) {
        reply->abort();
        reply->deleteLater();
    }
    refreshing_ = poll_busy_ = false;
    for (auto ws : {socket_, replacement_})
        if (ws) {
            ws->disconnect(this);
            ws->abort();
            ws->deleteLater();
        }
    socket_.clear();
    replacement_.clear();
    if (connected)
        connected(false);
    const int delay = std::min(60, 1 << std::min(6, retries_++));
    say(ui_text("Connection interrupted. Retrying in %1 seconds…").arg(delay));
    retry_.start(delay * 1000);
}
} // namespace awl::chat
