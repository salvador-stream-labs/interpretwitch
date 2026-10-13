// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <deque>
#include <functional>
#include <optional>

namespace awl::chat {
struct Part {
    QString text;
    bool protected_text = false;
};
struct Message {
    QString id, user_id, name, original;
    QVector<Part> parts;
    QString color;
};
struct Translation {
    QString text;
    bool translated = false;
    bool uncertain = false;
};
enum class RunOn { CPU, GPU };
inline QString run_on_name(RunOn device) {
    return device == RunOn::GPU ? "GPU" : "CPU";
}
enum class EventKind {
    Ignore,
    Welcome,
    Keepalive,
    Reconnect,
    Message,
    Delete,
    ClearUser,
    Clear,
    Revoked
};
struct Event {
    EventKind kind = EventKind::Ignore;
    Message message;
    QString id, user_id, session, reconnect_url, reason;
    int timeout = 30;
};
// Per-connection bounds. Twitch may redeliver events across a reconnect.
class Protocol {
  public:
    Event accept(const QByteArray &json, const QString &channel_id);
    void reset();

  private:
    QSet<QString> seen_;
    std::deque<QString> order_;
};
QVector<Part> protect_text(const QString &text);
QString normal_channel(QString text);
bool valid_client_id(const QString &text);
bool safe_reconnect_url(const QString &url);
bool repetitive(const QString &text);
struct Job {
    Message message;
    quint64 epoch = 0;
    qint64 arrived = 0;
};
class JobQueue {
  public:
    using Discard = std::function<void(const Job &)>;
    size_t offer(Job job, const Discard &discard = {});
    std::optional<Job> take(qint64 now, size_t &expired, const Discard &discard = {});
    void clear(const Discard &discard = {}) {
        if (discard)
            for (const auto &job : jobs_)
                discard(job);
        jobs_.clear();
    }
    size_t size() const {
        return jobs_.size();
    }

  private:
    std::deque<Job> jobs_;
};
} // namespace awl::chat
