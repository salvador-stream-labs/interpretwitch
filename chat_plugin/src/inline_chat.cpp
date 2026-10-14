// SPDX-License-Identifier: GPL-2.0-or-later
#include "inline_chat.hpp"
#include "diagnostics.hpp"
#include "model_profile.hpp"
#include "limits.hpp"
#include "ui_text.hpp"
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <QWebSocketCorsAuthenticator>
#include <algorithm>

static void init_inline_resources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(chat_inline);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace awl::chat {
InlineChat::InlineChat(Execute execute, QObject *parent)
    : QObject(parent), execute_(std::move(execute)),
      server_(QStringLiteral("Interpretwitch local translation"), QWebSocketServer::NonSecureMode) {
    server_.setMaxPendingConnections(1);
    server_.setHandshakeTimeout(2000);
    connect(&server_, &QWebSocketServer::originAuthenticationRequired, this,
            [](QWebSocketCorsAuthenticator *auth) {
                auth->setAllowed(auth->origin() == "https://www.twitch.tv");
                if (!auth->allowed())
                    log_event(LogLevel::Warning,
                              "Rejected a bridge connection: origin " + auth->origin().left(100));
            });
    connect(&server_, &QWebSocketServer::newConnection, this, &InlineChat::accept);
    attach_.setInterval(2000);
    connect(&attach_, &QTimer::timeout, this, &InlineChat::attach);
}
InlineChat::~InlineChat() {
    stop();
}
bool InlineChat::execute(const QString &script) const noexcept {
    try {
        return execute_ && execute_(script);
    } catch (...) {
        return false;
    }
}
bool InlineChat::start() {
    stop();
    init_inline_resources();
    QFile file(":/awl-chat/inline_chat.js");
    if (!execute_ || !file.open(QIODevice::ReadOnly) ||
        !server_.listen(QHostAddress::LocalHost, 0)) {
        log_event(LogLevel::Error, "Bridge could not start: " +
                                       (!execute_         ? QString("no browser interface")
                                        : !file.isOpen() ? QString("adapter script missing")
                                                         : server_.errorString()));
        if (status)
            status(ui_text("Could not start translation in the Twitch dock."));
        return false;
    }
    // Independent of Twitch auth. Fresh capability on each enable, never persisted.
    token_ =
        QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
    QJsonObject config{
        {"endpoint", QString("ws://127.0.0.1:%1/%2").arg(server_.serverPort()).arg(token_)},
        {"waiting", ui_text("Translating…")},
        {"locale", ui_locale()},
        {"detected", ui_text("Detected source language")},
        {"health", QJsonObject{{"name", ui_text("Interpretwitch")},
                               {"waiting", ui_text("Connecting")},
                               {"busy", ui_text("Translating…")},
                               {"failed", ui_text("Unavailable")},
                               {"checked", ui_text("Last reply: %1")},
                               {"detail", ui_text("Translation stopped. Original chat continues. "
                                                  "Open Tools → Interpretwitch to reconnect.")}}}};
    script_ = "(" + QString::fromUtf8(file.readAll()) + ")(" +
              QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)) + ");";
    attempts_ = 0;
    dock_found_ = false;
    log_event(LogLevel::Info,
              QString("Bridge listening on 127.0.0.1:%1").arg(server_.serverPort()));
    attach_.start();
    attach();
    return true;
}
void InlineChat::stop(bool failed) {
    if (server_.isListening())
        log_event(failed ? LogLevel::Warning : LogLevel::Info,
                  failed ? "Bridge stopped after a failure" : "Bridge stopped");
    attach_.stop();
    server_.close();
    if (!script_.isEmpty() && execute_)
        execute(failed ? "window.__awlInterpretwitch?.stop(true);"
                       : "window.__awlInterpretwitch?.stop();");
    script_.clear();
    token_.clear();
    pending_.clear();
    if (socket_) {
        auto socket = socket_.data();
        socket_ = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    while (server_.hasPendingConnections()) {
        auto socket = server_.nextPendingConnection();
        socket->abort();
        socket->deleteLater();
    }
}
void InlineChat::attach() {
    if (socket_ || !active())
        return;
    if (++attempts_ > 15) {
        log_event(LogLevel::Warning,
                  QString("Gave up attaching after 15 attempts: %1")
                      .arg(dock_found_ ? "Twitch Chat dock found, but its page did not connect"
                                       : "Twitch Chat dock not available"));
        stop(true);
        if (reset)
            reset();
        if (status)
            status(
                ui_text("Open OBS's Chat dock, then click Reconnect to chat. If Chat is missing, "
                        "reconnect Twitch in OBS Settings → Stream."));
        return;
    }
    const bool found = execute(script_);
    if (attempts_ == 1 || found != dock_found_)
        log_event(LogLevel::Info, QString(found ? "Adapter sent to the Twitch Chat dock (attempt %1)"
                                                : "Twitch Chat dock not available (attempt %1)")
                                      .arg(attempts_));
    dock_found_ = found;
    if (status)
        status(found ? ui_text("Waiting for Twitch's chat page…")
                     : ui_text("Open OBS's Twitch Chat dock to start. This OBS browser interface "
                               "must be compatible."));
}
void InlineChat::accept() {
    while (server_.hasPendingConnections()) {
        auto socket = server_.nextPendingConnection();
        if (socket_ || socket->origin() != "https://www.twitch.tv" ||
            socket->requestUrl().path() != "/" + token_ || !socket->peerAddress().isLoopback()) {
            // Never log the requested path: it would reveal the capability token.
            log_event(LogLevel::Warning,
                      "Rejected a bridge connection: " +
                          (socket_ ? QString("a page is already connected")
                           : socket->origin() != "https://www.twitch.tv"
                               ? "origin " + socket->origin().left(100)
                           : socket->requestUrl().path() != "/" + token_
                               ? QString("wrong capability")
                               : QString("not a loopback address")));
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket_ = socket;
        socket->setParent(this);
        socket->setMaxAllowedIncomingFrameSize(65536);
        socket->setMaxAllowedIncomingMessageSize(65536);
        rate_.start();
        received_ = 0;
        attach_.stop();
        connect(socket, &QWebSocket::textMessageReceived, this, &InlineChat::receive);
        connect(socket, &QWebSocket::binaryMessageReceived, this, [this] {
            reject("binary message");
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            if (socket_ == socket) {
                log_event(LogLevel::Warning,
                          QString("Twitch page disconnected (close code %1, %2 bridge requests "
                                  "pending)")
                              .arg(int(socket->closeCode()))
                              .arg(pending_.size()));
                socket_ = nullptr;
                pending_.clear();
                if (reset)
                    reset();
                if (active()) {
                    attach_.start();
                    if (status)
                        status(ui_text("Twitch page disconnected. Waiting to reconnect…"));
                }
            }
            socket->deleteLater();
        });
        log_event(LogLevel::Info, "Twitch page connected to the bridge");
        set_ready(ready_, health_, detail_);
        set_appearance(show_original_, language_label_, background_);
        if (status)
            status(ui_text("Connected to Twitch dock · target: %1").arg(language_name(target_)));
        if (connected)
            connected();
    }
}
void InlineChat::set_ready(bool ready, QString health, QString detail) {
    ready_ = ready;
    health_ =
        health.isEmpty() ? (ready ? ui_text("Ready") : ui_text("Preparing")) : std::move(health);
    detail_ = std::move(detail);
    const auto state = (ready ? "ready · " : "paused · ") + health_;
    if (state != logged_state_) {
        log_event(LogLevel::Info, "Bridge " + state);
        logged_state_ = state;
        unserved_logged_ = false;
    }
    if (!ready)
        pending_.clear();
    send({{"type", "state"},
          {"ready", ready},
          {"health", health_},
          {"detail", detail_},
          {"target_language", target_},
          {"language_labels", target_ == "ru" ? russian_language_labels() : QJsonObject{}}});
}
void InlineChat::set_target(const QString &target) {
    if (target == target_ || !supports_target(ModelProfile::Heavy, target))
        return;
    target_ = target;
    set_ready(false, ui_text("Preparing")); // Discard old-language requests and translations.
    if (socket_ && status)
        status(ui_text("Connected to Twitch dock · target: %1").arg(language_name(target_)));
}
void InlineChat::set_appearance(bool show_original, bool language_label, bool background) {
    show_original_ = show_original;
    language_label_ = language_label;
    background_ = background;
    send({{"type", "appearance"},
          {"show_original", show_original},
          {"language_label", language_label},
          {"background", background}});
}
void InlineChat::reject(const QString &why) {
    log_event(LogLevel::Warning, "Closed the Twitch page connection: " + why);
    if (socket_)
        socket_->abort();
}
void InlineChat::page_stopping(const QJsonObject &object) {
    // The page reports why it is about to stop; it closes the socket itself.
    static const QHash<QString, QString> reasons{
        {"timeout", "a translation request had no reply for 20 s"},
        {"layout-error", "a page layout error"},
        {"bad-reply", "an unreadable bridge reply"}};
    QString detail;
    for (const QChar c : object["detail"].toString().left(200))
        if (c.unicode() >= 0x20 && c.unicode() < 0x7f)
            detail += c;
    log_event(LogLevel::Warning,
              QString("Twitch page stopped translation after %1 (page waiting for %2, bridge "
                      "waiting for %3)%4")
                  .arg(reasons.value(object["reason"].toString(), "an unknown reason"))
                  .arg(std::clamp(object["pending"].toInt(), 0, 1000))
                  .arg(pending_.size())
                  .arg(detail.isEmpty() ? QString() : ": " + detail));
}
void InlineChat::send(const QJsonObject &value) {
    if (!socket_)
        return;
    if (socket_->bytesToWrite() > 65536) {
        reject("outgoing buffer over 64 KiB");
        return;
    }
    socket_->sendTextMessage(
        QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
}
void InlineChat::receive(const QString &text) {
    if (rate_.elapsed() >= 1000) {
        rate_.restart();
        received_ = 0;
    }
    const auto object = QJsonDocument::fromJson(text.toUtf8()).object();
    const auto id = object["id"].toString();
    static const QRegularExpression id_pattern("\\A[a-f0-9-]{36}\\z");
    if (++received_ > 64)
        return reject("more than 64 page messages in one second");
    if (object["type"] == "stopping")
        return page_stopping(object);
    if (object["type"] != "translate")
        return reject("unexpected page message");
    if (!id_pattern.match(id).hasMatch())
        return reject("invalid request id");
    if (!ready_ || pending_.size() >= limits::queued_translations ||
        pending_.values().contains(id)) {
        if (!unserved_logged_) {
            // Once per bridge state: busy chat can repeat this for every message.
            log_event(LogLevel::Info,
                      QString("Answering requests untranslated: %1")
                          .arg(!ready_ ? "model not ready"
                               : pending_.values().contains(id)
                                   ? "duplicate request"
                                   : "24 requests already waiting"));
            unserved_logged_ = true;
        }
        send({{"type", "result"}, {"id", id}, {"text", ""}});
        return;
    }
    const auto parts = object["parts"].toArray();
    Message m;
    m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (parts.isEmpty() || parts.size() > 128)
        return reject(QString("request with %1 fragments").arg(parts.size()));
    for (const auto &value : parts) {
        const auto part = value.toObject();
        const auto fragment = part["text"].toString();
        if (fragment.isEmpty() || fragment.size() > limits::message_characters)
            return reject(QString("request fragment of %1 characters").arg(fragment.size()));
        m.original += fragment;
        if (part["protected"].toBool())
            m.parts.push_back({fragment, true});
        else
            m.parts += protect_text(fragment);
    }
    if (m.original.size() > limits::message_characters)
        return reject(QString("request of %1 characters").arg(m.original.size()));
    pending_.insert(m.id, id);
    if (message)
        message(std::move(m));
    else
        deliver(m.id, {});
}
void InlineChat::deliver(const QString &id, const Translation &translation) {
    auto it = pending_.find(id);
    if (it == pending_.end())
        return;
    const auto page_id = it.value();
    pending_.erase(it);
    send({{"type", "result"},
          {"id", page_id},
          {"text", translation.translated ? translation.text : QString{}},
          {"source_language", translation.translated ? translation.source_language : QString{}}});
}
} // namespace awl::chat
