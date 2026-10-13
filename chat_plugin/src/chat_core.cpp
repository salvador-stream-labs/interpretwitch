// SPDX-License-Identifier: GPL-2.0-or-later
#include "limits.hpp"
#include "chat_core.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

namespace awl::chat {
QString normal_channel(QString text) {
    text = text.trimmed().toLower();
    if (text.startsWith('#'))
        text.remove(0, 1);
    static const QRegularExpression valid("^[a-z0-9_]{1,25}$");
    return valid.match(text).hasMatch() ? text : QString{};
}
bool valid_client_id(const QString &text) {
    static const QRegularExpression valid("^[A-Za-z0-9]{10,80}$");
    return valid.match(text).hasMatch();
}
bool safe_reconnect_url(const QString &text) {
    const QUrl url(text);
    return url.isValid() && url.scheme() == "wss" && url.host() == "eventsub.wss.twitch.tv" &&
           url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443);
}
QVector<Part> protect_text(const QString &text) {
    // Twitch emotes are protected separately by their typed fragments. Also
    // keep links, mentions and common third-party emote names out of the model.
    static const QRegularExpression re(
        R"((?:https?://|www\.)\S+|@[A-Za-z0-9_]+|\b(?:Kappa|PogChamp|Pog|KEKW|LUL|OMEGALUL|PepeHands|FeelsBadMan|FeelsGoodMan|MonkaS|monkaS|GG|gg|EZ|ez)\b)");
    QVector<Part> out;
    qsizetype at = 0;
    auto matches = re.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() > at)
            out.push_back({text.mid(at, match.capturedStart() - at), false});
        out.push_back({match.captured(), true});
        at = match.capturedEnd();
    }
    if (at < text.size())
        out.push_back({text.mid(at), false});
    return out;
}
bool repetitive(const QString &text) {
    const auto words = text.toCaseFolded().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (int width = 1; width <= 6; ++width) {
        int run = 0;
        for (qsizetype i = width; i < words.size(); ++i) {
            run = words[i] == words[i - width] ? run + 1 : 0;
            if (run >= width * 5)
                return true; // six consecutive identical phrases
        }
    }
    return false;
}
void Protocol::reset() {
    seen_.clear();
    order_.clear();
}
Event Protocol::accept(const QByteArray &json, const QString &channel) {
    if (json.size() > limits::event_bytes)
        return {};
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return {};
    const auto root = doc.object(), meta = root["metadata"].toObject(),
               payload = root["payload"].toObject();
    const auto type = meta["message_type"].toString();
    Event out;
    if (type == "session_welcome") {
        const auto session = payload["session"].toObject();
        out.session = session["id"].toString();
        if (out.session.isEmpty() || out.session.size() > 1024)
            return {};
        out.kind = EventKind::Welcome;
        out.timeout = std::clamp(session["keepalive_timeout_seconds"].toInt(30), 10, 600);
        return out;
    }
    if (type == "session_keepalive") {
        out.kind = EventKind::Keepalive;
        return out;
    }
    if (type == "session_reconnect") {
        out.reconnect_url = payload["session"].toObject()["reconnect_url"].toString();
        if (safe_reconnect_url(out.reconnect_url))
            out.kind = EventKind::Reconnect;
        return out;
    }
    if (type == "revocation") {
        out.kind = EventKind::Revoked;
        out.reason = payload["subscription"].toObject()["status"].toString().left(80);
        return out;
    }
    if (type != "notification")
        return {};
    const auto envelope = meta["message_id"].toString();
    if (envelope.isEmpty() || envelope.size() > 256 || seen_.contains(envelope))
        return {};
    const auto event = payload["event"].toObject();
    if (channel.isEmpty() || event["broadcaster_user_id"].toString() != channel)
        return {};
    seen_.insert(envelope);
    order_.push_back(envelope);
    if (order_.size() > limits::recent_event_ids) {
        seen_.remove(order_.front());
        order_.pop_front();
    }
    const auto subscription = meta["subscription_type"].toString();
    if (subscription == "channel.chat.clear")
        out.kind = EventKind::Clear;
    else if (subscription == "channel.chat.clear_user_messages") {
        out.kind = EventKind::ClearUser;
        out.user_id = event["target_user_id"].toString();
    } else if (subscription == "channel.chat.message_delete") {
        out.kind = EventKind::Delete;
        out.id = event["message_id"].toString();
    } else if (subscription == "channel.chat.message") {
        Message &m = out.message;
        m.id = event["message_id"].toString();
        m.user_id = event["chatter_user_id"].toString();
        m.name = event["chatter_user_name"].toString().left(64);
        const auto color = event["color"].toString();
        static const QRegularExpression color_pattern("^#[0-9A-Fa-f]{6}$");
        if (color_pattern.match(color).hasMatch())
            m.color = color;
        m.original = event["message"].toObject()["text"].toString();
        if (m.id.isEmpty() || m.id.size() > 256 || m.original.isEmpty() ||
            m.original.size() > limits::message_characters)
            return {};
        const auto fragments = event["message"].toObject()["fragments"].toArray();
        QString reconstructed;
        if (fragments.size() > 128)
            return {};
        for (const auto &value : fragments) {
            const auto f = value.toObject();
            const auto text = f["text"].toString();
            reconstructed += text;
            if (f["type"].toString() != "text")
                m.parts.push_back({text, true});
            else
                m.parts += protect_text(text);
        }
        if (reconstructed != m.original)
            m.parts = protect_text(m.original);
        out.kind = EventKind::Message;
    }
    return out;
}
size_t JobQueue::offer(Job job, const Discard &discard) {
    jobs_.push_back(std::move(job));
    if (jobs_.size() <= limits::queued_translations)
        return 0;
    if (discard)
        discard(jobs_.front());
    jobs_.pop_front();
    return 1;
}
std::optional<Job> JobQueue::take(qint64 now, size_t &expired, const Discard &discard) {
    while (!jobs_.empty() && now - jobs_.front().arrived > limits::translation_age_ms) {
        if (discard)
            discard(jobs_.front());
        jobs_.pop_front();
        ++expired;
    }
    if (jobs_.empty())
        return {};
    Job result = std::move(jobs_.front());
    jobs_.pop_front();
    return result;
}
} // namespace awl::chat
